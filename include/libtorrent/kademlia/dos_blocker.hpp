/*

Copyright (c) 2010, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DHT_DOS_BLOCKER
#define TORRENT_DHT_DOS_BLOCKER

#include <array>
#include <cstdint>
#include <limits>

#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/span.hpp"

namespace libtorrent {
namespace dht {

	struct dht_logger;

	// blocks abusive DHT peers. Two independent triggers ban an address:
	// query rate, bounding the lookup/verification/response work a flood
	// of cheap queries can force regardless of response size; and response
	// bytes, bounding how much this node can be abused as a reflection/
	// amplification vector against a spoofed source, independent of query
	// count.
	struct TORRENT_EXTRA_EXPORT dos_blocker
	{
		// call once for every incoming DHT message, regardless of type, to
		// account it against addr's packet-rate budget; a fresh violation
		// bans addr for m_block_timeout. Whether to actually drop the
		// message is decided later, per type (see should_ignore()). Also
		// refreshes addr's response-byte entry's last_active, if it has
		// one, so a still-banned source isn't LRU-evicted by unrelated
		// response traffic while it keeps sending us packets.
		bool incoming(address const& addr, time_point32 now, dht_logger* logger);

		// max queries accepted from a single source per window before banning it.
		void set_rate_limit(int const l)
		{
			// query_rate_limit() multiplies this by window_length.count(); l must
			// stay small enough for that to not overflow std::int32_t
			TORRENT_ASSERT_PRECOND_VAL(
				l <= (std::numeric_limits<std::int32_t>::max)() / window_length.count(), l);
			m_message_rate_limit = std::max(1, l);
		}

		// max response bytes sent to a single destination per window before banning it.
		void set_byte_limit(int const l) { m_response_byte_limit = std::max(1, l); }

		// call before sending a response, with its size. Accounts the bytes
		// against addr's budget, returns false if the response should be
		// dropped, and bans addr for m_block_timeout if this exceeds budget.
		bool account_response(address const& addr, int bytes, time_point32 now, dht_logger* logger);

		// read-only peek at whether addr is currently banned; doesn't affect
		// either method's accounting. A banned address may still be a
		// legitimate node awaiting our reply, so only call this for incoming
		// queries, not replies to our own outgoing ones.
		bool should_ignore(address const& addr, time_point32 now) const;

		void set_block_timer(int const t) { m_block_timeout = seconds32{std::max(1, t)}; }

	private:
		// per-address query count or response byte count within the current
		// window, and ban state. "count" is a query count in one table, a
		// byte count in the other.
		struct node_ban_entry
		{
			address src;
			// while count < the table's limit, marks the end of the current
			// accumulation window (now >= limit means it ran too long to
			// be a real burst, so count resets instead of banning); once
			// count >= limit, marks the end of the ban instead. count >=
			// limit disambiguates which meaning currently applies (see
			// account() and is_banned()).
			time_point32 limit = (time_point32::min)();
			// touched on every match, even while banned, so a banned entry
			// isn't mistaken for the LRU slot and evicted prematurely
			time_point32 last_active = (time_point32::min)();
			std::int32_t count = 0;
		};

		// shared by incoming() and account_response(): finds or evicts the
		// LRU slot for addr, accumulates amount, and bans past limit for
		// m_block_timeout (see node_ban_entry::limit). "what" names the
		// quantity, for logging.
		bool account(span<node_ban_entry> entries,
			address const& addr,
			std::int32_t amount,
			std::int32_t limit,
			time_point32 now,
			dht_logger* logger,
			char const* what);

		// used by should_ignore() for both tables; "limit" must be the same
		// threshold passed as account()'s "limit" for this table, since
		// node_ban_entry::limit's meaning depends on it.
		static bool is_banned(span<node_ban_entry const> entries,
			std::int32_t limit,
			address const& addr,
			time_point32 now);

		// refreshes addr's entry's last_active, if it has one; never
		// creates a new entry. Used by incoming() to keep a response-byte
		// ban entry from looking idle while its source keeps sending us
		// packets (see incoming()'s comment).
		static void touch(span<node_ban_entry> entries, address const& addr, time_point32 now);

		// the query-rate table's current threshold, shared by incoming()
		// and should_ignore()
		std::int32_t query_rate_limit() const;

		static constexpr int num_query_entries = 20;
		static constexpr int num_response_entries = 20;

		// accumulation window for the limits below
		static constexpr seconds32 window_length{10};

		// max queries accepted from a single source per window_length before banning it.
		int m_message_rate_limit = 5;

		// max response bytes sent to a single destination per window_length before banning it.
		int m_response_byte_limit = 8192;

		// ban duration once either limit above is exceeded
		seconds32 m_block_timeout{5 * 60};

		std::array<node_ban_entry, num_query_entries> m_query_entries;
		std::array<node_ban_entry, num_response_entries> m_response_entries;
	};
}
}

#endif
