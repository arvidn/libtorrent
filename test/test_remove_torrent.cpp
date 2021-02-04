/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2018, d-komarov
Copyright (c) 2018, Alden Torres
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
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/aux_/path.hpp"

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include <fstream>
#include <iostream>
#include <cstdint>

using namespace libtorrent;
namespace lt = libtorrent;
using std::ignore;

namespace {

enum test_case {
	complete_download,
	partial_download,
	mid_download
};

void test_remove_torrent(remove_flags_t const remove_options
	, test_case const test = complete_download)
{
	// this allows shutting down the sessions in parallel
	std::vector<session_proxy> sp;
	settings_pack pack = settings();

	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48075");
	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:49075");
	lt::session ses2(pack);

	torrent_handle tor1;
	torrent_handle tor2;

	int const num_pieces = (test == mid_download) ? 500 : 100;

	error_code ec;
	remove_all("tmp1_remove", ec);
	remove_all("tmp2_remove", ec);
	create_directory("tmp1_remove", ec);
	std::ofstream file("tmp1_remove/temporary");
	std::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary"
		, 8 * 1024, num_pieces, false, create_torrent::v1_only);
	file.close();

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	// test using piece sizes smaller than 16kB
	std::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, nullptr
		, true, false, true, "_remove", 8 * 1024, &t, false, nullptr, true, false, nullptr
		, create_torrent::v1_only);

	if (test == partial_download)
	{
		std::vector<download_priority_t> priorities(std::size_t(num_pieces), low_priority);
		// set half of the pieces to priority 0
		std::fill(priorities.begin(), priorities.begin() + (num_pieces / 2), dont_download);
		tor2.prioritize_pieces(priorities);
	}
	else if (test == mid_download)
	{
		tor1.set_upload_limit(static_cast<int>(t->total_size()));
		tor2.set_download_limit(static_cast<int>(t->total_size()));
	}

	torrent_status st1;
	torrent_status st2;

	for (int i = 0; i < 200; ++i)
	{
		print_alerts(ses1, "ses1", true, true);
//		print_alerts(ses2, "ses2", true, true);

		st1 = tor1.status();
		std::cout << "st1.total_payload_upload: " << st1.total_payload_upload << '\n';
		std::cout << "st2.num_pieces: " << st2.num_pieces << '\n';
		st2 = tor2.status();

		if (test == mid_download && st2.num_pieces > num_pieces / 2)
		{
			TEST_CHECK(st2.is_finished == false);
			break;
		}
		if (st2.is_finished) break;

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_resume_data
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data);

		// if nothing is being transferred after 4 seconds, we're failing the test
		if (st1.total_payload_upload == 0 && i > 40)
		{
			TEST_ERROR("no transfer");
			return;
		}

		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	TEST_CHECK(st1.num_pieces > 0);
	TEST_CHECK(st2.num_pieces > 0);

	ses2.remove_torrent(tor2, remove_options);
	ses1.remove_torrent(tor1, remove_options);

	std::cerr << "removed" << std::endl;

	for (int i = 0; tor2.is_valid() || tor1.is_valid(); ++i)
	{
		std::this_thread::sleep_for(lt::milliseconds(100));
		if (++i > 400)
		{
			std::cerr << "torrent handle(s) still valid: "
				<< (tor1.is_valid() ? "tor1 " : "")
				<< (tor2.is_valid() ? "tor2 " : "")
				<< std::endl;

			TEST_ERROR("handle did not become invalid");
			return;
		}
	}

	if (remove_options & session::delete_files)
	{
		TEST_CHECK(!exists("tmp1_remove/temporary"));
		TEST_CHECK(!exists("tmp2_remove/temporary"));
	}

	sp.push_back(ses1.abort());
	sp.push_back(ses2.abort());
}

} // anonymous namespace

TORRENT_TEST(remove_torrent)
{
	test_remove_torrent({});
}

TORRENT_TEST(remove_torrent_and_files)
{
	test_remove_torrent(session::delete_files);
}

TORRENT_TEST(remove_torrent_partial)
{
	test_remove_torrent({}, partial_download);
}

TORRENT_TEST(remove_torrent_and_files_partial)
{
	test_remove_torrent(session::delete_files, partial_download);
}

TORRENT_TEST(remove_torrent_mid_download)
{
	test_remove_torrent({}, mid_download);
}

TORRENT_TEST(remove_torrent_and_files_mid_download)
{
	test_remove_torrent(session::delete_files, mid_download);
}


