/*

Copyright (c) 2010, Arvid Norberg
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

#ifndef TORRENT_BLOOM_FILTER_HPP_INCLUDED
#define TORRENT_BLOOM_FILTER_HPP_INCLUDED

#include <boost/cstdint.hpp>
#include "libtorrent/peer_id.hpp" // for sha1_hash

namespace libtorrent
{
	void set_bit(boost::uint32_t b, boost::uint8_t* bits, int len);
	bool has_bit(boost::uint32_t b, boost::uint8_t const* bits, int len);

	template <int N>
	struct bloom_filter
	{
		bool find(sha1_hash const& k) const
		{
			return has_bit(k[0], bits, N)
				&& has_bit(k[1], bits, N)
				&& has_bit(k[2], bits, N);
		}

		void set(sha1_hash const& k)
		{
			set_bit(k[0], bits, N);
			set_bit(k[1], bits, N);
			set_bit(k[2], bits, N);
		}

		void clear() { memset(bits, 0, N); }

		bloom_filter() { clear(); }

	private:
		boost::uint8_t bits[N];
	};

}

#endif

