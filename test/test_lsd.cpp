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
#include "libtorrent/session_settings.hpp"
#include "libtorrent/hasher.hpp"
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/operations.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"

using boost::filesystem::remove_all;

void test_lsd()
{
	using namespace libtorrent;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48100, 49000));
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49100, 50000));
	session ses3(fingerprint("LT", 0, 1, 0, 0), std::make_pair(50100, 51000));

	// this is to avoid everything finish from a single peer
	// immediately. To make the swarm actually connect all
	// three peers before finishing.
	float rate_limit = 180000;
	ses1.set_upload_rate_limit(int(rate_limit));
	ses2.set_download_rate_limit(int(rate_limit));
	ses3.set_download_rate_limit(int(rate_limit));
	ses2.set_upload_rate_limit(int(rate_limit / 2));
	ses3.set_upload_rate_limit(int(rate_limit / 2));

	session_settings settings;
	settings.allow_multiple_connections_per_ip = true;
	settings.ignore_limits_on_local_network = false;
	ses1.set_settings(settings);
	ses2.set_settings(settings);
	ses3.set_settings(settings);

	ses1.start_lsd();
	ses2.start_lsd();
	ses3.start_lsd();

#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.out_enc_policy = pe_settings::forced;
	pes.in_enc_policy = pe_settings::forced;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
	ses3.set_pe_settings(pes);
#endif

	torrent_handle tor1;
	torrent_handle tor2;
	torrent_handle tor3;

	boost::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, true, false, false, "_lsd");

	for (int i = 0; i < 30; ++i)
	{
		print_alerts(ses1, "ses1", true);
		print_alerts(ses2, "ses2", true);
		print_alerts(ses3, "ses3", true);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();
		torrent_status st3 = tor3.status();

		std::cerr
			<< "\033[33m" << int(st1.upload_payload_rate / 1000.f) << "kB/s "
			<< st1.num_peers << ": "
			<< "\033[32m" << int(st2.download_payload_rate / 1000.f) << "kB/s "
			<< "\033[31m" << int(st2.upload_payload_rate / 1000.f) << "kB/s "
			<< "\033[0m" << int(st2.progress * 100) << "% "
			<< st2.num_peers << " - "
			<< "\033[32m" << int(st3.download_payload_rate / 1000.f) << "kB/s "
			<< "\033[31m" << int(st3.upload_payload_rate / 1000.f) << "kB/s "
			<< "\033[0m" << int(st3.progress * 100) << "% "
			<< st3.num_peers
			<< std::endl;

		if (tor2.is_seed() && tor3.is_seed()) break;
		test_sleep(1000);
	}

	TEST_CHECK(tor2.is_seed());
	TEST_CHECK(tor3.is_seed());

	if (tor2.is_seed() && tor3.is_seed()) std::cerr << "done\n";
}

int test_main()
{
	using namespace libtorrent;
	using namespace boost::filesystem;

	// in case the previous run was terminated
	try { remove_all("./tmp1_lsd"); } catch (std::exception&) {}
	try { remove_all("./tmp2_lsd"); } catch (std::exception&) {}
	try { remove_all("./tmp3_lsd"); } catch (std::exception&) {}

	test_lsd();
	
	remove_all("./tmp1_lsd");
	remove_all("./tmp2_lsd");
	remove_all("./tmp3_lsd");

	return 0;
}



