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

// TODO: Use two algorithms to estimate transfer rate.
// one (simple) for transfer rates that are >= 1 packet
// per second and one (low pass-filter) for rates < 1
// packet per second.

#include "libtorrent/pch.hpp"

#include <numeric>
#include <algorithm>

#include "libtorrent/stat.hpp"
#include "libtorrent/invariant_check.hpp"

namespace libtorrent
{

void stat_channel::second_tick(int tick_interval_ms)
{
	INVARIANT_CHECK;

	m_rate_sum -= m_rate_history[history-1];

	for (int i = history - 2; i >= 0; --i)
		m_rate_history[i + 1] = m_rate_history[i];

	m_rate_history[0] = size_type(m_counter) * 1000 / tick_interval_ms;
	TORRENT_ASSERT(m_rate_history[0] >= 0);
	m_rate_sum += m_rate_history[0];
	m_counter = 0;
}


}

