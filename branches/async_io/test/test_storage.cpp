#include "libtorrent/storage.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#include <boost/utility.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/thread/mutex.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
using namespace boost::filesystem;

const int piece_size = 16;

void on_read_piece(int ret, disk_io_job const& j, char const* data, int size)
{
	TEST_CHECK(ret == size);
	TEST_CHECK(std::equal(j.buffer, j.buffer + ret, data));
}

void run_storage_tests(torrent_info& info, bool compact_allocation = true)
{
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

	info.set_hash(0, hasher(piece0, piece_size).final());
	info.set_hash(1, hasher(piece1, piece_size).final());
	info.set_hash(2, hasher(piece2, piece_size).final());
	
	info.create_torrent();

	create_directory(initial_path() / "temp_storage");

	int num_pieces = (1 + 612 + 17 + piece_size - 1) / piece_size;
	TEST_CHECK(info.num_pieces() == num_pieces);

	char piece[piece_size];

	{ // avoid having two storages use the same files	
	file_pool fp;
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(info, initial_path(), fp));

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
	disk_io_thread io;
	boost::shared_ptr<int> dummy(new int);
	boost::intrusive_ptr<piece_manager> pm = new piece_manager(dummy, info
		, initial_path(), fp, io, default_storage_constructor);
	boost::mutex lock;
	libtorrent::aux::piece_checker_data d;

	std::vector<bool> pieces;
	num_pieces = 0;
	TEST_CHECK(pm->check_fastresume(d, pieces, num_pieces
		, compact_allocation) == false);
	bool finished = false;
	float progress;
	num_pieces = 0;
	boost::recursive_mutex mutex;
	while (!finished)
		boost::tie(finished, progress) = pm->check_files(pieces, num_pieces, mutex);

	TEST_CHECK(num_pieces == std::count(pieces.begin(), pieces.end()
		, true));

	TEST_CHECK(exists("temp_storage"));
	pm->async_move_storage("temp_storage2");
	test_sleep(2000);
	TEST_CHECK(!exists("temp_storage"));
	TEST_CHECK(exists("temp_storage2/temp_storage"));
	pm->async_move_storage(".");
	test_sleep(2000);
	TEST_CHECK(!exists("temp_storage2/temp_storage"));	
	remove_all("temp_storage2");

	peer_request r;
	r.piece = 0;
	r.start = 0;
	r.length = piece_size;
	pm->async_read(r, bind(&on_read_piece, _1, _2, piece0, piece_size));
	r.piece = 1;
	pm->async_read(r, bind(&on_read_piece, _1, _2, piece1, piece_size));
	r.piece = 2;
	pm->async_read(r, bind(&on_read_piece, _1, _2, piece2, piece_size));
	pm->async_release_files();

	}
}

int test_main()
{
	torrent_info info;
	info.set_piece_size(piece_size);
	info.add_file("temp_storage/test1.tmp", 17);
	info.add_file("temp_storage/test2.tmp", 612);
	info.add_file("temp_storage/test3.tmp", 0);
	info.add_file("temp_storage/test4.tmp", 0);
	info.add_file("temp_storage/test5.tmp", 1);

	run_storage_tests(info);

	// make sure the files have the correct size
	TEST_CHECK(file_size(initial_path() / "temp_storage" / "test1.tmp") == 17);
	TEST_CHECK(file_size(initial_path() / "temp_storage" / "test2.tmp") == 31);
	TEST_CHECK(exists("temp_storage/test3.tmp"));
	TEST_CHECK(exists("temp_storage/test4.tmp"));
	remove_all(initial_path() / "temp_storage");

	info = torrent_info();
	info.set_piece_size(piece_size);
	info.add_file("temp_storage/test1.tmp", 17 + 612 + 1);

	run_storage_tests(info);

	// 48 = piece_size * 3
	TEST_CHECK(file_size(initial_path() / "temp_storage" / "test1.tmp") == 48);
	remove_all(initial_path() / "temp_storage");

	// make sure full allocation mode actually allocates the files
	// and creates the directories
	run_storage_tests(info, false);

	std::cerr << file_size(initial_path() / "temp_storage" / "test1.tmp") << std::endl;
	TEST_CHECK(file_size(initial_path() / "temp_storage" / "test1.tmp") == 17 + 612 + 1);

	remove_all(initial_path() / "temp_storage");

	return 0;
}

