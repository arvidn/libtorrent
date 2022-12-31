/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BENCODER_HPP_INCLUDED
#define TORRENT_BENCODER_HPP_INCLUDED

#include <vector>

#include "libtorrent/string_view.hpp"
#include "libtorrent/entry.hpp" // for integer_to_str

namespace libtorrent::aux::bencode {

	using buffer = std::vector<char>;

	inline void write_string(buffer& out, string_view val)
	{
		std::array<char, 21> buf;
		auto const str = integer_to_str(buf, std::int64_t(val.size()));
		out.insert(out.end(), str.begin(), str.end());
		out.push_back(':');
		out.insert(out.end(), val.begin(), val.end());
	}

	inline void write_int(buffer& out, std::int64_t val)
	{
		std::array<char, 21> buf;
		auto const str = integer_to_str(buf, val);
		out.push_back('i');
		out.insert(out.end(), str.begin(), str.end());
		out.push_back('e');
	}

	struct list
	{
		list(buffer& o): m_out(o) { m_out.push_back('l'); }
		void add(string_view val) { write_string(m_out, val); }
		void add(std::int64_t val) { write_int(m_out, val); }
		~list() { m_out.push_back('e'); }
	private:
		buffer& m_out;
	};

	struct dict
	{
		dict(buffer& o): m_out(o) { m_out.push_back('d'); }
		void add(string_view key, std::int64_t val)
		{
			add_key(key);
			add_value(val);
		}

		void add(string_view key, string_view val)
		{
			add_key(key);
			add_value(val);
		}

		void add_key(string_view key) { write_string(m_out, key); }
		void add_value(string_view val) { write_string(m_out, val); }
		void add_value(std::int64_t val) { write_int(m_out, val); }
		~dict() { m_out.push_back('e'); }
	private:
		buffer& m_out;
	};
}
#endif
