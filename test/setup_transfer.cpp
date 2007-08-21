#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include <fstream>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "test.hpp"

using boost::filesystem::remove_all;
using boost::filesystem::create_directory;

void test_sleep(int millisec)
{
	boost::xtime xt;
	boost::xtime_get(&xt, boost::TIME_UTC);
	boost::uint64_t nanosec = (millisec % 1000) * 1000000 + xt.nsec;
	int sec = millisec / 1000;
	if (nanosec > 1000000000)
	{
		nanosec -= 1000000000;
		sec++;
	}
	xt.nsec = nanosec;
	xt.sec += sec;
	boost::thread::sleep(xt);
}

using namespace libtorrent;

boost::tuple<torrent_handle, torrent_handle, torrent_handle>
setup_transfer(session* ses1, session* ses2, session* ses3
	, bool clear_files, bool use_metadata_transfer)
{
	using namespace boost::filesystem;

	assert(ses1);
	assert(ses2);

	assert(ses1->id() != ses2->id());
	if (ses3)
		assert(ses3->id() != ses2->id());

	char const* tracker_url = "http://non-existent-name.com/announce";
	
	torrent_info t;
	int total_size = 2 * 1024 * 1024;
	t.add_file(path("temporary"), total_size);
	t.set_piece_size(16 * 1024);
	t.add_tracker(tracker_url);

	std::vector<char> piece(16 * 1024);
	for (int i = 0; i < int(piece.size()); ++i)
		piece[i] = (i % 26) + 'A';
	
	// calculate the hash for all pieces
	int num = t.num_pieces();
	sha1_hash ph = hasher(&piece[0], piece.size()).final();
	for (int i = 0; i < num; ++i)
		t.set_hash(i, ph);
	
	create_directory("./tmp1");
	std::ofstream file("./tmp1/temporary");
	while (total_size > 0)
	{
		file.write(&piece[0], (std::min)(int(piece.size()), total_size));
		total_size -= piece.size();
	}
	file.close();
	if (clear_files) remove_all("./tmp2/temporary");
	
	t.create_torrent();
	std::cerr << "generated torrent: " << t.info_hash() << std::endl;

	ses1->set_severity_level(alert::debug);
	ses2->set_severity_level(alert::debug);
	
	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	sha1_hash info_hash = t.info_hash();
	torrent_handle tor1 = ses1->add_torrent(t, "./tmp1");
	torrent_handle tor2;
	torrent_handle tor3;
	if (ses3) tor3 = ses3->add_torrent(t, "./tmp3");

  	if (use_metadata_transfer)
		tor2 = ses2->add_torrent(tracker_url
		, t.info_hash(), 0, "./tmp2");
	else
		tor2 = ses2->add_torrent(t, "./tmp2");

	assert(ses1->get_torrents().size() == 1);
	assert(ses2->get_torrents().size() == 1);

	test_sleep(100);

	std::cerr << "connecting peer\n";
	tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1")
		, ses2->listen_port()));

	if (ses3)
	{
		// give the other peers some time to get an initial
		// set of pieces before they start sharing with each-other
		tor3.connect_peer(tcp::endpoint(
			address::from_string("127.0.0.1")
			, ses2->listen_port()));
		tor3.connect_peer(tcp::endpoint(
			address::from_string("127.0.0.1")
			, ses1->listen_port()));
	}

	return boost::make_tuple(tor1, tor2, tor3);
}

