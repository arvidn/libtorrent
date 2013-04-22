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
#include "libtorrent/thread.hpp"

#include <boost/utility.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include <iostream>
#include <fstream>

using namespace libtorrent;

const int piece_size = 16 * 1024 * 16;
const int block_size = 16 * 1024;

const int half = piece_size / 2;

char* piece0 = page_aligned_allocator::malloc(piece_size);
char* piece1 = page_aligned_allocator::malloc(piece_size);
char* piece2 = page_aligned_allocator::malloc(piece_size);

void signal_bool(bool* b, char const* string)
{
	*b = true;
	std::cerr << string << std::endl;
}

void on_read_piece(int ret, disk_io_job const& j, char const* data, int size)
{
	std::cerr << "on_read_piece piece: " << j.piece << std::endl;
	TEST_EQUAL(ret, size);
	if (ret > 0) TEST_CHECK(std::equal(j.buffer, j.buffer + ret, data));
}

void on_check_resume_data(disk_io_job const* j, bool* done)
{
	std::cerr << "on_check_resume_data ret: " << j->ret;
	switch (j->ret)
	{
		case piece_manager::no_error:
			std::cerr << " success" << std::endl;
			break;
		case piece_manager::fatal_disk_error:
			std::cerr << " disk error: " << j->error.ec.message()
				<< " file: " << j->error.file << std::endl;
			break;
		case piece_manager::need_full_check:
			std::cerr << " need full check" << std::endl;
			break;
		case piece_manager::disk_check_aborted:
			std::cerr << " aborted" << std::endl;
			break;
	}
	*done = true;
}

void on_move_storage(int ret, bool* done, disk_io_job const& j, std::string path)
{
	std::cerr << "on_move_storage ret: " << ret << " path: " << (char const*)j.buffer << std::endl;
	TEST_EQUAL(ret, 0);
	TEST_EQUAL((char const*)j.buffer, path);
	*done = true;
	free(j.buffer);
}

void print_error(char const* call, int ret, storage_error const& ec)
{
	fprintf(stderr, "%s() returned: %d error: \"%s\" in file: %d operation: %d\n"
		, call, ret, ec.ec.message().c_str(), ec.file, ec.operation);
}

void run_until(io_service& ios, bool const& done)
{
	while (!done)
	{
		ios.reset();
		error_code ec;
		ios.run_one(ec);
		if (ec)
		{
			std::cerr << "run_one: " << ec.message() << std::endl;
			return;
		}
		std::cerr << "done: " << done << std::endl;
	}
}

void nop() {}

void run_storage_tests(boost::intrusive_ptr<torrent_info> info
	, file_storage& fs
	, std::string const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	TORRENT_ASSERT(fs.num_files() > 0);
	error_code ec;
	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "create_directory '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "temp_storage2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage2")
		<< "': " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "part0"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "part0")
		<< "': " << ec.message() << std::endl;

	int num_pieces = fs.num_pieces();
	TEST_CHECK(info->num_pieces() == num_pieces);

	aux::session_settings set;
	set.set_int(settings_pack::disk_io_write_mode
		, unbuffered ? session_settings::disable_os_cache
		: session_settings::enable_os_cache);
	set.set_int(settings_pack::disk_io_read_mode
		, unbuffered ? session_settings::disable_os_cache
		: session_settings::enable_os_cache);

	char* piece = page_aligned_allocator::malloc(piece_size);

	{ // avoid having two storages use the same files	
	file_pool fp;
	libtorrent::asio::io_service ios;
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop), NULL);
	storage_params p;
	p.path = test_path;
	p.files = &fs;
	p.pool = &fp;
	p.mode = storage_mode;
	boost::scoped_ptr<storage_interface> s(new default_storage(p));
	s->m_settings = &set;

	storage_error ec;
	s->initialize(ec);
	TEST_CHECK(!ec);
	if (ec) print_error("initialize", 0, ec);

	int ret = 0;

	// write piece 1 (in slot 0)
	file::iovec_t iov = { piece1, half};
	ret = s->writev(&iov, 1, 0, 0, 0, ec);
	if (ret != half) print_error("writev", ret, ec);

	iov.iov_base = piece1 + half;
	iov.iov_len = half;
	ret = s->writev(&iov, 1, 0, half, 0, ec);
	if (ret != half) print_error("writev", ret, ec);

	// test unaligned read (where the bytes are aligned)
	iov.iov_base = piece + 3;
	iov.iov_len = piece_size - 9;
	ret = s->readv(&iov, 1, 0, 3, 0, ec);
	if (ret != piece_size - 9) print_error("readv",ret, ec);
	TEST_CHECK(std::equal(piece+3, piece + piece_size-9, piece1+3));
	
	// test unaligned read (where the bytes are not aligned)
	iov.iov_base = piece;
	iov.iov_len = piece_size - 9;
	ret = s->readv(&iov, 1, 0, 3, 0, ec);
	TEST_CHECK(ret == piece_size - 9);
	if (ret != piece_size - 9) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size-9, piece1+3));

	// verify piece 1
	iov.iov_base = piece;
	iov.iov_len = piece_size;
	ret = s->readv(&iov, 1, 0, 0, 0, ec);
	TEST_CHECK(ret == piece_size);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1));
	
	// do the same with piece 0 and 2 (in slot 1 and 2)
	iov.iov_base = piece0;
	iov.iov_len = piece_size;
	ret = s->writev(&iov, 1, 1, 0, 0, ec);
	if (ret != piece_size) print_error("writev", ret, ec);

	iov.iov_base = piece2;
	iov.iov_len = piece_size;
	ret = s->writev(&iov, 1, 2, 0, 0, ec);
	if (ret != piece_size) print_error("writev", ret, ec);

	// verify piece 0 and 2
	iov.iov_base = piece;
	iov.iov_len = piece_size;
	ret = s->readv(&iov, 1, 1, 0, 0, ec);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0));

	iov.iov_base = piece;
	iov.iov_len = piece_size;
	ret = s->readv(&iov, 1, 2, 0, 0, ec);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2));

	s->release_files(ec);
	}

	page_aligned_allocator::free(piece);
}

void test_remove(std::string const& test_path, bool unbuffered)
{
	file_storage fs;
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));	
	fs.add_file("temp_storage/test1.tmp", 8);
	fs.add_file("temp_storage/folder1/test2.tmp", 8);
	fs.add_file("temp_storage/folder2/test3.tmp", 0);
	fs.add_file("temp_storage/_folder3/test4.tmp", 0);
	fs.add_file("temp_storage/_folder3/subfolder/test5.tmp", 8);
	libtorrent::create_torrent t(fs, 4, -1, 0);

	char buf_[4] = {0, 0, 0, 0};
	sha1_hash h = hasher(buf_, 4).final();
	for (int i = 0; i < 6; ++i) t.set_hash(i, h);
	
	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	boost::intrusive_ptr<torrent_info> info(new torrent_info(&buf[0], buf.size(), ec));

	aux::session_settings set;
	set.set_int(settings_pack::disk_io_write_mode
		, unbuffered ? session_settings::disable_os_cache
		: session_settings::enable_os_cache);
	set.set_int(settings_pack::disk_io_read_mode
		, unbuffered ? session_settings::disable_os_cache
		: session_settings::enable_os_cache);

	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop), NULL);

	storage_params p;
	p.files = &fs;
	p.pool = &fp;
	p.path = test_path;
	p.mode = storage_mode_sparse;
	boost::scoped_ptr<storage_interface> s(new default_storage(p));
	s->m_settings = &set;

	// allocate the files and create the directories
	storage_error se;
	s->initialize(se);
	if (se) print_error("initialize", 0, ec);

	// directories are not created up-front, unless they contain
	// an empty file (all of which are created up-front, along with
	// all required directories)
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage", combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));	

	// this directory and file is created up-front because it's an empty file
	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage", combine_path("folder2", "test3.tmp")))));	

	s->delete_files(se);
	if (se) print_error("delete_files", 0, ec);

	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));	
}

void test_check_files(std::string const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	boost::intrusive_ptr<torrent_info> info;

	error_code ec;
	const int piece_size = 16 * 1024;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", piece_size);
	fs.add_file("temp_storage/test2.tmp", piece_size * 2);
	fs.add_file("temp_storage/test3.tmp", piece_size);

	char piece0[piece_size];
	char piece2[piece_size];

	std::generate(piece0, piece0 + piece_size, std::rand);
	std::generate(piece2, piece2 + piece_size, std::rand);

	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, sha1_hash(0));
	t.set_hash(2, sha1_hash(0));
	t.set_hash(3, hasher(piece2, piece_size).final());

	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;

	std::ofstream f;
	f.open(combine_path(test_path, combine_path("temp_storage", "test1.tmp")).c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece0, sizeof(piece0));
	f.close();
	f.open(combine_path(test_path, combine_path("temp_storage", "test3.tmp")).c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece2, sizeof(piece2));
	f.close();

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = new torrent_info(&buf[0], buf.size(), ec);

	aux::session_settings set;
	file_pool fp;
	libtorrent::asio::io_service ios;
	disk_io_thread io(ios, NULL, NULL);
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop), NULL);
	storage_params p;
	p.files = &fs;
	p.path = test_path;
	p.pool = &fp;
	p.mode = storage_mode;

	boost::shared_ptr<void> dummy(new int);
	boost::intrusive_ptr<piece_manager> pm = new piece_manager(new default_storage(p), dummy, &fs);
	libtorrent::mutex lock;

	bool done = false;
	lazy_entry frd;
	io.async_check_fastresume(pm.get(), &frd, boost::bind(&on_check_resume_data, _1, &done));
	io.submit_jobs();
	ios.reset();
	run_until(ios, done);

	io.set_num_threads(0);
}

#ifdef TORRENT_NO_DEPRECATE
#define storage_mode_compact storage_mode_sparse
#endif

void run_test(std::string const& test_path, bool unbuffered)
{
	std::cerr << "\n=== " << test_path << " ===\n" << std::endl;

	boost::intrusive_ptr<torrent_info> info;

	{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 17);
	fs.add_file("temp_storage/test2.tmp", 612);
	fs.add_file("temp_storage/test3.tmp", 0);
	fs.add_file("temp_storage/test4.tmp", 0);
	fs.add_file("temp_storage/test5.tmp", 3253);
	fs.add_file("temp_storage/test6.tmp", 841);
	const int last_file_size = 4 * piece_size - fs.total_size();
	fs.add_file("temp_storage/test7.tmp", last_file_size);

	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, hasher(piece1, piece_size).final());
	t.set_hash(2, hasher(piece2, piece_size).final());
	
	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = new torrent_info(&buf[0], buf.size(), ec);
	std::cerr << "=== test 1 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact, unbuffered);

	// make sure the files have the correct size
	std::string base = combine_path(test_path, "temp_storage");
	fprintf(stderr, "base = \"%s\"\n", base.c_str());
	TEST_EQUAL(file_size(combine_path(base, "test1.tmp")), 17);
	TEST_EQUAL(file_size(combine_path(base, "test2.tmp")), 612);
	// these files should have been allocated since they are 0 sized
	TEST_CHECK(exists(combine_path(base, "test3.tmp")));
	TEST_CHECK(exists(combine_path(base, "test4.tmp")));
	TEST_EQUAL(file_size(combine_path(base, "test5.tmp")), 3253);
	TEST_EQUAL(file_size(combine_path(base, "test6.tmp")), 841);
	TEST_EQUAL(file_size(combine_path(base, "test7.tmp")), last_file_size - piece_size);
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	}

// ==============================================

	{
	error_code ec;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 3 * piece_size);
	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	TEST_CHECK(fs.file_path(*fs.begin()) == "temp_storage/test1.tmp");
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, hasher(piece1, piece_size).final());
	t.set_hash(2, hasher(piece2, piece_size).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = new torrent_info(&buf[0], buf.size(), ec);

	std::cerr << "=== test 3 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact, unbuffered);

	TEST_EQUAL(file_size(combine_path(test_path, "temp_storage/test1.tmp")), piece_size * 3);
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;

// ==============================================

	std::cerr << "=== test 4 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_allocate, unbuffered);

	std::cerr << file_size(combine_path(test_path, "temp_storage/test1.tmp")) << std::endl;
	TEST_EQUAL(file_size(combine_path(test_path, "temp_storage/test1.tmp")), 3 * piece_size);

	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;

	}

// ==============================================

	std::cerr << "=== test 5 ===" << std::endl;
	test_remove(test_path, unbuffered);

// ==============================================

	std::cerr << "=== test 6 ===" << std::endl;
	test_check_files(test_path, storage_mode_sparse, unbuffered);
	test_check_files(test_path, storage_mode_compact, unbuffered);
}

void test_fastresume(std::string const& test_path)
{
	error_code ec;
	std::cout << "\n\n=== test fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp1"), ec);
	if (ec) std::cerr << "create_directory '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp1/temporary").c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp1/temporary")));

	entry resume;
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		error_code ec;

		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p, ec);
				
		for (int i = 0; i < 10; ++i)
		{
			print_alerts(ses, "ses");
			test_sleep(1000);
			torrent_status s = h.status();
			if (s.progress == 1.0f) 
			{
				std::cout << "progress: 1.0f" << std::endl;
				break;
			}
		}
		// TODO: 3 don't use this deprecated function
		resume = h.write_resume_data();
		ses.remove_torrent(h, session::delete_files);
	}
	TEST_CHECK(!exists(combine_path(test_path, "tmp1/temporary")));
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif

	// make sure the fast resume check fails! since we removed the file
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_compact;
		std::vector<char> resume_buf;
		bencode(std::back_inserter(resume_buf), resume);
		p.resume_data = &resume_buf;
		torrent_handle h = ses.add_torrent(p, ec);
	
		std::auto_ptr<alert> a = ses.pop_alert();
		ptime end = time_now() + seconds(20);
		while (time_now() < end
			&& (a.get() == 0
				|| dynamic_cast<fastresume_rejected_alert*>(a.get()) == 0))
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
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
}

bool got_file_rename_alert(alert* a)
{
	return dynamic_cast<libtorrent::file_renamed_alert*>(a)
		|| dynamic_cast<libtorrent::file_rename_failed_alert*>(a);
}

void test_rename_file_in_fastresume(std::string const& test_path)
{
	error_code ec;
	std::cout << "\n\n=== test rename file in fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp2")
		<< "': " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp2"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp2/temporary").c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp2/temporary")));

	entry resume;
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);


		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p, ec);

		h.rename_file(0, "testing_renamed_files");
		std::cout << "renaming file" << std::endl;
		bool renamed = false;
		for (int i = 0; i < 5; ++i)
		{
			if (print_alerts(ses, "ses", true, true, true, &got_file_rename_alert)) renamed = true;
			test_sleep(1000);
			torrent_status s = h.status();
			if (s.state == torrent_status::downloading) break;
			if (s.state == torrent_status::seeding && renamed) break;
		}
		std::cout << "stop loop" << std::endl;
		torrent_status s = h.status();
		TEST_CHECK(s.state == torrent_status::seeding);
		// TODO: 3 don't use this deprecated function
		resume = h.write_resume_data();
		ses.remove_torrent(h);
	}
	TEST_CHECK(!exists(combine_path(test_path, "tmp2/temporary")));
	TEST_CHECK(exists(combine_path(test_path, "tmp2/testing_renamed_files")));
	TEST_CHECK(resume.dict().find("mapped_files") != resume.dict().end());
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif

	// make sure the fast resume check succeeds, even though we renamed the file
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_compact;
		std::vector<char> resume_buf;
		bencode(std::back_inserter(resume_buf), resume);
		p.resume_data = &resume_buf;
		torrent_handle h = ses.add_torrent(p, ec);

		for (int i = 0; i < 5; ++i)
		{
			print_alerts(ses, "ses");
			test_sleep(1000);
		}
		torrent_status stat = h.status();
		TEST_CHECK(stat.state == torrent_status::seeding);

		// TODO: 3 don't use this deprecated function
		resume = h.write_resume_data();
		ses.remove_torrent(h);
	}
	TEST_CHECK(resume.dict().find("mapped_files") != resume.dict().end());
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif
	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp2")
		<< "': " << ec.message() << std::endl;
}

int test_main()
{
	// initialize test pieces
	for (char* p = piece0, *end(piece0 + piece_size); p < end; ++p)
		*p = rand();
	for (char* p = piece1, *end(piece1 + piece_size); p < end; ++p)
		*p = rand();
	for (char* p = piece2, *end(piece2 + piece_size); p < end; ++p)
		*p = rand();

	std::vector<std::string> test_paths;
	char* env = std::getenv("TORRENT_TEST_PATHS");
	if (env == 0)
	{
		test_paths.push_back(current_working_directory());
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

	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&test_fastresume, _1));
	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&test_rename_file_in_fastresume, _1));

	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&run_test, _1, true));
	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&run_test, _1, false));

	file_storage fs;
	fs.set_piece_length(512);
	fs.add_file("temp_storage/test1.tmp", 17);
	fs.add_file("temp_storage/test2.tmp", 612);
	fs.add_file("temp_storage/test3.tmp", 0);
	fs.add_file("temp_storage/test4.tmp", 0);
	fs.add_file("temp_storage/test5.tmp", 3253);
	// size: 3882
	fs.add_file("temp_storage/test6.tmp", 841);
	// size: 4723

	peer_request rq = fs.map_file(0, 0, 10);
	TEST_EQUAL(rq.piece, 0);
	TEST_EQUAL(rq.start, 0);
	TEST_EQUAL(rq.length, 10);
	rq = fs.map_file(5, 0, 10);
	TEST_EQUAL(rq.piece, 7);
	TEST_EQUAL(rq.start, 298);
	TEST_EQUAL(rq.length, 10);
	rq = fs.map_file(5, 0, 1000);
	TEST_EQUAL(rq.piece, 7);
	TEST_EQUAL(rq.start, 298);
	TEST_EQUAL(rq.length, 841);


	return 0;
}

