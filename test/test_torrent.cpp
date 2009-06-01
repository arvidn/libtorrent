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
#include "libtorrent/hasher.hpp"
#include "libtorrent/create_torrent.hpp"
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;

void test_running_torrent(boost::intrusive_ptr<torrent_info> info, size_type file_size)
{
	session ses(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48130, 48140));

	add_torrent_params p;
	p.ti = info;
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);

	test_sleep(500);
	torrent_status st = h.status();

	std::cout << "total_wanted: " << st.total_wanted << " : " << file_size * 3 << std::endl;
	TEST_CHECK(st.total_wanted == file_size * 3);
	std::cout << "total_wanted_done: " << st.total_wanted_done << " : 0" << std::endl;
	TEST_CHECK(st.total_wanted_done == 0);

	std::vector<int> prio(3, 1);
	prio[0] = 0;
	h.prioritize_files(prio);

	test_sleep(500);
	st = h.status();

	std::cout << "total_wanted: " << st.total_wanted << " : " << file_size * 2 << std::endl;
	TEST_CHECK(st.total_wanted == file_size * 2);
	std::cout << "total_wanted_done: " << st.total_wanted_done << " : 0" << std::endl;
	TEST_CHECK(st.total_wanted_done == 0);

	prio[1] = 0;
	h.prioritize_files(prio);

	test_sleep(500);
	st = h.status();

	std::cout << "total_wanted: " << st.total_wanted << " : " << file_size << std::endl;
	TEST_CHECK(st.total_wanted == file_size);
	std::cout << "total_wanted_done: " << st.total_wanted_done << " : 0" << std::endl;
	TEST_CHECK(st.total_wanted_done == 0);
}

int test_main()
{
	{
		file_storage fs;
		size_type file_size = 1 * 1024 * 1024 * 1024;
		fs.add_file("test_torrent_dir2/tmp1", file_size);
		fs.add_file("test_torrent_dir2/tmp2", file_size);
		fs.add_file("test_torrent_dir2/tmp3", file_size);
		libtorrent::create_torrent t(fs, 4 * 1024 * 1024);
		t.add_tracker("http://non-existing.com/announce");

		std::vector<char> piece(4 * 1024 * 1024);
		for (int i = 0; i < int(piece.size()); ++i)
			piece[i] = (i % 26) + 'A';
		
		// calculate the hash for all pieces
		sha1_hash ph = hasher(&piece[0], piece.size()).final();
		int num = t.num_pieces();
		for (int i = 0; i < num; ++i)
			t.set_hash(i, ph);

		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, t.generate());
		boost::intrusive_ptr<torrent_info> info(new torrent_info(&tmp[0], tmp.size()));

		test_running_torrent(info, file_size);
	}

	{
		file_storage fs;

		fs.add_file("test_torrent_dir2/tmp1", 0);
		libtorrent::create_torrent t(fs, 4 * 1024 * 1024);
		t.add_tracker("http://non-existing.com/announce");

		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char> > out(tmp);
		bencode(out, t.generate());
		boost::intrusive_ptr<torrent_info> info(new torrent_info(&tmp[0], tmp.size()));
		test_running_torrent(info, 0);
	}

	return 0;
}


