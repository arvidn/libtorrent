/*

Copyright (c) 2014, Arvid Norberg
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
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/create_torrent.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;

boost::intrusive_ptr<torrent_info> generate_torrent()
{
	file_storage fs;
	fs.add_file("test_resume/tmp1", 128 * 1024 * 10);
	libtorrent::create_torrent t(fs, 128 * 1024, 6);

	t.add_tracker("http://torrent_file_tracker.com/announce");

	int num = t.num_pieces();
	TEST_CHECK(num > 0);
	for (int i = 0; i < num; ++i)
	{
		sha1_hash ph;
		for (int k = 0; k < 20; ++k) ph[k] = libtorrent::random();
		t.set_hash(i, ph);
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	return boost::intrusive_ptr<torrent_info>(new torrent_info(&buf[0], buf.size()));
}

std::vector<char> generate_resume_data(torrent_info* ti)
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hash().to_string();
	rd["blocks per piece"] = (std::max)(1, ti->piece_length() / 0x4000);
	rd["pieces"] = std::string(ti->num_pieces(), '\0');

	rd["total_uploaded"] = 1337;
	rd["total_downloaded"] = 1338;
	rd["active_time"] = 1339;
	rd["seeding_time"] = 1340;
	rd["num_seeds"] = 1341;
	rd["num_downloaders"] = 1342;
	rd["upload_rate_limit"] = 1343;
	rd["download_rate_limit"] = 1344;
	rd["max_connections"] = 1345;
	rd["max_uploads"] = 1346;
	rd["seed_mode"] = 0;
	rd["super_seeding"] = 0;
	rd["added_time"] = 1347;
	rd["completed_time"] = 1348;
	rd["last_scrape"] = 1349;
	rd["last_download"] = 1350;
	rd["last_upload"] = 1351;
	rd["finished_time"] = 1352;
	entry::list_type& file_prio = rd["file_priority"].list();
	file_prio.push_back(entry(1));

	rd["piece_priority"] = std::string(ti->num_pieces(), '\x01');
	rd["auto_managed"] = 0;
	rd["sequential_download"] = 0;
	rd["paused"] = 0;
	entry::list_type& trackers = rd["trackers"].list();
	trackers.push_back(entry(entry::list_t));
	trackers.back().list().push_back(entry("http://resume_data_tracker.com/announce"));
	entry::list_type& url_list = rd["url-list"].list();
	url_list.push_back(entry("http://resume_data_url_seed.com"));

	entry::list_type& httpseeds = rd["httpseeds"].list();
	httpseeds.push_back(entry("http://resume_data_http_seed.com"));

	rd["save_path"] = "/resume_data save_path";

	std::vector<char> ret;
	bencode(back_inserter(ret), rd);

	return ret;
}

torrent_status test_resume_flags(int flags)
{
	session ses;

	boost::intrusive_ptr<torrent_info> ti = generate_torrent();

	add_torrent_params p;
	
	p.ti = ti;
	p.flags = flags;
	p.save_path = "/add_torrent_params save_path";
	p.trackers.push_back("http://add_torrent_params_tracker.com/announce");
	p.url_seeds.push_back("http://add_torrent_params_url_seed.com");

	std::vector<char> rd = generate_resume_data(ti.get());
	p.resume_data.swap(rd);

	p.max_uploads = 1;
	p.max_connections = 2;
	p.upload_limit = 3;
	p.download_limit = 4;
	p.file_priorities.push_back(2);

	torrent_handle h = ses.add_torrent(p);
	torrent_status s = h.status();
	TEST_EQUAL(s.info_hash, ti->info_hash());
	return s;
}

void default_tests(torrent_status const& s)
{
	TEST_EQUAL(s.last_scrape, 1349);
	TEST_EQUAL(s.time_since_download, 1350);
	TEST_EQUAL(s.time_since_upload, 1351);
	TEST_EQUAL(s.active_time, 1339);
	TEST_EQUAL(s.finished_time, 1352);
	TEST_EQUAL(s.seeding_time, 1340);
	TEST_EQUAL(s.added_time, 1347);
	TEST_EQUAL(s.completed_time, 1348);
}

int test_main()
{
	torrent_status s;

	fprintf(stderr, "flags: 0\n");
	s = test_resume_flags(0);
	default_tests(s);
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	fprintf(stderr, "flags: use_resume_save_path\n");
	s = test_resume_flags(add_torrent_params::flag_use_resume_save_path);
	default_tests(s);
	TEST_EQUAL(s.save_path, "/resume_data save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	fprintf(stderr, "flags: override_resume_data\n");
	s = test_resume_flags(add_torrent_params::flag_override_resume_data
		| add_torrent_params::flag_paused);

	default_tests(s);
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, true);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);

	fprintf(stderr, "flags: seed_mode\n");
	s = test_resume_flags(add_torrent_params::flag_override_resume_data
		| add_torrent_params::flag_seed_mode);
	default_tests(s);
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, true);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);

	fprintf(stderr, "flags: upload_mode\n");
	s = test_resume_flags(add_torrent_params::flag_upload_mode);
	default_tests(s);
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, true);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	fprintf(stderr, "flags: share_mode\n");
	s = test_resume_flags(add_torrent_params::flag_override_resume_data
		| add_torrent_params::flag_share_mode);
	default_tests(s);
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, true);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);

	// resume data overrides the auto-managed flag
	fprintf(stderr, "flags: auto_managed\n");
	s = test_resume_flags(add_torrent_params::flag_auto_managed);
	default_tests(s);
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	// resume data overrides the paused flag
	fprintf(stderr, "flags: paused\n");
	s = test_resume_flags(add_torrent_params::flag_paused);
	default_tests(s);
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	// TODO: 2 test all other resume flags here too. This would require returning
	// more than just the torrent_status from test_resume_flags. Also http seeds
	// and trackers for instance
	return 0;
}


