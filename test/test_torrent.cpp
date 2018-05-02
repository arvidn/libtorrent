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
#include "libtorrent/time.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "settings.hpp"
#include <boost/tuple/tuple.hpp>
#include <boost/make_shared.hpp>
#include <iostream>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
namespace lt = libtorrent;

void test_running_torrent(boost::shared_ptr<torrent_info> info, boost::int64_t file_size)
{
	settings_pack pack = settings();
	pack.set_int(settings_pack::alert_mask, alert::storage_notification);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48130");
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	std::vector<boost::uint8_t> zeroes;
	zeroes.resize(1000, 0);
	add_torrent_params p;
	p.flags &= ~add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	p.ti = info;
	p.save_path = ".";

	// make sure we correctly handle the case where we pass in
	// more values than there are files
	p.file_priorities = zeroes;

	error_code ec;
	torrent_handle h = ses.add_torrent(p, ec);
	if (ec)
	{
		fprintf(stdout, "add_torrent: %s\n", ec.message().c_str());
		return;
	}

	std::vector<int> ones(info->num_files(), 1);
	h.prioritize_files(ones);

//	test_sleep(500);
	torrent_status st = h.status();

	TEST_EQUAL(st.total_wanted, file_size); // we want the single file
	TEST_EQUAL(st.total_wanted_done, 0);

	std::vector<int> prio(info->num_files(), 1);
	prio[0] = 0;
	h.prioritize_files(prio);
	st = h.status();

	TEST_EQUAL(st.total_wanted, 0); // we don't want anything
	TEST_EQUAL(st.total_wanted_done, 0);
	TEST_EQUAL(int(h.file_priorities().size()), info->num_files());
	if (!st.is_seeding)
	{
		TEST_EQUAL(h.file_priorities()[0], 0);
		if (info->num_files() > 1)
			TEST_EQUAL(h.file_priorities()[1], 1);
		if (info->num_files() > 2)
			TEST_EQUAL(h.file_priorities()[2], 1);
	}

	if (info->num_files() > 1)
	{
		prio[1] = 0;
		h.prioritize_files(prio);
		st = h.status();

		TEST_EQUAL(st.total_wanted, file_size);
		TEST_EQUAL(st.total_wanted_done, 0);
		if (!st.is_seeding)
		{
			TEST_EQUAL(int(h.file_priorities().size()), info->num_files());
			TEST_EQUAL(h.file_priorities()[0], 0);
			if (info->num_files() > 1)
				TEST_EQUAL(h.file_priorities()[1], 0);
			if (info->num_files() > 2)
				TEST_EQUAL(h.file_priorities()[2], 1);
		}
	}

	if (info->num_pieces() > 0)
	{
		h.piece_priority(0, 1);
		st = h.status();
		TEST_CHECK(st.pieces.size() > 0 && st.pieces[0] == false);
		std::vector<char> piece(info->piece_length());
		for (int i = 0; i < int(piece.size()); ++i)
			piece[i] = (i % 26) + 'A';
		h.add_piece(0, &piece[0], torrent_handle::overwrite_existing);

		// wait until the piece is done writing and hashing
		wait_for_alert(ses, piece_finished_alert::alert_type, "piece_finished_alert");
		st = h.status();
		TEST_CHECK(st.pieces.size() > 0);

		std::cout << "reading piece 0" << std::endl;
		h.read_piece(0);
		alert const* a = wait_for_alert(ses, read_piece_alert::alert_type, "read_piece");
		TEST_CHECK(a);
		read_piece_alert const* rpa = alert_cast<read_piece_alert>(a);
		TEST_CHECK(rpa);
		if (rpa)
		{
			std::cout << "SUCCEEDED!" << std::endl;
			TEST_CHECK(memcmp(&piece[0], rpa->buffer.get(), info->piece_size(0)) == 0);
			TEST_CHECK(rpa->size == info->piece_size(0));
			TEST_CHECK(rpa->piece == 0);
			TEST_CHECK(hasher(&piece[0], piece.size()).final() == info->hash_for_piece(0));
		}
	}
}

TORRENT_TEST(long_names)
{
	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name"] = "slightly shorter name, it's kind of sad that people started the trend of incorrectly encoding the regular name field and then adding another one with correct encoding";
	info["name.utf-8"] = "this is a long ass name in order to try to make make_magnet_uri overflow and hopefully crash. Although, by the time you read this that particular bug should have been fixed";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry torrent;
	torrent["info"] = info;

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	error_code ec;
	boost::shared_ptr<torrent_info> ti(boost::make_shared<torrent_info>(&buf[0], buf.size(), boost::ref(ec)));
	TEST_CHECK(!ec);
}

TORRENT_TEST(total_wanted)
{
	file_storage fs;

	fs.add_file("test_torrent_dir4/tmp1", 1024);
	fs.add_file("test_torrent_dir4/tmp2", 1024);
	fs.add_file("test_torrent_dir4/tmp3", 1024);
	fs.add_file("test_torrent_dir4/tmp4", 1024);

	libtorrent::create_torrent t(fs, 1024);
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	error_code ec;
	boost::shared_ptr<torrent_info> info(boost::make_shared<torrent_info>(
		&tmp[0], tmp.size(), boost::ref(ec)));

	settings_pack pack = settings();
	pack.set_int(settings_pack::alert_mask, alert::storage_notification);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48130");
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	add_torrent_params p;
	p.ti = info;
	p.save_path = ".";

	// we just want 1 out of 4 files, 1024 out of 4096 bytes
	p.file_priorities.resize(4, 0);
	p.file_priorities[1] = 1;

	p.ti = info;

	torrent_handle h = ses.add_torrent(p);

	torrent_status st = h.status();
	std::cout << "total_wanted: " << st.total_wanted << " : " << 1024 << std::endl;
	TEST_EQUAL(st.total_wanted, 1024);
	std::cout << "total_wanted_done: " << st.total_wanted_done << " : 0" << std::endl;
	TEST_EQUAL(st.total_wanted_done, 0);
}

TORRENT_TEST(added_peers)
{
	file_storage fs;

	fs.add_file("test_torrent_dir4/tmp1", 1024);

	libtorrent::create_torrent t(fs, 1024);
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	error_code ec;
	boost::shared_ptr<torrent_info> info(boost::make_shared<torrent_info>(
		&tmp[0], tmp.size(), boost::ref(ec)));

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48130");
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	add_torrent_params p;
	p.ti = info;
	p.save_path = ".";
	p.url = "?x.pe=127.0.0.1:48081&x.pe=127.0.0.2:48082";

	torrent_handle h = ses.add_torrent(p);

	std::vector<peer_list_entry> v;
	h.get_full_peer_list(v);
	TEST_EQUAL(v.size(), 2);
}

TORRENT_TEST(torrent)
{
/*	{
		remove("test_torrent_dir2/tmp1");
		remove("test_torrent_dir2/tmp2");
		remove("test_torrent_dir2/tmp3");
		file_storage fs;
		boost::int64_t file_size = 256 * 1024;
		fs.add_file("test_torrent_dir2/tmp1", file_size);
		fs.add_file("test_torrent_dir2/tmp2", file_size);
		fs.add_file("test_torrent_dir2/tmp3", file_size);
		libtorrent::create_torrent t(fs, 128 * 1024);
		t.add_tracker("http://non-existing.com/announce");

		std::vector<char> piece(128 * 1024);
		for (int i = 0; i < int(piece.size()); ++i)
			piece[i] = (i % 26) + 'A';

		// calculate the hash for all pieces
		sha1_hash ph = hasher(&piece[0], piece.size()).final();
		int num = t.num_pieces();
		TEST_CHECK(t.num_pieces() > 0);
		for (int i = 0; i < num; ++i)
			t.set_hash(i, ph);

		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, t.generate());
		error_code ec;
		boost::shared_ptr<torrent_info> info(boost::make_shared<torrent_info>(&tmp[0], tmp.size(), boost::ref(ec), 0));
		TEST_CHECK(info->num_pieces() > 0);

		test_running_torrent(info, file_size);
	}
*/
	{
		file_storage fs;

		fs.add_file("test_torrent_dir2/tmp1", 1024);
		libtorrent::create_torrent t(fs, 128 * 1024, 6);

		std::vector<char> piece(128 * 1024);
		for (int i = 0; i < int(piece.size()); ++i)
			piece[i] = (i % 26) + 'A';

		// calculate the hash for all pieces
		sha1_hash ph = hasher(&piece[0], piece.size()).final();
		int num = t.num_pieces();
		TEST_CHECK(t.num_pieces() > 0);
		for (int i = 0; i < num; ++i)
			t.set_hash(i, ph);

		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, t.generate());
		error_code ec;
		boost::shared_ptr<torrent_info> info(boost::make_shared<torrent_info>(&tmp[0], tmp.size(), boost::ref(ec), 0));
		test_running_torrent(info, 1024);
	}
}

#ifndef TORRENT_DISABLE_EXTENSIONS
struct test_plugin : libtorrent::torrent_plugin {};

struct plugin_creator
{
	plugin_creator(int& c) : m_called(c) {}

	boost::shared_ptr<libtorrent::torrent_plugin>
	operator()(torrent_handle const&, void*)
	{
		++m_called;
		return boost::make_shared<test_plugin>();
	}

	int& m_called;
};

TORRENT_TEST(duplicate_is_not_error)
{
	file_storage fs;

	fs.add_file("test_torrent_dir2/tmp1", 1024);
	libtorrent::create_torrent t(fs, 128 * 1024, 6);

	std::vector<char> piece(128 * 1024);
	for (int i = 0; i < int(piece.size()); ++i)
		piece[i] = (i % 26) + 'A';

	// calculate the hash for all pieces
	sha1_hash ph = hasher(&piece[0], piece.size()).final();
	int num = t.num_pieces();
	TEST_CHECK(t.num_pieces() > 0);
	for (int i = 0; i < num; ++i)
		t.set_hash(i, ph);

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);
	bencode(out, t.generate());
	error_code ec;

	int called = 0;
	plugin_creator creator(called);

	add_torrent_params p;
	p.ti = boost::make_shared<torrent_info>(&tmp[0], tmp.size(), boost::ref(ec), 0);
	p.flags &= ~add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	p.flags &= ~add_torrent_params::flag_duplicate_is_error;
	p.save_path = ".";
	p.extensions.push_back(creator);

	lt::session ses(settings());
	ses.async_add_torrent(p);
	ses.async_add_torrent(p);

	wait_for_downloading(ses, "ses");

	// we should only have added the plugin once
	TEST_EQUAL(called, 1);
}
#endif

TORRENT_TEST(torrent_total_size_zero)
{
	file_storage fs;
	error_code ec;

	fs.add_file("test_torrent_dir2/tmp1", 0);
	TEST_CHECK(fs.num_files() == 1);
	TEST_CHECK(fs.total_size() == 0);

	ec.clear();
	libtorrent::create_torrent t1(fs);
	set_piece_hashes(t1, ".", ec);
	TEST_CHECK(ec);

	fs.add_file("test_torrent_dir2/tmp2", 0);
	TEST_CHECK(fs.num_files() == 2);
	TEST_CHECK(fs.total_size() == 0);

	ec.clear();
	libtorrent::create_torrent t2(fs);
	set_piece_hashes(t2, ".", ec);
	TEST_CHECK(ec);
}

void test_queue(add_torrent_params p)
{
	lt::settings_pack pack = settings();
	// we're not testing the hash check, just accept the data we write
	pack.set_bool(settings_pack::disable_hash_checks, true);
	lt::session ses(pack);

	std::vector<torrent_handle> torrents;
	for(int i = 0; i < 6; i++)
	{
		file_storage fs;
		std::stringstream file_path;
		file_path << "test_torrent_dir4/queue" << i;
		fs.add_file(file_path.str(), 1024);
		libtorrent::create_torrent t(fs, 128 * 1024, 6);

		std::vector<char> buf;
		bencode(std::back_inserter(buf), t.generate());
		boost::shared_ptr<torrent_info> ti = boost::make_shared<torrent_info>(&buf[0], buf.size());
		p.ti = ti;
		p.save_path = ".";
		torrents.push_back(ses.add_torrent(p));
	}

	print_alerts(ses, "ses");

	std::vector<int> pieces = torrents[5].piece_priorities();
	std::vector<std::pair<int, int> > piece_prios;
	for (int i = 0; i < int(pieces.size()); ++i) {
		piece_prios.push_back(std::make_pair(i,0));
	}
	torrents[5].prioritize_pieces(piece_prios);
	torrent_handle finished = torrents[5];

	wait_for_alert(ses, torrent_finished_alert::alert_type, "ses");

	// add_torrent should be ordered
	TEST_EQUAL(finished.queue_position(), -1);
	TEST_EQUAL(torrents[0].queue_position(), 0);
	TEST_EQUAL(torrents[1].queue_position(), 1);
	TEST_EQUAL(torrents[2].queue_position(), 2);
	TEST_EQUAL(torrents[3].queue_position(), 3);
	TEST_EQUAL(torrents[4].queue_position(), 4);

	// test top and bottom
	torrents[2].queue_position_top();
	torrents[1].queue_position_bottom();

	TEST_EQUAL(finished.queue_position(), -1);
	TEST_EQUAL(torrents[2].queue_position(), 0);
	TEST_EQUAL(torrents[0].queue_position(), 1);
	TEST_EQUAL(torrents[3].queue_position(), 2);
	TEST_EQUAL(torrents[4].queue_position(), 3);
	TEST_EQUAL(torrents[1].queue_position(), 4);

	// test set pos
	torrents[0].queue_position_set(0);
	torrents[1].queue_position_set(1);
	// torrent 2 should be get moved down by 0 and 1 to pos 2

	TEST_EQUAL(finished.queue_position(), -1);
	TEST_EQUAL(torrents[0].queue_position(), 0);
	TEST_EQUAL(torrents[1].queue_position(), 1);
	TEST_EQUAL(torrents[2].queue_position(), 2);
	TEST_EQUAL(torrents[3].queue_position(), 3);
	TEST_EQUAL(torrents[4].queue_position(), 4);

	//test strange up and down commands
	torrents[0].queue_position_up();
	torrents[4].queue_position_down();

	TEST_EQUAL(finished.queue_position(), -1);
	TEST_EQUAL(torrents[0].queue_position(), 0);
	TEST_EQUAL(torrents[1].queue_position(), 1);
	TEST_EQUAL(torrents[2].queue_position(), 2);
	TEST_EQUAL(torrents[3].queue_position(), 3);
	TEST_EQUAL(torrents[4].queue_position(), 4);

	torrents[1].queue_position_up();
	torrents[3].queue_position_down();
	finished.queue_position_up();

	TEST_EQUAL(finished.queue_position(), -1);
	TEST_EQUAL(torrents[1].queue_position(), 0);
	TEST_EQUAL(torrents[0].queue_position(), 1);
	TEST_EQUAL(torrents[2].queue_position(), 2);
	TEST_EQUAL(torrents[4].queue_position(), 3);
	TEST_EQUAL(torrents[3].queue_position(), 4);

	torrents[1].queue_position_down();
	torrents[3].queue_position_up();
	finished.queue_position_down();


	TEST_EQUAL(finished.queue_position(), -1);
	TEST_EQUAL(torrents[0].queue_position(), 0);
	TEST_EQUAL(torrents[1].queue_position(), 1);
	TEST_EQUAL(torrents[2].queue_position(), 2);
	TEST_EQUAL(torrents[3].queue_position(), 3);
	TEST_EQUAL(torrents[4].queue_position(), 4);

	// test set pos on not existing pos
	torrents[3].queue_position_set(10);
	finished.queue_position_set(10);

	TEST_EQUAL(finished.queue_position(), -1);
	TEST_EQUAL(torrents[0].queue_position(), 0);
	TEST_EQUAL(torrents[1].queue_position(), 1);
	TEST_EQUAL(torrents[2].queue_position(), 2);
	TEST_EQUAL(torrents[4].queue_position(), 3);
	TEST_EQUAL(torrents[3].queue_position(), 4);
}

TORRENT_TEST(queue)
{
	test_queue(add_torrent_params());
}

TORRENT_TEST(queue_paused)
{
	add_torrent_params p;
	p.flags |= add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	test_queue(p);
}

TORRENT_TEST(test_move_storage_no_metadata)
{
	lt::session ses(settings());
	add_torrent_params p;
	p.save_path = "save_path";
	error_code ec;
	parse_magnet_uri("magnet?xt=urn:btih:abababababababababababababababababababab", p, ec);
	torrent_handle h = ses.add_torrent(p);

	TEST_EQUAL(h.status().save_path, complete("save_path"));

	h.move_storage("save_path_1");

	TEST_EQUAL(h.status().save_path, complete("save_path_1"));
}

