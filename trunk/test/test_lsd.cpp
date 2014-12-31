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
#include "libtorrent/thread.hpp"
#include <boost/tuple/tuple.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include <iostream>

void test_lsd()
{
	using namespace libtorrent;
	namespace lt = libtorrent;

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	settings_pack pack;
	pack.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_bool(settings_pack::enable_lsd, true);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_str(settings_pack::listen_interfaces, "127.0.0.1:48100");

	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, "127.0.0.1:49100");
	lt::session ses2(pack);

	torrent_handle tor1;
	torrent_handle tor2;

	using boost::tuples::ignore;
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0, true, false, false, "_lsd"
		, 16 * 1024, 0, false, 0, false);

	for (int i = 0; i < 30; ++i)
	{
		print_alerts(ses1, "ses1", true);
		print_alerts(ses2, "ses2", true);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		print_ses_rate(i, &st1, &st2);

		if (st2.is_seeding /*&& st3.is_seeding*/) break;
		test_sleep(1000);
	}

	TEST_CHECK(tor2.status().is_seeding);

	if (tor2.status().is_seeding) std::cerr << "done\n";

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
}

int test_main()
{
	using namespace libtorrent;

	// in case the previous run was terminated
	error_code ec;
	remove_all("./tmp1_lsd", ec);
	remove_all("./tmp2_lsd", ec);
	remove_all("./tmp3_lsd", ec);

	test_lsd();
	
	remove_all("./tmp1_lsd", ec);
	remove_all("./tmp2_lsd", ec);
	remove_all("./tmp3_lsd", ec);

	return 0;
}



