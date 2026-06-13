/*

Copyright (c) 2014-2015, 2017, 2020-2022, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "disk_io_test.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"

using namespace lt;

const int proxy = lt::settings_pack::none;

#if TORRENT_USE_SSL
TORRENT_TEST_DISK_IO(url_seed_ssl_keepalive) { run_http_suite(proxy, "https", disk_io); }

TORRENT_TEST_DISK_IO(url_seed_ssl) { run_http_suite(proxy, "https", disk_io, false, false, false); }
#endif

TORRENT_TEST_DISK_IO(url_seed_keepalive) { run_http_suite(proxy, "http", disk_io); }

TORRENT_TEST_DISK_IO(url_seed) { run_http_suite(proxy, "http", disk_io, false, false, false); }

TORRENT_TEST_DISK_IO(url_seed_host_header)
{
	run_http_suite(proxy, "http", disk_io, false, false, false, false, true, true);
}

TORRENT_TEST_DISK_IO(url_seed_keepalive_rename)
{
	// TODO: Run this test for all disk I/O backends
	using fn_t = std::unique_ptr<lt::disk_interface> (*)(
		lt::io_context&, lt::settings_interface const&, lt::counters&);
	auto const* tgt = disk_io.target<fn_t>();
	if (tgt != nullptr && *tgt == &lt::pread_disk_io_constructor) return;

	run_http_suite(proxy, "http", disk_io, false, true);
}
