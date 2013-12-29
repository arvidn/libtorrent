/*

Copyright (c) 2013, Arvid Norberg
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

#include "libtorrent/performance_counters.hpp"
#include "libtorrent/assert.hpp"
#include <string.h> // for memset

#ifdef TORRENT_USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

namespace libtorrent {


	counters::counters()
	{
		memset(m_stats_counter, 0, sizeof(m_stats_counter));
	}

	boost::int64_t counters::operator[](int i) const
	{
		TORRENT_ASSERT(i >= 0);
		TORRENT_ASSERT(i < num_counters);
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_VALUE_IS_DEFINED(m_stats_counter[i]);
#endif
		return m_stats_counter[i];
	}

	// the argument specifies which counter to
	// increment or decrement
	boost::uint64_t counters::inc_stats_counter(int c, boost::int64_t value)
	{
		// if c >= num_stats_counters, it means it's not
		// a monotonically increasing counter, but a gauge
		// and it's allowed to be decremented
		TORRENT_ASSERT(value >= 0 || c >= num_stats_counters);
		TORRENT_ASSERT(c >= 0);
		TORRENT_ASSERT(c < num_counters);

		TORRENT_ASSERT(m_stats_counter[c] + value >= 0);
		return m_stats_counter[c] += value;
	}

	void counters::set_value(int c, boost::int64_t value)
	{
		TORRENT_ASSERT(c >= 0);
		TORRENT_ASSERT(c < num_counters);

		// if this assert fires, someone is trying to decrement a counter
		// which is not allowed. Counters are monotonically increasing
		TORRENT_ASSERT(value >= m_stats_counter[c] || c >= num_stats_counters);
	
		m_stats_counter[c] = value;
	}

}

