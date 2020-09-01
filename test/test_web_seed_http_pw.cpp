/*

Copyright (c) 2013, 2015, 2017, 2019-2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"

using namespace lt;

const int proxy = lt::settings_pack::http_pw;

TORRENT_TEST(web_seed_http_pw)
{
	run_http_suite(proxy, "http", false);
}

TORRENT_TEST(url_seed_http_pw)
{
	run_http_suite(proxy, "http", true);
}

#if TORRENT_USE_SSL
TORRENT_TEST(web_seed_http_pw_ssl)
{
	run_http_suite(proxy, "https", false);
}

TORRENT_TEST(url_seed_http_pw_ssl)
{
	run_http_suite(proxy, "https", true);
}
#endif
