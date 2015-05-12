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
		enum { number_size = 20 };
	public:
		// the number of bytes of the number
		static const int size = number_size;

		// constructs an all-sero sha1-hash
		sha1_hash() { clear(); }

		// returns an all-F sha1-hash. i.e. the maximum value
		// representable by a 160 bit number (20 bytes). This is
		// a static member function.
		static sha1_hash max()
		{
			sha1_hash ret;
			memset(ret.m_number, 0xff, size);
			return ret;
		}

		// returns an all-zero sha1-hash. i.e. the minimum value
		// representable by a 160 bit number (20 bytes). This is
		// a static member function.
		static sha1_hash min()
		{
			sha1_hash ret;
			memset(ret.m_number, 0, size);
			return ret;
		}

		// copies 20 bytes from the pointer provided, into the sha1-hash.
		// The passed in string MUST be at least 20 bytes. NULL terminators
		// are ignored, ``s`` is treated like a raw memory buffer.
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

		// set the sha1-hash to all zeroes.
		void clear() { std::memset(m_number, 0, number_size); }

		// return true if the sha1-hash is all zero.
		bool is_all_zeros() const
		{
			for (const unsigned char* i = m_number; i < m_number+number_size; ++i)
				if (*i != 0) return false;
			return true;
		}

		// shift left ``n`` bits.
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

		// shift r ``n`` bits.
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
					m_number[i] |= (m_number[i-1] << (8 - n)) & 0xff;
				}
				m_number[0] >>= n;
			}
			return *this;
		}

		// standard comparison operators
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
		
		// returns a bit-wise negated copy of the sha1-hash
		sha1_hash operator~()
		{
			sha1_hash ret;
			for (int i = 0; i< number_size; ++i)
				ret.m_number[i] = ~m_number[i];
			return ret;
		}
		
		// returns the bit-wise XOR of the two sha1-hashes.
		sha1_hash operator^(sha1_hash const& n) const
		{
			sha1_hash ret = *this;
			ret ^= n;
			return ret;
		}

		// in-place bit-wise XOR with the passed in sha1_hash.
		sha1_hash& operator^=(sha1_hash const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] ^= n.m_number[i];
			return *this;
		}

		// returns the bit-wise AND of the two sha1-hashes.
		sha1_hash operator&(sha1_hash const& n) const
		{
			sha1_hash ret = *this;
			ret &= n;
			return ret;
		}

		// in-place bit-wise AND of the passed in sha1_hash
		sha1_hash& operator&=(sha1_hash const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] &= n.m_number[i];
			return *this;
		}

		// in-place bit-wise OR of the two sha1-hash.
		sha1_hash& operator|=(sha1_hash const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] |= n.m_number[i];
			return *this;
		}

		// accessors for specific bytes
		unsigned char& operator[](int i)
		{ TORRENT_ASSERT(i >= 0 && i < number_size); return m_number[i]; }
		unsigned char const& operator[](int i) const
		{ TORRENT_ASSERT(i >= 0 && i < number_size); return m_number[i]; }

		typedef const unsigned char* const_iterator;
		typedef unsigned char* iterator;

		// start and end iterators for the hash. The value type
		// of these iterators is ``unsigned char``.
		const_iterator begin() const { return m_number; }
		const_iterator end() const { return m_number+number_size; }
		iterator begin() { return m_number; }
		iterator end() { return m_number+number_size; }

		// return a copy of the 20 bytes representing the sha1-hash as a std::string.
		// It's still a binary string with 20 binary characters.
		std::string to_string() const
		{ return std::string((char const*)&m_number[0], number_size); }

	private:

		unsigned char m_number[number_size];

	};

	typedef sha1_hash peer_id;

#if TORRENT_USE_IOSTREAM

	// print a sha1_hash object to an ostream as 40 hexadecimal digits
	inline std::ostream& operator<<(std::ostream& os, sha1_hash const& peer)
	{
		char out[41];
		to_hex((char const*)&peer[0], sha1_hash::size, out);
		return os << out;
	}

	// read 40 hexadecimal digits from an istream into a sha1_hash
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

