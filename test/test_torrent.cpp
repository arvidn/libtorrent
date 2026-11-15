/*

Copyright (c) 2008-2009, 2013-2022, Arvid Norberg
Copyright (c) 2016, 2018-2021, Alden Torres
Copyright (c) 2017, Falcosc
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, d-komarov
Copyright (c) 2020, AllSeeingEyeTolledEweSew
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/path.hpp" // for combine_path, current_path
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/ip_filter.hpp"
#include "settings.hpp"
#include <tuple>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
#include <memory>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "make_torrent.hpp"

using namespace lt;
using namespace std::chrono_literals;

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

#if TORRENT_ABI_VERSION < 4
using torrent_info_ptr = std::shared_ptr<torrent_info>;
#else
using torrent_info_ptr = std::shared_ptr<torrent_info const>;
#endif

void test_running_torrent(torrent_info_ptr info, std::int64_t file_size)
{
	settings_pack pack = settings();
	pack.set_int(settings_pack::alert_mask, alert_category::piece_progress | alert_category::storage);
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
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
	torrent_handle h = ses.add_torrent(std::move(p), ec);
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
	prio[0_file] = 0_pri;
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
		h.piece_priority(0_piece, 1_pri);
		st = h.status();
		TEST_CHECK(st.pieces.size() > 0 && st.pieces[0_piece] == false);
		std::vector<char> piece(std::size_t(info->piece_length()));
		for (int i = 0; i < int(piece.size()); ++i)
			piece[std::size_t(i)] = (i % 26) + 'A';
		h.add_piece(0_piece, &piece[0], torrent_handle::overwrite_existing);

		// wait until the piece is done writing and hashing
		wait_for_alert(ses, piece_finished_alert::alert_type, "piece_finished_alert");
		st = h.status();
		TEST_CHECK(st.pieces.size() > 0);

		std::cout << "reading piece 0" << std::endl;
		h.read_piece(0_piece);
		alert const* a = wait_for_alert(ses, read_piece_alert::alert_type, "read_piece");
		TEST_CHECK(a);
		read_piece_alert const* rpa = alert_cast<read_piece_alert>(a);
		TEST_CHECK(rpa);
		if (rpa)
		{
			std::cout << "SUCCEEDED!" << std::endl;
			TEST_CHECK(std::memcmp(&piece[0], rpa->buffer.get()
				, std::size_t(info->piece_size(0_piece))) == 0);
			TEST_CHECK(rpa->size == info->piece_size(0_piece));
			TEST_CHECK(rpa->piece == 0_piece);
			TEST_CHECK(hasher(piece).final() == info->hash_for_piece(0_piece));
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
	try
	{
		add_torrent_params atp = load_torrent_buffer(buf);
		// we expect this to fail with an exception
		TEST_CHECK(false);
	}
	catch (lt::system_error const& e)
	{
		TEST_CHECK(e.code() == error_code(lt::errors::torrent_missing_piece_length));
	}
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

	std::vector<char> const buf = bencode(torrent);
	auto atp = load_torrent_buffer(buf);
#if TORRENT_ABI_VERSION < 4
	auto ti = std::make_shared<torrent_info>(buf, from_span);
	TEST_CHECK(atp.ti->info_hashes() == ti->info_hashes());
#endif
}

TORRENT_TEST(large_piece_size)
{
	test_large_piece_size(32768 * 16 * 1024);
	test_large_piece_size(65535 * 16 * 1024);
}

TORRENT_TEST(total_wanted)
{
	std::vector<lt::create_file_entry> fs;

	fs.emplace_back("test_torrent_dir4/tmp1", default_block_size);
	fs.emplace_back("test_torrent_dir4/tmp2", default_block_size);
	fs.emplace_back("test_torrent_dir4/tmp3", default_block_size);
	fs.emplace_back("test_torrent_dir4/tmp4", default_block_size);

	lt::create_torrent t(std::move(fs), default_block_size);
	t.set_hash(0_piece, sha1_hash::max());
	t.set_hash(1_piece, sha1_hash::max());
	t.set_hash(2_piece, sha1_hash::max());
	t.set_hash(3_piece, sha1_hash::max());

	std::vector<char> const tmp = bencode(t.generate());
	add_torrent_params p = load_torrent_buffer(tmp);
	p.save_path = ".";

	settings_pack pack = settings();
	pack.set_int(settings_pack::alert_mask, alert_category::storage);
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	// we just want 1 out of 4 files, 1024 out of 4096 bytes
	p.file_priorities.resize(8, 0_pri);
	p.file_priorities[2] = 1_pri;

	torrent_handle h = ses.add_torrent(std::move(p));

	torrent_status st = h.status();
	TEST_EQUAL(st.total_wanted, default_block_size);
	TEST_EQUAL(st.total_wanted_done, 0);

	// make sure that selecting and unseleting a file quickly still end up with
	// the last set priority
	h.file_priority(file_index_t{2}, default_priority);
	h.file_priority(file_index_t{2}, dont_download);
	TEST_CHECK(wait_priority(h, aux::vector<download_priority_t, file_index_t>(t.end_file(), dont_download)));
	TEST_EQUAL(h.status({}).total_wanted, 0);
}

TORRENT_TEST(added_peers)
{
	std::vector<lt::create_file_entry> fs;

	fs.emplace_back("test_torrent_dir4/tmp1", 1024);

	lt::create_torrent t(std::move(fs), 1024);
	t.set_hash(0_piece, sha1_hash::max());
	std::vector<char> const tmp = bencode(t.generate());
	auto info = load_torrent_buffer(tmp).ti;

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:abababababababababababababababababababab&x.pe=127.0.0.1:48081&x.pe=127.0.0.2:48082");
	p.ti = info;
	p.info_hashes = info_hash_t{};
#if TORRENT_ABI_VERSION < 3
	p.info_hash = sha1_hash{};
#endif
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
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_torrent_dir4/tmp1", 1024);
	lt::create_torrent t(std::move(fs), 1024);
	t.set_hash(0_piece, sha1_hash::max());
	std::vector<char> const tmp = bencode(t.generate());
	add_torrent_params p = load_torrent_buffer(tmp);

	// this info-hash is definitely different from the one in `info`, this
	// should trigger a failure
	p.info_hashes = lt::info_hash_t(lt::sha1_hash("01010101010101010101"));
	p.save_path = ".";

	lt::session ses(settings());
	error_code ec;
	torrent_handle h = ses.add_torrent(std::move(p), ec);
	TEST_CHECK(ec == lt::errors::mismatching_info_hash);
	TEST_CHECK(!h.is_valid());
}

TORRENT_TEST(exceed_file_prio)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_torrent_dir4/tmp1", 1024);
	lt::create_torrent t(std::move(fs), 1024, create_torrent::v1_only);
	t.set_hash(0_piece, sha1_hash::max());
	std::vector<char> const tmp = bencode(t.generate());
	add_torrent_params p = load_torrent_buffer(tmp);
	p.file_priorities.resize(9999, lt::low_priority);
	p.save_path = ".";

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(std::move(p));
	auto const prios = h.get_file_priorities();
	TEST_CHECK(prios.size() == 1);
}

TORRENT_TEST(exceed_piece_prio)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_torrent_dir4/tmp1", 1024);
	lt::create_torrent t(std::move(fs), 1024);
	t.set_hash(0_piece, sha1_hash::max());
	std::vector<char> const tmp = bencode(t.generate());

	add_torrent_params p = load_torrent_buffer(tmp);
	std::size_t const num_pieces = std::size_t(p.ti->num_pieces());
	p.piece_priorities.resize(9999, lt::low_priority);
	p.save_path = ".";

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(std::move(p));
	auto const prios = h.get_piece_priorities();
	TEST_CHECK(prios.size() == num_pieces);
}

TORRENT_TEST(exceed_piece_prio_magnet)
{
	add_torrent_params p;
	p.info_hashes = lt::info_hash_t(lt::sha1_hash("01010101010101010101"));
	p.piece_priorities.resize(9999, lt::low_priority);
	p.save_path = ".";

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(std::move(p));
	auto const prios = h.get_piece_priorities();
	TEST_CHECK(prios.empty());
}

// regression test: prioritize_files()/file_priority() calls arriving while
// a previous file_priority disk job is outstanding must coalesce into the
// deferred priorities instead of each dispatching their own job, since a
// job dispatched from a stale m_file_priority snapshot would clobber
// priorities set by a still-in-flight job.
TORRENT_TEST(file_priority_coalesce_deferred)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_torrent_dir5/tmp1", 1024);
	fs.emplace_back("test_torrent_dir5/tmp2", 1024);
	fs.emplace_back("test_torrent_dir5/tmp3", 1024);
	lt::create_torrent t(std::move(fs), 4096, create_torrent::v1_only);
	t.set_hash(0_piece, sha1_hash::max());
	std::vector<char> const tmp = bencode(t.generate());
	add_torrent_params p = load_torrent_buffer(tmp);
	p.save_path = ".";

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(std::move(p));

	// issue all 3 calls from a single handler on the network thread, rather
	// than as 3 separate torrent_handle calls (each of which posts its own
	// handler from this, the test, thread). Otherwise, whether the 2nd and
	// 3rd calls actually land while the 1st is still outstanding is a race
	// against the disk job's completion being posted back from the disk
	// thread. Running all 3 within one handler makes the outstanding-job
	// state deterministic: the disk thread can't complete and post its
	// result in between them.
	auto const tor = h.native_handle();
	post(ses.get_context(), [tor] {
		tor->set_file_priority(0_file, 7_pri);
		tor->prioritize_files(aux::vector<download_priority_t, file_index_t>{1_pri, 1_pri, 1_pri});
		tor->set_file_priority(1_file, 6_pri);
	});

	aux::vector<download_priority_t, file_index_t> const expected{1_pri, 6_pri, 1_pri};
	TEST_CHECK(wait_priority(h, expected));

	// the 1st call's job is already dispatched, so it always alerts on its
	// own; the coalesced 2nd/3rd calls must produce a single follow-up
	// alert, for 2 total, not 3. Poll the whole window rather than
	// stopping at 2, since the failure mode is a 3rd, straggling alert.
	int num_file_prio_alerts = 0;
	for (int i = 0; i < 10; ++i)
	{
		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
			if (a->type() == file_prio_alert::alert_type)
				++num_file_prio_alerts;
		std::this_thread::sleep_for(100ms);
	}
	TEST_EQUAL(num_file_prio_alerts, 2);
}

TORRENT_TEST(torrent)
{
/*	{
		remove("test_torrent_dir2/tmp1");
		remove("test_torrent_dir2/tmp2");
		remove("test_torrent_dir2/tmp3");
		std::vector<lt::create_file_entry> fs;
		std::int64_t file_size = 256 * 1024;
		fs.emplace_back("test_torrent_dir2/tmp1", file_size);
		fs.emplace_back("test_torrent_dir2/tmp2", file_size);
		fs.emplace_back("test_torrent_dir2/tmp3", file_size);
		lt::create_torrent t(std::move(fs), 128 * 1024);
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

		std::vector<char> const tmp = bencode(t.generate());
		error_code ec;
		auto info = std::make_shared<torrent_info>(tmp, std::ref(ec), from_span);
		TEST_CHECK(info->num_pieces() > 0);

		test_running_torrent(info, file_size);
	}
*/
	{
		std::vector<lt::create_file_entry> fs;

		fs.emplace_back("test_torrent_dir2/tmp1", default_block_size);
		lt::create_torrent t(std::move(fs), default_block_size);

		std::vector<char> piece(default_block_size);
		for (int i = 0; i < int(piece.size()); ++i)
			piece[std::size_t(i)] = (i % 26) + 'A';

		// calculate the hash for all pieces
		sha1_hash const ph = hasher(piece).final();
		TEST_CHECK(t.num_pieces() > 0);
		for (auto const i : t.piece_range())
			t.set_hash(i, ph);

		t.set_hash2(file_index_t{ 0 }, piece_index_t::diff_type{ 0 }, lt::hasher256(piece).final());

		std::vector<char> const tmp = bencode(t.generate());
		auto info = load_torrent_buffer(tmp).ti;
		test_running_torrent(info, default_block_size);
	}
}

#ifndef TORRENT_DISABLE_EXTENSIONS
struct test_plugin : lt::torrent_plugin {};

struct plugin_creator
{
	explicit plugin_creator(int& c) : m_called(c) {}

	std::shared_ptr<lt::torrent_plugin>
	operator()(torrent_handle const&, client_data_t)
	{
		++m_called;
		return std::make_shared<test_plugin>();
	}

	int& m_called;
};

TORRENT_TEST(duplicate_is_not_error)
{
	std::vector<lt::create_file_entry> fs;

	fs.emplace_back("test_torrent_dir2/tmp1", 1024);
	lt::create_torrent t(std::move(fs), 128 * 1024);

	std::vector<char> piece(128 * 1024);
	for (int i = 0; i < int(piece.size()); ++i)
		piece[std::size_t(i)] = (i % 26) + 'A';

	// calculate the hash for all pieces
	sha1_hash ph = hasher(piece).final();
	TEST_CHECK(t.num_pieces() > 0);
	for (auto const i : t.piece_range())
		t.set_hash(i, ph);

	std::vector<char> const tmp = bencode(t.generate());

	int called = 0;
	plugin_creator creator(called);

	add_torrent_params p = load_torrent_buffer(tmp);
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
	std::vector<lt::create_file_entry> fs;

	fs.emplace_back("test_torrent_dir2/tmp1", 0);
	TEST_CHECK(fs.size() == 1);

	TEST_THROW(lt::create_torrent t1(fs));

	fs.emplace_back("test_torrent_dir2/tmp2", 0);
	TEST_CHECK(fs.size() == 2);

	TEST_THROW(lt::create_torrent t2(fs));
}

#if TORRENT_ABI_VERSION < 4
// files aren't allowed to be renamed directly in the torrent_info
// in the new API.
TORRENT_TEST(rename_file)
{
	std::vector<lt::create_file_entry> fs;

	fs.emplace_back("test3/tmp1", 20);
	fs.emplace_back("test3/tmp2", 20);
	lt::create_torrent t(std::move(fs), 128 * 1024, lt::create_torrent::v1_only);
	t.set_hash(0_piece, sha1_hash::max());

	std::vector<char> const tmp = bencode(t.generate());
	auto info = load_torrent_buffer(tmp).ti;

	TEST_EQUAL(info->files().file_path(0_file), combine_path("test3","tmp1"));

	// move "test3/tmp1" -> "test3/renamed"
	info->rename_file(0_file, combine_path("test3", "renamed"));

	TEST_EQUAL(info->files().file_path(0_file), combine_path("test3", "renamed"));

	// a multi-file torrent's file may also be renamed outside of the
	// torrent's own directory, to a bare, root-less filename
	info->rename_file(0_file, "renamed2");
	TEST_EQUAL(info->files().file_path(0_file), "renamed2");

	// an absolute new_filename detaches the file from save_path
	std::string const abs = combine_path(absolute("."), combine_path("test3", "detached"));
	info->rename_file(1_file, abs);
	TEST_CHECK(info->files().file_absolute_path(1_file));
	TEST_EQUAL(info->files().file_path(1_file, "save_path"), abs);
}
#endif

TORRENT_TEST(torrent_status)
{
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_none), -1);
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_metadata), -4);
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_ssl_ctx), -3);
	TEST_EQUAL(static_cast<int>(torrent_status::error_file_exception), -5);
}

namespace {

void test_queue(add_torrent_params const& atp)
{
	lt::settings_pack pack = settings();
	// we're not testing the hash check, just accept the data we write
	pack.set_bool(settings_pack::disable_hash_checks, true);
	lt::session ses(pack);

	std::vector<torrent_handle> torrents;
	for(int i = 0; i < 6; i++)
	{
		std::vector<lt::create_file_entry> fs;
		std::stringstream file_path;
		file_path << "test_torrent_dir4/queue" << i;
		fs.emplace_back(file_path.str(), 1024);
		lt::create_torrent t(std::move(fs), 128 * 1024);
		t.set_hash(0_piece, sha1_hash::max());

		std::vector<char> const buf = bencode(t.generate());
		auto ti = load_torrent_buffer(buf).ti;
		add_torrent_params p = atp;
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
	torrent_handle h = ses.add_torrent(std::move(p));

	TEST_EQUAL(h.status().save_path, absolute("save_path"));

	h.move_storage("save_path_1");

	TEST_EQUAL(h.status().save_path, absolute("save_path_1"));
}

TORRENT_TEST(test_have_piece_no_metadata)
{
	lt::session ses(settings());
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abababababababababababababababababababab");
	p.save_path = "save_path";
	torrent_handle h = ses.add_torrent(std::move(p));

	TEST_EQUAL(h.have_piece(piece_index_t{-1}), false);
	TEST_EQUAL(h.have_piece(0_piece), false);
	TEST_EQUAL(h.have_piece(100_piece), false);
}

TORRENT_TEST(test_have_piece_out_of_range)
{
	lt::session ses(settings());

	static std::array<const int, 2> const file_sizes{{100000, 100000}};
	int const piece_size = 0x8000;
	auto files = create_random_files(".", file_sizes);
	add_torrent_params p = make_torrent(std::move(files), piece_size);

	p.save_path = ".";
	p.flags |= torrent_flags::seed_mode;
	torrent_handle h = ses.add_torrent(std::move(p));

	TEST_EQUAL(h.have_piece(piece_index_t{-1}), false);
	TEST_EQUAL(h.have_piece(0_piece), true);
	TEST_EQUAL(h.have_piece(100_piece), false);
}

TORRENT_TEST(test_read_piece_no_metadata)
{
	lt::session ses(settings());
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abababababababababababababababababababab");
	p.save_path = "save_path";
	torrent_handle h = ses.add_torrent(std::move(p));

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

	int const piece_size = 0x8000;
	add_torrent_params p = make_torrent(make_files({{100000, false}, {100000, false}}), piece_size);
	p.save_path = "save_path";
	p.flags |= torrent_flags::seed_mode;
	torrent_handle h = ses.add_torrent(std::move(p));

	h.read_piece(piece_index_t{-1});

	alert const* a = wait_for_alert(ses, read_piece_alert::alert_type, "read_piece_alert");
	TEST_CHECK(a);
	if (auto* rp = alert_cast<read_piece_alert>(a))
	{
		TEST_CHECK(rp->error == error_code(lt::errors::invalid_piece_index
			, lt::libtorrent_category()));
	}
}

// a seed that was just resumed must keep its slot in the auto manager long
// enough to be classified as inactive, even when it has accumulated a lot of
// active time in earlier sessions and the tracker reports other seeds. A
// queued torrent without any scrape data ranks as if it had no other seeds,
// which must not outrank the recently started one
TORRENT_TEST(seed_rank_recently_started)
{
	lt::session ses(settings());
	int const piece_size = 0x8000;

	static std::array<const int, 1> const running_sizes{{100000}};
	add_torrent_params running = make_torrent(create_random_files(".", running_sizes), piece_size);
	running.save_path = ".";
	running.flags |= torrent_flags::seed_mode;
	running.flags &= ~torrent_flags::paused;
	running.flags &= ~torrent_flags::auto_managed;
	// the seed targets have been met in earlier sessions
	running.active_time = 100000;
	running.finished_time = 90000;
	running.seeding_time = 90000;
	running.total_uploaded = 10000000;
	running.num_complete = 5;
	running.num_incomplete = 1;
	torrent_handle const h_running = ses.add_torrent(std::move(running));

	static std::array<const int, 1> const queued_sizes{{200000}};
	add_torrent_params queued = make_torrent(create_random_files(".", queued_sizes), piece_size);
	queued.save_path = ".";
	queued.flags |= torrent_flags::seed_mode;
	queued.flags |= torrent_flags::paused;
	queued.flags &= ~torrent_flags::auto_managed;
	torrent_handle const h_queued = ses.add_torrent(std::move(queued));

	for (int i = 0; i < 100 && h_running.status().state != torrent_status::seeding; ++i)
		std::this_thread::sleep_for(100ms);
	TEST_EQUAL(h_running.status().state, torrent_status::seeding);

	int const running_rank = h_running.status().seed_rank;
	int const queued_rank = h_queued.status().seed_rank;
	std::printf("running: %d queued: %d\n", running_rank, queued_rank);
	TEST_CHECK(running_rank > queued_rank);
}

// want_peers_finished()/want_peers_download() are derived from the
// connect-candidate count, while a torrent's membership in
// session_impl::m_torrent_lists[torrent_want_peers_*] is a separately
// maintained mirror of them. torrent::check_invariant() requires the two
// to always agree.
TORRENT_TEST(ban_ip_updates_want_peers)
{
	settings_pack sett = settings();
	// keeps the connect-candidate added below from being consumed by a
	// real connection attempt before the ip_filter is applied to it
	sett.set_int(settings_pack::connection_speed, 0);
	lt::session ses(sett);

	static std::array<const int, 1> const file_sizes{{100000}};
	int const piece_size = 0x8000;
	auto files = create_random_files(".", file_sizes);
	add_torrent_params p = make_torrent(std::move(files), piece_size);
	p.save_path = ".";
	p.flags |= torrent_flags::seed_mode;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	torrent_handle h = ses.add_torrent(std::move(p));

	for (int i = 0; i < 100 && h.status().state != torrent_status::seeding; ++i)
		std::this_thread::sleep_for(lt::milliseconds(100));
	TEST_EQUAL(h.status().state, torrent_status::seeding);

	// gives the torrent exactly one connect-candidate, so banning it drives
	// the count to 0 and want_peers_finished() to false
	tcp::endpoint const peer_addr = ep("1.2.3.4", 6881);
	h.connect_peer(peer_addr);
	for (int i = 0; i < 100 && h.status().connect_candidates != 1; ++i)
		std::this_thread::sleep_for(100ms);
	TEST_EQUAL(h.status().connect_candidates, 1);

	ip_filter filter;
	filter.add_rule(peer_addr.address(), peer_addr.address(), ip_filter::blocked);
	ses.set_ip_filter(filter);

	for (int i = 0; i < 100 && h.status().connect_candidates != 0; ++i)
		std::this_thread::sleep_for(100ms);
	TEST_EQUAL(h.status().connect_candidates, 0);

#if TORRENT_USE_INVARIANT_CHECKS
	auto const tor = h.native_handle();
	auto checked = std::make_shared<std::atomic<bool>>(false);
	post(ses.native_handle()->get_context(), [tor, checked] {
		tor->check_invariant();
		*checked = true;
	});
	for (int i = 0; i < 100 && !*checked; ++i)
		std::this_thread::sleep_for(lt::milliseconds(100));
	TEST_CHECK(checked->load());
#endif
}

namespace {
int const piece_size = 0x4000 * 128;

file_storage test_fs()
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file_borrow({}, "temp", 99999999999);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	return fs;
}
}

TORRENT_TEST(test_calc_bytes_pieces)
{
	auto const fs = test_fs();
	TEST_EQUAL(aux::calc_bytes(fs, aux::piece_count{2, 0, false}), 2 * piece_size);
}

TORRENT_TEST(test_calc_bytes_pieces_last)
{
	auto const fs = test_fs();
	TEST_EQUAL(aux::calc_bytes(fs, aux::piece_count{2, 0, true}), piece_size + fs.total_size() % piece_size);
}

TORRENT_TEST(test_calc_bytes_no_pieces)
{
	auto const fs = test_fs();
	TEST_EQUAL(aux::calc_bytes(fs, aux::piece_count{0, 0, false}), 0);
}

TORRENT_TEST(test_calc_bytes_all_pieces)
{
	auto const fs = test_fs();
	TEST_EQUAL(aux::calc_bytes(fs, aux::piece_count{fs.num_pieces(), 0, true}), fs.total_size());
}

TORRENT_TEST(test_calc_bytes_all_pieces_one_pad)
{
	auto const fs = test_fs();
	TEST_EQUAL(aux::calc_bytes(fs, aux::piece_count{fs.num_pieces(), 0x4000, true}), fs.total_size() - 0x4000);
}

TORRENT_TEST(test_calc_bytes_all_pieces_two_pad)
{
	auto const fs = test_fs();
	TEST_EQUAL(aux::calc_bytes(fs, aux::piece_count{fs.num_pieces(), 0x8000, true}), fs.total_size() - 2 * 0x4000);
}

#if TORRENT_HAS_SYMLINK
TORRENT_TEST(symlinks_restore)
{
	// downloading test torrent with symlinks
	std::string const work_dir = current_path();
	lt::add_torrent_params p = load_torrent_file(combine_path(
		combine_path(parent_path(work_dir), "test_torrents"), "symlink2.torrent"));
	p.flags &= ~lt::torrent_flags::paused;
	p.save_path = work_dir;
	settings_pack pack = settings();
	pack.set_int(lt::settings_pack::alert_mask, lt::alert_category::status | lt::alert_category::error);
	lt::session ses(std::move(pack));
	ses.add_torrent(std::move(p));

	wait_for_alert(ses, torrent_checked_alert::alert_type, "torrent_checked_alert");

	std::string const f = combine_path(combine_path(work_dir, "Some.framework"), "SDL2");
	lt::file_status st;
	lt::error_code ec;
	lt::stat_file(f, &st, ec, dont_follow_links);
	TEST_CHECK(st.mode & file_status::symlink);
	TEST_CHECK(!ec);
	error_code sym_ec;
	TEST_EQUAL(aux::get_symlink_path(f, sym_ec), "Versions/A/SDL2");
	TEST_CHECK(!sym_ec);
}
#endif

TORRENT_TEST(redundant_add_piece)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("tmp1", 128 * 1024 * 8);
	lt::create_torrent t(std::move(fs), 128 * 1024);

	TEST_CHECK(t.num_pieces() > 0);

	std::vector<char> piece_data(std::size_t(t.piece_length()), 0);
	aux::random_bytes(piece_data);

	sha1_hash const ph = lt::hasher(piece_data).final();
	for (auto const i : t.piece_range())
		t.set_hash(i, ph);

	std::vector<char> const buf = bencode(t.generate());

	lt::session ses(settings());
	lt::add_torrent_params atp = load_torrent_buffer(buf);
	int const num_pieces = atp.ti->num_pieces();
	atp.flags &= ~torrent_flags::paused;
	atp.save_path = ".";
	auto h = ses.add_torrent(std::move(atp));
	wait_for_downloading(ses, "");

	h.add_piece(0_piece, piece_data);
	h.set_piece_deadline(0_piece, 0, torrent_handle::alert_when_available);
	h.prioritize_pieces(std::vector<lt::download_priority_t>(std::size_t(num_pieces), lt::dont_download));
	h.add_piece(0_piece, std::move(piece_data));
	// the session destructor below drains and joins the disk threads, so any
	// crash from the redundant add_piece would still surface without an
	// extra settle delay here.
}

TORRENT_TEST(test_in_session)
{
	lt::session ses(settings());
	add_torrent_params p = generate_torrent();
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(std::move(p));
	TEST_CHECK(h.in_session());
	ses.remove_torrent(h);
	TEST_CHECK(!h.in_session());
}

// trackers passed in via add_torrent_params::trackers must be attributed to
// the torrent file when add_torrent_params::ti is set, and to the magnet
// link otherwise, even though both cases populate the same field.
TORRENT_TEST(tracker_source_attribution)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_torrent_dir5/tmp1", 1024);
	lt::create_torrent t(std::move(fs), 1024);
	t.set_hash(0_piece, sha1_hash::max());
	auto const ti = load_torrent_buffer(bencode(t.generate())).ti;

	lt::session ses(settings());

	{
		add_torrent_params p;
		p.ti = ti;
		p.trackers.emplace_back("http://torrent-tracker.com/announce");
		p.save_path = ".";
		torrent_handle h = ses.add_torrent(std::move(p));
		std::vector<announce_entry> const trackers = h.trackers();
		TEST_EQUAL(trackers.size(), 1);
		if (!trackers.empty())
			TEST_EQUAL(trackers[0].source, announce_entry::source_torrent);
		ses.remove_torrent(h);
	}

	{
		add_torrent_params p;
		p.info_hashes = ti->info_hashes();
		p.trackers.emplace_back("http://magnet-tracker.com/announce");
		p.save_path = ".";
		torrent_handle h = ses.add_torrent(std::move(p));
		std::vector<announce_entry> const trackers = h.trackers();
		TEST_EQUAL(trackers.size(), 1);
		if (!trackers.empty())
			TEST_EQUAL(trackers[0].source, announce_entry::source_magnet_link);
		ses.remove_torrent(h);
	}
}

// a file whose name was changed by duplicate resolution, and then renamed by
// the user back to its original (colliding) name, must keep the user's name
// across a resume data round trip
TORRENT_TEST(resume_keeps_user_rename_to_original_duplicate_name)
{
	std::string const resolved_name = combine_path("dup", "Foo-1.txt");
	std::string const user_name = combine_path("dup", "Foo.txt");

	std::vector<char> torrent;
	{
		std::vector<lt::create_file_entry> fs;
		fs.emplace_back("dup/foo.txt", 1024);
		fs.emplace_back("dup/Foo.txt", 1024);
		lt::create_torrent t(std::move(fs), 16 * 1024, lt::create_torrent::v1_only);
		t.set_hash(0_piece, sha1_hash::max());
		bencode(std::back_inserter(torrent), t.generate());
	}

	// add_torrent_params::renamed_path_elements is what carries the
	// default per-directory duplicate-name resolution into the torrent's
	// rename map; file_storage itself is immutable and never reflects it.
	add_torrent_params resume;
	{
		lt::session ses(settings());
		add_torrent_params p = load_torrent_buffer(torrent);
		{
			lt::renamed_files rf;
			rf.import_filenames(p.ti->layout(), p.renamed_files);
			rf.import_path_elements(p.ti->layout(), p.renamed_path_elements);
			lt::filenames const fn(p.ti->layout(), rf);
			TEST_EQUAL(fn.file_path(1_file), resolved_name);
		}
		p.save_path = ".";
		p.flags |= torrent_flags::paused;
		torrent_handle h = ses.add_torrent(std::move(p));

		h.rename_file(1_file, user_name);
		alert const* a =
			wait_for_alert(ses, file_renamed_alert::alert_type, "ses", pop_alerts::cache_alerts);
		TEST_CHECK(a);
		{
			lt::renamed_files const rf = h.get_renamed_files();
			lt::filenames const fn(h.torrent_file()->layout(), rf);
			TEST_EQUAL(fn.file_path(1_file), user_name);
		}

		h.save_resume_data();
		a = wait_for_alert(
			ses, save_resume_data_alert::alert_type, "ses", pop_alerts::cache_alerts);
		TEST_CHECK(a);
		if (!a)
			return;
		resume = alert_cast<save_resume_data_alert>(a)->params;
	}

	// the resume data round trip, as it would happen across sessions
	error_code ec;
	add_torrent_params p = read_resume_data(write_resume_data_buf(resume), ec);
	TEST_CHECK(!ec);
	p.ti = load_torrent_buffer(torrent).ti;
	p.save_path = ".";
	p.flags |= torrent_flags::paused;

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(std::move(p));
	lt::renamed_files const rf = h.get_renamed_files();
	lt::filenames const fn(h.torrent_file()->layout(), rf);
	TEST_EQUAL(fn.file_path(1_file), user_name);
}

namespace {

// returns true if the web seed was rejected by the SSRF mitigation (rather
// than being connected to)
bool web_seed_ssrf_blocked(std::string const& url)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::ssrf_mitigation, true);
	lt::session ses(pack);

	add_torrent_params p = make_torrent(make_files({{100000, false}}), 0x8000);
	p.save_path = "ssrf_web_seed";
	p.url_seeds.push_back(url);
	ses.add_torrent(std::move(p));

	return wait_for_alert(
		ses,
		"ses",
		[&](lt::alert const* a) {
			auto const* usa = alert_cast<url_seed_alert>(a);
			return usa && usa->error == lt::errors::ssrf_mitigation;
		},
		lt::seconds(5));
}

} // anonymous namespace

// a web seed on the local network may not use query string parameters
TORRENT_TEST(web_seed_ssrf_query_string)
{
	TEST_CHECK(web_seed_ssrf_blocked("http://127.0.0.1:1/file?x=1"));
	TEST_CHECK(web_seed_ssrf_blocked("http://[::1]:1/file?x=1"));
}

namespace {

void wait_for_seed(lt::session& ses, torrent_handle const& h)
{
	for (int i = 0; i < 100; ++i)
	{
		print_alerts(ses, "ses");
		if (h.status().is_seeding) return;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}
}

lt::add_torrent_params unload_test_atp()
{
	torrent_args args;
	args.name("unload_test");
	args.file("40960,name=file0");
	args.file("24576,name=file1");

	lt::add_torrent_params atp = make_test_torrent(args);
	atp.flags &= ~torrent_flags::paused;
	atp.flags &= ~torrent_flags::auto_managed;
	return atp;
}

lt::add_torrent_params unload_test_torrent()
{
	std::filesystem::remove_all("unload_test");
	lt::add_torrent_params atp = unload_test_atp();
	atp.save_path = ".";
	generate_files(*atp.ti, ".");
	return atp;
}

bool has_part_file(std::string const& dir)
{
	if (!std::filesystem::exists(dir)) return false;
	for (auto const& e : std::filesystem::directory_iterator(dir))
		if (e.path().extension() == ".parts") return true;
	return false;
}

std::vector<char> read_file(std::string const& path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<char>(std::istreambuf_iterator<char>(f)
		, std::istreambuf_iterator<char>());
}

void wait_until_checked(lt::session& ses, torrent_handle const& h)
{
	for (int i = 0; i < 100; ++i)
	{
		print_alerts(ses, "ses");
		auto const st = h.status().state;
		if (st != torrent_status::checking_files
			&& st != torrent_status::checking_resume_data)
			return;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}
}

void check_unloaded_state(torrent_handle const& h)
{
	auto const unloaded = h.get_unloaded_files();
	TEST_CHECK(unloaded.size() >= 2);
	if (unloaded.size() < 2) return;
	TEST_CHECK(unloaded[0]);
	TEST_CHECK(!unloaded[1]);

	TEST_CHECK(!h.have_piece(0_piece));
	TEST_CHECK(h.have_piece(1_piece));
	TEST_EQUAL(h.status().state, torrent_status::seeding);
	TEST_CHECK(h.status().is_seeding);
	TEST_EQUAL(h.status().total_wanted, 32768);
	TEST_EQUAL(h.status().total_done, 32768);
	TEST_EQUAL(h.status().total_wanted_done, h.status().total_wanted);

	// only the unloaded file's half of the shared piece is left on disk
	std::vector<std::int64_t> const fp = h.file_progress();
	TEST_EQUAL(fp.size(), std::size_t(2));
	if (fp.size() == 2)
	{
		TEST_EQUAL(fp[0], 8192);
		TEST_EQUAL(fp[1], 24576);
	}
}

} // anonymous namespace

TORRENT_TEST(unload_file_from_finished_torrent)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);
	TEST_CHECK(h.status().is_seeding);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");

	TEST_CHECK(!exists("unload_test/file0"));
	TEST_CHECK(exists("unload_test/file1"));
	check_unloaded_state(h);

	// unloading must not be treated as the torrent finishing
	std::this_thread::sleep_for(lt::milliseconds(500));
	std::vector<alert*> alerts;
	ses.pop_alerts(&alerts);
	for (alert const* a : alerts)
		TEST_CHECK(!alert_cast<torrent_finished_alert>(a));

	// state and wanted-byte accounting stay stable over several ticks
	check_unloaded_state(h);
	TEST_EQUAL(h.status().state, torrent_status::seeding);
}

TORRENT_TEST(unload_file_from_paused_torrent)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);
	TEST_CHECK(h.status().is_seeding);
	h.pause();

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");

	TEST_CHECK(!exists("unload_test/file0"));
	TEST_CHECK(exists("unload_test/file1"));
	check_unloaded_state(h);
}

TORRENT_TEST(unload_file_from_seed_mode_torrent)
{
	lt::session ses(settings());
	lt::add_torrent_params atp = unload_test_torrent();
	atp.flags |= torrent_flags::seed_mode;
	torrent_handle h = ses.add_torrent(atp);
	wait_for_seed(ses, h);
	TEST_CHECK(h.status().is_seeding);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");

	TEST_CHECK(!exists("unload_test/file0"));
	TEST_CHECK(exists("unload_test/file1"));
	TEST_CHECK(!(h.flags() & torrent_flags::seed_mode));
	check_unloaded_state(h);
}

TORRENT_TEST(unload_file_rejects_invalid_index)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);

	h.unload_file(file_index_t(2));
	alert const* const a = wait_for_alert(ses, file_error_alert::alert_type
		, "invalid unload");
	auto const* fe = alert_cast<file_error_alert>(a);
	TEST_CHECK(fe != nullptr);
	if (fe)
	{
		TEST_EQUAL(fe->error, error_code(boost::system::errc::invalid_argument
			, generic_category()));
		TEST_CHECK(fe->op == operation_t::unknown);
	}

	TEST_CHECK(exists("unload_test/file0"));
	TEST_CHECK(exists("unload_test/file1"));
	TEST_CHECK(h.have_piece(0_piece));
	TEST_CHECK(h.have_piece(1_piece));
}

TORRENT_TEST(unload_file_rejects_without_metadata)
{
	lt::session ses(settings());
	add_torrent_params p;
	p.info_hashes = info_hash_t(sha1_hash("01010101010101010101"));
	p.save_path = ".";
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	torrent_handle h = ses.add_torrent(p);

	h.unload_file(file_index_t(100000));
	alert const* const a = wait_for_alert(ses, file_error_alert::alert_type
		, "no-metadata unload");
	auto const* fe = alert_cast<file_error_alert>(a);
	TEST_CHECK(fe != nullptr);
	if (fe)
	{
		TEST_EQUAL(fe->error, error_code(boost::system::errc::operation_not_supported
			, generic_category()));
		TEST_CHECK(fe->op == operation_t::unknown);
	}
}

TORRENT_TEST(unload_file_rejects_while_checking)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);

	// force_recheck() enters the checking state synchronously
	h.force_recheck();
	h.unload_file(0_file);
	alert const* const a = wait_for_alert(ses, file_error_alert::alert_type
		, "unload while checking");
	auto const* fe = alert_cast<file_error_alert>(a);
	TEST_CHECK(fe != nullptr);
	if (fe)
	{
		TEST_EQUAL(fe->error, error_code(boost::system::errc::operation_in_progress
			, generic_category()));
		TEST_CHECK(fe->op == operation_t::unknown);
	}
	TEST_CHECK(exists("unload_test/file0"));
}

// a seed that unloads a file must not request the dropped pieces from a peer
TORRENT_TEST(unload_file_from_seed_does_not_redownload)
{
	unload_test_torrent();

	settings_pack seed_pack = settings();
	seed_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session seed_ses(seed_pack);
	lt::add_torrent_params seed_atp = unload_test_atp();
	seed_atp.save_path = ".";
	torrent_handle seed = seed_ses.add_torrent(seed_atp);
	wait_for_seed(seed_ses, seed);
	TEST_CHECK(seed.status().is_seeding);

	settings_pack dl_pack = settings();
	dl_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session dl_ses(dl_pack);
	std::filesystem::remove_all("unload_downloader");
	std::filesystem::create_directories("unload_downloader");
	lt::add_torrent_params dl_atp = unload_test_atp();
	dl_atp.save_path = "unload_downloader";
	torrent_handle dl = dl_ses.add_torrent(dl_atp);
	dl.connect_peer(ep("127.0.0.1", seed_ses.listen_port()));
	wait_for_seed(dl_ses, dl);
	TEST_CHECK(dl.status().is_seeding);

	std::this_thread::sleep_for(lt::milliseconds(300));
	std::int64_t const dl_upload = dl.status().total_payload_upload;
	std::int64_t const seed_download = seed.status().total_payload_download;

	seed.unload_file(0_file);
	wait_for_alert(seed_ses, file_prio_alert::alert_type, "unload");
	check_unloaded_state(seed);

	for (int i = 0; i < 20; ++i)
	{
		print_alerts(seed_ses, "seed");
		print_alerts(dl_ses, "dl");
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	TEST_CHECK(!seed.have_piece(0_piece));
	check_unloaded_state(seed);
	TEST_EQUAL(seed.status().total_payload_download, seed_download);
	TEST_EQUAL(dl.status().total_payload_upload, dl_upload);
}

// unloading a file before it has been downloaded must remove its pieces from
// the wanted set, so they are never requested from the peer
TORRENT_TEST(unload_file_while_downloading_drops_wanted_bytes)
{
	unload_test_torrent();

	settings_pack seed_pack = settings();
	seed_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session seed_ses(seed_pack);
	lt::add_torrent_params seed_atp = unload_test_atp();
	seed_atp.save_path = ".";
	torrent_handle seed = seed_ses.add_torrent(seed_atp);
	wait_for_seed(seed_ses, seed);

	settings_pack dl_pack = settings();
	dl_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session dl_ses(dl_pack);
	std::filesystem::remove_all("unload_downloader2");
	std::filesystem::create_directories("unload_downloader2");
	lt::add_torrent_params dl_atp = unload_test_atp();
	dl_atp.save_path = "unload_downloader2";
	torrent_handle dl = dl_ses.add_torrent(dl_atp);

	for (int i = 0; i < 100; ++i)
	{
		auto const st = dl.status().state;
		if (st != torrent_status::checking_files
			&& st != torrent_status::checking_resume_data)
			break;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	dl.unload_file(0_file);
	wait_for_alert(dl_ses, file_prio_alert::alert_type, "unload");
	TEST_EQUAL(dl.status().total_wanted, 32768);

	dl.connect_peer(ep("127.0.0.1", seed_ses.listen_port()));
	for (int i = 0; i < 200 && dl.status().total_wanted_done < dl.status().total_wanted; ++i)
	{
		print_alerts(dl_ses, "dl");
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	TEST_CHECK(!dl.have_piece(0_piece));
	TEST_CHECK(dl.have_piece(1_piece));
	TEST_EQUAL(dl.status().total_wanted, 32768);
	TEST_CHECK(dl.status().total_payload_download <= 32768);
}

// a piece priority stays in effect when the file priority is 0. unloading the
// file must still remove its pieces from the wanted set.
TORRENT_TEST(unload_file_drops_piece_priorities)
{
	std::filesystem::remove_all("unload_piece_prio");
	std::filesystem::create_directories("unload_piece_prio");

	lt::session ses(settings());
	lt::add_torrent_params atp = unload_test_atp();
	atp.save_path = "unload_piece_prio";
	torrent_handle h = ses.add_torrent(atp);
	wait_until_checked(ses, h);

	h.file_priority(0_file, dont_download);
	wait_for_alert(ses, file_prio_alert::alert_type, "file prio");
	h.piece_priority(0_piece, top_priority);
	TEST_EQUAL(h.status().total_wanted, 65536);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");

	TEST_CHECK(!h.have_piece(0_piece));
	TEST_EQUAL(h.status().total_wanted, 32768);
	TEST_EQUAL(h.piece_priority(0_piece), dont_download);
	TEST_CHECK(!has_part_file("unload_piece_prio"));
}

// two unload requests queued behind a priority job are dispatched together.
// The first file commits, the second fails because its boundary bytes cannot
// be read from the removed file, and the first must still be fully dropped
// from the torrent state.
TORRENT_TEST(unload_file_partial_batch_failure)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);
	TEST_CHECK(h.status().is_seeding);

	std::filesystem::remove("unload_test/file1");

	h.file_priority(0_file, low_priority);
	h.unload_file(0_file);
	h.unload_file(1_file);

	wait_for_alert(ses, file_error_alert::alert_type, "unload");

	TEST_CHECK(!exists("unload_test/file0"));
	auto const unloaded = h.get_unloaded_files();
	TEST_CHECK(unloaded.size() >= 2);
	if (unloaded.size() >= 2)
	{
		TEST_CHECK(unloaded[0]);
		TEST_CHECK(!unloaded[1]);
	}
	TEST_CHECK(!h.have_piece(0_piece));
	TEST_CHECK(h.have_piece(1_piece));

	// the committed file stays unloaded across a resume round-trip
	h.save_resume_data();
	alert const* const a = wait_for_alert(ses, save_resume_data_alert::alert_type
		, "save resume");
	if (auto const* ra = alert_cast<save_resume_data_alert>(a))
	{
		std::vector<char> const buf = write_resume_data_buf(ra->params);
		add_torrent_params const loaded = read_resume_data(buf);
		TEST_CHECK(!loaded.unloaded_files.empty());
		if (!loaded.unloaded_files.empty()) TEST_CHECK(loaded.unloaded_files[0]);
	}
}

TORRENT_TEST(unload_file_move_storage)
{
	std::filesystem::remove_all("unload_move");
	std::filesystem::remove_all("unload_move_new");

	lt::add_torrent_params atp = unload_test_atp();
	generate_files(*atp.ti, "unload_move");
	atp.save_path = "unload_move";

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(atp);
	wait_for_seed(ses, h);

	// move while the unload is still in flight; the move job is queued behind
	// the unload job on the disk thread
	h.unload_file(0_file);
	h.move_storage("unload_move_new");

	bool moved = false;
	bool unloaded = false;
	TEST_CHECK(wait_for_alert(ses, "unload + move", [&](alert const* a)
	{
		if (alert_cast<storage_moved_alert>(a)) moved = true;
		if (alert_cast<file_prio_alert>(a)) unloaded = true;
		return moved && unloaded;
	}, seconds(10)));
	TEST_CHECK(moved);
	TEST_CHECK(unloaded);

	TEST_CHECK(!exists("unload_move_new/unload_test/file0"));
	TEST_CHECK(exists("unload_move_new/unload_test/file1"));
	TEST_CHECK(has_part_file("unload_move_new"));
	TEST_CHECK(!has_part_file("unload_move"));
	TEST_CHECK(!h.have_piece(0_piece));
	TEST_CHECK(h.have_piece(1_piece));
}

TORRENT_TEST(unload_file_delete_files)
{
	std::filesystem::remove_all("unload_delete");
	lt::add_torrent_params atp = unload_test_atp();
	generate_files(*atp.ti, "unload_delete");
	atp.save_path = "unload_delete";

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(atp);
	wait_for_seed(ses, h);
	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");
	TEST_CHECK(has_part_file("unload_delete"));

	ses.remove_torrent(h, session::delete_files);
	wait_for_alert(ses, torrent_removed_alert::alert_type, "remove");
	TEST_CHECK(!has_part_file("unload_delete"));
	TEST_CHECK(!exists("unload_delete/unload_test/file1"));
}

TORRENT_TEST(unload_file_restart_and_recheck)
{
	std::filesystem::remove_all("unload_restart");
	std::filesystem::remove_all("unload_seed_full");

	lt::add_torrent_params atp = unload_test_atp();
	generate_files(*atp.ti, "unload_restart");
	generate_files(*atp.ti, "unload_seed_full");
	atp.save_path = "unload_restart";

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses(pack);
	torrent_handle h = ses.add_torrent(atp);
	wait_for_seed(ses, h);
	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");

	h.save_resume_data(torrent_handle::save_info_dict);
	alert const* const resume = wait_for_alert(ses
		, save_resume_data_alert::alert_type, "save resume");
	auto const* ra = alert_cast<save_resume_data_alert>(resume);
	TEST_CHECK(ra != nullptr);
	if (!ra) return;
	std::vector<char> const resume_buf = write_resume_data_buf(ra->params);

	ses.remove_torrent(h);
	wait_for_alert(ses, torrent_removed_alert::alert_type, "remove");

	settings_pack pack2 = settings();
	pack2.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses2(pack2);
	add_torrent_params rd = read_resume_data(resume_buf);
	rd.save_path = "unload_restart";
	torrent_handle h2 = ses2.add_torrent(rd);
	wait_until_checked(ses2, h2);

	TEST_CHECK(!exists("unload_restart/unload_test/file0"));
	TEST_CHECK(exists("unload_restart/unload_test/file1"));
	TEST_CHECK(!h2.have_piece(0_piece));
	TEST_CHECK(h2.have_piece(1_piece));

	h2.force_recheck();
	wait_for_alert(ses2, torrent_checked_alert::alert_type, "recheck");
	wait_until_checked(ses2, h2);
	TEST_CHECK(!exists("unload_restart/unload_test/file0"));
	TEST_CHECK(!h2.have_piece(0_piece));
	TEST_CHECK(h2.have_piece(1_piece));

	settings_pack pack3 = settings();
	pack3.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses3(pack3);
	lt::add_torrent_params seed_atp = unload_test_atp();
	seed_atp.save_path = "unload_seed_full";
	torrent_handle seeder = ses3.add_torrent(seed_atp);
	wait_for_seed(ses3, seeder);

	std::int64_t const before = h2.status().total_payload_download;
	h2.connect_peer(ep("127.0.0.1", ses3.listen_port()));
	for (int i = 0; i < 20; ++i)
	{
		print_alerts(ses2, "restart");
		print_alerts(ses3, "seed");
		std::this_thread::sleep_for(lt::milliseconds(100));
	}
	TEST_CHECK(!h2.have_piece(0_piece));
	TEST_EQUAL(h2.status().total_payload_download, before);
}

// re-enabling an unloaded file must export its boundary bytes from the part
// file and download the rest
TORRENT_TEST(unload_file_then_reenable)
{
	std::filesystem::remove_all("unload_reenable");
	std::filesystem::remove_all("unload_reenable_seed");

	lt::add_torrent_params atp = unload_test_atp();
	generate_files(*atp.ti, "unload_reenable");
	generate_files(*atp.ti, "unload_reenable_seed");
	atp.save_path = "unload_reenable";

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses(pack);
	torrent_handle h = ses.add_torrent(atp);
	wait_for_seed(ses, h);
	TEST_CHECK(h.status().is_seeding);

	settings_pack seed_pack = settings();
	seed_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session seed_ses(seed_pack);
	lt::add_torrent_params seed_atp = unload_test_atp();
	seed_atp.save_path = "unload_reenable_seed";
	torrent_handle seeder = seed_ses.add_torrent(seed_atp);
	wait_for_seed(seed_ses, seeder);
	TEST_CHECK(seeder.status().is_seeding);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");
	TEST_CHECK(!exists("unload_reenable/unload_test/file0"));

	h.file_priority(0_file, default_priority);
	wait_for_alert(ses, file_prio_alert::alert_type, "reenable");
	TEST_CHECK(exists("unload_reenable/unload_test/file0"));
	TEST_CHECK(!h.get_unloaded_files()[0]);

	h.connect_peer(ep("127.0.0.1", seed_ses.listen_port()));
	for (int i = 0; i < 300 && !(h.status().is_seeding && h.have_piece(0_piece)); ++i)
	{
		print_alerts(ses, "ses");
		print_alerts(seed_ses, "seed");
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	TEST_CHECK(h.status().is_seeding);
	TEST_CHECK(h.have_piece(0_piece));
	std::vector<char> const downloaded = read_file("unload_reenable/unload_test/file0");
	std::vector<char> const original = read_file("unload_reenable_seed/unload_test/file0");
	TEST_EQUAL(downloaded.size(), original.size());
	TEST_CHECK(downloaded == original);

	// the resume data no longer marks the file as unloaded
	h.save_resume_data();
	alert const* const a = wait_for_alert(ses, save_resume_data_alert::alert_type
		, "save resume");
	auto const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra != nullptr);
	if (!ra) return;
	add_torrent_params const rd = read_resume_data(write_resume_data_buf(ra->params));
	TEST_CHECK(rd.unloaded_files.empty() || !rd.unloaded_files[0]);
}

// the bytes of the shared piece that belong to the unloaded file are still
// served from the part file; a peer that only wants the other file can
// download the whole shared piece
TORRENT_TEST(unload_file_shared_piece_still_seeded)
{
	unload_test_torrent();

	settings_pack seed_pack = settings();
	seed_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session seed_ses(seed_pack);
	lt::add_torrent_params seed_atp = unload_test_atp();
	seed_atp.save_path = ".";
	torrent_handle seed = seed_ses.add_torrent(seed_atp);
	wait_for_seed(seed_ses, seed);
	TEST_CHECK(seed.status().is_seeding);

	seed.unload_file(0_file);
	wait_for_alert(seed_ses, file_prio_alert::alert_type, "unload");
	TEST_CHECK(!seed.have_piece(0_piece));
	TEST_CHECK(seed.have_piece(1_piece));

	std::filesystem::remove_all("unload_shared_dl");
	std::filesystem::create_directories("unload_shared_dl");
	settings_pack dl_pack = settings();
	dl_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session dl_ses(dl_pack);
	lt::add_torrent_params dl_atp = unload_test_atp();
	dl_atp.save_path = "unload_shared_dl";
	// only want file1, which shares a piece with the unloaded file0
	dl_atp.file_priorities = {dont_download, default_priority};
	torrent_handle dl = dl_ses.add_torrent(dl_atp);
	dl.connect_peer(ep("127.0.0.1", seed_ses.listen_port()));

	for (int i = 0; i < 200 && !dl.have_piece(1_piece); ++i)
	{
		print_alerts(dl_ses, "dl");
		print_alerts(seed_ses, "seed");
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	TEST_CHECK(dl.have_piece(1_piece));
	TEST_CHECK(!dl.have_piece(0_piece));
	TEST_CHECK(!exists("unload_shared_dl/unload_test/file0"));
	TEST_CHECK(read_file("unload_shared_dl/unload_test/file1")
		== read_file("unload_test/file1"));
}

TORRENT_TEST(unload_file_all_files)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);
	TEST_CHECK(h.status().is_seeding);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload 0");
	h.unload_file(1_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload 1");

	TEST_CHECK(!exists("unload_test/file0"));
	TEST_CHECK(!exists("unload_test/file1"));
	auto const unloaded = h.get_unloaded_files();
	TEST_CHECK(unloaded.size() >= 2);
	if (unloaded.size() >= 2)
	{
		TEST_CHECK(unloaded[0]);
		TEST_CHECK(unloaded[1]);
	}
	TEST_CHECK(!h.status().errc);
	TEST_CHECK(!h.have_piece(0_piece));
	TEST_EQUAL(h.status().total_wanted, 0);
	TEST_EQUAL(h.status().total_wanted_done, h.status().total_wanted);

	// both files stay unloaded across a resume round-trip
	h.save_resume_data();
	alert const* const a = wait_for_alert(ses, save_resume_data_alert::alert_type
		, "save resume");
	auto const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra != nullptr);
	if (!ra) return;
	add_torrent_params const rd = read_resume_data(write_resume_data_buf(ra->params));
	TEST_CHECK(rd.unloaded_files.size() >= 2);
	if (rd.unloaded_files.size() < 2) return;
	TEST_CHECK(rd.unloaded_files[0]);
	TEST_CHECK(rd.unloaded_files[1]);
}

// a shared boundary piece with no other wanted file must be dropped from the
// wanted set even when the unloaded file's priority was already 0 (so the
// per-file priority update does not run)
TORRENT_TEST(unload_file_boundary_piece_without_wanted_file)
{
	std::filesystem::remove_all("unload_boundary_one");

	lt::session ses(settings());
	lt::add_torrent_params atp = unload_test_atp();
	atp.save_path = "unload_boundary_one";
	atp.file_priorities = {dont_download, dont_download};
	torrent_handle h = ses.add_torrent(atp);
	wait_until_checked(ses, h);

	// piece 1 is shared by file 0 and file 1, but neither file is wanted
	h.piece_priority(1_piece, default_priority);
	TEST_EQUAL(h.status().total_wanted, 32768);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload 0");

	TEST_CHECK(!exists("unload_boundary_one/unload_test/file0"));
	TEST_EQUAL(h.piece_priority(1_piece), dont_download);
	TEST_EQUAL(h.status().total_wanted, 0);
}

// unloading every file also drops a shared boundary piece that was kept
// wanted by an explicit piece priority
TORRENT_TEST(unload_file_boundary_piece_all_files)
{
	std::filesystem::remove_all("unload_boundary_all");

	lt::session ses(settings());
	lt::add_torrent_params atp = unload_test_atp();
	atp.save_path = "unload_boundary_all";
	atp.file_priorities = {dont_download, dont_download};
	torrent_handle h = ses.add_torrent(atp);
	wait_until_checked(ses, h);

	h.piece_priority(1_piece, default_priority);
	TEST_EQUAL(h.status().total_wanted, 32768);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload 0");
	h.unload_file(1_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload 1");

	auto const unloaded = h.get_unloaded_files();
	TEST_CHECK(unloaded.size() >= 2);
	if (unloaded.size() >= 2)
	{
		TEST_CHECK(unloaded[0]);
		TEST_CHECK(unloaded[1]);
	}
	TEST_EQUAL(h.piece_priority(1_piece), dont_download);
	TEST_EQUAL(h.status().total_wanted, 0);
}

// a resume save requested while the unload job is in flight is deferred until
// the unload commits, so it includes the unloaded file
TORRENT_TEST(unload_file_in_flight_resume_save)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);

	// queue the unload behind an in-flight priority update, then ask for the
	// resume data as well. Both have to be deferred, and the save must see the
	// committed unloaded state.
	h.file_priority(1_file, low_priority);
	h.unload_file(0_file);
	h.save_resume_data(torrent_handle::save_info_dict);

	std::vector<char> resume_buf;
	bool const got_resume = wait_for_alert(ses, "unload + save resume"
		, [&](alert const* a)
		{
			if (auto const* ra = alert_cast<save_resume_data_alert>(a))
				resume_buf = write_resume_data_buf(ra->params);
			return !resume_buf.empty();
		}, seconds(10));
	TEST_CHECK(got_resume);
	if (!got_resume) return;
	TEST_CHECK(h.get_unloaded_files()[0]);

	// the save was deferred until the unload committed, so it must include it
	add_torrent_params rd = read_resume_data(resume_buf);
	TEST_CHECK(!rd.unloaded_files.empty());
	if (rd.unloaded_files.empty()) return;
	TEST_CHECK(rd.unloaded_files[0]);
	TEST_CHECK(!rd.file_priorities.empty());
	if (!rd.file_priorities.empty())
		TEST_EQUAL(rd.file_priorities[0], dont_download);

	// loading the resume data in a fresh session must not recreate or
	// re-download the unloaded file
	ses.remove_torrent(h);
	wait_for_alert(ses, torrent_removed_alert::alert_type, "remove");

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses2(pack);
	rd.save_path = ".";
	torrent_handle h2 = ses2.add_torrent(rd);
	wait_until_checked(ses2, h2);

	TEST_CHECK(!exists("unload_test/file0"));
	TEST_CHECK(exists("unload_test/file1"));
	TEST_CHECK(h2.get_unloaded_files()[0]);
	TEST_CHECK(!h2.have_piece(0_piece));
	TEST_CHECK(h2.have_piece(1_piece));
}

// a peer connected before the unload must learn that the dropped pieces are
// gone, rather than keeping a stale bitfield
TORRENT_TEST(unload_file_sends_lt_donthave)
{
	lt::add_torrent_params const seed_atp = unload_test_torrent();

	settings_pack seed_pack = settings();
	seed_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session seed_ses(seed_pack);
	torrent_handle seed = seed_ses.add_torrent(seed_atp);
	wait_for_seed(seed_ses, seed);
	TEST_CHECK(seed.status().is_seeding);

	std::filesystem::remove_all("unload_donthave_dl");
	std::filesystem::create_directories("unload_donthave_dl");

	settings_pack dl_pack = settings();
	dl_pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	// keep the connection even after the wanted file completes
	dl_pack.set_bool(settings_pack::close_redundant_connections, false);
	lt::session dl_ses(dl_pack);
	lt::add_torrent_params dl_atp = unload_test_atp();
	dl_atp.save_path = "unload_donthave_dl";
	dl_atp.file_priorities = {dont_download, default_priority};
	torrent_handle dl = dl_ses.add_torrent(dl_atp);
	dl.connect_peer(ep("127.0.0.1", seed_ses.listen_port()));

	std::vector<peer_info> pi;
	for (int i = 0; i < 100; ++i)
	{
		print_alerts(dl_ses, "dl");
		dl.get_peer_info(pi);
		if (!pi.empty() && (pi[0].flags & peer_info::seed)) break;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}
	TEST_CHECK(!pi.empty());
	if (pi.empty()) return;
	TEST_CHECK(pi[0].pieces[0_piece]);
	TEST_CHECK(pi[0].pieces[1_piece]);

	seed.unload_file(0_file);
	wait_for_alert(seed_ses, file_prio_alert::alert_type, "unload");

	for (int i = 0; i < 100 && !pi.empty() && pi[0].pieces[0_piece]; ++i)
	{
		print_alerts(dl_ses, "dl");
		dl.get_peer_info(pi);
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	TEST_EQUAL(pi.size(), std::size_t(1));
	if (pi.size() != 1) return;
	TEST_CHECK(!pi[0].pieces[0_piece]);
	TEST_CHECK(pi[0].pieces[1_piece]);
	TEST_CHECK(!(pi[0].flags & peer_info::seed));
}

TORRENT_TEST(unload_file_share_mode)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);

	h.set_flags(torrent_flags::share_mode);
	wait_for_alert(ses, file_prio_alert::alert_type, "share mode");

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");

	TEST_CHECK(!exists("unload_test/file0"));
	TEST_CHECK(!h.have_piece(0_piece));
	TEST_CHECK(h.have_piece(1_piece));
}

TORRENT_TEST(unload_file_reenable_prioritize_files)
{
	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(unload_test_torrent());
	wait_for_seed(ses, h);

	h.unload_file(0_file);
	wait_for_alert(ses, file_prio_alert::alert_type, "unload");
	TEST_CHECK(!exists("unload_test/file0"));

	h.prioritize_files({default_priority, default_priority});
	wait_for_alert(ses, file_prio_alert::alert_type, "reenable");

	TEST_CHECK(exists("unload_test/file0"));
	TEST_CHECK(!h.get_unloaded_files()[0]);
	TEST_EQUAL(h.file_priority(0_file), default_priority);
}
