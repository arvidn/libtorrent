/*

Copyright (c) 2010, 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/kademlia/dos_blocker.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/socket_io.hpp" // for print_endpoint
#include <cstdarg>

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

TORRENT_TEST(dos_blocker)
{
#ifndef TORRENT_DISABLE_LOGGING
#ifndef TORRENT_DISABLE_DHT
	using namespace lt::dht;

	log_t l;
	dos_blocker b;

	address spammer = make_address_v4("10.10.10.10");

	time_point now = clock_type::now();
	for (int i = 0; i < 1000; ++i)
	{
		b.incoming(spammer, now, &l);
		now += milliseconds(1);
		TEST_EQUAL(b.incoming(rand_v4(), now, &l), true);
		now += milliseconds(1);
	}

	now += milliseconds(1);

	TEST_EQUAL(b.incoming(spammer, now, &l), false);
#endif
#endif
}
