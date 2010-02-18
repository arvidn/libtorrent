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
#include "libtorrent/file_pool.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/thread.hpp"
#include <boost/tuple/tuple.hpp>
#include <fstream>
#include <iostream>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;

// proxy: 0=none, 1=socks4, 2=socks5, 3=socks5_pw 4=http 5=http_pw
void test_transfer(boost::intrusive_ptr<torrent_info> torrent_file, int proxy, int port)
{
	using namespace libtorrent;

	session ses(fingerprint("  ", 0,0,0,0), 0);
	session_settings settings;
	settings.max_queued_disk_bytes = 256 * 1024;
	ses.set_settings(settings);
	ses.set_alert_mask(~alert::progress_notification);
	ses.listen_on(std::make_pair(51000, 52000));
	error_code ec;
	remove_all("./tmp2_web_seed", ec);

	char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	fprintf(stderr, "\n\n  ==== TESTING %s proxy ====\n\n\n", test_name[proxy]);
	
	if (proxy)
	{
		start_proxy(8002, proxy);
		proxy_settings ps;
		ps.hostname = "127.0.0.1";
		ps.port = 8002;
		ps.username = "testuser";
		ps.password = "testpass";
		ps.type = (proxy_settings::proxy_type)proxy;
		ses.set_web_seed_proxy(ps);
	}

	add_torrent_params p;
	p.ti = torrent_file;
	p.save_path = "./tmp2_web_seed";
	p.storage_mode = storage_mode_compact;
	torrent_handle th = ses.add_torrent(p, ec);

	std::vector<announce_entry> empty;
	th.replace_trackers(empty);

	const size_type total_size = torrent_file->total_size();

	float rate_sum = 0.f;
	float ses_rate_sum = 0.f;

	cache_status cs;

	for (int i = 0; i < 10; ++i)
	{
		torrent_status s = th.status();
		session_status ss = ses.status();
		rate_sum += s.download_payload_rate;
		ses_rate_sum += ss.payload_download_rate;

		cs = ses.get_cache_status();
		if (cs.blocks_read < 1) cs.blocks_read = 1;
		if (cs.blocks_written < 1) cs.blocks_written = 1;

		std::cerr << (s.progress * 100.f) << " %"
			<< " torrent rate: " << (s.download_rate / 1000.f) << " kB/s"
			<< " session rate: " << (ss.download_rate / 1000.f) << " kB/s"
			<< " session total: " << ss.total_payload_download
			<< " torrent total: " << s.total_payload_download
			<< " rate sum:" << ses_rate_sum
			<< " cache: " << cs.cache_size
			<< " rcache: " << cs.read_cache_size
			<< " buffers: " << cs.total_used_buffers
			<< std::endl;

		print_alerts(ses, "  >>  ses");

		if (th.is_seed()/* && ss.download_rate == 0.f*/)
		{
			TEST_CHECK(th.status().total_payload_download == total_size);
			// we need to sleep here a bit to let the session sync with the torrent stats
			test_sleep(1000);
			TEST_CHECK(ses.status().total_payload_download == total_size);
			break;
		}
		test_sleep(500);
	}

	TEST_CHECK(cs.cache_size == 0);
	TEST_CHECK(cs.total_used_buffers == 0);

	std::cerr << "total_size: " << total_size
		<< " rate_sum: " << rate_sum
		<< " session_rate_sum: " << ses_rate_sum
		<< " session total download: " << ses.status().total_payload_download
		<< " torrent total download: " << th.status().total_payload_download
		<< std::endl;

	// the rates for each second should sum up to the total, with a 10% error margin
//	TEST_CHECK(fabs(rate_sum - total_size) < total_size * .1f);
//	TEST_CHECK(fabs(ses_rate_sum - total_size) < total_size * .1f);

	TEST_CHECK(th.is_seed());

	if (proxy) stop_proxy(8002);

	TEST_CHECK(exists(combine_path("./tmp2_web_seed", torrent_file->file_at(0).path)));
	remove_all("./tmp2_web_seed", ec);
}

int test_main()
{
	using namespace libtorrent;

	error_code ec;
	create_directories("./tmp1_web_seed/test_torrent_dir", ec);

	char random_data[300000];
	std::srand(10);
//	memset(random_data, 1, sizeof(random_data));
	std::generate(random_data, random_data + sizeof(random_data), &std::rand);
	std::ofstream("./tmp1_web_seed/test_torrent_dir/test1").write(random_data, 35);
	std::ofstream("./tmp1_web_seed/test_torrent_dir/test2").write(random_data, 16536 - 35);
	std::ofstream("./tmp1_web_seed/test_torrent_dir/test3").write(random_data, 16536);
	std::ofstream("./tmp1_web_seed/test_torrent_dir/test4").write(random_data, 17);
	std::ofstream("./tmp1_web_seed/test_torrent_dir/test5").write(random_data, 16536);
	std::ofstream("./tmp1_web_seed/test_torrent_dir/test6").write(random_data, 300000);
	std::ofstream("./tmp1_web_seed/test_torrent_dir/test7").write(random_data, 300000);

	file_storage fs;
	add_files(fs, "./tmp1_web_seed/test_torrent_dir");

	int port = start_web_server();

	libtorrent::create_torrent t(fs, 16 * 1024);
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "http://127.0.0.1:%d/tmp1_web_seed", port);
	t.add_url_seed(tmp);

	// calculate the hash for all pieces
	set_piece_hashes(t, "./tmp1_web_seed", ec);
	boost::intrusive_ptr<torrent_info> torrent_file(new torrent_info(t.generate()));

	for (int i = 0; i < 6; ++i)
		test_transfer(torrent_file, i, port);
	
	torrent_file->rename_file(0, "./tmp2_web_seed/test_torrent_dir/renamed_test1");
	test_transfer(torrent_file, 0, port);

	stop_web_server();
	remove_all("./tmp1_web_seed", ec);
	return 0;
}

