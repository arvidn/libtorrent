/*

Copyright (c) 2003, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/


#ifndef TORRENT_BENCODE_HPP_INCLUDED
#define TORRENT_BENCODE_HPP_INCLUDED



/*
 * This file declares the following functions:
 *
 *----------------------------------
 * template<class OutIt>
 * void libtorrent::bencode(OutIt out, const libtorrent::entry& e);
 *
 * Encodes a message entry with bencoding into the output
 * iterator given. The bencoding is described in the BitTorrent
 * protocol description document OutIt must be an OutputIterator
 * of type char. This may throw libtorrent::invalid_encoding if
 * the entry contains invalid nodes (undefined_t for example).
 *
 *----------------------------------
 * template<class InIt>
 * libtorrent::entry libtorrent::bdecode(InIt start, InIt end);
 *
 * Decodes the buffer given by the start and end iterators
 * and returns the decoded entry. InIt must be an InputIterator
 * of type char. May throw libtorrent::invalid_encoding if
 * the string is not correctly bencoded.
 *
 */




#include <cstdlib>
#include <boost/lexical_cast.hpp>

#include "libtorrent/entry.hpp"

#ifdef _MSC_VER
namespace std
{
	using ::isdigit;
	using ::atoi;
};

#define for if (false) {}  else for
#endif


namespace libtorrent
{

	struct invalid_encoding: std::exception
	{
		virtual const char* what() const throw() { return "invalid bencoding"; }
	};

	namespace detail
	{
		template <class OutIt>
		void write_string(OutIt& out, const std::string& val)
		{
			std::string::const_iterator end = val.begin()+val.length();
			for (std::string::const_iterator i = val.begin(); i != end; ++i)
			{
				*out = *i;
				++out;
			}
		}

		template <class OutIt>
		void write_integer(OutIt& out, entry::integer_type val)
		{
			write_string(out, boost::lexical_cast<std::string>(val));
		}

		template <class InIt>
		std::string read_until(InIt& in, InIt end, char end_token)
		{
			if (in == end) throw invalid_encoding();
			std::string ret;
			while (*in != end_token)
			{
				ret += *in;
				++in;
				if (in == end) throw invalid_encoding();
			}
			return ret;
		}

		template<class InIt>
		std::string read_string(InIt& in, InIt end,  int len)
		{
			std::string ret;
			for (int i = 0; i < len; ++i)
			{
				if (in == end) throw invalid_encoding();
				ret += *in;
				++in;
			}
			return ret;
		}

		template<class OutIt>
		void bencode_recursive(OutIt& out, const entry& e)
		{
			switch(e.type())
			{
			case entry::int_t:
				*out = 'i'; ++out;
				write_integer(out, e.integer());
				*out = 'e'; ++out;
				break;
			case entry::string_t:
				write_integer(out, e.string().length());
				*out = ':'; ++out;
				write_string(out, e.string());
				break;
			case entry::list_t:
				*out = 'l'; ++out;
				for (entry::list_type::const_iterator i = e.list().begin(); i != e.list().end(); ++i)
					bencode_recursive(out, *i);
				*out = 'e'; ++out;
				break;
			case entry::dictionary_t:
				*out = 'd'; ++out;
				for (entry::dictionary_type::const_iterator i = e.dict().begin(); i != e.dict().end(); ++i)
				{
					// write key
					write_integer(out, i->first.length());
					*out = ':'; ++out;
					write_string(out, i->first);
					// write value
					bencode_recursive(out, i->second);
				}
				*out = 'e'; ++out;
				break;
			default:
				throw invalid_encoding();
			}
		}

		template<class InIt>
		entry bdecode_recursive(InIt& in, InIt end)
		{
			if (in == end) throw invalid_encoding();
			entry ret;
			switch (*in)
			{

			// ----------------------------------------------
			// integer
			case 'i':
				{
				++in; // 'i' 
				std::string val = read_until(in, end, 'e');
				assert(*in == 'e');
				++in; // 'e' 
				ret = entry(entry::int_t);
				ret.integer() = boost::lexical_cast<entry::integer_type>(val);
				} break;

			// ----------------------------------------------
			// list
			case 'l':
				{
				ret = entry(entry::list_t);
				++in; // 'l'
				while (*in != 'e')
				{
					ret.list().push_back(bdecode_recursive(in, end));
					if (in == end) throw invalid_encoding();
				}
				assert(*in == 'e');
				++in; // 'e'
				} break;

			// ----------------------------------------------
			// dictionary
			case 'd':
				{
				ret = entry(entry::dictionary_t);
				++in; // 'd'
				while (*in != 'e')
				{
					entry key = bdecode_recursive(in, end);
					ret.dict()[key.string()] = bdecode_recursive(in, end);
					if (in == end) throw invalid_encoding();
				}
				assert(*in == 'e');
				++in; // 'e'
				} break;

			// ----------------------------------------------
			// string
			default:
				if (std::isdigit(*in))
				{
					std::string len_s = read_until(in, end, ':');
					assert(*in == ':');
					++in; // ':'
					int len = std::atoi(len_s.c_str());
					ret = entry(entry::string_t);
					ret.string() = read_string(in, end, len);
				}
				else
				{
					throw invalid_encoding();
				}
			}
			return ret;
		}

	}

	template<class OutIt>
	void bencode(OutIt out, const entry& e)
	{
		detail::bencode_recursive(out, e);
	}

	template<class InIt>
	entry bdecode(InIt start, InIt end)
	{
		try
		{
			return detail::bdecode_recursive(start, end);
		}
		catch(type_error&)
		{
			throw invalid_encoding();
		}
	}

}

#endif // TORRENT_BENCODE_HPP_INCLUDED
