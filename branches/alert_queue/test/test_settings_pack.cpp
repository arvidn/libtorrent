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
#include <iostream>

using namespace libtorrent;
using namespace libtorrent::aux;

int test_main()
{
	settings_pack sp;

	sp.set_int(settings_pack::max_out_request_queue, 1337);

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

	apply_pack(&sp, sett);

	TEST_EQUAL(sett.get_int(settings_pack::max_out_request_queue), 1337);
	save_settings_to_dict(sett, e.dict());
	TEST_EQUAL(e.dict().size(), 1);


#define TEST_NAME(n) \
	TEST_EQUAL(setting_by_name(#n), settings_pack:: n) \
	TEST_CHECK(strcmp(name_for_setting(settings_pack:: n), #n) == 0)

	TEST_NAME(contiguous_recv_buffer);
	TEST_NAME(choking_algorithm);
	TEST_NAME(seeding_piece_quota);
#ifndef TORRENT_NO_DEPRECATE
	TEST_NAME(half_open_limit);
#endif
	TEST_NAME(peer_turnover_interval);
	TEST_NAME(mmap_cache);

	settings_pack p;
	p.set_str(settings_pack::peer_fingerprint, "abc");
	p.set_str(settings_pack::peer_fingerprint, "cde");
	p.set_str(settings_pack::peer_fingerprint, "efg");
	p.set_str(settings_pack::peer_fingerprint, "hij");

	TEST_EQUAL(p.get_str(settings_pack::peer_fingerprint), "hij");

	return 0;
}

