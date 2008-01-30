/*

Copyright (c) 2003, Magnus Jonsson, Arvid Norberg
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

#ifndef TORRENT_RESOURCE_REQUEST_HPP_INCLUDED
#define TORRENT_RESOURCE_REQUEST_HPP_INCLUDED

#include <boost/integer_traits.hpp>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include "libtorrent/config.hpp"

namespace libtorrent
{
	struct TORRENT_EXPORT resource_request
	{
		resource_request()
			: used(0)
			, min(0)
			, max(0)
			, given(0)
			, leftovers(0)
		{}

		resource_request(int used_, int min_, int max_, int given_)
			: used(used_)
			, min(min_)
			, max(max_)
			, given(given_)
			, leftovers(0)
		{}

		int left() const
		{
			assert(given <= max);
			assert(given >= min);
			assert(used >= 0);
			return (std::max)(given - used, 0);
		}
		
		void reset() { used = leftovers; leftovers = 0; }

		static const int inf = boost::integer_traits<int>::const_max;

		// right now I'm actively using this amount
		int used;

		// given cannot be smaller than min
		// and not greater than max.
		int min;
		int max;

		// Reply: Okay, you're allowed to use this amount (a compromise):
		int given;

		// this is the amount of resources that exceeded the
		// given limit. When the used field is reset (after resources
		// have been distributed), it is reset to this number.
		int leftovers;
	};
}


#endif
