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
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/path.hpp" // for combine_path, current_working_directory
#include "libtorrent/magnet_uri.hpp"
#include "settings.hpp"
#include <tuple>
#include <iostream>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

using namespace lt;

namespace {

bool wait_priority(torrent_handle const& h, aux::vector<download_priority_t, file_index_t> const& prio)
{
	for (int i = 0; i < 10; ++i)
	{
		if (h.get_file_priorities() == prio) return true;

#ifdef NDEBUG
		std::this_thread::sleep_for(lt::milliseconds(100));
#else
		std::this_thread::sleep_for(lt::milliseconds(300));
#endif
	}

	return h.get_file_priorities() == prio;
}

bool prioritize_files(torrent_handle const& h, aux::vector<download_priority_t, file_index_t> const& prio)
{
	h.prioritize_files(prio);
	return wait_priority(h, prio);
}

void test_running_torrent(std::shared_ptr<torrent_info> info, std::int64_t file_size)
{
	settings_pack pack = settings();
	pack.set_int(settings_pack::alert_mask, alert_category::piece_progress | alert_category::storage);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48130");
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	aux::vector<download_priority_t, file_index_t> zeroes;
	zeroes.resize(1000, 0_pri);
	add_torrent_params p;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.ti = info;
	p.save_path = ".";

	// make sure we correctly handle the case where we pass in
	// more values than there are files
	p.file_priorities = zeroes;

	error_code ec;
	torrent_handle h = ses.add_torrent(p, ec);
	if (ec)
	{
		std::printf("add_torrent: %s\n", ec.message().c_str());
		return;
	}

	aux::vector<download_priority_t, file_index_t> ones(std::size_t(info->num_files()), 1_pri);
	TEST_CHECK(prioritize_files(h, ones));

	torrent_status st = h.status();

	TEST_EQUAL(st.total_wanted, file_size); // we want the single file
	TEST_EQUAL(st.total_wanted_done, 0);

	aux::vector<download_priority_t, file_index_t> prio(std::size_t(info->num_files()), 1_pri);
	prio[file_index_t(0)] = 0_pri;
	TEST_CHECK(prioritize_files(h, prio));
	st = h.status();

	st = h.status();
	TEST_EQUAL(st.total_wanted, 0); // we don't want anything
	TEST_EQUAL(st.total_wanted_done, 0);
	TEST_EQUAL(int(h.get_file_priorities().size()), info->num_files());

	if (info->num_files() > 1)
	{
		prio[file_index_t{1}] = 0_pri;
		TEST_CHECK(prioritize_files(h, prio));

		st = h.status();
		TEST_EQUAL(st.total_wanted, file_size);
		TEST_EQUAL(st.total_wanted_done, 0);
	}

	if (info->num_pieces() > 0)
	{
		h.piece_priority(piece_index_t(0), 1_pri);
		st = h.status();
		TEST_CHECK(st.pieces.size() > 0 && st.pieces[piece_index_t(0)] == false);
		std::vector<char> piece(std::size_t(info->piece_length()));
		for (int i = 0; i < int(piece.size()); ++i)
			piece[std::size_t(i)] = (i % 26) + 'A';
		h.add_piece(piece_index_t(0), &piece[0], torrent_handle::overwrite_existing);

		// wait until the piece is done writing and hashing
		wait_for_alert(ses, piece_finished_alert::alert_type, "piece_finished_alert");
		st = h.status();
		TEST_CHECK(st.pieces.size() > 0);

		std::cout << "reading piece 0" << std::endl;
		h.read_piece(piece_index_t(0));
		alert const* a = wait_for_alert(ses, read_piece_alert::alert_type, "read_piece");
		TEST_CHECK(a);
		read_piece_alert const* rpa = alert_cast<read_piece_alert>(a);
		TEST_CHECK(rpa);
		if (rpa)
		{
			std::cout << "SUCCEEDED!" << std::endl;
			TEST_CHECK(std::memcmp(&piece[0], rpa->buffer.get()
				, std::size_t(info->piece_size(piece_index_t(0)))) == 0);
			TEST_CHECK(rpa->size == info->piece_size(piece_index_t(0)));
			TEST_CHECK(rpa->piece == piece_index_t(0));
			TEST_CHECK(hasher(piece).final() == info->hash_for_piece(piece_index_t(0)));
		}
	}

	TEST_CHECK(h.get_file_priorities() == prio);
}

void test_large_piece_size(int const size)
{
	entry torrent;
	entry& info = torrent["info"];
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name"] = "test";
	info["piece length"] = size;
	info["length"] = size;

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	add_torrent_params atp;
	atp.ti = std::make_shared<torrent_info>(buf, from_span);
	atp.save_path = ".";

	lt::session ses;
	auto h = ses.add_torrent(atp);
	TEST_CHECK(h.status().errc == error_code(lt::errors::invalid_piece_size));
	h.clear_error();
	TEST_CHECK(h.status().errc == error_code(lt::errors::invalid_piece_size));
}

} // anonymous namespace

TORRENT_TEST(long_names)
{
	entry torrent;
	entry& info = torrent["info"];
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name"] = "slightly shorter name, it's kind of sad that people started "
		"the trend of incorrectly encoding the regular name field and then adding "
		"another one with correct encoding";
	info["name.utf-8"] = "this is a long ass name in order to try to make "
		"make_magnet_uri overflow and hopefully crash. Although, by the time you "
		"read this that particular bug should have been fixed";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	auto ti = std::make_shared<torrent_info>(buf, from_span);
}

TORRENT_TEST(large_piece_size)
{
	test_large_piece_size(32768 * 16 * 1024);
	test_large_piece_size(65536 * 16 * 1024);
	test_large_piece_size(65537 * 16 * 1024);
}

TORRENT_TEST(total_wanted)
{
	file_storage fs;

	fs.add_file("test_torrent_dir4/tmp1", 1024);
	fs.add_file("test_torrent_dir4/tmp2", 1024);
	fs.add_file("test_torrent_dir4/tmp3", 1024);
	fs.add_file("test_torrent_dir4/tmp4", 1024);

	lt::create_torrent t(fs, 1024);
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	auto info = std::make_shared<torrent_info>(tmp, from_span);

	settings_pack pack = settings();
	pack.set_int(settings_pack::alert_mask, alert_category::storage);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48130");
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	add_torrent_params p;
	p.ti = info;
	p.save_path = ".";

	// we just want 1 out of 4 files, 1024 out of 4096 bytes
	p.file_priorities.resize(4, 0_pri);
	p.file_priorities[1] = 1_pri;

	torrent_handle h = ses.add_torrent(std::move(p));

	torrent_status st = h.status();
	TEST_EQUAL(st.total_wanted, 1024);
	TEST_EQUAL(st.total_wanted_done, 0);

	// make sure that selecting and unseleting a file quickly still end up with
	// the last set priority
	h.file_priority(file_index_t{1}, default_priority);
	h.file_priority(file_index_t{1}, dont_download);
	TEST_CHECK(wait_priority(h, aux::vector<download_priority_t, file_index_t>(static_cast<std::size_t>(fs.num_files()))));
	TEST_EQUAL(h.status({}).total_wanted, 0);
}

TORRENT_TEST(added_peers)
{
	file_storage fs;

	fs.add_file("test_torrent_dir4/tmp1", 1024);

	lt::create_torrent t(fs, 1024);
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	auto info = std::make_shared<torrent_info>(tmp, from_span);

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48130");
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:abababababababababababababababababababab&x.pe=127.0.0.1:48081&x.pe=127.0.0.2:48082");
	p.ti = info;
	p.info_hash.clear();
	p.save_path = ".";

	torrent_handle h = ses.add_torrent(std::move(p));

	h.save_resume_data();
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra) TEST_EQUAL(ra->params.peers.size(), 2);
}

TORRENT_TEST(mismatching_info_hash)
{
	file_storage fs;
	fs.add_file("test_torrent_dir4/tmp1", 1024);
	lt::create_torrent t(fs, 1024);
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	auto info = std::make_shared<torrent_info>(tmp, from_span);

	add_torrent_params p;
	p.ti = std::move(info);

	// this info-hash is definitely different from the one in `info`, this
	// should trigger a failure
	p.info_hash = lt::sha1_hash("01010101010101010101");
	p.save_path = ".";

	lt::session ses(settings());
	error_code ec;
	torrent_handle h = ses.add_torrent(std::move(p), ec);
	TEST_CHECK(ec == lt::errors::mismatching_info_hash);
	TEST_CHECK(!h.is_valid());
}

TORRENT_TEST(exceed_file_prio)
{
	file_storage fs;
	fs.add_file("test_torrent_dir4/tmp1", 1024);
	lt::create_torrent t(fs, 1024);
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	auto info = std::make_shared<torrent_info>(tmp, from_span);

	add_torrent_params p;
	p.ti = std::move(info);

	p.file_priorities.resize(9999, lt::low_priority);
	p.save_path = ".";

	lt::session ses(settings());
	error_code ec;
	torrent_handle h = ses.add_torrent(std::move(p));
	auto const prios = h.get_file_priorities();
	TEST_CHECK(prios.size() == 1);
}

TORRENT_TEST(exceed_piece_prio)
{
	file_storage fs;
	fs.add_file("test_torrent_dir4/tmp1", 1024);
	lt::create_torrent t(fs, 1024);
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	auto info = std::make_shared<torrent_info>(tmp, from_span);
	std::size_t const num_pieces = std::size_t(info->num_pieces());

	add_torrent_params p;
	p.ti = std::move(info);

	p.piece_priorities.resize(9999, lt::low_priority);
	p.save_path = ".";

	lt::session ses(settings());
	error_code ec;
	torrent_handle h = ses.add_torrent(std::move(p));
	auto const prios = h.get_piece_priorities();
	TEST_CHECK(prios.size() == num_pieces);
}

TORRENT_TEST(exceed_piece_prio_magnet)
{
	add_torrent_params p;
	p.info_hash = sha1_hash("abababababababababab");
	p.piece_priorities.resize(9999, lt::low_priority);
	p.save_path = ".";

	lt::session ses(settings());
	error_code ec;
	torrent_handle h = ses.add_torrent(std::move(p));
	auto const prios = h.get_piece_priorities();
	TEST_CHECK(prios.empty());
}

TORRENT_TEST(torrent)
{
/*	{
		remove("test_torrent_dir2/tmp1");
		remove("test_torrent_dir2/tmp2");
		remove("test_torrent_dir2/tmp3");
		file_storage fs;
		std::int64_t file_size = 256 * 1024;
		fs.add_file("test_torrent_dir2/tmp1", file_size);
		fs.add_file("test_torrent_dir2/tmp2", file_size);
		fs.add_file("test_torrent_dir2/tmp3", file_size);
		lt::create_torrent t(fs, 128 * 1024);
		t.add_tracker("http://non-existing.com/announce");

		std::vector<char> piece(128 * 1024);
		for (int i = 0; i < int(piece.size()); ++i)
			piece[i] = (i % 26) + 'A';

		// calculate the hash for all pieces
		sha1_hash ph = hasher(piece).final();
		int num = t.num_pieces();
		TEST_CHECK(t.num_pieces() > 0);
		for (int i = 0; i < num; ++i)
			t.set_hash(i, ph);

		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char>> out(tmp);
		bencode(out, t.generate());
		error_code ec;
		auto info = std::make_shared<torrent_info>(tmp, std::ref(ec), from_span);
		TEST_CHECK(info->num_pieces() > 0);

		test_running_torrent(info, file_size);
	}
*/
	{
		file_storage fs;

		fs.add_file("test_torrent_dir2/tmp1", 1024);
		lt::create_torrent t(fs, 1024, 6);

		std::vector<char> piece(1024);
		for (int i = 0; i < int(piece.size()); ++i)
			piece[std::size_t(i)] = (i % 26) + 'A';

		// calculate the hash for all pieces
		sha1_hash const ph = hasher(piece).final();
		TEST_CHECK(t.num_pieces() > 0);
		for (auto const i : fs.piece_range())
			t.set_hash(i, ph);

		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char>> out(tmp);
		bencode(out, t.generate());
		auto info = std::make_shared<torrent_info>(tmp, from_span);
		test_running_torrent(info, 1024);
	}
}

#ifndef TORRENT_DISABLE_EXTENSIONS
struct test_plugin : lt::torrent_plugin {};

struct plugin_creator
{
	explicit plugin_creator(int& c) : m_called(c) {}

	std::shared_ptr<lt::torrent_plugin>
	operator()(torrent_handle const&, void*)
	{
		++m_called;
		return std::make_shared<test_plugin>();
	}

	int& m_called;
};

TORRENT_TEST(duplicate_is_not_error)
{
	file_storage fs;

	fs.add_file("test_torrent_dir2/tmp1", 1024);
	lt::create_torrent t(fs, 128 * 1024, 6);

	std::vector<char> piece(128 * 1024);
	for (int i = 0; i < int(piece.size()); ++i)
		piece[std::size_t(i)] = (i % 26) + 'A';

	// calculate the hash for all pieces
	sha1_hash ph = hasher(piece).final();
	TEST_CHECK(t.num_pieces() > 0);
	for (auto const i : fs.piece_range())
		t.set_hash(i, ph);

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char>> out(tmp);
	bencode(out, t.generate());

	int called = 0;
	plugin_creator creator(called);

	add_torrent_params p;
	p.ti = std::make_shared<torrent_info>(tmp, from_span);
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.flags &= ~torrent_flags::duplicate_is_error;
	p.save_path = ".";
	p.extensions.push_back(creator);

	lt::session ses(settings());
	ses.async_add_torrent(p);
	ses.async_add_torrent(std::move(p));

	wait_for_downloading(ses, "ses");

	// we should only have added the plugin once
	TEST_EQUAL(called, 1);
}
#endif

TORRENT_TEST(torrent_total_size_zero)
{
	file_storage fs;

	fs.add_file("test_torrent_dir2/tmp1", 0);
	TEST_CHECK(fs.num_files() == 1);
	TEST_CHECK(fs.total_size() == 0);

	error_code ec;
	lt::create_torrent t1(fs);
	set_piece_hashes(t1, ".", ec);
	TEST_CHECK(ec);

	fs.add_file("test_torrent_dir2/tmp2", 0);
	TEST_CHECK(fs.num_files() == 2);
	TEST_CHECK(fs.total_size() == 0);

	ec.clear();
	lt::create_torrent t2(fs);
	set_piece_hashes(t2, ".", ec);
	TEST_CHECK(ec);
}

TORRENT_TEST(rename_file)
{
	file_storage fs;

	fs.add_file("test3/tmp1", 20);
	fs.add_file("test3/tmp2", 20);
	lt::create_torrent t(fs, 128 * 1024, 6);

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char>> out(tmp);
	bencode(out, t.generate());
	auto info = std::make_shared<torrent_info>(tmp, from_span);

	TEST_EQUAL(info->files().file_path(file_index_t(0)), combine_path("test3","tmp1"));

	// move "test3/tmp1" -> "tmp1"
	info->rename_file(file_index_t(0), "tmp1");

	TEST_EQUAL(info->files().file_path(file_index_t(0)), "tmp1");
}

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(async_load_deprecated)
{
	settings_pack pack = settings();
	lt::session ses(pack);

	add_torrent_params p;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	std::string dir = parent_path(current_working_directory());

	p.url = "file://" + combine_path(combine_path(dir, "test_torrents"), "base.torrent");
	p.save_path = ".";
	ses.async_add_torrent(std::move(p));

	alert const* a = wait_for_alert(ses, add_torrent_alert::alert_type);
	TEST_CHECK(a);
	if (a == nullptr) return;
	auto const* ta = alert_cast<add_torrent_alert const>(a);
	TEST_CHECK(ta);
	if (ta == nullptr) return;
	TEST_CHECK(!ta->error);
	TEST_CHECK(ta->params.ti->name() == "temp");
}
#endif

TORRENT_TEST(torrent_status)
{
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_none), -1);
#if TORRENT_ABI_VERSION == 1
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_url), -2);
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_metadata), -4);
#endif
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_ssl_ctx), -3);
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_exception), -5);
}

namespace {

void test_queue(add_torrent_params)
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
		lt::create_torrent t(fs, 128 * 1024, 6);

		std::vector<char> buf;
		bencode(std::back_inserter(buf), t.generate());
		auto ti = std::make_shared<torrent_info>(buf, from_span);
		add_torrent_params p;
		p.ti = ti;
		p.save_path = ".";
		torrents.push_back(ses.add_torrent(std::move(p)));
	}

	print_alerts(ses, "ses");

	std::vector<download_priority_t> pieces(
		std::size_t(torrents[5].torrent_file()->num_pieces()), 0_pri);
	torrents[5].prioritize_pieces(pieces);
	torrent_handle finished = torrents[5];

	wait_for_alert(ses, torrent_finished_alert::alert_type, "ses");

	// add_torrent should be ordered
	TEST_EQUAL(finished.queue_position(), no_pos);
	TEST_EQUAL(torrents[0].queue_position(), queue_position_t{0});
	TEST_EQUAL(torrents[1].queue_position(), queue_position_t{1});
	TEST_EQUAL(torrents[2].queue_position(), queue_position_t{2});
	TEST_EQUAL(torrents[3].queue_position(), queue_position_t{3});
	TEST_EQUAL(torrents[4].queue_position(), queue_position_t{4});

	// test top and bottom
	torrents[2].queue_position_top();
	torrents[1].queue_position_bottom();

	TEST_EQUAL(finished.queue_position(), no_pos);
	TEST_EQUAL(torrents[2].queue_position(), queue_position_t{0});
	TEST_EQUAL(torrents[0].queue_position(), queue_position_t{1});
	TEST_EQUAL(torrents[3].queue_position(), queue_position_t{2});
	TEST_EQUAL(torrents[4].queue_position(), queue_position_t{3});
	TEST_EQUAL(torrents[1].queue_position(), queue_position_t{4});

	// test set pos
	torrents[0].queue_position_set(queue_position_t{0});
	torrents[1].queue_position_set(queue_position_t{1});
	// torrent 2 should be get moved down by 0 and 1 to pos 2

	TEST_EQUAL(finished.queue_position(), no_pos);
	TEST_EQUAL(torrents[0].queue_position(), queue_position_t{0});
	TEST_EQUAL(torrents[1].queue_position(), queue_position_t{1});
	TEST_EQUAL(torrents[2].queue_position(), queue_position_t{2});
	TEST_EQUAL(torrents[3].queue_position(), queue_position_t{3});
	TEST_EQUAL(torrents[4].queue_position(), queue_position_t{4});

	//test strange up and down commands
	torrents[0].queue_position_up();
	torrents[4].queue_position_down();

	TEST_EQUAL(finished.queue_position(), no_pos);
	TEST_EQUAL(torrents[0].queue_position(), queue_position_t{0});
	TEST_EQUAL(torrents[1].queue_position(), queue_position_t{1});
	TEST_EQUAL(torrents[2].queue_position(), queue_position_t{2});
	TEST_EQUAL(torrents[3].queue_position(), queue_position_t{3});
	TEST_EQUAL(torrents[4].queue_position(), queue_position_t{4});

	torrents[1].queue_position_up();
	torrents[3].queue_position_down();
	finished.queue_position_up();

	TEST_EQUAL(finished.queue_position(), no_pos);
	TEST_EQUAL(torrents[1].queue_position(), queue_position_t{0});
	TEST_EQUAL(torrents[0].queue_position(), queue_position_t{1});
	TEST_EQUAL(torrents[2].queue_position(), queue_position_t{2});
	TEST_EQUAL(torrents[4].queue_position(), queue_position_t{3});
	TEST_EQUAL(torrents[3].queue_position(), queue_position_t{4});

	torrents[1].queue_position_down();
	torrents[3].queue_position_up();
	finished.queue_position_down();


	TEST_EQUAL(finished.queue_position(), no_pos);
	TEST_EQUAL(torrents[0].queue_position(), queue_position_t{0});
	TEST_EQUAL(torrents[1].queue_position(), queue_position_t{1});
	TEST_EQUAL(torrents[2].queue_position(), queue_position_t{2});
	TEST_EQUAL(torrents[3].queue_position(), queue_position_t{3});
	TEST_EQUAL(torrents[4].queue_position(), queue_position_t{4});

	// test set pos on not existing pos
	torrents[3].queue_position_set(queue_position_t{10});
	finished.queue_position_set(queue_position_t{10});

	TEST_EQUAL(finished.queue_position(), no_pos);
	TEST_EQUAL(torrents[0].queue_position(), queue_position_t{0});
	TEST_EQUAL(torrents[1].queue_position(), queue_position_t{1});
	TEST_EQUAL(torrents[2].queue_position(), queue_position_t{2});
	TEST_EQUAL(torrents[4].queue_position(), queue_position_t{3});
	TEST_EQUAL(torrents[3].queue_position(), queue_position_t{4});
}

} // anonymous namespace

TORRENT_TEST(queue)
{
	test_queue(add_torrent_params());
}

TORRENT_TEST(queue_paused)
{
	add_torrent_params p;
	p.flags |= torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	test_queue(p);
}

TORRENT_TEST(test_move_storage_no_metadata)
{
	lt::session ses(settings());
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abababababababababababababababababababab");
	p.save_path = "save_path";
	torrent_handle h = ses.add_torrent(p);

	TEST_EQUAL(h.status().save_path, complete("save_path"));

	h.move_storage("save_path_1");

	TEST_EQUAL(h.status().save_path, complete("save_path_1"));
}

TORRENT_TEST(test_have_piece_no_metadata)
{
	lt::session ses(settings());
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abababababababababababababababababababab");
	p.save_path = "save_path";
	torrent_handle h = ses.add_torrent(p);

	TEST_EQUAL(h.have_piece(piece_index_t{-1}), false);
	TEST_EQUAL(h.have_piece(piece_index_t{0}), false);
	TEST_EQUAL(h.have_piece(piece_index_t{100}), false);
}

TORRENT_TEST(test_have_piece_out_of_range)
{
	lt::session ses(settings());

	add_torrent_params p;
	static std::array<const int, 2> const file_sizes{{100000, 100000}};
	int const piece_size = 0x8000;
	p.ti = make_torrent(file_sizes, piece_size);
	p.save_path = "save_path";
	p.flags |= torrent_flags::seed_mode;
	torrent_handle h = ses.add_torrent(p);

	TEST_EQUAL(h.have_piece(piece_index_t{-1}), false);
	TEST_EQUAL(h.have_piece(piece_index_t{0}), true);
	TEST_EQUAL(h.have_piece(piece_index_t{100}), false);
}

TORRENT_TEST(test_read_piece_no_metadata)
{
	lt::session ses(settings());
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abababababababababababababababababababab");
	p.save_path = "save_path";
	torrent_handle h = ses.add_torrent(p);

	h.read_piece(piece_index_t{-1});

	alert const* a = wait_for_alert(ses, read_piece_alert::alert_type, "read_piece_alert");
	TEST_CHECK(a);
	if (auto* rp = alert_cast<read_piece_alert>(a))
	{
		TEST_CHECK(rp->error == error_code(lt::errors::no_metadata, lt::libtorrent_category()));
	}
}

TORRENT_TEST(test_read_piece_out_of_range)
{
	lt::session ses(settings());

	add_torrent_params p;
	static std::array<const int, 2> const file_sizes{{100000, 100000}};
	int const piece_size = 0x8000;
	p.ti = make_torrent(file_sizes, piece_size);
	p.save_path = "save_path";
	p.flags |= torrent_flags::seed_mode;
	torrent_handle h = ses.add_torrent(p);

	h.read_piece(piece_index_t{-1});

	alert const* a = wait_for_alert(ses, read_piece_alert::alert_type, "read_piece_alert");
	TEST_CHECK(a);
	if (auto* rp = alert_cast<read_piece_alert>(a))
	{
		TEST_CHECK(rp->error == error_code(lt::errors::invalid_piece_index
			, lt::libtorrent_category()));
	}
}

namespace {
int const piece_size = 0x4000 * 128;

file_storage test_fs()
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file("temp", 99999999999);
	fs.set_num_pieces(int((fs.total_size() + piece_size - 1) / piece_size));
	return fs;
}
}

TORRENT_TEST(test_calc_bytes_pieces)
{
	auto const fs = test_fs();
	TEST_EQUAL(calc_bytes(fs, piece_count{2, 0, false}), 2 * piece_size);
}

TORRENT_TEST(test_calc_bytes_pieces_last)
{
	auto const fs = test_fs();
	TEST_EQUAL(calc_bytes(fs, piece_count{2, 0, true}), piece_size + fs.total_size() % piece_size);
}

TORRENT_TEST(test_calc_bytes_no_pieces)
{
	auto const fs = test_fs();
	TEST_EQUAL(calc_bytes(fs, piece_count{0, 0, false}), 0);
}

TORRENT_TEST(test_calc_bytes_all_pieces)
{
	auto const fs = test_fs();
	TEST_EQUAL(calc_bytes(fs, piece_count{fs.num_pieces(), 0, true}), fs.total_size());
}

TORRENT_TEST(test_calc_bytes_all_pieces_one_pad)
{
	auto const fs = test_fs();
	TEST_EQUAL(calc_bytes(fs, piece_count{fs.num_pieces(), 1, true}), fs.total_size() - 0x4000);
}

TORRENT_TEST(test_calc_bytes_all_pieces_two_pad)
{
	auto const fs = test_fs();
	TEST_EQUAL(calc_bytes(fs, piece_count{fs.num_pieces(), 2, true}), fs.total_size() - 2 * 0x4000);
}

#if TORRENT_HAS_SYMLINK
TORRENT_TEST(symlinks_restore)
{
	// downloading test torrent with symlinks
	std::string const work_dir = current_working_directory();
	lt::add_torrent_params p;
	p.ti = std::make_shared<lt::torrent_info>(combine_path(
		combine_path(parent_path(work_dir), "test_torrents"), "symlink2.torrent"));
	p.flags &= ~lt::torrent_flags::paused;
	p.save_path = work_dir;
	settings_pack pack = settings();
	pack.set_int(libtorrent::settings_pack::alert_mask, libtorrent::alert_category::status | libtorrent::alert_category::error);
	lt::session ses(std::move(pack));
	ses.add_torrent(p);

	wait_for_alert(ses, torrent_checked_alert::alert_type, "torrent_checked_alert");

	std::string const f = combine_path(combine_path(work_dir, "Some.framework"), "SDL2");
	TEST_CHECK(aux::get_file_attributes(f) & file_storage::flag_symlink);
	TEST_CHECK(aux::get_symlink_path(f) == "Versions/A/SDL2");
}
#endif
