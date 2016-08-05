/*

Copyright (c) 2016, Arvid Norberg
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

#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/aux_/cpuid.hpp"
#include "libtorrent/aux_/ffs.hpp"
#include "libtorrent/hex.hpp" // to_hex, from_hex

#if TORRENT_USE_IOSTREAM
#include <iostream>
#include <iomanip>
#endif // TORRENT_USE_IOSTREAM

namespace libtorrent
{
#if TORRENT_USE_IOSTREAM

	// print a sha1_hash object to an ostream as 40 hexadecimal digits
	std::ostream& operator<<(std::ostream& os, sha1_hash const& peer)
	{
		char out[sha1_hash::size() * 2 + 1];
		aux::to_hex(peer, out);
		return os << out;
	}

	// read 40 hexadecimal digits from an istream into a sha1_hash
	std::istream& operator>>(std::istream& is, sha1_hash& peer)
	{
		char hex[sha1_hash::size() * 2];
		is.read(hex, sha1_hash::size() * 2);
		if (!aux::from_hex(hex, peer.data()))
			is.setstate(std::ios_base::failbit);
		return is;
	}

#endif // TORRENT_USE_IOSTREAM

	int sha1_hash::count_leading_zeroes() const
	{
#if TORRENT_HAS_BUILTIN_CLZ || defined _MSC_VER
		return aux::clz_hw({m_number, number_size});
#else
		return aux::clz_sw({m_number, number_size});
#endif
	}

	sha1_hash& sha1_hash::operator<<=(int n)
	{
		TORRENT_ASSERT(n >= 0);
		const int num_words = n / 32;
		if (num_words >= number_size)
		{
			std::memset(m_number, 0, size());
			return *this;
		}

		if (num_words > 0)
		{
			std::memmove(m_number, m_number + num_words
				, (number_size - num_words) * sizeof(std::uint32_t));
			std::memset(m_number + (number_size - num_words)
				, 0, num_words * sizeof(std::uint32_t));
			n -= num_words * 32;
		}
		if (n > 0)
		{
			// keep in mind that the uint32_t are stored in network
			// byte order, so they have to be byteswapped before
			// applying the shift operations, and then byteswapped
			// back again.
			m_number[0] = aux::network_to_host(m_number[0]);
			for (int i = 0; i < number_size - 1; ++i)
			{
				m_number[i] <<= n;
				m_number[i+1] = aux::network_to_host(m_number[i+1]);
				m_number[i] |= m_number[i+1] >> (32 - n);
				m_number[i] = aux::host_to_network(m_number[i]);
			}
			m_number[number_size-1] <<= n;
			m_number[number_size-1] = aux::host_to_network(m_number[number_size-1]);
		}
		return *this;
	}

	sha1_hash& sha1_hash::operator>>=(int n)
	{
		TORRENT_ASSERT(n >= 0);
		const int num_words = n / 32;
		if (num_words >= number_size)
		{
			std::memset(m_number, 0, size());
			return *this;
		}
		if (num_words > 0)
		{
			std::memmove(m_number + num_words
				, m_number, (number_size - num_words) * sizeof(std::uint32_t));
			std::memset(m_number, 0, num_words * sizeof(std::uint32_t));
			n -= num_words * 32;
		}
		if (n > 0)
		{
			// keep in mind that the uint32_t are stored in network
			// byte order, so they have to be byteswapped before
			// applying the shift operations, and then byteswapped
			// back again.
			m_number[number_size-1] = aux::network_to_host(m_number[number_size-1]);

			for (int i = number_size - 1; i > 0; --i)
			{
				m_number[i] >>= n;
				m_number[i-1] = aux::network_to_host(m_number[i-1]);
				m_number[i] |= (m_number[i-1] << (32 - n)) & 0xffffffff;
				m_number[i] = aux::host_to_network(m_number[i]);
			}
			m_number[0] >>= n;
			m_number[0] = aux::host_to_network(m_number[0]);
		}
		return *this;
	}

}

