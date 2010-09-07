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

#ifndef TORRENT_PEER_ID_HPP_INCLUDED
#define TORRENT_PEER_ID_HPP_INCLUDED

#include <iostream>
#include <iomanip>
#include <cctype>
#include <algorithm>
#include <string>
#include <cstring>

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/escape_string.hpp"

namespace libtorrent
{

	class TORRENT_EXPORT big_number
	{
		// the number of bytes of the number
		enum { number_size = 20 };
	public:
		enum { size = number_size };

		big_number() {}

		explicit big_number(char const* s)
		{
			if (s == 0) clear();
			else std::memcpy(m_number, s, size);
		}

		explicit big_number(std::string const& s)
		{
			TORRENT_ASSERT(s.size() >= 20);
			int sl = int(s.size()) < size ? int(s.size()) : size;
			std::memcpy(m_number, &s[0], sl);
		}

		void assign(std::string const& s)
		{
			TORRENT_ASSERT(s.size() >= 20);
			int sl = int(s.size()) < size ? int(s.size()) : size;
			std::memcpy(m_number, &s[0], sl);
		}

		void clear()
		{
			std::fill(m_number,m_number+number_size,0);
		}

		bool is_all_zeros() const
		{
			return std::count(m_number,m_number+number_size,0) == number_size;
		}

		bool operator==(big_number const& n) const
		{
			return std::equal(n.m_number, n.m_number+number_size, m_number);
		}

		bool operator!=(big_number const& n) const
		{
			return !std::equal(n.m_number, n.m_number+number_size, m_number);
		}

		bool operator<(big_number const& n) const
		{
			for (int i = 0; i < number_size; ++i)
			{
				if (m_number[i] < n.m_number[i]) return true;
				if (m_number[i] > n.m_number[i]) return false;
			}
			return false;
		}
		
		big_number operator~()
		{
			big_number ret;
			for (int i = 0; i< number_size; ++i)
				ret.m_number[i] = ~m_number[i];
			return ret;
		}
		
		big_number& operator &= (big_number const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] &= n.m_number[i];
			return *this;
		}

		big_number& operator |= (big_number const& n)
		{
			for (int i = 0; i< number_size; ++i)
				m_number[i] |= n.m_number[i];
			return *this;
		}

		big_number& operator ^= (big_number const& n)
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

	typedef big_number peer_id;
	typedef big_number sha1_hash;

	inline std::ostream& operator<<(std::ostream& os, big_number const& peer)
	{
		for (big_number::const_iterator i = peer.begin();
			i != peer.end(); ++i)
		{
			os << std::hex << std::setw(2) << std::setfill('0')
				<< static_cast<unsigned int>(*i);
		}
		os << std::dec << std::setfill(' ');
		return os;
	}

	inline std::istream& operator>>(std::istream& is, big_number& peer)
	{
		for (big_number::iterator i = peer.begin();
			i != peer.end(); ++i)
		{
			char c[2];
			is >> c[0] >> c[1];
			c[0] = tolower(c[0]);
			c[1] = tolower(c[1]);
			if (
				((c[0] < '0' || c[0] > '9') && (c[0] < 'a' || c[0] > 'f'))
				|| ((c[1] < '0' || c[1] > '9') && (c[1] < 'a' || c[1] > 'f'))
				|| is.fail())
			{
				is.setstate(std::ios_base::failbit);
				return is;
			}
			*i = ((is_digit(c[0])?c[0]-'0':c[0]-'a'+10) << 4)
				+ (is_digit(c[1])?c[1]-'0':c[1]-'a'+10);
		}
		return is;
	}

}

#endif // TORRENT_PEER_ID_HPP_INCLUDED

