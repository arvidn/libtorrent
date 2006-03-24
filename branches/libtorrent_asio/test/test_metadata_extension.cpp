#include "libtorrent/session.hpp"
#include <boost/thread.hpp>

#include "test.hpp"

void sleep(int sec)
{
	boost::xtime xt;
	boost::xtime_get(&xt, boost::TIME_UTC);
	xt.sec += sec;
	boost::thread::sleep(xt);
}

int test_main()
{
	using namespace libtorrent;
	using namespace boost::filesystem;

	session ses1;
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49000, 50000));

	char const* tracker_url = "http://non-existant-name.com/announce";
	
	torrent_info t;
	t.add_file(path("test"), 42);
	t.set_piece_size(256 * 1024);
	t.add_tracker(tracker_url);
	// calculate the hash for all pieces
	int num = t.num_pieces();
	for (int i = 0; i < num; ++i)
	{
		t.set_hash(i, sha1_hash());
	}

	t.create_torrent();
	
	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	torrent_handle tor1 = ses1.add_torrent(t, "./tmp1");
	torrent_handle tor2 = ses2.add_torrent(tracker_url
		, t.info_hash(), "./tmp2");

	sleep(3);

	tor1.connect_peer(tcp::endpoint(ses2.listen_port(), "127.0.0.1"));	

	sleep(5);

	TEST_CHECK(tor2.has_metadata());
	
	return 0;
}

