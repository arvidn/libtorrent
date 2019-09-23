/*

Copyright (c) 2012, Arvid Norberg
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
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"
#include <iostream>

using namespace lt;
using namespace lt::aux;

TORRENT_TEST(default_settings)
{
	aux::session_settings sett;

	entry e;
	save_settings_to_dict(sett, e.dict());
	// all default values are supposed to be skipped
	// by save_settings
	TEST_EQUAL(e.dict().size(), 0);

#if TORRENT_USE_IOSTREAM
	if (e.dict().size() > 0)
		std::cout << e << std::endl;
#endif
}

TORRENT_TEST(default_settings2)
{
	aux::session_settings sett;

	settings_pack def = default_settings();

	for (int i = 0; i < settings_pack::num_string_settings; ++i)
	{
		TEST_EQUAL(sett.get_str(settings_pack::string_type_base + i)
			, def.get_str(settings_pack::string_type_base + i));
	}

	for (int i = 0; i < settings_pack::num_int_settings; ++i)
	{
		TEST_EQUAL(sett.get_int(settings_pack::int_type_base + i)
			, def.get_int(settings_pack::int_type_base + i));
	}

	for (int i = 0; i < settings_pack::num_bool_settings; ++i)
	{
		TEST_EQUAL(sett.get_bool(settings_pack::bool_type_base + i)
			, def.get_bool(settings_pack::bool_type_base + i));
	}
}

TORRENT_TEST(apply_pack)
{
	aux::session_settings sett;
	settings_pack sp;
	sp.set_int(settings_pack::max_out_request_queue, 1337);

	TEST_CHECK(sett.get_int(settings_pack::max_out_request_queue) != 1337);

	apply_pack(&sp, sett);

	TEST_EQUAL(sett.get_int(settings_pack::max_out_request_queue), 1337);
	entry e;
	save_settings_to_dict(sett, e.dict());
	TEST_EQUAL(e.dict().size(), 1);

	std::string out;
	bencode(std::back_inserter(out), e);
	TEST_EQUAL(out, "d21:max_out_request_queuei1337ee");
}

TORRENT_TEST(sparse_pack)
{
	settings_pack pack;
	TEST_EQUAL(pack.has_val(settings_pack::send_redundant_have), false);

	pack.set_bool(settings_pack::send_redundant_have, true);

	TEST_EQUAL(pack.has_val(settings_pack::send_redundant_have), true);
	TEST_EQUAL(pack.has_val(settings_pack::user_agent), false);
	TEST_EQUAL(pack.get_bool(settings_pack::send_redundant_have), true);
}

TORRENT_TEST(test_name)
{
#define TEST_NAME(n) \
	TEST_EQUAL(setting_by_name(#n), settings_pack:: n); \
	TEST_EQUAL(name_for_setting(settings_pack:: n), std::string(#n))

#if TORRENT_ABI_VERSION == 1
	TEST_NAME(contiguous_recv_buffer);
#endif
	TEST_NAME(choking_algorithm);
	TEST_NAME(seeding_piece_quota);
#if TORRENT_ABI_VERSION == 1
	TEST_NAME(half_open_limit);
	TEST_NAME(mmap_cache);
#endif
	TEST_NAME(peer_turnover_interval);
	TEST_NAME(peer_fingerprint);
	TEST_NAME(proxy_tracker_connections);
	TEST_NAME(cache_size_volatile);
	TEST_NAME(predictive_piece_announce);
	TEST_NAME(max_metadata_size);
	TEST_NAME(num_optimistic_unchoke_slots);
}

TORRENT_TEST(clear)
{
	settings_pack pack;
	TEST_EQUAL(pack.has_val(settings_pack::send_redundant_have), false);

	pack.set_bool(settings_pack::send_redundant_have, true);

	TEST_EQUAL(pack.has_val(settings_pack::send_redundant_have), true);
	TEST_EQUAL(pack.has_val(settings_pack::user_agent), false);
	TEST_EQUAL(pack.get_bool(settings_pack::send_redundant_have), true);

	pack.clear();

	TEST_EQUAL(pack.has_val(settings_pack::send_redundant_have), false);
	TEST_EQUAL(pack.has_val(settings_pack::user_agent), false);
}

TORRENT_TEST(clear_single_int)
{
	settings_pack sp;
	sp.set_int(settings_pack::max_out_request_queue, 1337);

	TEST_EQUAL(sp.get_int(settings_pack::max_out_request_queue), 1337);

	sp.clear(settings_pack::max_out_request_queue);

	TEST_EQUAL(sp.get_int(settings_pack::max_out_request_queue), 0);
}

TORRENT_TEST(clear_single_bool)
{
	settings_pack sp;
	sp.set_bool(settings_pack::send_redundant_have, true);

	TEST_EQUAL(sp.get_bool(settings_pack::send_redundant_have), true);

	sp.clear(settings_pack::send_redundant_have);

	TEST_EQUAL(sp.get_bool(settings_pack::send_redundant_have), false);
}

TORRENT_TEST(clear_single_string)
{
	settings_pack sp;
	sp.set_str(settings_pack::user_agent, "foobar");

	TEST_EQUAL(sp.get_str(settings_pack::user_agent), "foobar");

	sp.clear(settings_pack::user_agent);

	TEST_EQUAL(sp.get_str(settings_pack::user_agent), std::string());
}

TORRENT_TEST(duplicates)
{
	settings_pack p;
	p.set_str(settings_pack::peer_fingerprint, "abc");
	p.set_str(settings_pack::peer_fingerprint, "cde");
	p.set_str(settings_pack::peer_fingerprint, "efg");
	p.set_str(settings_pack::peer_fingerprint, "hij");

	TEST_EQUAL(p.get_str(settings_pack::peer_fingerprint), "hij");
}

TORRENT_TEST(load_pack_from_dict)
{
	aux::session_settings p1;
	p1.set_str(settings_pack::peer_fingerprint, "abc");
	p1.set_int(settings_pack::max_out_request_queue, 1337);
	p1.set_bool(settings_pack::send_redundant_have, false);

	entry e;
	save_settings_to_dict(p1, e.dict());

	std::string s;
	bencode(std::back_inserter(s), e);

	bdecode_node n;
	error_code ec;
	int ret = bdecode(s.data(), s.data() + int(s.size()), n, ec);
	TEST_EQUAL(ret, 0);
	TEST_CHECK(!ec);

	settings_pack p2 = load_pack_from_dict(n);
	TEST_EQUAL(p2.get_str(settings_pack::peer_fingerprint), "abc");
	TEST_EQUAL(p2.get_int(settings_pack::max_out_request_queue), 1337);
	TEST_EQUAL(p2.get_bool(settings_pack::send_redundant_have), false);
}

TORRENT_TEST(settings_pack_abi)
{
	// make sure enum values are preserved across libtorrent versions
	// for ABI compatibility
	// These values are only allowed to change across major versions

	TEST_EQUAL(settings_pack::string_type_base, 0x0000);
	TEST_EQUAL(settings_pack::int_type_base, 0x4000);
	TEST_EQUAL(settings_pack::bool_type_base, 0x8000);
	TEST_EQUAL(settings_pack::type_mask, 0xc000);

	// strings
	TEST_EQUAL(settings_pack::outgoing_interfaces, settings_pack::string_type_base + 4);
	TEST_EQUAL(settings_pack::dht_bootstrap_nodes, settings_pack::string_type_base + 11);

	// bool
	TEST_EQUAL(settings_pack::use_dht_as_fallback, settings_pack::bool_type_base + 4);
	TEST_EQUAL(settings_pack::use_read_cache, settings_pack::bool_type_base + 7);
	TEST_EQUAL(settings_pack::proxy_tracker_connections, settings_pack::bool_type_base + 67);

	// ints
	TEST_EQUAL(settings_pack::tracker_completion_timeout, settings_pack::int_type_base + 0);
	TEST_EQUAL(settings_pack::tracker_receive_timeout, settings_pack::int_type_base + 1);
	TEST_EQUAL(settings_pack::stop_tracker_timeout, settings_pack::int_type_base + 2);
	TEST_EQUAL(settings_pack::max_suggest_pieces, settings_pack::int_type_base + 66);
	TEST_EQUAL(settings_pack::connections_slack, settings_pack::int_type_base + 86);
	TEST_EQUAL(settings_pack::aio_threads, settings_pack::int_type_base + 104);
	TEST_EQUAL(settings_pack::max_http_recv_buffer_size, settings_pack::int_type_base + 115);
	TEST_EQUAL(settings_pack::web_seed_name_lookup_retry, settings_pack::int_type_base + 128);
	TEST_EQUAL(settings_pack::close_file_interval, settings_pack::int_type_base + 129);
	TEST_EQUAL(settings_pack::max_web_seed_connections, settings_pack::int_type_base + 131);
	TEST_EQUAL(settings_pack::resolver_cache_timeout, settings_pack::int_type_base + 132);
}
