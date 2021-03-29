/*

Copyright (c) 2010, 2013-2020, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/performance_counters.hpp"
#include "libtorrent/assert.hpp"
#include <cstring> // for memset

namespace lt {

	// TODO: move stats_counter_t out of counters
	// TODO: should bittorrent keep-alive messages have a counter too?
	// TODO: It would be nice if this could be an internal type. default_disk_constructor depends on it now
	counters::counters() TORRENT_COUNTER_NOEXCEPT
	{
#ifdef ATOMIC_LLONG_LOCK_FREE
		for (auto& counter : m_stats_counter)
			counter.store(0, std::memory_order_relaxed);
#else
		m_stats_counter.fill(0);
#endif
	}

	counters::counters(counters const& c) TORRENT_COUNTER_NOEXCEPT
	{
#ifdef ATOMIC_LLONG_LOCK_FREE
		for (int i = 0; i < m_stats_counter.end_index(); ++i)
			m_stats_counter[i].store(
				c.m_stats_counter[i].load(std::memory_order_relaxed)
					, std::memory_order_relaxed);
#else
		std::lock_guard<std::mutex> l(c.m_mutex);
		m_stats_counter = c.m_stats_counter;
#endif
	}

	counters& counters::operator=(counters const& c) & TORRENT_COUNTER_NOEXCEPT
	{
		if (&c == this) return *this;
#ifdef ATOMIC_LLONG_LOCK_FREE
		for (int i = 0; i < m_stats_counter.end_index(); ++i)
			m_stats_counter[i].store(
				c.m_stats_counter[i].load(std::memory_order_relaxed)
					, std::memory_order_relaxed);
#else
		std::lock_guard<std::mutex> l(m_mutex);
		std::lock_guard<std::mutex> l2(c.m_mutex);
		m_stats_counter = c.m_stats_counter;
#endif
		return *this;
	}

	std::int64_t counters::operator[](int i) const TORRENT_COUNTER_NOEXCEPT
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
	std::int64_t counters::inc_stats_counter(int const c, std::int64_t const value) TORRENT_COUNTER_NOEXCEPT
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

	// ratio is a value between 0 and 100 representing the percentage the value
	// is blended in at.
	void counters::blend_stats_counter(int const c, std::int64_t const value, int const ratio) TORRENT_COUNTER_NOEXCEPT
	{
		TORRENT_ASSERT(c >= num_stats_counters);
		TORRENT_ASSERT(c < num_counters);
		TORRENT_ASSERT(ratio >= 0);
		TORRENT_ASSERT(ratio <= 100);

#ifdef ATOMIC_LLONG_LOCK_FREE
		std::int64_t current = m_stats_counter[c].load(std::memory_order_relaxed);
		std::int64_t new_value = (current * (100 - ratio) + value * ratio) / 100;

		while (!m_stats_counter[c].compare_exchange_weak(current, new_value
			, std::memory_order_relaxed))
		{
			new_value = (current * (100 - ratio) + value * ratio) / 100;
		}
#else
		std::lock_guard<std::mutex> l(m_mutex);
		std::int64_t current = m_stats_counter[c];
		m_stats_counter[c] = (current * (100 - ratio) + value * ratio) / 100;
#endif
	}

	void counters::set_value(int const c, std::int64_t const value) TORRENT_COUNTER_NOEXCEPT
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
