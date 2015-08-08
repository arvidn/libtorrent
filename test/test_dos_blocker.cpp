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

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/kademlia/dos_blocker.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket_io.hpp" // for print_endpoint
#include <stdarg.h>

using namespace libtorrent;

struct log_t : libtorrent::dht::dht_logger
{
	virtual void log(dht_logger::module_t m, char const* fmt, ...)
		TORRENT_OVERRIDE TORRENT_FORMAT(3, 4)
	{
		va_list v;
		va_start(v, fmt);
		vfprintf(stderr, fmt, v);
		va_end(v);
	}

	virtual void log_packet(message_direction_t dir, char const* pkt, int len
		, udp::endpoint node) TORRENT_OVERRIDE
	{
		libtorrent::bdecode_node print;
		libtorrent::error_code ec;
		int ret = bdecode(pkt, pkt + len, print, ec, NULL, 100, 100);
		TEST_EQUAL(ret, 0);

		std::string msg = print_entry(print, true);
		printf("%s", msg.c_str());

		char const* prefix[2] = { "<==", "==>"};
		printf("%s [%s] %s", prefix[dir], print_endpoint(node).c_str()
			, msg.c_str());
	}
};

TORRENT_TEST(dos_blocker)
{
#ifndef TORRENT_DISABLE_DHT
	using namespace libtorrent::dht;

	log_t l;
	dos_blocker b;

	address spammer = address_v4::from_string("10.10.10.10");

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
}

