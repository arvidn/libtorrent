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
void test_transfer(boost::intrusive_ptr<torrent_info> torrent_file
	, int proxy, int port, char const* protocol, bool url_seed, bool chunked_encoding, bool test_ban)
{
	using namespace libtorrent;

	session ses(fingerprint("  ", 0,0,0,0), 0);
	session_settings settings;
	settings.max_queued_disk_bytes = 256 * 1024;
	ses.set_settings(settings);
	ses.set_alert_mask(~(alert::progress_notification | alert::stats_notification));
	error_code ec;
	ses.listen_on(std::make_pair(51000, 52000), ec);
	if (ec) fprintf(stderr, "listen_on failed: %s\n", ec.message().c_str());

	remove_all("tmp2_web_seed", ec);

	char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	fprintf(stderr, "\n\n  ==== TESTING === proxy: %s ==== protocol: %s ==== seed: %s === transfer-encoding: %s === corruption: %s\n\n\n"
		, test_name[proxy], protocol, url_seed ? "URL seed" : "HTTP seed", chunked_encoding ? "chunked": "none", test_ban ? "yes" : "no");
	
	if (proxy)
	{
		start_proxy(8002, proxy);
		proxy_settings ps;
		ps.hostname = "127.0.0.1";
		ps.port = 8002;
		ps.username = "testuser";
		ps.password = "testpass";
		ps.type = (proxy_settings::proxy_type)proxy;
		ses.set_proxy(ps);
	}

	add_torrent_params p;
	p.flags &= ~add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	p.ti = torrent_file;
	p.save_path = "tmp2_web_seed";
#ifndef TORRENT_NO_DEPRECATE
	p.storage_mode = storage_mode_compact;
#endif
	torrent_handle th = ses.add_torrent(p, ec);

	std::vector<announce_entry> empty;
	th.replace_trackers(empty);

	const size_type total_size = torrent_file->total_size();

	float rate_sum = 0.f;
	float ses_rate_sum = 0.f;

	cache_status cs;

	file_storage const& fs = torrent_file->files();
	int pad_file_size = 0;
	for (int i = 0; i < fs.num_files(); ++i)
	{
		file_entry f = fs.at(i);
		if (f.pad_file) pad_file_size += f.size;
	}

	for (int i = 0; i < 30; ++i)
	{
		torrent_status s = th.status();
		session_status ss = ses.status();
		rate_sum += s.download_payload_rate;
		ses_rate_sum += ss.payload_download_rate;

		cs = ses.get_cache_status();
		if (cs.blocks_read < 1) cs.blocks_read = 1;
		if (cs.blocks_written < 1) cs.blocks_written = 1;
/*
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
*/
		print_alerts(ses, "  >>  ses", test_ban, false, false, 0, true);

		if (test_ban && th.url_seeds().empty())
		{
			// when we don't have any web seeds left, we know we successfully banned it
			break;
		}

		if (s.is_seeding /* && ss.download_rate == 0.f*/)
		{
			TEST_EQUAL(s.total_payload_download - s.total_redundant_bytes, total_size - pad_file_size);
			// we need to sleep here a bit to let the session sync with the torrent stats
			test_sleep(1000);
			TEST_EQUAL(ses.status().total_payload_download - ses.status().total_redundant_bytes
				, total_size - pad_file_size);
			break;
		}
		test_sleep(500);
	}

	// for test_ban tests, make sure we removed
	// the url seed (i.e. banned it)
	TEST_CHECK(!test_ban || th.url_seeds().empty());

	TEST_EQUAL(cs.cache_size, 0);
	TEST_EQUAL(cs.total_used_buffers, 0);

	std::cerr << "total_size: " << total_size
		<< " rate_sum: " << rate_sum
		<< " session_rate_sum: " << ses_rate_sum
		<< " session total download: " << ses.status().total_payload_download
		<< " torrent total download: " << th.status().total_payload_download
		<< " redundant: " << th.status().total_redundant_bytes
		<< std::endl;

	// the rates for each second should sum up to the total, with a 10% error margin
//	TEST_CHECK(fabs(rate_sum - total_size) < total_size * .1f);
//	TEST_CHECK(fabs(ses_rate_sum - total_size) < total_size * .1f);

	// if test_ban is true, we're not supposed to have completed the download
	// otherwise, we are supposed to have
	TEST_CHECK(th.status().is_seeding == !test_ban);

	if (proxy) stop_proxy(8002);

	TEST_CHECK(exists(combine_path("tmp2_web_seed", torrent_file->files().file_path(
		torrent_file->file_at(0)))) || test_ban);
	remove_all("tmp2_web_seed", ec);
}

void save_file(char const* filename, char const* data, int size)
{
	error_code ec;
	file out(filename, file::write_only, ec);
	TEST_CHECK(!ec);
	if (ec)
	{
		fprintf(stderr, "ERROR opening file '%s': %s\n", filename, ec.message().c_str());
		return;
	}
	file::iovec_t b = { (void*)data, size_t(size) };
	out.writev(0, &b, 1, ec);
	TEST_CHECK(!ec);
	if (ec)
	{
		fprintf(stderr, "ERROR writing file '%s': %s\n", filename, ec.message().c_str());
		return;
	}

}

sha1_hash file_hash(std::string const& name)
{
	std::vector<char> buf;
	error_code ec;
	load_file(name, buf, ec);
	if (buf.empty()) return sha1_hash(0);
	hasher h(&buf[0], buf.size());
	return h.final();
}

// test_url_seed determines whether to use url-seed or http-seed
int run_suite(char const* protocol, bool test_url_seed, bool chunked_encoding, bool test_ban)
{
	using namespace libtorrent;

	error_code ec;
	create_directories("tmp1_web_seed/test_torrent_dir", ec);

	file_storage fs;
	std::srand(10);
	int piece_size = 0x4000;
	static const int file_sizes[] =
	{ 5, 16 - 5, 16000, 17, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
		,1,1,1,1,1,1,13,65000,34,75,2,30,400,500,23000,900,43000,400,4300,6, 4};

	if (test_url_seed)
	{
		create_random_files("tmp1_web_seed/test_torrent_dir", file_sizes, sizeof(file_sizes)/sizeof(file_sizes[0]));
		add_files(fs, "tmp1_web_seed/test_torrent_dir");
	}
	else
	{
		piece_size = 64 * 1024;
		char* random_data = (char*)malloc(64 * 1024 * 25);
		std::generate(random_data, random_data + 64 * 1024 * 25, &std::rand);
		save_file("tmp1_web_seed/seed", random_data, 64 * 1024 * 25);
		fs.add_file("seed", 64 * 1024 * 25);
		free(random_data);
	}

	int port = start_web_server(strcmp(protocol, "https") == 0, chunked_encoding);

	// generate a torrent with pad files to make sure they
	// are not requested web seeds
	libtorrent::create_torrent t(fs, piece_size, 0x4000, libtorrent::create_torrent::optimize
		| libtorrent::create_torrent::calculate_file_hashes);

	char tmp[512];
	if (test_url_seed)
	{
		snprintf(tmp, sizeof(tmp), "%s://127.0.0.1:%d/tmp1_web_seed", protocol, port);
		t.add_url_seed(tmp);
	}
	else
	{
		snprintf(tmp, sizeof(tmp), "%s://127.0.0.1:%d/seed", protocol, port);
		t.add_http_seed(tmp);
	}
	fprintf(stderr, "testing: %s\n", tmp);

	for (int i = 0; i < fs.num_files(); ++i)
	{
		file_entry f = fs.at(i);
		fprintf(stderr, "  %04x: %d %s\n", int(f.offset), f.pad_file, f.path.c_str());
	}

//	for (int i = 0; i < 1000; ++i) sleep(1000);

	// calculate the hash for all pieces
	set_piece_hashes(t, "tmp1_web_seed", ec);

	if (ec)
	{
		fprintf(stderr, "error creating hashes for test torrent: %s\n"
			, ec.message().c_str());
		TEST_CHECK(false);
		return 0;
	}

	if (test_ban)
	{
		// corrupt the files now, so that the web seed will be banned
		if (test_url_seed)
		{
			create_random_files("tmp1_web_seed/test_torrent_dir", file_sizes, sizeof(file_sizes)/sizeof(file_sizes[0]));
		}
		else
		{
			piece_size = 64 * 1024;
			char* random_data = (char*)malloc(64 * 1024 * 25);
			std::generate(random_data, random_data + 64 * 1024 * 25, &std::rand);
			save_file("tmp1_web_seed/seed", random_data, 64 * 1024 * 25);
			free(random_data);
		}
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	boost::intrusive_ptr<torrent_info> torrent_file(new torrent_info(&buf[0], buf.size(), ec));

	// no point in testing the hashes since we know the data is corrupt
	if (!test_ban)
	{
		// verify that the file hashes are correct
		for (int i = 0; i < torrent_file->num_files(); ++i)
		{
			sha1_hash h1 = torrent_file->file_at(i).filehash;
			sha1_hash h2 = file_hash(combine_path("tmp1_web_seed"
				, torrent_file->file_at(i).path));
//			fprintf(stderr, "%s: %s == %s\n"
//				, torrent_file->file_at(i).path.c_str()
//				, to_hex(h1.to_string()).c_str(), to_hex(h2.to_string()).c_str());
			TEST_EQUAL(h1, h2);
		}
	}

	for (int i = 0; i < 6; ++i)
		test_transfer(torrent_file, i, port, protocol, test_url_seed, chunked_encoding, test_ban);
	
	if (test_url_seed)
	{
		torrent_file->rename_file(0, "tmp2_web_seed/test_torrent_dir/renamed_test1");
		test_transfer(torrent_file, 0, port, protocol, test_url_seed, chunked_encoding, test_ban);
	}

	stop_web_server();
	remove_all("tmp1_web_seed", ec);
	return 0;
}

int test_main()
{
	int ret = 0;
	for (int url_seed = 0; url_seed < 2; ++url_seed)
	{
		for (int chunked = 0; chunked < 2; ++chunked)
		{
			for (int ban = 0; ban < 2; ++ban)
			{
#ifdef TORRENT_USE_OPENSSL
				run_suite("https", url_seed, chunked, ban);
#endif
				run_suite("http", url_seed, chunked, ban);
			}
		}
	}
	return ret;
}

