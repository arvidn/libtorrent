/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/thread.hpp"
#include <boost/tuple/tuple.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/extensions/metadata_transfer.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include <iostream>

using boost::tuples::ignore;

void test_transfer(bool clear_files, bool disconnect
	, boost::shared_ptr<libtorrent::torrent_plugin> (*constructor)(libtorrent::torrent*, void*))
{
	using namespace libtorrent;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48100, 49000), "0.0.0.0", 0);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49100, 50000), "0.0.0.0", 0);
	session ses3(fingerprint("LT", 0, 1, 0, 0), std::make_pair(50100, 51000), "0.0.0.0", 0);
	ses1.add_extension(constructor);
	ses2.add_extension(constructor);
	ses3.add_extension(constructor);
	torrent_handle tor1;
	torrent_handle tor2;
	torrent_handle tor3;
#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.out_enc_policy = pe_settings::forced;
	pes.in_enc_policy = pe_settings::forced;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
	ses3.set_pe_settings(pes);
#endif

	boost::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, clear_files, true, true, "_meta");	

	for (int i = 0; i < 80; ++i)
	{
		// make sure this function can be called on
		// torrents without metadata
		if (!disconnect) tor2.status();
		print_alerts(ses1, "ses1", false, true);
		print_alerts(ses2, "ses2", false, true);

		if (disconnect && tor2.is_valid()) ses2.remove_torrent(tor2);
		if (!disconnect
			&& tor2.status().has_metadata
			&& tor3.status().has_metadata) break;
		test_sleep(100);
	}

	if (disconnect) return;

	TEST_CHECK(tor2.status().has_metadata);
	TEST_CHECK(tor3.status().has_metadata);
	std::cerr << "waiting for transfer to complete\n";

	for (int i = 0; i < 30; ++i)
	{
		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		std::cerr
			<< "\033[33m" << int(st1.upload_payload_rate / 1000.f) << "kB/s "
			<< st1.num_peers << ": "
			<< "\033[32m" << int(st2.download_payload_rate / 1000.f) << "kB/s "
			<< "\033[31m" << int(st2.upload_payload_rate / 1000.f) << "kB/s "
			<< "\033[0m" << int(st2.progress * 100) << "% "
			<< st2.num_peers
			<< std::endl;
		if (st2.is_seeding) break;
		test_sleep(1000);
	}

	TEST_CHECK(tor2.status().is_seeding);
	if (tor2.status().is_seeding) std::cerr << "done\n";

	error_code ec;
	remove_all("tmp1_meta", ec);
	remove_all("tmp2_meta", ec);
	remove_all("tmp3_meta", ec);
}

int test_main()
{
	using namespace libtorrent;

	// test to disconnect one client prematurely
	test_transfer(true, true, &create_metadata_plugin);
	// test where one has data and one doesn't
	test_transfer(true, false, &create_metadata_plugin);
	// test where both have data (to trigger the file check)
	test_transfer(false, false, &create_metadata_plugin);

	// test to disconnect one client prematurely
	test_transfer(true, true, &create_ut_metadata_plugin);
	// test where one has data and one doesn't
	test_transfer(true, false, &create_ut_metadata_plugin);
	// test where both have data (to trigger the file check)
	test_transfer(false, false, &create_ut_metadata_plugin);

	error_code ec;
	remove_all("tmp1", ec);
	remove_all("tmp2", ec);

	return 0;
}

