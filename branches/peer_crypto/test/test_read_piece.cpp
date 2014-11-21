/*

Copyright (c) 2013, Arvid Norberg
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
#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"

enum flags_t
{
	seed_mode = 1,
	time_critical = 2
};

void test_read_piece(int flags)
{
	using namespace libtorrent;
	namespace lt = libtorrent;

	fprintf(stderr, "==== TEST READ PIECE =====\n");

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_read_piece", ec);
	if (ec) fprintf(stderr, "ERROR: removing tmp1_read_piece: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	create_directory("tmp1_read_piece", ec);
	if (ec) fprintf(stderr, "ERROR: creating directory tmp1_read_piece: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	create_directory(combine_path("tmp1_read_piece", "test_torrent"), ec);
	if (ec) fprintf(stderr, "ERROR: creating directory test_torrent: (%d) %s\n"
		, ec.value(), ec.message().c_str());
	
	file_storage fs;
	std::srand(10);
	int piece_size = 0x4000;

	static const int file_sizes[] ={ 100000, 10000 };

	create_random_files(combine_path("tmp1_read_piece", "test_torrent")
		, file_sizes, 2);

	add_files(fs, combine_path("tmp1_read_piece", "test_torrent"));
	libtorrent::create_torrent t(fs, piece_size, 0x4000);

	// calculate the hash for all pieces
	set_piece_hashes(t, "tmp1_read_piece", ec);
	if (ec) fprintf(stderr, "ERROR: set_piece_hashes: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	boost::shared_ptr<torrent_info> ti(new torrent_info(&buf[0], buf.size(), ec));

	fprintf(stderr, "generated torrent: %s tmp1_read_piece/test_torrent\n"
		, to_hex(ti->info_hash().to_string()).c_str());

	lt::session ses(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48000, 49000), "0.0.0.0", 0);
	settings_pack sett;
	sett.set_int(settings_pack::alert_mask, alert::all_categories);
	ses.apply_settings(sett);

	add_torrent_params p;
	p.save_path = "tmp1_read_piece";
	p.ti = ti;
	if (flags & seed_mode)
		p.flags |= add_torrent_params::flag_seed_mode;
	torrent_handle tor1 = ses.add_torrent(p, ec);
	TEST_CHECK(!ec);
	TEST_CHECK(tor1.is_valid());

	std::auto_ptr<alert> a = wait_for_alert(ses, torrent_finished_alert::alert_type, "ses");
	TEST_CHECK(a.get());

	TEST_CHECK(tor1.status().is_seeding);

	if (flags & time_critical)
	{
		tor1.set_piece_deadline(1, 0, torrent_handle::alert_when_available);
	}
	else
	{
		tor1.read_piece(1);
	}

	a = wait_for_alert(ses, read_piece_alert::alert_type, "ses");

	TEST_CHECK(a.get());
	if (a.get())
	{
		read_piece_alert* rp = alert_cast<read_piece_alert>(a.get());
		TEST_CHECK(rp);
		if (rp)
		{
			TEST_EQUAL(rp->piece, 1);
		}
	}

	remove_all("tmp1_read_piece", ec);
	if (ec) fprintf(stderr, "ERROR: removing tmp1_read_piece: (%d) %s\n"
		, ec.value(), ec.message().c_str());
}

int test_main()
{
	test_read_piece(0);
	test_read_piece(seed_mode);
	test_read_piece(time_critical);
	return 0;
}

