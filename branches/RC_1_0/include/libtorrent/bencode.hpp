/*

Copyright (c) 2003-2014, Arvid Norberg
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



// OVERVIEW
// 
// Bencoding is a common representation in bittorrent used for
// for dictionary, list, int and string hierarchies. It's used
// to encode .torrent files and some messages in the network
// protocol. libtorrent also uses it to store settings, resume
// data and other state between sessions.
//
// Strings in bencoded structures are not necessarily representing
// text. Strings are raw byte buffers of a certain length. If a
// string is meant to be interpreted as text, it is required to
// be UTF-8 encoded. See `BEP 3`_.
//
// There are two mechanims to *decode* bencoded buffers in libtorrent.
//
// The most flexible one is bdecode(), which returns a structure
// represented by entry. When a buffer is decoded with this function,
// it can be discarded. The entry does not contain any references back
// to it. This means that bdecode() actually copies all the data out
// of the buffer and into its own hierarchy. This makes this
// function potentially expensive, if you're parsing large amounts
// of data.
//
// Another consideration is that bdecode() is a recursive parser.
// For this reason, in order to avoid DoS attacks by triggering
// a stack overflow, there is a recursion limit. This limit is
// a sanity check to make sure it doesn't run the risk of
// busting the stack.
//
// The second mechanism is lazy_bdecode(), which returns a
// bencoded structure represented by lazy_entry. This function
// builds a tree that points back into the original buffer.
// The returned lazy_entry will not be valid once the buffer
// it was parsed out of is discarded.
//
// Not only is this function more efficient because of less
// memory allocation and data copy, the parser is also not
// recursive, which means it probably performs a little bit
// better and can have a higher recursion limit on the structures
// it's parsing.

#include <stdlib.h>
#include <string>
#include <exception>
#include <iterator> // for distance

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/static_assert.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/entry.hpp"
#include "libtorrent/config.hpp"

#include "libtorrent/assert.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/io.hpp" // for write_string

namespace libtorrent
{

#ifndef TORRENT_NO_DEPRECATE
	// thrown by bdecode() if the provided bencoded buffer does not contain
	// valid encoding.
	struct TORRENT_EXPORT invalid_encoding: std::exception
	{
		// hidden
		virtual const char* what() const throw() { return "invalid bencoding"; }
	};
#endif

	namespace detail
	{
		// this is used in the template, so it must be available to the client
		TORRENT_EXPORT char const* integer_to_str(char* buf, int size
			, entry::integer_type val);

		template <class OutIt>
		int write_integer(OutIt& out, entry::integer_type val)
		{
			// the stack allocated buffer for keeping the
			// decimal representation of the number can
			// not hold number bigger than this:
			BOOST_STATIC_ASSERT(sizeof(entry::integer_type) <= 8);
			char buf[21];
			int ret = 0;
			for (char const* str = integer_to_str(buf, 21, val);
				*str != 0; ++str)
			{
				*out = *str;
				++out;
				++ret;
			}
			return ret;
		}	
		
		template <class OutIt>
		void write_char(OutIt& out, char c)
		{
			*out = c;
			++out;
		}

		template <class InIt>
		std::string read_until(InIt& in, InIt end, char end_token, bool& err)
		{
			std::string ret;
			if (in == end)
			{
				err = true;
				return ret;
			}
			while (*in != end_token)
			{
				ret += *in;
				++in;
				if (in == end)
				{
					err = true;
					return ret;
				}
			}
			return ret;
		}

		template<class InIt>
		void read_string(InIt& in, InIt end, int len, std::string& str, bool& err)
		{
			TORRENT_ASSERT(len >= 0);
			for (int i = 0; i < len; ++i)
			{
				if (in == end)
				{
					err = true;
					return;
				}
				str += *in;
				++in;
			}
		}

		template<class OutIt>
		int bencode_recursive(OutIt& out, const entry& e)
		{
			int ret = 0;
			switch(e.type())
			{
			case entry::int_t:
				write_char(out, 'i');
				ret += write_integer(out, e.integer());
				write_char(out, 'e');
				ret += 2;
				break;
			case entry::string_t:
				ret += write_integer(out, e.string().length());
				write_char(out, ':');
				ret += write_string(e.string(), out);
				ret += 1;
				break;
			case entry::list_t:
				write_char(out, 'l');
				for (entry::list_type::const_iterator i = e.list().begin(); i != e.list().end(); ++i)
					ret += bencode_recursive(out, *i);
				write_char(out, 'e');
				ret += 2;
				break;
			case entry::dictionary_t:
				write_char(out, 'd');
				for (entry::dictionary_type::const_iterator i = e.dict().begin();
					i != e.dict().end(); ++i)
				{
					// write key
					ret += write_integer(out, i->first.length());
					write_char(out, ':');
					ret += write_string(i->first, out);
					// write value
					ret += bencode_recursive(out, i->second);
					ret += 1;
				}
				write_char(out, 'e');
				ret += 2;
				break;
			default:
				// trying to encode a structure with uninitialized values!
				TORRENT_ASSERT_VAL(false, e.type());
				// do nothing
				break;
			}
			return ret;
		}

		template<class InIt>
		void bdecode_recursive(InIt& in, InIt end, entry& ret, bool& err, int depth)
		{
			if (depth >= 100)
			{
				err = true;
				return;
			}

			if (in == end)
			{
				err = true;
#ifdef TORRENT_DEBUG
				ret.m_type_queried = false;
#endif
				return;
			}
			switch (*in)
			{

			// ----------------------------------------------
			// integer
			case 'i':
				{
				++in; // 'i'
				std::string val = read_until(in, end, 'e', err);
				if (err) return;
				TORRENT_ASSERT(*in == 'e');
				++in; // 'e'
				ret = entry(entry::int_t);
				char* end_pointer;
				ret.integer() = strtoll(val.c_str(), &end_pointer, 10);
#ifdef TORRENT_DEBUG
				ret.m_type_queried = false;
#endif
				if (end_pointer == val.c_str())
				{
					err = true;
					return;
				}
				} break;

			// ----------------------------------------------
			// list
			case 'l':
				{
				ret = entry(entry::list_t);
				++in; // 'l'
				while (*in != 'e')
				{
					ret.list().push_back(entry());
					entry& e = ret.list().back();
					bdecode_recursive(in, end, e, err, depth + 1);
					if (err)
					{
#ifdef TORRENT_DEBUG
						ret.m_type_queried = false;
#endif
						return;
					}
					if (in == end)
					{
						err = true;
#ifdef TORRENT_DEBUG
						ret.m_type_queried = false;
#endif
						return;
					}
				}
#ifdef TORRENT_DEBUG
				ret.m_type_queried = false;
#endif
				TORRENT_ASSERT(*in == 'e');
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
					entry key;
					bdecode_recursive(in, end, key, err, depth + 1);
					if (err || key.type() != entry::string_t)
					{
#ifdef TORRENT_DEBUG
						ret.m_type_queried = false;
#endif
						return;
					}
					entry& e = ret[key.string()];
					bdecode_recursive(in, end, e, err, depth + 1);
					if (err)
					{
#ifdef TORRENT_DEBUG
						ret.m_type_queried = false;
#endif
						return;
					}
					if (in == end)
					{
						err = true;
#ifdef TORRENT_DEBUG
						ret.m_type_queried = false;
#endif
						return;
					}
				}
#ifdef TORRENT_DEBUG
				ret.m_type_queried = false;
#endif
				TORRENT_ASSERT(*in == 'e');
				++in; // 'e'
				} break;

			// ----------------------------------------------
			// string
			default:
				if (is_digit((unsigned char)*in))
				{
					std::string len_s = read_until(in, end, ':', err);
					if (err)
					{
#ifdef TORRENT_DEBUG
						ret.m_type_queried = false;
#endif
						return;
					}
					TORRENT_ASSERT(*in == ':');
					++in; // ':'
					int len = atoi(len_s.c_str());
					ret = entry(entry::string_t);
					read_string(in, end, len, ret.string(), err);
					if (err)
					{
#ifdef TORRENT_DEBUG
						ret.m_type_queried = false;
#endif
						return;
					}
				}
				else
				{
					err = true;
#ifdef TORRENT_DEBUG
					ret.m_type_queried = false;
#endif
					return;
				}
#ifdef TORRENT_DEBUG
				ret.m_type_queried = false;
#endif
			}
		}
	}

	// These functions will encode data to bencoded_ or decode bencoded_ data.
	// 
	// If possible, lazy_bdecode() should be preferred over ``bdecode()``.
	// 
	// The entry_ class is the internal representation of the bencoded data
	// and it can be used to retrieve information, an entry_ can also be build by
	// the program and given to ``bencode()`` to encode it into the ``OutIt``
	// iterator.
	// 
	// The ``OutIt`` and ``InIt`` are iterators
	// (InputIterator_ and OutputIterator_ respectively). They
	// are templates and are usually instantiated as ostream_iterator_,
	// back_insert_iterator_ or istream_iterator_. These
	// functions will assume that the iterator refers to a character
	// (``char``). So, if you want to encode entry ``e`` into a buffer
	// in memory, you can do it like this::
	// 
	//	std::vector<char> buffer;
	//	bencode(std::back_inserter(buf), e);
	// 
	// .. _InputIterator: http://www.sgi.com/tech/stl/InputIterator.html
	// .. _OutputIterator: http://www.sgi.com/tech/stl/OutputIterator.html
	// .. _ostream_iterator: http://www.sgi.com/tech/stl/ostream_iterator.html
	// .. _back_insert_iterator: http://www.sgi.com/tech/stl/back_insert_iterator.html
	// .. _istream_iterator: http://www.sgi.com/tech/stl/istream_iterator.html
	// 
	// If you want to decode a torrent file from a buffer in memory, you can do it like this::
	// 
	//	std::vector<char> buffer;
	//	// ...
	//	entry e = bdecode(buf.begin(), buf.end());
	// 
	// Or, if you have a raw char buffer::
	// 
	//	const char* buf;
	//	// ...
	//	entry e = bdecode(buf, buf + data_size);
	// 
	// Now we just need to know how to retrieve information from the entry.
	// 
	// If ``bdecode()`` encounters invalid encoded data in the range given to it
	// it will return a default constructed ``entry`` object.
	template<class OutIt> int bencode(OutIt out, const entry& e)
	{
		return detail::bencode_recursive(out, e);
	}
	template<class InIt> entry bdecode(InIt start, InIt end)
	{
		entry e;
		bool err = false;
		detail::bdecode_recursive(start, end, e, err, 0);
#ifdef TORRENT_DEBUG
		TORRENT_ASSERT(e.m_type_queried == false);
#endif
		if (err) return entry();
		return e;
	}
	template<class InIt> entry bdecode(InIt start, InIt end, int& len)
	{
		entry e;
		bool err = false;
		InIt s = start;
		detail::bdecode_recursive(start, end, e, err, 0);
		len = std::distance(s, start);
		TORRENT_ASSERT(len >= 0);
		if (err) return entry();
		return e;
	}
}

#endif // TORRENT_BENCODE_HPP_INCLUDED

