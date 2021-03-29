/*

Copyright (c) 2007-2008, 2010, 2013-2015, 2017, 2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"

using namespace lt;

const int proxy = lt::settings_pack::none;

#if TORRENT_USE_SSL
TORRENT_TEST(web_seed_ssl)
{
	run_http_suite(proxy, "https", false);
}
#endif

TORRENT_TEST(web_seed)
{
	run_http_suite(proxy, "http", false);
}
