/*

Copyright (c) 2013-2016, Arvid Norberg
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
#include <cstring> // for memset

namespace libtorrent {


	counters::counters()
	{
#ifdef ATOMIC_LLONG_LOCK_FREE
		for (auto& counter : m_stats_counter)
			counter.store(0, std::memory_order_relaxed);
#else
		std::memset(m_stats_counter, 0, sizeof(m_stats_counter));
#endif
	}

	counters::counters(counters const& c)
	{
#ifdef ATOMIC_LLONG_LOCK_FREE
		for (std::size_t i = 0; i < m_stats_counter.size(); ++i)
			m_stats_counter[i].store(
				c.m_stats_counter[i].load(std::memory_order_relaxed)
					, std::memory_order_relaxed);
#else
		std::lock_guard<std::mutex> l(c.m_mutex);
		std::memcpy(m_stats_counter, c.m_stats_counter, sizeof(m_stats_counter));
#endif
	}

	counters& counters::operator=(counters const& c)
	{
#ifdef ATOMIC_LLONG_LOCK_FREE
		for (std::size_t i = 0; i < m_stats_counter.size(); ++i)
			m_stats_counter[i].store(
				c.m_stats_counter[i].load(std::memory_order_relaxed)
					, std::memory_order_relaxed);
#else
		std::lock_guard<std::mutex> l(m_mutex);
		std::lock_guard<std::mutex> l2(c.m_mutex);
		std::memcpy(m_stats_counter, c.m_stats_counter, sizeof(m_stats_counter));
#endif
		return *this;
	}

	std::int64_t counters::operator[](int i) const
	{
		TORRENT_ASSERT(i >= 0);
		TORRENT_ASSERT(i < num_counters);

#ifdef ATOMIC_LLONG_LOCK_FREE
		return m_stats_counter[i].load(std::memory_order_relaxed);
#else
		std::lock_guard<std::mutex> l(m_mutex);
		return m_stats_counter[i];
#endif
	}

	// the argument specifies which counter to
	// increment or decrement
	std::int64_t counters::inc_stats_counter(int c, std::int64_t value)
	{
		// if c >= num_stats_counters, it means it's not
		// a monotonically increasing counter, but a gauge
		// and it's allowed to be decremented
		TORRENT_ASSERT(value >= 0 || c >= num_stats_counters);
		TORRENT_ASSERT(c >= 0);
		TORRENT_ASSERT(c < num_counters);

#ifdef ATOMIC_LLONG_LOCK_FREE
		std::int64_t pv = m_stats_counter[c].fetch_add(value, std::memory_order_relaxed);
		TORRENT_ASSERT(pv + value >= 0);
		return pv + value;
#else
		std::lock_guard<std::mutex> l(m_mutex);
		TORRENT_ASSERT(m_stats_counter[c] + value >= 0);
		return m_stats_counter[c] += value;
#endif
	}

	// ratio is a vaue between 0 and 100 representing the percentage the value
	// is blended in at.
	void counters::blend_stats_counter(int c, std::int64_t value, int ratio)
	{
		TORRENT_ASSERT(c >= num_stats_counters);
		TORRENT_ASSERT(c < num_counters);
		TORRENT_ASSERT(ratio >= 0);
		TORRENT_ASSERT(ratio <= 100);

#ifdef ATOMIC_LLONG_LOCK_FREE
		std::int64_t current = m_stats_counter[c].load(std::memory_order_relaxed);
		std::int64_t new_value = (current * (100-ratio) + value * ratio) / 100;

		while (!m_stats_counter[c].compare_exchange_weak(current, new_value
			, std::memory_order_relaxed))
		{
			new_value = (current * (100-ratio) + value * ratio) / 100;
		}
#else
		std::lock_guard<std::mutex> l(m_mutex);
		std::int64_t current = m_stats_counter[c];
		m_stats_counter[c] = (current * (100-ratio) + value * ratio) / 100;
#endif
	}

	void counters::set_value(int c, std::int64_t value)
	{
		TORRENT_ASSERT(c >= 0);
		TORRENT_ASSERT(c < num_counters);

#ifdef ATOMIC_LLONG_LOCK_FREE
		m_stats_counter[c].store(value);
#else
		std::lock_guard<std::mutex> l(m_mutex);

		// if this assert fires, someone is trying to decrement a counter
		// which is not allowed. Counters are monotonically increasing
		TORRENT_ASSERT(value >= m_stats_counter[c] || c >= num_stats_counters);

		m_stats_counter[c] = value;
#endif
	}

}
