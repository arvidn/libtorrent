/*

Copyright (c) 2003-2005, 2007-2009, 2012-2019, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2019, Amir Abrams
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BENCODE_HPP_INCLUDED
#define TORRENT_BENCODE_HPP_INCLUDED

// OVERVIEW
//
// Bencoding is a common representation in bittorrent used for dictionary,
// list, int and string hierarchies. It's used to encode .torrent files and
// some messages in the network protocol. libtorrent also uses it to store
// settings, resume data and other session state.
//
// Strings in bencoded structures do not necessarily represent text.
// Strings are raw byte buffers of a certain length. If a string is meant to be
// interpreted as text, it is required to be UTF-8 encoded. See `BEP 3`_.
//
// The function for decoding bencoded data bdecode(), returning a bdecode_node.
// This function builds a tree that points back into the original buffer. The
// returned bdecode_node will not be valid once the buffer it was parsed out of
// is discarded.
//
// It's possible to construct an entry from a bdecode_node, if a structure needs
// to be altered and re-encoded.

#include <string>
#include <iterator> // for distance

#include "libtorrent/config.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/io_bytes.hpp" // for write_string
#include "libtorrent/string_util.hpp" // for is_digit

namespace libtorrent {

#if TORRENT_ABI_VERSION == 1
	using invalid_encoding = system_error;
#endif

namespace aux {

	template <class OutIt, class In, typename Cond
		= typename std::enable_if<std::is_integral<In>::value>::type>
	int write_integer(OutIt& out, In data)
	{
		entry::integer_type const val = entry::integer_type(data);
		TORRENT_ASSERT(data == In(val));
		// the stack allocated buffer for keeping the
		// decimal representation of the number can
		// not hold number bigger than this:
		static_assert(sizeof(entry::integer_type) <= 8, "64 bit integers required");
		static_assert(sizeof(data) <= sizeof(entry::integer_type), "input data too big, see entry::integer_type");
		std::array<char, 21> buf;
		auto const str = integer_to_str(buf, val);
		for (char const c : str)
		{
			*out = c;
			++out;
		}
		return static_cast<int>(str.size());
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

	template <typename OutIt>
	struct bencode_visitor
	{
		OutIt& out;

		int operator()(entry::integer_type i)
		{
			write_char(out, 'i');
			int const ret = write_integer(out, i);
			write_char(out, 'e');
			return ret + 2;
		}

		int operator()(entry::string_type const& str)
		{
			int ret = write_integer(out, str.length());
			write_char(out, ':');
			ret += write_string(str, out);
			return ret + 1;
		}

		int operator()(entry::list_type const& l)
		{
			write_char(out, 'l');
			int ret = 2;
			for (auto const& i : l)
				ret += std::visit(*this, static_cast<entry::variant_type const&>(i));
			write_char(out, 'e');
			return ret;
		}

		int operator()(entry::dictionary_type const& d)
		{
			write_char(out, 'd');
			int ret = 2;
			for (auto const& i : d)
			{
				// write key
				ret += write_integer(out, i.first.length());
				write_char(out, ':');
				ret += write_string(i.first, out);
				// write value
				ret += std::visit(*this, static_cast<entry::variant_type const&>(i.second));
				ret += 1;
			}
			write_char(out, 'e');
			return ret;
		}

		int operator()(entry::preformatted_type const& pre)
		{
			std::copy(pre.begin(), pre.end(), out);
			return static_cast<int>(pre.size());
		}

		int operator()(entry::uninitialized_type const&)
		{
			// empty string
			write_char(out, '0');
			write_char(out, ':');
			return 2;
		}
	};

#if TORRENT_ABI_VERSION == 1
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
			return;
		}
		switch (*in)
		{

		// ----------------------------------------------
		// integer
		case 'i':
			{
			++in; // 'i'
			std::string const val = read_until(in, end, 'e', err);
			if (err) return;
			TORRENT_ASSERT(*in == 'e');
			++in; // 'e'
			ret = entry(entry::int_t);
			char* end_pointer;
			ret.integer() = std::strtoll(val.c_str(), &end_pointer, 10);
			if (end_pointer == val.c_str())
			{
				err = true;
				return;
			}
			}
			break;

		// ----------------------------------------------
		// list
		case 'l':
			ret = entry(entry::list_t);
			++in; // 'l'
			while (*in != 'e')
			{
				ret.list().emplace_back();
				entry& e = ret.list().back();
				bdecode_recursive(in, end, e, err, depth + 1);
				if (err)
				{
					return;
				}
				if (in == end)
				{
					err = true;
					return;
				}
			}
			TORRENT_ASSERT(*in == 'e');
			++in; // 'e'
			break;

		// ----------------------------------------------
		// dictionary
		case 'd':
			ret = entry(entry::dictionary_t);
			++in; // 'd'
			while (*in != 'e')
			{
				entry key;
				bdecode_recursive(in, end, key, err, depth + 1);
				if (err || key.type() != entry::string_t) return;
				entry& e = ret[key.string()];
				bdecode_recursive(in, end, e, err, depth + 1);
				if (err) return;
				if (in == end)
				{
					err = true;
					return;
				}
			}
			TORRENT_ASSERT(*in == 'e');
			++in; // 'e'
			break;

		// ----------------------------------------------
		// string
		default:
			static_assert(sizeof(*in) == 1, "Input iterator to 8 bit data required");
			if (is_digit(char(*in)))
			{
				std::string len_s = read_until(in, end, ':', err);
				if (err) return;
				TORRENT_ASSERT(*in == ':');
				++in; // ':'
				int len = atoi(len_s.c_str());
				ret = entry(entry::string_t);
				read_string(in, end, len, ret.string(), err);
				if (err) return;
			}
			else
			{
				err = true;
				return;
			}
		}
	}
#endif // TORRENT_ABI_VERSION
}

	// This function will encode data to bencoded form.
	//
	// The entry_ class is the internal representation of the bencoded data
	// and it can be used to retrieve information, an entry_ can also be build by
	// the program and given to ``bencode()`` to encode it into the ``OutIt``
	// iterator.
	//
	// ``OutIt`` is an OutputIterator_. It's a template and usually
	// instantiated as ostream_iterator_ or back_insert_iterator_. This
	// function assumes the value_type of the iterator is a ``char``.
	// In order to encode entry ``e`` into a buffer, do::
	//
	//	std::vector<char> buffer;
	//	bencode(std::back_inserter(buf), e);
	//
	// .. _OutputIterator:  https://en.cppreference.com/w/cpp/named_req/OutputIterator
	// .. _ostream_iterator: https://en.cppreference.com/w/cpp/iterator/ostream_iterator
	// .. _back_insert_iterator: https://en.cppreference.com/w/cpp/iterator/back_insert_iterator
	template<class OutIt> int bencode(OutIt out, const entry& e)
	{
		return std::visit(aux::bencode_visitor<OutIt>{out}, static_cast<entry::variant_type const&>(e));
	}

#if TORRENT_ABI_VERSION == 1
	template<class InIt>
	TORRENT_DEPRECATED
	entry bdecode(InIt start, InIt end)
	{
		entry e;
		bool err = false;
		aux::bdecode_recursive(start, end, e, err, 0);
		if (err) return entry();
		return e;
	}
	template<class InIt>
	TORRENT_DEPRECATED
	entry bdecode(InIt start, InIt end
		, typename std::iterator_traits<InIt>::difference_type& len)
	{
		entry e;
		bool err = false;
		InIt s = start;
		aux::bdecode_recursive(start, end, e, err, 0);
		len = std::distance(s, start);
		TORRENT_ASSERT(len >= 0);
		if (err) return entry();
		return e;
	}
#endif
}

#endif // TORRENT_BENCODE_HPP_INCLUDED
