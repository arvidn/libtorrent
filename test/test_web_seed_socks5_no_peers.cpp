/*

Copyright (c) 2015, 2017, 2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"

using namespace lt;

const int proxy = lt::settings_pack::socks5;

TORRENT_TEST(url_seed_socks5_no_peers_ssl)
{
#if TORRENT_USE_SSL
	run_http_suite(proxy, "https", false, false, false, false, false);
#endif
}

TORRENT_TEST(url_seed_socks5_no_peers)
{
	run_http_suite(proxy, "http", false, false, false, false, false);
}
