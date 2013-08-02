/*

Copyright (c) 2003-2012, Arvid Norberg
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

#ifndef TORRENT_SHA1_HASH_HPP_INCLUDED
#define TORRENT_SHA1_HASH_HPP_INCLUDED

#include <cctype>
#include <algorithm>
#include <string>
#include <cstring>

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

#if TORRENT_USE_IOSTREAM
#include "libtorrent/escape_string.hpp" // to_hex, from_hex
#include <iostream>
#include <iomanip>
#endif

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace libtorrent
{

	// This type holds a SHA-1 digest or any other kind of 20 byte
	// sequence. It implements a number of convenience functions, such
	// as bit operations, comparison operators etc.
	//
	// In libtorrent it is primarily used to hold info-hashes, piece-hashes,
	// peer IDs, node IDs etc.
	class TORRENT_EXPORT sha1_hash
	{
		// the number of bytes of the number
		enum { number_size = 20 };
	public:
		enum { size = number_size };

		sha1_hash() { clear(); }

		static sha1_hash max()
		{
			sha1_hash ret;
			memset(ret.m_number, 0xff, size);
			return ret;
		}

		static sha1_hash min()
		{
			sha1_hash ret;
			memset(ret.m_number, 0, size);
			return ret;
		}

		explicit sha1_hash(char const* s)
		{
			if (s == 0) clear();
			else std::memcpy(m_number, s, size);
		}

		explicit sha1_hash(std::string const& s)
		{
			TORRENT_ASSERT(s.size() >= 20);
			int sl = int(s.size()) < size ? int(s.size()) : size;
			std::memcpy(m_number, s.c_str(), sl);
		}

		void assign(std::string const& s)
		{
			TORRENT_ASSERT(s.size() >= 20);
			int sl = int(s.size()) < size ? int(s.size()) : size;
			std::memcpy(m_number, s.c_str(), sl);
		}

		void assign(char const* str) { std::memcpy(m_number, str, size); }
		void clear() { std::memset(m_number, 0, number_size); }

		bool is_all_zeros() const
		{
			for (const unsigned char* i = m_number; i < m_number+number_size; ++i)
				if (*i != 0) return false;
			return true;
		}

		sha1_hash& operator<<=(int n)
		{
			TORRENT_ASSERT(n >= 0);
			int num_bytes = n / 8;
			if (num_bytes >= number_size)
			{
				std::memset(m_number, 0, number_size);
				return *this;
			}

			if (num_bytes > 0)
			{
				std::memmove(m_number, m_number + num_bytes, number_size - num_bytes);
				std::memset(m_number + number_size - num_bytes, 0, num_bytes);
				n -= num_bytes * 8;
			}
			if (n > 0)
			{
				for (int i = 0; i < number_size - 1; ++i)
				{
					m_number[i] <<= n;
					m_number[i] |= m_number[i+1] >> (8 - n);
				}
				m_number[number_size-1] <<= n;
			}
			return *this;
		}

		sha1_hash& operator>>=(int n)
		{
			TORRENT_ASSERT(n >= 0);
			int num_bytes = n / 8;
			if (num_bytes >= number_size)
			{
				std::memset(m_number, 0, number_size);
				return *this;
			}
			if (num_bytes > 0)
			{
				std::memmove(m_number + num_bytes, m_number, number_size - num_bytes);
				std::memset(m_number, 0, num_bytes);
				n -= num_bytes * 8;
			}
			if (n > 0)
			{
				for (int i = number_size - 1; i > 0; --i)
				{
					m_number[i] >>= n;
					m_number[i] |= m_number[i-1] << (8 - n);
				}
				m_number[0] >>= n;
			}
			return *this;
		}

		bool operator==(sha1_hash const& n) const
		{
			return std::equal(n.m_number, n.m_number+number_size, m_number);
		}

		bool operator!=(sha1_hash const& n) const
		{
			return !std::equal(n.m_number, n.m_number+number_size, m_number);
		}

		bool operator<(sha1_hash const& n) const
		{
			for (int i = 0; i < number_size; ++i)
			{
				if (m_number[i] < n.m_number[i]) return true;
				if (m_number[i] > n.m_number[i]) return false;
			}
			return false;
		}
		
		sha1_hash operator~()
		{
			sha1_hash ret;
			for (int i = 0; i< number_size; ++i)
				ret.m_number[i] = ~m_number[i];
			return ret;
		}
		
		sha1_hash operator^(sha1_hash const& n) const
		{
			sha1_hash ret = *this;
			ret ^= n;
			return ret;
		}

		sha1_hash operator&(sha1_hash const& n) const
		{
			sha1_hash ret = *this;
			ret &= n;
			return ret;
		}

		sha1_hash& operator&=(sha1_hash const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] &= n.m_number[i];
			return *this;
		}

		sha1_hash& operator|=(sha1_hash const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] |= n.m_number[i];
			return *this;
		}

		sha1_hash& operator^=(sha1_hash const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] ^= n.m_number[i];
			return *this;
		}
		
		unsigned char& operator[](int i)
		{ TORRENT_ASSERT(i >= 0 && i < number_size); return m_number[i]; }

		unsigned char const& operator[](int i) const
		{ TORRENT_ASSERT(i >= 0 && i < number_size); return m_number[i]; }

		typedef const unsigned char* const_iterator;
		typedef unsigned char* iterator;

		const_iterator begin() const { return m_number; }
		const_iterator end() const { return m_number+number_size; }

		iterator begin() { return m_number; }
		iterator end() { return m_number+number_size; }

		std::string to_string() const
		{ return std::string((char const*)&m_number[0], number_size); }

	private:

		unsigned char m_number[number_size];

	};

	typedef sha1_hash peer_id;
	typedef sha1_hash sha1_hash;

#if TORRENT_USE_IOSTREAM
	inline std::ostream& operator<<(std::ostream& os, sha1_hash const& peer)
	{
		char out[41];
		to_hex((char const*)&peer[0], sha1_hash::size, out);
		return os << out;
	}

	inline std::istream& operator>>(std::istream& is, sha1_hash& peer)
	{
		char hex[40];
		is.read(hex, 40);
		if (!from_hex(hex, 40, (char*)&peer[0]))
			is.setstate(std::ios_base::failbit);
		return is;
	}
#endif // TORRENT_USE_IOSTREAM
}

#endif // TORRENT_PEER_ID_HPP_INCLUDED
