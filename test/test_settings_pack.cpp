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
#include <iostream>

using namespace libtorrent;
using namespace libtorrent::aux;

TORRENT_TEST(default_settings)
{
	aux::session_settings sett;
	initialize_default_settings(sett);

	entry e;
	save_settings_to_dict(sett, e.dict());
	// all default values are supposed to be skipped
	// by save_settings
	TEST_EQUAL(e.dict().size(), 0);

#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	if (e.dict().size() > 0)
		std::cerr << e << std::endl;
#endif
}

TORRENT_TEST(apply_pack)
{
	aux::session_settings sett;
	initialize_default_settings(sett);
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
	TEST_EQUAL(setting_by_name(#n), settings_pack:: n) \
	TEST_CHECK(strcmp(name_for_setting(settings_pack:: n), #n) == 0)

#ifndef TORRENT_NO_DEPRECATE
	TEST_NAME(contiguous_recv_buffer);
#endif
	TEST_NAME(choking_algorithm);
	TEST_NAME(seeding_piece_quota);
#ifndef TORRENT_NO_DEPRECATE
	TEST_NAME(half_open_limit);
#endif
	TEST_NAME(peer_turnover_interval);
	TEST_NAME(mmap_cache);
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

TORRENT_TEST(duplicates)
{
	settings_pack p;
	p.set_str(settings_pack::peer_fingerprint, "abc");
	p.set_str(settings_pack::peer_fingerprint, "cde");
	p.set_str(settings_pack::peer_fingerprint, "efg");
	p.set_str(settings_pack::peer_fingerprint, "hij");

	TEST_EQUAL(p.get_str(settings_pack::peer_fingerprint), "hij");
}

// TODO: load_pack_from_dict

