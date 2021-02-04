/*

Copyright (c) 2013-2019, Arvid Norberg
Copyright (c) 2015, Jakob Petsovits
Copyright (c) 2016, Andrei Kurushin
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

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/string_view.hpp"

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "web_seed_suite.hpp"
#include "make_torrent.hpp"

#include <tuple>
#include <fstream>
#include "setup_transfer.hpp"

#include <iostream>

using namespace lt;

namespace {

int peer_disconnects = 0;

bool on_alert(alert const* a)
{
	if (alert_cast<peer_disconnected_alert>(a))
		++peer_disconnects;
	else if (alert_cast<peer_error_alert>(a))
		++peer_disconnects;

	return false;
}

static char const* proxy_name[] = {"", "_socks4", "_socks5", "_socks5_pw", "_http", "_http_pw", "_i2p"};

} // anonymous namespace

// proxy: 0=none, 1=socks4, 2=socks5, 3=socks5_pw 4=http 5=http_pw
void test_transfer(lt::session& ses, std::shared_ptr<torrent_info> torrent_file
	, int proxy, char const* protocol, bool url_seed
	, bool chunked_encoding, bool test_ban, bool keepalive, bool proxy_peers)
{
	using namespace lt;

	TORRENT_ASSERT(torrent_file->web_seeds().size() > 0);

	std::string save_path = "tmp2_web_seed";
	save_path += proxy_name[proxy];

	error_code ec;
	remove_all(save_path, ec);

	static char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	std::printf("\n\n  ==== TESTING === proxy: %s ==== protocol: %s "
		"==== seed: %s === transfer-encoding: %s === corruption: %s "
		"==== keepalive: %s\n\n\n"
		, test_name[proxy], protocol, url_seed ? "URL seed" : "HTTP seed"
		, chunked_encoding ? "chunked": "none", test_ban ? "yes" : "no"
		, keepalive ? "yes" : "no");

	int proxy_port = 0;
	settings_pack pack;
	// we use a self-signed cert for HTTPS trackers, the test would fail if we
	// tried to validate it.
	if (protocol == "https"_sv)
		pack.set_bool(settings_pack::validate_https_trackers, false);
	if (proxy)
	{
		proxy_port = start_proxy(proxy);
		if (proxy_port < 0)
		{
			std::printf("failed to start proxy");
			return;
		}
		pack.set_str(settings_pack::proxy_hostname, "127.0.0.1");
		pack.set_str(settings_pack::proxy_username, "testuser");
		pack.set_str(settings_pack::proxy_password, "testpass");
		pack.set_int(settings_pack::proxy_type, proxy);
		pack.set_int(settings_pack::proxy_port, proxy_port);
		pack.set_bool(settings_pack::proxy_peer_connections, proxy_peers);
	}
	else
	{
		pack.set_str(settings_pack::proxy_hostname, "");
		pack.set_str(settings_pack::proxy_username, "");
		pack.set_str(settings_pack::proxy_password, "");
		pack.set_int(settings_pack::proxy_type, settings_pack::none);
		pack.set_int(settings_pack::proxy_port, 0);
		pack.set_bool(settings_pack::proxy_peer_connections, proxy_peers);
	}
	ses.apply_settings(pack);

	add_torrent_params p;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;

	// the reason to set sequential download is to make sure that the order in
	// which files are requested from the web server is consistent. Any specific
	// scenario that needs testing should be an explicit test case
	p.flags |= torrent_flags::sequential_download;
	p.ti = torrent_file;
	p.save_path = save_path;
	torrent_handle th = ses.add_torrent(p, ec);
	std::printf("adding torrent, save_path = \"%s\" cwd = \"%s\" torrent = \"%s\"\n"
		, save_path.c_str(), current_working_directory().c_str()
		, torrent_file->name().c_str());

	std::vector<announce_entry> empty;
	th.replace_trackers(empty);

	const std::int64_t total_size = torrent_file->total_size();

	file_storage const& fs = torrent_file->files();
	int pad_file_size = 0;
	for (auto const i : fs.file_range())
	{
		if (fs.file_flags(i) & file_storage::flag_pad_file)
			pad_file_size += int(fs.file_size(i));
	}

	peer_disconnects = 0;
	std::map<std::string, std::int64_t> cnt = get_counters(ses);

	for (int i = 0; i < 40; ++i)
	{
		torrent_status s = th.status();

		cnt = get_counters(ses);

		print_ses_rate(i / 10.f, &s, nullptr);
		print_alerts(ses, "  >>  ses", false, false, &on_alert);

		if (test_ban && th.url_seeds().empty() && th.http_seeds().empty())
		{
			std::printf("testing ban: URL seed removed\n");
			// when we don't have any web seeds left, we know we successfully banned it
			break;
		}

		if (s.is_seeding)
		{
			std::printf("SEEDING\n");
			std::printf("session.payload: %d session.redundant: %d\n"
				, int(cnt["net.recv_payload_bytes"]), int(cnt["net.recv_redundant_bytes"]));
			std::printf("torrent.payload: %d torrent.redundant: %d\n"
				, int(s.total_payload_download), int(s.total_redundant_bytes));

			TEST_EQUAL(s.total_payload_download - s.total_redundant_bytes, total_size - pad_file_size);
			break;
		}

		// if the web seed connection is disconnected, we're going to fail
		// the test. make sure to do so quickly
		if (!test_ban && keepalive && peer_disconnects >= 1) break;

		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	cnt = get_counters(ses);

	if (test_ban)
	{
		// for test_ban tests, make sure we removed
		// the url seed (i.e. banned it)
		// torrents that don't have very many pieces will not ban the web seeds,
		// since they won't have an opportunity to accrue enough negative points
		if (torrent_file->files().num_pieces() > 3)
		{
			TEST_CHECK(th.url_seeds().empty());
			TEST_CHECK(th.http_seeds().empty());
		}
	}
	else
	{
		// if the web seed sent corrupt data and we banned it, we probably didn't
		// end up using all the cache anyway
		torrent_status st = th.status();
		TEST_EQUAL(st.is_seeding, true);
	}

	std::cout << "total_size: " << total_size
		<< " read cache size: " << cnt["disk.disk_blocks_in_use"]
		<< " total used buffer: " << cnt["disk.disk_blocks_in_use"]
		<< " session total download: " << cnt["net.recv_payload_bytes"]
		<< " torrent total download: " << th.status().total_payload_download
		<< " redundant: " << th.status().total_redundant_bytes
		<< std::endl;

	// if test_ban is true, we're not supposed to have completed the download
	// otherwise, we are supposed to have
	TEST_CHECK(th.status().is_seeding == !test_ban);

	if (proxy) stop_proxy(proxy_port);

	th.flush_cache();

	// synchronize to make sure the files have been created on disk
	wait_for_alert(ses, cache_flushed_alert::alert_type, "ses");

	print_alerts(ses, "  >>  ses", true, false, &on_alert, true);

	if (!test_ban)
	{
		for (auto const i : fs.file_range())
		{
			bool const expect = !fs.pad_file_at(i);
			std::string file_path = combine_path(save_path, fs.file_path(i));
			std::printf("checking file: %s\n", file_path.c_str());
			TEST_EQUAL(exists(file_path), expect);
		}
	}

	ses.remove_torrent(th);
}

// proxy: 0=none, 1=socks4, 2=socks5, 3=socks5_pw 4=http 5=http_pw
// protocol: "http" or "https"
// test_url_seed determines whether to use url-seed or http-seed
int EXPORT run_http_suite(int proxy, char const* protocol, bool test_url_seed
	, bool chunked_encoding, bool test_ban, bool keepalive, bool test_rename
	, bool proxy_peers)
{
	using namespace lt;

	std::string save_path = "web_seed";
	save_path += proxy_name[proxy];

	error_code ec;
	int const port = start_web_server(protocol == "https"_sv, chunked_encoding, keepalive);

	std::vector<torrent_args> test_cases;

	if (test_url_seed)
	{
		char url[512];
		std::snprintf(url, sizeof(url), "%s://127.0.0.1:%d/%s", protocol, port, save_path.c_str());
		std::printf("testing: %s\n", url);

		create_directories(combine_path(save_path, "torrent_dir"), ec);

		// test case 1
		test_cases.push_back(torrent_args().file("0").file("5,padfile").file("11")
			.file("16000").file("368,padfile")
			.file("16384,padfile").file("16384,padfile").file("17").file("10")
			.file("8000").file("8000").file("1").file("1").file("1").file("1")
			.file("1").file("100").file("0").file("1").file("1").file("1")
			.file("100").file("1").file("1").file("1").file("1").file("1,padfile")
			.file("1,padfile").file("1,padfile").file("1").file("0").file("0")
			.file("0").file("1").file("13").file("65000").file("34").file("75")
			.file("2").file("30").file("400").file("500").file("23000")
			.file("900").file("43000").file("400").file("4300").file("6")
			.file("4,padfile")
			.name("torrent_dir")
			.url_seed(url));

		// test case 2 (the end of the torrent are padfiles)
		test_cases.push_back(torrent_args()
			.file("0,padfile")
			.file("11")
			.file("5")
			.file("16000")
			.file("368,padfile")
			.file("16384,padfile")
			.name("torrent_dir")
			.url_seed(url));

		// test case 3 (misaligned)
		test_cases.push_back(torrent_args()
			.file("16383")
			.file("11")
			.file("5")
			.file("16000")
			.name("torrent_dir")
			.url_seed(url));

		// test case 4 (a full piece padfile)
		test_cases.push_back(torrent_args()
			.file("32768,padfile")
			.file("16000")
			.file("11")
			.file("5")
			.name("torrent_dir")
			.url_seed(url));

		// test case 5 (properly aligned padfile)
		test_cases.push_back(torrent_args()
			.file("32760")
			.file("8,padfile")
			.file("32760")
			.file("8")
			.file("32700")
			.file("68,padfile")
			.file("32000")
			.name("torrent_dir")
			.url_seed(url));

		std::snprintf(url, sizeof(url), "%s://127.0.0.1:%d/%s/test-single-file"
			, protocol, port, save_path.c_str());

		// test case 6 (single file torrent)
		test_cases.push_back(torrent_args()
			.file("199092,name=test-single-file")
			.name("torrent_dir")
			.url_seed(url));
	}
	else
	{
		char url[512];
		std::snprintf(url, sizeof(url), "%s://127.0.0.1:%d/%s/seed", protocol, port, save_path.c_str());
		std::printf("testing: %s\n", url);

		// there's really just one test case for http seeds
		test_cases.push_back(torrent_args().file("589824,name=seed")
			.http_seed(url));
	}

	int idx = 0;
	for (auto const& c : test_cases)
	{
		std::printf("\n\n ====  test case %d ====\n\n\n", idx++);

		std::shared_ptr<torrent_info> torrent_file = make_test_torrent(c);

		// if test_ban is true, we create the files with alternate content (that
		// doesn't match the hashes in the .torrent file)
		generate_files(*torrent_file, save_path, test_ban);

		if (ec)
		{
			std::printf("error creating hashes for test torrent: %s\n"
				, ec.message().c_str());
			TEST_CHECK(false);
			return 0;
		}

		{
			settings_pack pack = settings();
			pack.set_int(settings_pack::max_queued_disk_bytes, 256 * 1024);
			pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:51000");
			pack.set_int(settings_pack::max_retry_port_bind, 1000);
			pack.set_bool(settings_pack::enable_lsd, false);
			pack.set_bool(settings_pack::enable_natpmp, false);
			pack.set_bool(settings_pack::enable_upnp, false);
			pack.set_bool(settings_pack::enable_dht, false);
			lt::session ses(session_params{pack, {}});

			test_transfer(ses, torrent_file, proxy, protocol, test_url_seed
				, chunked_encoding, test_ban, keepalive, proxy_peers);

			if (test_url_seed && test_rename)
			{
				torrent_file->rename_file(file_index_t(0), combine_path(save_path, combine_path("torrent_dir", "renamed_test1")));
				test_transfer(ses, torrent_file, 0, protocol, test_url_seed
					, chunked_encoding, test_ban, keepalive, proxy_peers);
			}
		}
	}

	stop_web_server();
	return 0;
}
