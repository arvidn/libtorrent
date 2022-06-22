/*

Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2016-2020, Arvid Norberg
Copyright (c) 2017, Steven Siloti
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
#include "libtorrent/hex.hpp" // to_hex, from_hex

#if TORRENT_USE_IOSTREAM
#include <iostream>
#include <iomanip>
#endif // TORRENT_USE_IOSTREAM

namespace libtorrent {

#if TORRENT_USE_IOSTREAM

	// print a sha1_hash object to an ostream as 40 hexadecimal digits
	template <std::ptrdiff_t N>
	void digest32<N>::stream_out(std::ostream& os) const
	{
		os << aux::to_hex(*this);
	}

	// read hexadecimal digits from an istream into a digest32
	template <std::ptrdiff_t N>
	void digest32<N>::stream_in(std::istream& is)
	{
		char hex[size() * 2];
		is.read(hex, size() * 2);
		if (!aux::from_hex(hex, data()))
			is.setstate(std::ios_base::failbit);
	}

	// explicitly instantiate these template functions for sha1 and sha256
	template TORRENT_EXPORT void digest32<160>::stream_out(std::ostream&) const;
	template TORRENT_EXPORT void digest32<256>::stream_out(std::ostream&) const;

	template TORRENT_EXPORT void digest32<160>::stream_in(std::istream&);
	template TORRENT_EXPORT void digest32<256>::stream_in(std::istream&);

#endif // TORRENT_USE_IOSTREAM

namespace {

	void bits_shift_left(span<std::uint32_t> const number, int n) noexcept
	{
		TORRENT_ASSERT(n >= 0);
		int const num_words = n / 32;
		int const number_size = int(number.size());
		if (num_words >= number_size)
		{
			std::memset(number.data(), 0, std::size_t(number.size() * 4));
			return;
		}

		if (num_words > 0)
		{
			std::memmove(number.data(), number.data() + num_words
				, std::size_t(number_size - num_words) * sizeof(std::uint32_t));
			std::memset(number.data() + (number_size - num_words)
				, 0, std::size_t(num_words) * sizeof(std::uint32_t));
			n -= num_words * 32;
		}
		if (n > 0)
		{
			// keep in mind that the uint32_t are stored in network
			// byte order, so they have to be byteswapped before
			// applying the shift operations, and then byteswapped
			// back again.
			number[0] = aux::network_to_host(number[0]);
			for (int i = 0; i < number_size - 1; ++i)
			{
				number[i] <<= n;
				number[i + 1] = aux::network_to_host(number[i + 1]);
				number[i] |= number[i + 1] >> (32 - n);
				number[i] = aux::host_to_network(number[i]);
			}
			number[number_size - 1] <<= n;
			number[number_size - 1] = aux::host_to_network(number[number_size - 1]);
		}
	}

	void bits_shift_right(span<std::uint32_t> const number, int n) noexcept
	{
		TORRENT_ASSERT(n >= 0);
		int const num_words = n / 32;
		int const number_size = int(number.size());
		if (num_words >= number_size)
		{
			std::memset(number.data(), 0, std::size_t(number.size() * 4));
			return;
		}
		if (num_words > 0)
		{
			std::memmove(number.data() + num_words
				, number.data(), std::size_t(number_size - num_words) * sizeof(std::uint32_t));
			std::memset(number.data(), 0, std::size_t(num_words) * sizeof(std::uint32_t));
			n -= num_words * 32;
		}
		if (n > 0)
		{
			// keep in mind that the uint32_t are stored in network
			// byte order, so they have to be byteswapped before
			// applying the shift operations, and then byteswapped
			// back again.
			number[number_size - 1] = aux::network_to_host(number[number_size - 1]);

			for (int i = number_size - 1; i > 0; --i)
			{
				number[i] >>= n;
				number[i - 1] = aux::network_to_host(number[i - 1]);
				number[i] |= (number[i - 1] << (32 - n)) & 0xffffffff;
				number[i] = aux::host_to_network(number[i]);
			}
			number[0] >>= n;
			number[0] = aux::host_to_network(number[0]);
		}
	}
}

		// shift left ``n`` bits.
	template <std::ptrdiff_t N>
	digest32<N>& digest32<N>::operator<<=(int const n) & noexcept
	{
		bits_shift_left(m_number, n);
		return *this;
	}

	// shift right ``n`` bits.
	template <std::ptrdiff_t N>
	digest32<N>& digest32<N>::operator>>=(int const n) & noexcept
	{
		bits_shift_right(m_number, n);
		return *this;
	}

	template TORRENT_EXPORT digest32<160>& digest32<160>::operator<<=(int) & noexcept;
	template TORRENT_EXPORT digest32<256>& digest32<256>::operator<<=(int) & noexcept;
	template TORRENT_EXPORT digest32<160>& digest32<160>::operator>>=(int) & noexcept;
	template TORRENT_EXPORT digest32<256>& digest32<256>::operator>>=(int) & noexcept;

	static_assert(std::is_nothrow_move_constructible<sha1_hash>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_move_assignable<sha1_hash>::value
		, "should be nothrow move assignable");
	static_assert(std::is_nothrow_default_constructible<sha1_hash>::value
		, "should be nothrow default constructible");
}
