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

enum flags_t
{
	clear_files = 1,
	disconnect = 2,
	full_encryption = 4,
	reverse = 8
};

void test_transfer(int flags
	, boost::shared_ptr<libtorrent::torrent_plugin> (*constructor)(libtorrent::torrent*, void*)
	, int timeout)
{
	using namespace libtorrent;

	fprintf(stderr, "test transfer: timeout=%d %s%s%s%s\n"
		, timeout
		, (flags & clear_files) ? "clear-files " : ""
		, (flags & disconnect) ? "disconnect " : ""
		, (flags & full_encryption) ? "encryption " : ""
		, (flags & reverse) ? "reverse " : "");

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	// TODO: it would be nice to test reversing
	// which session is making the connection as well
	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48100, 49000), "0.0.0.0", 0);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49100, 50000), "0.0.0.0", 0);
	ses1.add_extension(constructor);
	ses2.add_extension(constructor);
	torrent_handle tor1;
	torrent_handle tor2;
#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.prefer_rc4 = (flags & full_encryption);
	pes.out_enc_policy = pe_settings::forced;
	pes.in_enc_policy = pe_settings::forced;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
#endif

	session* downloader = &ses2;
	session* seed = &ses1;

	boost::tie(tor1, tor2, ignore) = setup_transfer(seed, downloader, NULL
		, flags & clear_files, true, false, "_meta");	

	if (flags & reverse)
	{
		error_code ec;
		int port = seed->listen_port();
		fprintf(stderr, "%s: downloader: connecting peer port: %d\n"
			, time_now_string(), port);
		tor2.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
			, port));
	}
	else
	{
		error_code ec;
		int port = downloader->listen_port();
		fprintf(stderr, "%s: seed: connecting peer port: %d\n"
			, time_now_string(), port);
		tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
			, port));
	}

	for (int i = 0; i < timeout * 10; ++i)
	{
		// make sure this function can be called on
		// torrents without metadata
		if ((flags & disconnect) == 0) tor2.status();
		print_alerts(*seed, "seed", false, true);
		print_alerts(*downloader, "downloader", false, true);

		if ((flags & disconnect) && tor2.is_valid()) downloader->remove_torrent(tor2);
		if ((flags & disconnect) == 0
			&& tor2.status().has_metadata) break;
		test_sleep(100);
	}

	if (flags & disconnect) goto done;

	TEST_CHECK(tor2.status().has_metadata);
	std::cerr << "waiting for transfer to complete\n";

	for (int i = 0; i < timeout * 10; ++i)
	{
		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		print_alerts(*seed, "seed", false, true);
		print_alerts(*downloader, "downloader", false, true);

		print_ses_rate(i / 10.f, &st1, &st2);
		if (st2.is_seeding) break;
		test_sleep(100);
	}

	TEST_CHECK(tor2.status().is_seeding);
	if (tor2.status().is_seeding) std::cerr << "done\n";

done:

	// this allows shutting down the sessions in parallel
	p1 = seed->abort();
	p2 = downloader->abort();

	error_code ec;
	remove_all("tmp1_meta", ec);
	remove_all("tmp2_meta", ec);
}

int test_main()
{
	using namespace libtorrent;

#ifndef TORRENT_NO_DEPRECATE

#ifdef TORRENT_USE_VALGRIND
	const int timeout = 8;
#else
	const int timeout = 3;
#endif

	test_transfer(full_encryption | reverse, &create_ut_metadata_plugin, timeout);
	test_transfer(reverse, &create_ut_metadata_plugin, timeout);

	for (int f = 0; f <= (clear_files | disconnect | full_encryption); ++f)
		test_transfer(f, &create_metadata_plugin, timeout * 2);


	for (int f = 0; f <= (clear_files | disconnect | full_encryption); ++f)
		test_transfer(f, &create_ut_metadata_plugin, timeout);

	error_code ec;
	remove_all("tmp1", ec);
	remove_all("tmp2", ec);

#endif // TORRENT_NO_DEPRECATE

	return 0;
}

