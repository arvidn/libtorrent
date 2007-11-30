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
	std::cerr << "on_read_piece piece: " << j.piece << std::endl;
	TEST_CHECK(ret == size);
	TEST_CHECK(std::equal(j.buffer, j.buffer + ret, data));
}

void run_storage_tests(boost::intrusive_ptr<torrent_info> info
	, path const& test_path
	, libtorrent::storage_mode_t storage_mode)
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

	info->set_hash(0, hasher(piece0, piece_size).final());
	info->set_hash(1, hasher(piece1, piece_size).final());
	info->set_hash(2, hasher(piece2, piece_size).final());
	
	info->create_torrent();

	create_directory(test_path / "temp_storage");

	int num_pieces = (1 + 612 + 17 + piece_size - 1) / piece_size;
	TEST_CHECK(info->num_pieces() == num_pieces);

	char piece[piece_size];

	{ // avoid having two storages use the same files	
	file_pool fp;
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(info, test_path, fp));

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
		, test_path, fp, io, default_storage_constructor);
	boost::mutex lock;
	libtorrent::aux::piece_checker_data d;

	std::vector<bool> pieces;
	num_pieces = 0;
	std::string error_msg;
	TEST_CHECK(pm->check_fastresume(d, pieces, num_pieces
		, storage_mode, error_msg) == false);
	bool finished = false;
	float progress;
	num_pieces = 0;
	boost::recursive_mutex mutex;
	while (!finished)
		boost::tie(finished, progress) = pm->check_files(pieces, num_pieces, mutex);

	TEST_CHECK(num_pieces == std::count(pieces.begin(), pieces.end()
		, true));


	boost::function<void(int, disk_io_job const&)> none;
	TEST_CHECK(exists(test_path / "temp_storage"));
	pm->async_move_storage(test_path / "temp_storage2", none);
	test_sleep(2000);
	TEST_CHECK(!exists(test_path / "temp_storage"));
	TEST_CHECK(exists(test_path / "temp_storage2/temp_storage"));
	pm->async_move_storage(test_path, none);
	test_sleep(2000);
	TEST_CHECK(!exists(test_path / "temp_storage2/temp_storage"));	
	remove_all(test_path / "temp_storage2");

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

	}
}

void test_remove(path const& test_path)
{
	boost::intrusive_ptr<torrent_info> info(new torrent_info());
	info->set_piece_size(4);
	info->add_file("temp_storage/test1.tmp", 8);
	info->add_file("temp_storage/folder1/test2.tmp", 8);
	info->add_file("temp_storage/folder2/test3.tmp", 0);
	info->add_file("temp_storage/_folder3/test4.tmp", 0);
	info->add_file("temp_storage/_folder3/subfolder/test5.tmp", 8);

	char buf[4] = {0, 0, 0, 0};
	sha1_hash h = hasher(buf, 4).final();
	for (int i = 0; i < 6; ++i) info->set_hash(i, h);
	
	info->create_torrent();

	file_pool fp;
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(info, test_path, fp));

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

	boost::intrusive_ptr<torrent_info> info(new torrent_info());
	info->set_piece_size(piece_size);
	info->add_file("temp_storage/test1.tmp", 17);
	info->add_file("temp_storage/test2.tmp", 612);
	info->add_file("temp_storage/test3.tmp", 0);
	info->add_file("temp_storage/test4.tmp", 0);
	info->add_file("temp_storage/test5.tmp", 1);

	std::cerr << "=== test 1 ===" << std::endl;

	run_storage_tests(info, test_path, storage_mode_compact);

	// make sure the files have the correct size
	std::cerr << file_size(test_path / "temp_storage" / "test1.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 17);
	std::cerr << file_size(test_path / "temp_storage" / "test2.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test2.tmp") == 31);
	TEST_CHECK(exists(test_path / "temp_storage/test3.tmp"));
	TEST_CHECK(exists(test_path / "temp_storage/test4.tmp"));
	remove_all(test_path / "temp_storage");

// ==============================================

	// make sure remap_files works
	std::vector<file_entry> map;
	file_entry fe;
	fe.path = "temp_storage/test.tmp";
	fe.size = 17;
	fe.file_base = 612 + 1;
	map.push_back(fe);
	fe.path = "temp_storage/test.tmp";
	fe.size = 612 + 1;
	fe.file_base = 0;
	map.push_back(fe);

	bool ret = info->remap_files(map);
	TEST_CHECK(ret);

	std::cerr << "=== test 2 ===" << std::endl;

	run_storage_tests(info, test_path, storage_mode_compact);

	std::cerr << file_size(test_path / "temp_storage" / "test.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test.tmp") == 17 + 612 + 1);

	remove_all(test_path / "temp_storage");

// ==============================================
	
	info = new torrent_info();
	info->set_piece_size(piece_size);
	info->add_file("temp_storage/test1.tmp", 17 + 612 + 1);

	std::cerr << "=== test 3 ===" << std::endl;

	run_storage_tests(info, test_path, storage_mode_compact);

	// 48 = piece_size * 3
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 48);
	remove_all(test_path / "temp_storage");

// ==============================================

	std::cerr << "=== test 4 ===" << std::endl;

	run_storage_tests(info, test_path, storage_mode_allocate);

	std::cerr << file_size(test_path / "temp_storage" / "test1.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 17 + 612 + 1);

	remove_all(test_path / "temp_storage");


// ==============================================

	std::cerr << "=== test 5 ===" << std::endl;
	test_remove(test_path);

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

	return 0;
}

