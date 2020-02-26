/*

Copyright (c) 2016, Alden Torres
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

#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/hex.hpp"
#include "setup_transfer.hpp" // for addr6
#include "settings.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace lt;
using dht::dht_storage_interface;
using dht::dht_state;

namespace
{
#ifndef TORRENT_DISABLE_DHT
	bool g_storage_constructor_invoked = false;

	std::unique_ptr<dht_storage_interface> dht_custom_storage_constructor(
		dht::dht_settings const& settings)
	{
		g_storage_constructor_invoked = true;
		return dht_default_storage_constructor(settings);
	}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
	bool g_plugin_added_invoked = false;

	struct custom_plugin : plugin
	{
		void added(session_handle const& h) override
		{
			TORRENT_UNUSED(h);
			g_plugin_added_invoked = true;
		}
	};
#endif
}

TORRENT_TEST(default_plugins)
{
	session_params p1;
#ifndef TORRENT_DISABLE_EXTENSIONS
	TEST_EQUAL(int(p1.extensions.size()), 3);
#else
	TEST_EQUAL(int(p1.extensions.size()), 0);
#endif

	std::vector<std::shared_ptr<plugin>> exts;
	session_params p2(settings_pack(), exts);
	TEST_EQUAL(int(p2.extensions.size()), 0);
}

#ifndef TORRENT_DISABLE_DHT
TORRENT_TEST(custom_dht_storage)
{
	g_storage_constructor_invoked = false;
	settings_pack p = settings();
	p.set_bool(settings_pack::enable_dht, true);
	session_params params(p);
	params.dht_storage_constructor = dht_custom_storage_constructor;
	lt::session ses(params);

	TEST_CHECK(ses.is_dht_running() == true);
	TEST_EQUAL(g_storage_constructor_invoked, true);
}

TORRENT_TEST(dht_state)
{
	settings_pack p = settings();
	p.set_bool(settings_pack::enable_dht, true);

	dht::dht_settings sett;
	sett.max_dht_items = 10000;
	sett.max_peers = 20000;

	dht_state s;
	s.nids.emplace_back(addr4("0.0.0.0"), to_hash("0000000000000000000000000000000000000001"));
	s.nodes.push_back(uep("1.1.1.1", 1));
	s.nodes.push_back(uep("2.2.2.2", 2));
	// not important that IPv6 is disabled here
	s.nids.emplace_back(addr6("::"), to_hash("0000000000000000000000000000000000000002"));

	session_params params(p);
	params.dht_settings = sett;
	params.dht_state = s;

	params.settings.set_str(settings_pack::listen_interfaces, "127.0.0.1:6881");

	lt::session ses1(params);
	TEST_CHECK(ses1.is_dht_running() == true);
	entry e;
	ses1.save_state(e);

	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), e);

	bdecode_node n;
	error_code ec;
	int r = bdecode(&tmp[0], &tmp[0] + tmp.size(), n, ec);
	TEST_CHECK(!r);

	session_params params1 = read_session_params(n);
	TEST_EQUAL(params1.dht_settings.max_dht_items, 10000);
	TEST_EQUAL(params1.dht_settings.max_peers, 20000);

	TEST_EQUAL(params1.dht_state.nids.size(), 1);

	if (params1.dht_state.nids.size() >= 1) {
		// not a chance the nid will be the fake initial ones
		TEST_CHECK(params1.dht_state.nids[0].second != s.nids[0].second);
	}
}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
TORRENT_TEST(add_plugin)
{
	g_plugin_added_invoked = false;
	session_params params(settings());
	params.extensions.push_back(std::make_shared<custom_plugin>());
	lt::session ses(params);

	TEST_EQUAL(g_plugin_added_invoked, true);
}
#endif
