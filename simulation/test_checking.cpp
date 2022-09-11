/*

Copyright (c) 2016-2022, Arvid Norberg
Copyright (c) 2017, Steven Siloti
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/file.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/hex.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp" // for create_random_files
#include "create_torrent.hpp"
#include "utils.hpp"
#include "test_utils.hpp"

#include <fstream>
#include <iostream>

template <typename Setup, typename Test>
void run_test(Setup const& setup, Test const& test)
{
	// this is a seeding torrent
	lt::add_torrent_params atp = create_torrent(0, true);

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	auto ios = std::make_unique<sim::asio::io_context>(
		sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();
	setup(atp, pack);

	auto ses = std::make_shared<lt::session>(pack, *ios);

	ses->async_add_torrent(atp);

	print_alerts(*ses);

	sim::timer t(sim, lt::seconds(6), [&](boost::system::error_code const&)
	{
		test(*ses);

		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

template <typename SetupTorrent, typename SetupFiles, typename Test>
void run_force_recheck_test(SetupTorrent const& setup1, SetupFiles const& setup2, Test const& test)
{
	// this is a seeding torrent
	lt::add_torrent_params atp = create_torrent(0, true);

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	auto ios = std::make_unique<sim::asio::io_context>(
		sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();
	setup1(atp, pack);

	auto ses = std::make_shared<lt::session>(pack, *ios);

	ses->async_add_torrent(atp);

	print_alerts(*ses);

	sim::timer t1(sim, lt::seconds(6), [&](boost::system::error_code const&)
	{
		setup2(atp);
		ses->get_torrents()[0].force_recheck();
	});

	sim::timer t2(sim, lt::seconds(12), [&](boost::system::error_code const&)
	{
		test(*ses);

		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

TORRENT_TEST(no_truncate_checking)
{
	std::string filename;
	int size = 0;
	run_test(
		[&](lt::add_torrent_params& atp, lt::settings_pack&) {
			filename = lt::combine_path(atp.save_path, atp.ti->files().file_path(lt::file_index_t{0}));
			std::ofstream f(filename);
			// create a file that's 100 bytes larger
			size = int(atp.ti->files().file_size(lt::file_index_t{0}) + 100);
			std::vector<char> dummy(size);
			f.write(dummy.data(), dummy.size());
		},
		[](lt::session&) {}
		);

	// file should not have been truncated just by checking
	std::ifstream f(filename);
	f.seekg(0, std::ios_base::end);
	TEST_EQUAL(f.tellg(), std::fstream::pos_type(size));
}

std::shared_ptr<lt::torrent_info> create_multifile_torrent()
{
	// the two first files are exactly the size of a piece
	static std::array<const int, 8> const file_sizes{{ 0x40000, 0x40000, 4300, 0, 400, 4300, 6, 4}};

	auto fs = create_random_files("test_torrent_dir", file_sizes);
	// the torrent needs to be v1 only because the zero_priority_missing_partfile
	// test relies on non-aligned files
	lt::create_torrent t(std::move(fs), 0x40000, lt::create_torrent::v1_only);

	// calculate the hash for all pieces
	set_piece_hashes(t, ".");

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), t.generate());
	return std::make_shared<lt::torrent_info>(buf, lt::from_span);
}

TORRENT_TEST(checking_first_piece_missing)
{
	run_force_recheck_test(
		[&](lt::add_torrent_params&, lt::settings_pack& pack) {
			pack.set_int(lt::settings_pack::checking_mem_usage, 1);
		},
		[&](lt::add_torrent_params& atp) {
		std::string filename = lt::combine_path(
			atp.save_path, atp.ti->files().file_path(lt::file_index_t{0}));
			FILE* f = ::fopen(filename.c_str(), "rb+");
			::fwrite("0000", 4, 1, f);
			::fclose(f);
		},
		[](lt::session& ses) {
			lt::torrent_handle tor = ses.get_torrents()[0];
			lt::torrent_status st = tor.status(lt::torrent_handle::query_pieces);

			TEST_EQUAL(st.is_finished, false);


			lt::typed_bitfield<lt::piece_index_t> expected_pieces(st.pieces.size(), true);
			expected_pieces.clear_bit(0_piece);

			// check that just the first piece is missing
			for (lt::piece_index_t p : expected_pieces.range())
				TEST_EQUAL(st.pieces[p], expected_pieces[p]);
		}
	);
}

TORRENT_TEST(aligned_zero_priority)
{
	run_test(
		[&](lt::add_torrent_params& atp, lt::settings_pack&) {
			atp.file_priorities.push_back(lt::download_priority_t{1});
			atp.file_priorities.push_back(lt::download_priority_t{0});
			atp.ti = create_multifile_torrent();
			atp.save_path = ".";
		},
		[](lt::session& ses) {
			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);
			TEST_EQUAL(tor[0].status().is_finished, true);
		}
	);
}

// we have a zero-priority file that also does not exist on disk. It does not
// overlap any piece in another file, so we don't need a partfile
TORRENT_TEST(aligned_zero_priority_no_file)
{
	std::string partfile;
	run_test(
		[&](lt::add_torrent_params& atp, lt::settings_pack&) {
			atp.ti = create_multifile_torrent();
			atp.save_path = ".";
			atp.file_priorities.push_back(lt::download_priority_t{1});
			atp.file_priorities.push_back(lt::download_priority_t{0});
			std::string filename = lt::combine_path(lt::current_working_directory()
				, lt::combine_path(atp.save_path, atp.ti->files().file_path(lt::file_index_t{1})));
			partfile = lt::combine_path(lt::current_working_directory()
				, lt::combine_path(atp.save_path, "." + lt::aux::to_hex(atp.ti->info_hashes().v1.to_string()) + ".parts"));
			lt::error_code ec;
			lt::remove(filename, ec);
			TEST_CHECK(!ec);
		},
		[](lt::session& ses) {
			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);
			TEST_EQUAL(tor[0].status().is_finished, true);
		}
	);

	// the part file should not have been created. There is no need for a
	// partfile
	lt::error_code ec;
	lt::file_status fs;
	stat_file(partfile, &fs, ec);
	TEST_EQUAL(ec, boost::system::errc::no_such_file_or_directory);
}

// we have a file whose priority is 0, we don't have the file on disk nor a
// part-file for it. The checking should complete and enter download state.
TORRENT_TEST(zero_priority_missing_partfile)
{
	std::shared_ptr<lt::torrent_info> ti = create_multifile_torrent();
	run_test(
		[&](lt::add_torrent_params& atp, lt::settings_pack&) {
			atp.ti = ti;
			atp.save_path = ".";
			atp.file_priorities.push_back(lt::download_priority_t{1});
			atp.file_priorities.push_back(lt::download_priority_t{1});
			atp.file_priorities.push_back(lt::download_priority_t{0});
			std::string const filename = lt::combine_path(lt::current_working_directory()
				, lt::combine_path(atp.save_path, atp.ti->files().file_path(lt::file_index_t{2})));

			std::cout << "removing: " << filename << "\n";
			lt::error_code ec;
			lt::remove(filename, ec);
			TEST_CHECK(!ec);
		},
		[&](lt::session& ses) {
			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);
			TEST_EQUAL(tor[0].status().num_pieces, ti->num_pieces() - 1);
			TEST_EQUAL(tor[0].status().is_finished, false);
		}
	);
}

TORRENT_TEST(checking)
{
	run_test(
		[](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.flags |= lt::torrent_flags::auto_managed;
#if TORRENT_ABI_VERSION == 1
			p.set_int(lt::settings_pack::cache_size, 100);
#endif
		},
		[](lt::session& ses) {

			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);

			TEST_EQUAL(tor[0].status().is_seeding, true);
		});
}

