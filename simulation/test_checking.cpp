/*

Copyright (c) 2016, Arvid Norberg
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
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp" // for create_random_files
#include "create_torrent.hpp"
#include "utils.hpp"

#include <fstream>

template <typename Setup, typename Test>
void run_test(Setup const& setup, Test const& test)
{
	// this is a seeding torrent
	lt::add_torrent_params atp = create_torrent(0, true);

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	auto ios = std::unique_ptr<sim::asio::io_service>(new sim::asio::io_service(
		sim, lt::address_v4::from_string("50.0.0.1")));
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();
	setup(atp, pack);

	auto ses = std::make_shared<lt::session>(pack, *ios);

	ses->async_add_torrent(atp);

	print_alerts(*ses);

	sim::timer t(sim, lt::seconds(6), [&](boost::system::error_code const& ec)
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
		[&](lt::add_torrent_params& atp, lt::settings_pack& p) {
			filename = lt::current_working_directory() + "/" + atp.save_path + "/" + atp.ti->files().file_path(0);
			std::ofstream f(filename);
			// create a file that's 100 bytes larger
			size = atp.ti->files().file_size(0) + 100;
			std::vector<char> dummy(size);
			f.write(dummy.data(), dummy.size());
		},
		[](lt::session& ses) {}
		);

	// file should not have been truncated just by checking
	std::ifstream f(filename);
	f.seekg(0, std::ios_base::end);
	TEST_EQUAL(f.tellg(), std::fstream::pos_type(size));
}

boost::shared_ptr<lt::torrent_info> create_multifile_torrent()
{
	// the two first files are exactly the size of a piece
	static const int file_sizes[] = { 0x40000, 0x40000, 4300, 0, 400, 4300, 6, 4};
	const int num_files = sizeof(file_sizes)/sizeof(file_sizes[0]);

	lt::file_storage fs;
	create_random_files("test_torrent_dir", file_sizes, num_files, &fs);
	lt::create_torrent t(fs, 0x40000, 0x4000);

	// calculate the hash for all pieces
	lt::error_code ec;
	set_piece_hashes(t, ".");

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), t.generate());
	return boost::make_shared<lt::torrent_info>(&buf[0], buf.size());
}

TORRENT_TEST(aligned_zero_priority)
{
	run_test(
		[&](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.file_priorities.push_back(1);
			atp.file_priorities.push_back(0);
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
		[&](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.ti = create_multifile_torrent();
			atp.save_path = ".";
			atp.file_priorities.push_back(1);
			atp.file_priorities.push_back(0);
			std::string filename = lt::current_working_directory() + "/" + atp.save_path + "/" + atp.ti->files().file_path(1);
			partfile = lt::current_working_directory() + "/" + atp.save_path + "/." + lt::to_hex(atp.ti->info_hash().to_string()) + ".parts";
			lt::error_code ec;
			lt::remove(filename, ec);
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
	boost::shared_ptr<lt::torrent_info> ti = create_multifile_torrent();
	run_test(
		[&](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.ti = ti;
			atp.save_path = ".";
			atp.file_priorities.push_back(1);
			atp.file_priorities.push_back(1);
			atp.file_priorities.push_back(0);
			std::string filename = lt::current_working_directory() + "/" + atp.save_path + "/" + atp.ti->files().file_path(2);
			lt::error_code ec;
			lt::remove(filename, ec);
		},
		[&](lt::session& ses) {
			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);
			TEST_EQUAL(tor[0].status().num_pieces, ti->num_pieces() - 1);
		}
	);
}

TORRENT_TEST(cache_after_checking)
{
	run_test(
		[](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.flags |= lt::add_torrent_params::flag_auto_managed;
			p.set_int(lt::settings_pack::cache_size, 100);
		},
		[](lt::session& ses) {
			int cache = get_cache_size(ses);
			TEST_CHECK(cache > 0);

			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);

			TEST_EQUAL(tor[0].status().is_seeding, true);
		});
}

TORRENT_TEST(checking_no_cache)
{
	run_test(
		[](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.flags |= lt::add_torrent_params::flag_auto_managed;
			p.set_int(lt::settings_pack::cache_size, 0);
		},
		[](lt::session& ses) {
			int cache = get_cache_size(ses);
			TEST_EQUAL(cache, 0);

			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);

			TEST_EQUAL(tor[0].status().is_seeding, true);
		});
}

TORRENT_TEST(checking_limit_volatile)
{
	run_test(
		[](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.flags |= lt::add_torrent_params::flag_auto_managed;
			p.set_int(lt::settings_pack::cache_size, 300);
			p.set_int(lt::settings_pack::cache_size_volatile, 2);
		},
		[](lt::session& ses) {
			int cache = get_cache_size(ses);
			// the cache fits 300 blocks, but only allows two volatile blocks
			TEST_EQUAL(cache, 2);

			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);

			TEST_EQUAL(tor[0].status().is_seeding, true);
		});
}

TORRENT_TEST(checking_volatile_limit_cache_size)
{
	run_test(
		[](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.flags |= lt::add_torrent_params::flag_auto_managed;
			p.set_int(lt::settings_pack::cache_size, 10);
			p.set_int(lt::settings_pack::cache_size_volatile, 300);
		},
		[](lt::session& ses) {
			int cache = get_cache_size(ses);
			// the cache allows 300 volatile blocks, but only fits 2 blocks
			TEST_CHECK(cache > 0);
			TEST_CHECK(cache <= 10);

			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);

			TEST_EQUAL(tor[0].status().is_seeding, true);
		});
}

