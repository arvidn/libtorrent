#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>

#include "test.hpp"

void sleep(int msec)
{
	boost::xtime xt;
	boost::xtime_get(&xt, boost::TIME_UTC);
	xt.nsec += msec * 1000000;
	boost::thread::sleep(xt);
}

using namespace libtorrent;

boost::tuple<torrent_handle, torrent_handle> setup_transfer(
	session& ses1, session& ses2, bool clear_files)
{
	using namespace boost::filesystem;

	char const* tracker_url = "http://non-existent-name.com/announce";
	
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
	if (clear_files) remove_all("./tmp2/temporary");
	
	t.create_torrent();


	ses1.set_severity_level(alert::debug);
	ses2.set_severity_level(alert::debug);
	
	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	torrent_handle tor1 = ses1.add_torrent(t, "./tmp1");
	torrent_handle tor2 = ses2.add_torrent(tracker_url
		, t.info_hash(), "./tmp2");

	std::cerr << "connecting peer\n";
	tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1")
		, ses2.listen_port()));

	return boost::make_tuple(tor1, tor2);
}

