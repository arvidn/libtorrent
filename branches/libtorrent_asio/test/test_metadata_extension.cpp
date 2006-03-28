#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include <boost/thread.hpp>

#include "test.hpp"

void sleep(int msec)
{
	boost::xtime xt;
	boost::xtime_get(&xt, boost::TIME_UTC);
	xt.nsec += msec * 1000000;
	boost::thread::sleep(xt);
}

void test_transfer(char const* tracker_url, libtorrent::torrent_info const& t)
{
	using namespace libtorrent;

	session ses1;
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49000, 50000));
	
	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	torrent_handle tor1 = ses1.add_torrent(t, "./tmp1");
	torrent_handle tor2 = ses2.add_torrent(tracker_url
		, t.info_hash(), "./tmp2");

	std::cerr << "waiting for file check to complete\n";
	
	// wait for 5 seconds or until the torrent is in a state
	// were it can accept connections
	for (int i = 0; i < 50; ++i)
	{
		torrent_status st = tor1.status();
		if (st.state != torrent_status::queued_for_checking
			&&st.state != torrent_status::checking_files)
			break;
		sleep(100);
	}

	std::cerr << "connecting peer\n";
	tor1.connect_peer(tcp::endpoint(ses2.listen_port(), "127.0.0.1"));	

	for (int i = 0; i < 50; ++i)
	{
		// make sure this function can be called on
		// torrents without metadata
		tor2.status();
		if (tor2.has_metadata()) break;
		sleep(100);
	}

	std::cerr << "metadata received. waiting for transfer to complete\n";
	TEST_CHECK(tor2.has_metadata());

	for (int i = 0; i < 50; ++i)
	{
		tor2.status();
		if (tor2.is_seed()) break;
		sleep(100);
	}

	std::cerr << "done\n";
	TEST_CHECK(tor2.is_seed());
}

int test_main()
{
	using namespace libtorrent;
	using namespace boost::filesystem;

	char const* tracker_url = "http://non-existant-name.com/announce";
	
	torrent_info t;
	t.add_file(path("temporary"), 42);
	t.set_piece_size(256 * 1024);
	t.add_tracker(tracker_url);

	std::vector<char> piece(42);
	std::fill(piece.begin(), piece.end(), 0xfe);
	
	// calculate the hash for all pieces
	int num = t.num_pieces();
	for (int i = 0; i < num; ++i)
	{
		t.set_hash(i, hasher(&piece[0], piece.size()).final());
	}
	
	create_directory("./tmp1");
	std::ofstream file("./tmp1/temporary");
	file.write(&piece[0], piece.size());
	file.close();
	remove_all("./tmp2/temporary");
	
	t.create_torrent();
	
	// test where one has data and one doesn't
	test_transfer(tracker_url, t);

	// test where both have data (to trigger the file check)
	test_transfer(tracker_url, t);

	return 0;
}

