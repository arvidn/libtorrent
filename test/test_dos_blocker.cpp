/*

Copyright (c) 2010, 2014-2017, 2019-2021, Arvid Norberg
Copyright (c) 2016, 2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/kademlia/dos_blocker.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/socket_io.hpp" // for print_endpoint
#include <cstdarg>
#include <string>

using namespace lt;

#ifndef TORRENT_DISABLE_LOGGING
struct log_t : lt::dht::dht_logger
{
	bool should_log(module_t) const override { return true; }

	void log(dht_logger::module_t, char const* fmt, ...)
		override TORRENT_FORMAT(3, 4)
	{
		va_list v;
		va_start(v, fmt);
		std::vprintf(fmt, v);
		va_end(v);
	}

	void log_packet(message_direction_t dir, span<char const> pkt
		, udp::endpoint const& node) override
	{
		lt::bdecode_node print;
		lt::error_code ec;
		int ret = bdecode(pkt.data(), pkt.data() + int(pkt.size()), print, ec, nullptr, 100, 100);
		TEST_EQUAL(ret, 0);

		std::string msg = print_entry(print, true);
		std::printf("%s", msg.c_str());

		char const* prefix[2] = { "<==", "==>"};
		std::printf("%s [%s] %s", prefix[dir], aux::print_endpoint(node).c_str()
			, msg.c_str());
	}

	virtual ~log_t() = default;
};
#endif

TORRENT_TEST(dos_blocker_response_bytes)
{
#ifndef TORRENT_DISABLE_LOGGING
#ifndef TORRENT_DISABLE_DHT
	using namespace lt::dht;

	log_t l;
	dos_blocker b;

	address spammer = make_address_v4("10.10.10.10");

	time_point32 now = aux::time_now32();

	// a spread of small responses to unrelated destinations should never
	// come close to any single destination's budget
	for (int i = 0; i < 1000; ++i)
	{
		TEST_EQUAL(b.account_response(rand_v4(), 50, now, &l), true);
	}

	// repeatedly sending response bytes to the same destination should
	// eventually exceed its budget and get it banned
	bool banned = false;
	for (int i = 0; i < 1000 && !banned; ++i)
	{
		if (!b.account_response(spammer, 100, now, &l))
			banned = true;
	}

	TEST_EQUAL(banned, true);
	TEST_EQUAL(b.should_ignore(spammer, now), true);

	// an unrelated destination is unaffected by the ban
	TEST_EQUAL(b.should_ignore(rand_v4(), now), false);

	// the ban is lifted once the block timeout has passed
	now += seconds32{5 * 60 + 1};
	TEST_EQUAL(b.should_ignore(spammer, now), false);
#endif
#endif
}

TORRENT_TEST(dos_blocker_query_rate)
{
#ifndef TORRENT_DISABLE_LOGGING
#ifndef TORRENT_DISABLE_DHT
	using namespace lt::dht;

	log_t l;
	dos_blocker b;

	address spammer = make_address_v4("10.10.10.11");

	time_point32 now = aux::time_now32();

	// a spread of queries from unrelated sources should never come close to
	// any single source's rate limit
	for (int i = 0; i < 1000; ++i)
	{
		TEST_EQUAL(b.incoming(rand_v4(), now, &l), true);
	}

	// repeatedly querying from the same source should eventually exceed its
	// rate and get it banned, even though each query is cheap on its own
	bool banned = false;
	for (int i = 0; i < 1000 && !banned; ++i)
	{
		if (!b.incoming(spammer, now, &l))
			banned = true;
	}

	TEST_EQUAL(banned, true);
	TEST_EQUAL(b.should_ignore(spammer, now), true);

	// an unrelated source is unaffected by the ban
	TEST_EQUAL(b.should_ignore(rand_v4(), now), false);

	// the ban is lifted once the block timeout has passed
	now += seconds32{5 * 60 + 1};
	TEST_EQUAL(b.should_ignore(spammer, now), false);
#endif
#endif
}

TORRENT_TEST(dos_blocker_oversized_update_after_window_expiry)
{
#ifndef TORRENT_DISABLE_LOGGING
#ifndef TORRENT_DISABLE_DHT
	using namespace lt::dht;

	log_t l;
	dos_blocker b;
	b.set_byte_limit(1000);

	address spammer = make_address_v4("10.10.10.12");

	time_point32 now = aux::time_now32();

	// a small, unremarkable first response, well under the budget
	TEST_EQUAL(b.account_response(spammer, 10, now, &l), true);

	// let the accumulation window (10 seconds) expire without ever coming
	// close to the limit
	now += seconds32{11};

	// a single response, on its own, several times over the byte budget,
	// arriving right as a fresh window starts: this must be rejected and
	// ban the source immediately, not be let through for free just because
	// the window happened to just roll over
	TEST_EQUAL(b.account_response(spammer, 2000, now, &l), false);
	TEST_EQUAL(b.should_ignore(spammer, now), true);
#endif
#endif
}

TORRENT_TEST(dos_blocker_lru_tie_break)
{
#ifndef TORRENT_DISABLE_LOGGING
#ifndef TORRENT_DISABLE_DHT
	using namespace lt::dht;

	log_t l;
	dos_blocker b;

	// a rate limit of 1 still means 10 hits before banning (the window is
	// 10 seconds), so a surviving entry needs 9 more hits after its
	// initial one to reach the threshold.
	b.set_rate_limit(1);

	time_point32 const now = aux::time_now32();

	auto const addr_from_index = [](int const i) {
		return make_address_v4(
			"10.0." + std::to_string((i >> 8) & 0xff) + "." + std::to_string(i & 0xff));
	};

	// dos_blocker tracks at most 20 query-rate entries; fill all of them
	// with distinct addresses, all at the same 1-second timestamp, so
	// every later insertion has to break a full 20-way tie for LRU
	// eviction.
	int const num_entries = 20;
	for (int i = 0; i < num_entries; ++i)
	{
		TEST_EQUAL(b.incoming(addr_from_index(i), now, &l), true);
	}

	// a long burst of evictions, all still tied at the same timestamp;
	// with fair (random) tie-breaking, every original entry should get
	// evicted at least once over this many insertions.
	int const storm = 4000;
	for (int i = 0; i < storm; ++i)
	{
		b.incoming(addr_from_index(num_entries + i), now, &l);
	}

	// an entry that survived the storm still has its original count of 1;
	// 9 more hits bring it to the ban threshold of 10. An entry that got
	// evicted starts over from scratch, so 9 more hits only bring it to
	// 9, short of the threshold.
	int survived = 0;
	for (int i = 0; i < num_entries; ++i)
	{
		bool banned = false;
		for (int j = 0; j < 9; ++j)
			banned = !b.incoming(addr_from_index(i), now, &l);
		if (banned)
			++survived;
	}

	// with a tie-break biased toward always evicting the same slot, 19 of
	// the 20 original entries would never be touched by the storm and
	// would all still show up as "survived" here.
	TEST_CHECK(survived <= 1);
#endif
#endif
}
