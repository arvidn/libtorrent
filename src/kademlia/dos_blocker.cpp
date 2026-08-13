/*

Copyright (c) 2010, 2014-2018, 2020-2021, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/kademlia/dos_blocker.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/random.hpp"

#include <cinttypes> // for PRId32 et.al.

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/aux_/socket_io.hpp" // for print_address
#include "libtorrent/kademlia/dht_observer.hpp" // for dht_logger
#endif

namespace libtorrent::dht {

bool dos_blocker::account(span<node_ban_entry> const entries,
	address const& addr,
	std::int32_t const amount,
	std::int32_t const limit,
	time_point32 const now,
	dht_logger* logger,
	char const* const what)
{
	TORRENT_UNUSED(logger);
	TORRENT_UNUSED(what);

	auto const ban = [&](node_ban_entry& e) {
#ifndef TORRENT_DISABLE_LOGGING
		if (logger != nullptr && logger->should_log(dht_logger::tracker))
		{
			logger->log(dht_logger::tracker,
				"BANNING PEER [ ip: %s %s: %" PRId32 " limit: %" PRId32 " time: %d s ]",
				aux::print_address(addr).c_str(),
				what,
				e.count,
				limit,
				m_block_timeout.count());
		}
#endif
		e.limit = now + m_block_timeout;
	};

	node_ban_entry* match = nullptr;
	node_ban_entry* lru = &entries[0];
	// number of entries seen so far tied with *lru for oldest last_active;
	// used to break ties uniformly at random (reservoir sampling) instead
	// of always keeping whichever tied entry was found first, which would
	// otherwise let one array slot become a revolving door under bursty
	// traffic, since time_point32 only has 1-second resolution
	int lru_ties = 1;
	for (node_ban_entry& e : entries)
	{
		if (e.src == addr)
		{
			match = &e;
			break;
		}
		if (e.last_active < lru->last_active)
		{
			lru = &e;
			lru_ties = 1;
		}
		else if (e.last_active == lru->last_active && &e != lru)
		{
			++lru_ties;
			if (aux::random(std::uint32_t(lru_ties - 1)) == 0)
				lru = &e;
		}
	}

	if (match == nullptr)
	{
		// evict the LRU slot to start tracking addr
		match = lru;
		match->src = addr;
		match->count = 0;
		match->last_active = now;
		match->limit = now + window_length;
	}
	else
	{
		// touched even while banned, so a banned entry isn't mistaken for
		// the LRU slot and evicted prematurely
		match->last_active = now;
	}

	match->count += amount;

	if (match->count >= limit)
	{
		if (now < match->limit)
		{
			// still within the window (or ban) "limit" currently marks the
			// end of; the update that pushes us over is what promotes it
			// from a window marker to a real ban
			if (match->count - amount < limit)
				ban(*match);
			return false;
		}

		// "limit" has already passed: whatever pushed count over the top
		// took longer than window_length to arrive, so it wasn't actually
		// a burst (or a prior ban has fully served its time), start a new
		// window, seeded with this update rather than discarding it, so
		// an update that's oversized on its own still gets banned
		match->count = amount;
		match->limit = now + window_length;

		if (match->count >= limit)
		{
			ban(*match);
			return false;
		}
	}

	return true;
}

bool dos_blocker::is_banned(span<node_ban_entry const> const entries,
	std::int32_t const limit,
	address const& addr,
	time_point32 const now)
{
	for (auto const& e : entries)
	{
		if (e.src == addr)
			return e.count >= limit && now < e.limit;
	}
	return false;
}

void dos_blocker::touch(
	span<node_ban_entry> const entries, address const& addr, time_point32 const now)
{
	for (node_ban_entry& e : entries)
	{
		if (e.src == addr)
		{
			e.last_active = now;
			return;
		}
	}
}

std::int32_t dos_blocker::query_rate_limit() const
{
	return aux::numeric_cast<std::int32_t>(
		std::int64_t(m_message_rate_limit) * window_length.count());
}

bool dos_blocker::incoming(address const& addr, time_point32 const now, dht_logger* logger)
{
	touch(m_response_entries, addr, now);

	return account(m_query_entries, addr, 1, query_rate_limit(), now, logger, "queries");
}

bool dos_blocker::account_response(
	address const& addr, int const bytes, time_point32 const now, dht_logger* logger)
{
	return account(
		m_response_entries, addr, bytes, m_response_byte_limit, now, logger, "bytes-sent");
}

bool dos_blocker::should_ignore(address const& addr, time_point32 const now) const
{
	return is_banned(m_query_entries, query_rate_limit(), addr, now)
		|| is_banned(m_response_entries, m_response_byte_limit, addr, now);
}
}
