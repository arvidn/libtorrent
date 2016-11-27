/*

Copyright (c) 2003-2016, Arvid Norberg
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
#include "libtorrent/aux_/byteswap.hpp"
#include "libtorrent/aux_/ffs.hpp"

#if TORRENT_USE_IOSTREAM
#include <iosfwd>
#endif // TORRENT_USE_IOSTREAM

namespace libtorrent
{
	// TODO: find a better place for these functions
	namespace aux
	{
		TORRENT_EXTRA_EXPORT void bits_shift_left(span<std::uint32_t> number, int n);
		TORRENT_EXTRA_EXPORT void bits_shift_right(span<std::uint32_t> number, int n);
	}

	// This type holds an N digest or any other kind of N bits
	// sequence. It implements a number of convenience functions, such
	// as bit operations, comparison operators etc.
	//
	// This data structure is 32 bits aligned, like it's the case for
	// each SHA-N specification.
	template <int N>
	class digest32
	{
		static_assert(N % 32 == 0, "N must be a multiple of 32");
		enum { number_size = N / 32 };
	public:

		// the size of the hash in bytes
		static constexpr size_t size() { return N / 8; }

		// constructs an all-zero digest
		digest32() { clear(); }

		digest32(digest32 const&) = default;
		digest32(digest32&&) = default;
		digest32& operator=(digest32 const&) = default;
		digest32& operator=(digest32&&) = default;

		// returns an all-F digest. i.e. the maximum value
		// representable by an N bit number (N/8 bytes). This is
		// a static member function.
		static digest32 max()
		{
			digest32 ret;
			std::memset(ret.m_number, 0xff, size());
			return ret;
		}

		// returns an all-zero digest. i.e. the minimum value
		// representable by an N bit number (N/8 bytes). This is
		// a static member function.
		static digest32 min()
		{
			digest32 ret;
			// all bits are already 0
			return ret;
		}

		// copies N/8 bytes from the pointer provided, into the digest.
		// The passed in string MUST be at least N/8 bytes. 0-terminators
		// are ignored, ``s`` is treated like a raw memory buffer.
		explicit digest32(char const* s)
		{
			if (s == nullptr) clear();
			else std::memcpy(m_number, s, size());
		}
#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED
		explicit digest32(std::string const& s)
		{
			assign(s.data());
		}
#endif
		explicit digest32(span<char const> s)
		{
			assign(s);
		}
		void assign(span<char const> s)
		{
			TORRENT_ASSERT(s.size() >= N / 8);
			size_t const sl = s.size() < size() ? s.size() : size();
			std::memcpy(m_number, s.data(), sl);
		}
		void assign(char const* str) { std::memcpy(m_number, str, size()); }

		char const* data() const { return reinterpret_cast<char const*>(&m_number[0]); }
		char* data() { return reinterpret_cast<char*>(&m_number[0]); }

		// set the digest to all zeroes.
		void clear() { std::memset(m_number, 0, size()); }

		// return true if the digest is all zero.
		bool is_all_zeros() const
		{
			for (int i = 0; i < number_size; ++i)
				if (m_number[i] != 0) return false;
			return true;
		}

		// shift left ``n`` bits.
		digest32& operator<<=(int n)
		{
			aux::bits_shift_left({m_number, number_size}, n);
			return *this;
		}

		// shift right ``n`` bits.
		digest32& operator>>=(int n)
		{
			aux::bits_shift_right({m_number, number_size}, n);
			return *this;
		}

		// standard comparison operators
		bool operator==(digest32 const& n) const
		{
			return std::equal(n.m_number, n.m_number + number_size, m_number);
		}
		bool operator!=(digest32 const& n) const
		{
			return !std::equal(n.m_number, n.m_number + number_size, m_number);
		}
		bool operator<(digest32 const& n) const
		{
			for (int i = 0; i < number_size; ++i)
			{
				std::uint32_t const lhs = aux::network_to_host(m_number[i]);
				std::uint32_t const rhs = aux::network_to_host(n.m_number[i]);
				if (lhs < rhs) return true;
				if (lhs > rhs) return false;
			}
			return false;
		}

		int count_leading_zeroes() const
		{
			return aux::count_leading_zeros({m_number, number_size});
		}

		// returns a bit-wise negated copy of the digest
		digest32 operator~() const
		{
			digest32 ret;
			for (int i = 0; i < number_size; ++i)
				ret.m_number[i] = ~m_number[i];
			return ret;
		}

		// returns the bit-wise XOR of the two digests.
		digest32 operator^(digest32 const& n) const
		{
			digest32 ret = *this;
			ret ^= n;
			return ret;
		}

		// in-place bit-wise XOR with the passed in digest.
		digest32& operator^=(digest32 const& n)
		{
			for (int i = 0; i < number_size; ++i)
				m_number[i] ^= n.m_number[i];
			return *this;
		}

		// returns the bit-wise AND of the two digests.
		digest32 operator&(digest32 const& n) const
		{
			digest32 ret = *this;
			ret &= n;
			return ret;
		}

		// in-place bit-wise AND of the passed in digest
		digest32& operator&=(digest32 const& n)
		{
			for (int i = 0; i < number_size; ++i)
				m_number[i] &= n.m_number[i];
			return *this;
		}

		// in-place bit-wise OR of the two digests.
		digest32& operator|=(digest32 const& n)
		{
			for (int i = 0; i < number_size; ++i)
				m_number[i] |= n.m_number[i];
			return *this;
		}

		// accessors for specific bytes
		std::uint8_t& operator[](std::size_t i)
		{
			TORRENT_ASSERT(i < size());
			return reinterpret_cast<std::uint8_t*>(m_number)[i];
		}
		std::uint8_t const& operator[](std::size_t i) const
		{
			TORRENT_ASSERT(i < size());
			return reinterpret_cast<std::uint8_t const*>(m_number)[i];
		}

		using const_iterator = std::uint8_t const*;
		using iterator = std::uint8_t*;

		// start and end iterators for the hash. The value type
		// of these iterators is ``std::uint8_t``.
		const_iterator begin() const
		{ return reinterpret_cast<std::uint8_t const*>(m_number); }
		const_iterator end() const
		{ return reinterpret_cast<std::uint8_t const*>(m_number) + size(); }
		iterator begin()
		{ return reinterpret_cast<std::uint8_t*>(m_number); }
		iterator end()
		{ return reinterpret_cast<std::uint8_t*>(m_number) + size(); }

		// return a copy of the N/8 bytes representing the digest as a std::string.
		// It's still a binary string with N/8 binary characters.
		std::string to_string() const
		{
			return std::string(reinterpret_cast<char const*>(&m_number[0]), size());
		}

	private:

		std::uint32_t m_number[number_size];

	};

	// This type holds a SHA-1 digest or any other kind of 20 byte
	// sequence. It implements a number of convenience functions, such
	// as bit operations, comparison operators etc.
	//
	// In libtorrent it is primarily used to hold info-hashes, piece-hashes,
	// peer IDs, node IDs etc.
	using sha1_hash = digest32<160>;

#if TORRENT_USE_IOSTREAM

	// print a sha1_hash object to an ostream as 40 hexadecimal digits
	TORRENT_EXPORT std::ostream& operator<<(std::ostream& os, sha1_hash const& peer);

	// read 40 hexadecimal digits from an istream into a sha1_hash
	TORRENT_EXPORT std::istream& operator>>(std::istream& is, sha1_hash& peer);

#endif // TORRENT_USE_IOSTREAM
}

namespace std
{
	template <>
	struct hash<libtorrent::sha1_hash>
	{
		std::size_t operator()(libtorrent::sha1_hash const& k) const
		{
			std::size_t ret;
			// this is OK because sha1_hash is already a hash
			std::memcpy(&ret, &k[0], sizeof(ret));
			return ret;
		}
	};
}

#endif // TORRENT_SHA1_HASH_HPP_INCLUDED
