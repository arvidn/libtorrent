#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"

#include <boost/utility.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/thread/mutex.hpp>

#include "test.hpp"

using namespace libtorrent;
using namespace boost::filesystem;

int test_main()
{
	const int piece_size = 16;
	const int half = piece_size / 2;
	torrent_info info;
	info.set_piece_size(piece_size);
	info.add_file("temp_storage/test1.tmp", 17);
	info.add_file("temp_storage/test2.tmp", 613);

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

	int num_pieces = (613 + 17 + piece_size - 1) / piece_size;
	TEST_CHECK(info.num_pieces() == num_pieces);
	
	storage s(info, initial_path());

	// write piece 1 (in slot 0)
	s.write(piece1, 0, 0, half);
	s.write(piece1 + half, 0, half, half);

	// verify piece 1
	char piece[piece_size];
	s.read(piece, 0, 0, piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1));
	
	// do the same with piece 0 and 2 (in slot 1 and 2)
	s.write(piece0, 1, 0, piece_size);
	s.write(piece2, 2, 0, piece_size);

	// verify piece 0 and 2
	s.read(piece, 1, 0, piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0));

	s.read(piece, 2, 0, piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2));
	
	// make sure the files have the correct size
	TEST_CHECK(file_size(initial_path() / "temp_storage" / "test1.tmp") == 17);
	TEST_CHECK(file_size(initial_path() / "temp_storage" / "test2.tmp") == 31);

	// make sure the piece_manager can identify the pieces
	piece_manager pm(info, initial_path());
	boost::mutex lock;
	libtorrent::detail::piece_checker_data d;

	std::vector<bool> pieces;
	pm.check_pieces(lock, d, pieces, true);

	pm.read(piece, 0, 0, piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0));

	pm.read(piece, 1, 0, piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1));

	pm.read(piece, 2, 0, piece_size);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2));
	
	return 0;
}

