/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/settings_pack.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/read_resume_data.hpp"

#include "transfer_sim.hpp"

using lt::settings_pack;

void no_init(lt::session& ses0, lt::session& ses1) {}

record_finished_pieces::record_finished_pieces(std::set<lt::piece_index_t>& p)
	: m_passed(&p)
{}

void record_finished_pieces::operator()(lt::session&, lt::alert const* a) const
{
	if (auto const* pf = lt::alert_cast<lt::piece_finished_alert>(a))
		m_passed->insert(pf->piece_index);
}

template <typename Container>
void lengthen(Container& c)
{
	if (c.size() == 0) return;
	c.resize(c.size() + 1);
}

restore_from_resume::restore_from_resume()
	: m_last_check()
{}

void restore_from_resume::operator()(lt::session& ses, lt::alert const* a)
{
	if (m_done) return;

	if (auto* rd = lt::alert_cast<lt::save_resume_data_alert>(a))
	{
		m_resume_buffer = lt::write_resume_data_buf(rd->params);
		auto torrents = ses.get_torrents();
		ses.remove_torrent(torrents[0]);
		return;
	}

	if (lt::alert_cast<lt::torrent_removed_alert>(a))
	{
		lt::add_torrent_params atp = lt::read_resume_data(m_resume_buffer);
		m_resume_buffer.clear();

		// make sure loading resume data tolerates oversized bitfields
		lengthen(atp.have_pieces);
		lengthen(atp.verified_pieces);

		for (auto& m : atp.merkle_tree_mask)
			lengthen(m);

		for (auto& v : atp.verified_leaf_hashes)
			lengthen(v);

		ses.async_add_torrent(atp);
		m_done = true;
		return;
	}

	// we only want to do this once
	if (m_triggered)
		return;

	auto const now = lt::clock_type::now();
	if (now < m_last_check + lt::milliseconds(100))
		return;

	m_last_check = now;
	auto torrents = ses.get_torrents();
	if (torrents.empty())
		return;

	auto h = torrents.front();
	if (h.status().num_pieces < 7)
		return;

	h.save_resume_data(lt::torrent_handle::save_info_dict);
	m_triggered = true;
}

expect_seed::expect_seed(bool e) : m_expect(e) {}
void expect_seed::operator()(std::shared_ptr<lt::session> ses[2]) const
{
	TEST_EQUAL(is_seed(*ses[0]), m_expect);
}

int blocks_per_piece(test_transfer_flags_t const flags)
{
	if (flags & tx::small_pieces) return 1;
	if (flags & tx::large_pieces) return 4;
	return 2;
}

int num_pieces(test_transfer_flags_t const flags)
{
	if (flags & tx::multiple_files)
	{
		// since v1 torrents don't pad files by default, there will be fewer
		// pieces on those torrents
		if (flags & tx::v1_only)
			return 31;
		else
			return 33;
	}
	return 11;
}

bool run_matrix_test(test_transfer_flags_t const flags, existing_files_mode const files)
{
	// v2 (compatible) torrents require power-of-2
	// piece sizes
	if ((flags & tx::odd_pieces) && !(flags & tx::v1_only))
		return false;

	// you can't download the metadata from a web
	// seed, so we don't support web-seeding and
	// magnet download
	if ((flags & tx::web_seed) && (flags & tx::magnet_download))
		return false;

	// the web server in libsimulator only supports a single connection at a
	// time. When disconnecting and re-connecting quickly, the initial
	// connection is still held open, causing the second connection to fail.
	// therefore, this test configuration does not work (yet). Perhaps the
	// server could be changed to boot any existing connection when accepting a
	// new one.
	if ((flags & tx::web_seed) && (flags && tx::resume_restart))
		return false;

	// this will clear the history of all output we've printed so far.
	// if we encounter an error from now on, we'll only print the relevant
	// iteration
	::unit_test::reset_output();

	// re-seed the random engine each iteration, to make the runs
	// deterministic
	lt::aux::random_engine().seed(0x23563a7f);

	std::cout << "\n\nTEST CASE: "
		<< ((flags & tx::small_pieces) ? "small_pieces"
			: (flags & tx::large_pieces) ? "large_pieces"
			: (flags & tx::odd_pieces) ? "odd_pieces"
			: "normal_pieces")
		<< "-" << ((flags & tx::corruption) ? "corruption" : "valid")
		<< "-" << ((flags & tx::v2_only) ? "v2_only" : (flags & tx::v1_only) ? "v1_only" : "hybrid")
		<< "-" << ((flags & tx::magnet_download) ? "magnet" : "torrent")
		<< "-" << ((flags & tx::multiple_files) ? "multi_file" : "single_file")
		<< "-" << ((flags & tx::web_seed) ? "web_seed" : "bt_peers")
		<< "-" << ((flags & tx::resume_restart) ? "resume_restart" : "continuous")
		<< "-" << files
		<< "\n\n";

	auto downloader_disk = test_disk().set_files(files);
	auto seeder_disk = test_disk();
	if (flags & tx::corruption)
		seeder_disk = seeder_disk.send_corrupt_data(num_pieces(flags) / 4 * blocks_per_piece(flags));
	std::set<lt::piece_index_t> passed;

	combine_t handler;
	handler.add(record_finished_pieces(passed));

	if (flags & tx::resume_restart)
		handler.add(restore_from_resume());

	run_test(no_init
		, handler
		, expect_seed(!(flags & tx::corruption))
		, flags
		, downloader_disk
		, seeder_disk
		);

	int const expected_pieces = num_pieces(flags);

	// we we send some corrupt pieces, it's not straight-forward to predict
	// exactly how many will pass the hash check, since a failure will cause
	// a re-request and also a request of the block hashes (for v2 torrents)
	if (flags & tx::corruption)
	{
		TEST_CHECK(int(passed.size()) < expected_pieces);
	}
	else
	{
		TEST_EQUAL(int(passed.size()), expected_pieces);
	}

	return ::unit_test::g_test_failures > 0;
}

