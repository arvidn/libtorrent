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

#include "libtorrent/storage.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/create_torrent.hpp"

#include <boost/utility.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/thread/mutex.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
using namespace boost::filesystem;

const int piece_size = 16;

const int half = piece_size / 2;

char piece0[piece_size] =
{ 6, 6, 6, 6, 6, 6, 6, 6
, 9, 9, 9, 9, 9, 9, 9, 9};

char piece1[piece_size] =
{ 0, 0, 0, 0, 0, 0, 0, 0
, 1, 1, 1, 1, 1, 1, 1, 1};

char piece2[piece_size] =
{ 0, 0, 1, 0, 0, 0, 0, 0
, 1, 1, 1, 1, 1, 1, 1, 1};

void on_read_piece(int ret, disk_io_job const& j, char const* data, int size)
{
	std::cerr << "on_read_piece piece: " << j.piece << std::endl;
	TEST_CHECK(ret == size);
	TEST_CHECK(std::equal(j.buffer, j.buffer + ret, data));
}

void on_check_resume_data(int ret, disk_io_job const& j)
{
	std::cerr << "on_check_resume_data ret: " << ret << " " << j.piece << std::endl;
}

void on_check_files(int ret, disk_io_job const& j)
{
	std::cerr << "on_check_files ret: " << ret << " " << j.piece << std::endl;
}

void on_move_storage(int ret, disk_io_job const& j, std::string path)
{
	std::cerr << "on_move_storage ret: " << ret << " path:" << j.str << std::endl;
	TEST_CHECK(ret == 0);
	TEST_CHECK(j.str == path);
}

void run_storage_tests(boost::intrusive_ptr<torrent_info> info
	, file_storage& fs
	, path const& test_path
	, libtorrent::storage_mode_t storage_mode)
{
	TORRENT_ASSERT(fs.num_files() > 0);
	create_directory(test_path / "temp_storage");

	int num_pieces = (1 + 612 + 17 + piece_size - 1) / piece_size;
	TEST_CHECK(info->num_pieces() == num_pieces);

	char piece[piece_size];

	{ // avoid having two storages use the same files	
	file_pool fp;
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(fs, test_path, fp));

	// write piece 1 (in slot 0)
	s->write(piece1, 0, 0, half);
	s->write(piece1 + half, 0, half, half);

	// verify piece 1
	TEST_CHECK(s->read(piece, 0, 0, piece_size) == piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1));
	
	// do the same with piece 0 and 2 (in slot 1 and 2)
	s->write(piece0, 1, 0, piece_size);
	s->write(piece2, 2, 0, piece_size);

	// verify piece 0 and 2
	TEST_CHECK(s->read(piece, 1, 0, piece_size) == piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0));

	s->read(piece, 2, 0, piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2));
	
	s->release_files();
	}

	// make sure the piece_manager can identify the pieces
	{
	file_pool fp;
	libtorrent::asio::io_service ios;
	disk_io_thread io(ios);
	boost::shared_ptr<int> dummy(new int);
	boost::intrusive_ptr<piece_manager> pm = new piece_manager(dummy, info
		, test_path, fp, io, default_storage_constructor, storage_mode);
	boost::mutex lock;

	lazy_entry frd;
	pm->async_check_fastresume(&frd, &on_check_resume_data);
	ios.reset();
	ios.run();

	pm->async_check_files(&on_check_files);
	for (int i = 0; i < 4; ++i)
	{
		ios.reset();
		ios.run_one();
	}
	ios.reset();
	ios.poll();

	// test move_storage
	boost::function<void(int, disk_io_job const&)> none;
	TEST_CHECK(exists(test_path / "temp_storage"));
	pm->async_move_storage(test_path / "temp_storage2", bind(on_move_storage, _1, _2, (test_path / "temp_storage2").string()));

	test_sleep(2000);
	ios.reset();
	ios.poll();

	TEST_CHECK(!exists(test_path / "temp_storage"));
	TEST_CHECK(exists(test_path / "temp_storage2/temp_storage"));
	pm->async_move_storage(test_path, bind(on_move_storage, _1, _2, test_path.string()));

	test_sleep(2000);
	ios.reset();
	ios.poll();

	TEST_CHECK(!exists(test_path / "temp_storage2/temp_storage"));	
	remove_all(test_path / "temp_storage2");

	// test rename_file
	remove(test_path / "part0");
	TEST_CHECK(exists(test_path / "temp_storage/test1.tmp"));
	TEST_CHECK(!exists(test_path / "part0"));	
	pm->async_rename_file(0, "part0", none);

	test_sleep(2000);
	ios.reset();
	ios.poll();

	TEST_CHECK(!exists(test_path / "temp_storage/test1.tmp"));
	TEST_CHECK(exists(test_path / "part0"));

	peer_request r;
	r.piece = 0;
	r.start = 0;
	r.length = piece_size;
	pm->async_read(r, bind(&on_read_piece, _1, _2, piece0, piece_size));
	r.piece = 1;
	pm->async_read(r, bind(&on_read_piece, _1, _2, piece1, piece_size));
	r.piece = 2;
	pm->async_read(r, bind(&on_read_piece, _1, _2, piece2, piece_size));
	pm->async_release_files(none);

	pm->async_rename_file(0, "temp_storage/test1.tmp", none);
	test_sleep(1000);
	TEST_CHECK(!exists(test_path / "part0"));	

	ios.run();

	io.join();
	}
}

void test_remove(path const& test_path)
{
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 8);
	fs.add_file("temp_storage/folder1/test2.tmp", 8);
	fs.add_file("temp_storage/folder2/test3.tmp", 0);
	fs.add_file("temp_storage/_folder3/test4.tmp", 0);
	fs.add_file("temp_storage/_folder3/subfolder/test5.tmp", 8);
	libtorrent::create_torrent t(fs, 4);

	char buf[4] = {0, 0, 0, 0};
	sha1_hash h = hasher(buf, 4).final();
	for (int i = 0; i < 6; ++i) t.set_hash(i, h);
	
	boost::intrusive_ptr<torrent_info> info(new torrent_info(t.generate()));

	file_pool fp;
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(fs, test_path, fp));

	// allocate the files and create the directories
	s->initialize(true);

	TEST_CHECK(exists(test_path / "temp_storage/_folder3/subfolder/test5.tmp"));	
	TEST_CHECK(exists(test_path / "temp_storage/folder2/test3.tmp"));	

	s->delete_files();

	TEST_CHECK(!exists(test_path / "temp_storage"));	
}

void run_test(path const& test_path)
{
	std::cerr << "\n=== " << test_path.string() << " ===\n" << std::endl;

	boost::intrusive_ptr<torrent_info> info;

	{
	remove_all(test_path / "temp_storage");
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 17);
	fs.add_file("temp_storage/test2.tmp", 612);
	fs.add_file("temp_storage/test3.tmp", 0);
	fs.add_file("temp_storage/test4.tmp", 0);
	fs.add_file("temp_storage/test5.tmp", 1);

	libtorrent::create_torrent t(fs, piece_size);
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, hasher(piece1, piece_size).final());
	t.set_hash(2, hasher(piece2, piece_size).final());
	

	info = new torrent_info(t.generate());
	std::cerr << "=== test 1 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact);

	// make sure the files have the correct size
	std::cerr << file_size(test_path / "temp_storage" / "test1.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 17);
	std::cerr << file_size(test_path / "temp_storage" / "test2.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test2.tmp") == 31);
	TEST_CHECK(exists(test_path / "temp_storage/test3.tmp"));
	TEST_CHECK(exists(test_path / "temp_storage/test4.tmp"));
	remove_all(test_path / "temp_storage");
	}

// ==============================================

	{
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 17 + 612 + 1);
	libtorrent::create_torrent t(fs, piece_size);
	TEST_CHECK(fs.begin()->path == "temp_storage/test1.tmp");
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, hasher(piece1, piece_size).final());
	t.set_hash(2, hasher(piece2, piece_size).final());

	info = new torrent_info(t.generate());

	std::cerr << "=== test 3 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact);

	// 48 = piece_size * 3
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 48);
	remove_all(test_path / "temp_storage");

// ==============================================

	std::cerr << "=== test 4 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_allocate);

	std::cerr << file_size(test_path / "temp_storage" / "test1.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 17 + 612 + 1);

	remove_all(test_path / "temp_storage");

	}

// ==============================================

	std::cerr << "=== test 5 ===" << std::endl;
	test_remove(test_path);

}

void test_fastresume()
{
	std::cout << "=== test fastresume ===" << std::endl;
	create_directory("tmp1");
	std::ofstream file("tmp1/temporary");
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists("tmp1/temporary"));

	entry resume;
	{
		session ses;

		torrent_handle h = ses.add_torrent(boost::intrusive_ptr<torrent_info>(new torrent_info(*t))
			, "tmp1", entry()
			, storage_mode_compact);

		for (int i = 0; i < 10; ++i)
		{
			test_sleep(1000);
			torrent_status s = h.status();
			if (s.progress == 1.0f) 
			{
				std::cout << "progress: 1.0f" << std::endl;
				break;
			}
		}
		resume = h.write_resume_data();
		ses.remove_torrent(h, session::delete_files);
	}
	TEST_CHECK(!exists("tmp1/temporary"));
	resume.print(std::cout);

	{
		session ses;
		ses.set_alert_mask(alert::all_categories);
		torrent_handle h = ses.add_torrent(t, "tmp1", resume
			, storage_mode_compact);
	

		std::auto_ptr<alert> a = ses.pop_alert();
		ptime end = time_now() + seconds(20);
		while (a.get() == 0 || dynamic_cast<fastresume_rejected_alert*>(a.get()) == 0)
		{
			if (ses.wait_for_alert(end - time_now()) == 0)
			{
				std::cerr << "wait_for_alert() expired" << std::endl;
				break;
			}
			a = ses.pop_alert();
			assert(a.get());
			std::cerr << a->message() << std::endl;
		}
		TEST_CHECK(dynamic_cast<fastresume_rejected_alert*>(a.get()) != 0);
	}
}

int test_main()
{
	std::vector<path> test_paths;
	char* env = std::getenv("TORRENT_TEST_PATHS");
	if (env == 0)
	{
		test_paths.push_back(initial_path());
	}
	else
	{
		char* p = std::strtok(env, ";");
		while (p != 0)
		{
			test_paths.push_back(complete(p));
			p = std::strtok(0, ";");
		}
	}

	std::for_each(test_paths.begin(), test_paths.end(), bind(&run_test, _1));

	test_fastresume();

	return 0;
}

