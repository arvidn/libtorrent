#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/storage.hpp"
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/operations.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace boost::filesystem;
using namespace libtorrent;

void add_files(
	torrent_info& t
	, path const& p
	, path const& l)
{
	if (l.leaf()[0] == '.') return;
	path f(p / l);
	if (is_directory(f))
	{
		for (directory_iterator i(f), end; i != end; ++i)
			add_files(t, p, l / i->leaf());
	}
	else
	{
		std::cerr << "adding \"" << l.string() << "\"\n";
		t.add_file(l, file_size(f));
	}
}

void test_transfer()
{
	using namespace libtorrent;
	
	torrent_info torrent_file;
	torrent_file.add_url_seed("http://127.0.0.1/bravia_paint_ad_70sec_1280x720.mov");

	path full_path = "/Library/WebServer/Documents/bravia_paint_ad_70sec_1280x720.mov";
	add_files(torrent_file, full_path.branch_path(), full_path.leaf());

	file_pool fp;
	boost::scoped_ptr<storage_interface> s(default_storage_constructor(
		torrent_file, full_path.branch_path(), fp));
	// calculate the hash for all pieces
	int num = torrent_file.num_pieces();
	std::vector<char> buf(torrent_file.piece_length());
	for (int i = 0; i < num; ++i)
	{
		s->read(&buf[0], i, 0, torrent_file.piece_size(i));
		hasher h(&buf[0], torrent_file.piece_size(i));
		torrent_file.set_hash(i, h.final());
	}
	
	// to calculate the info_hash
	torrent_file.create_torrent();

	session ses;
	ses.listen_on(std::make_pair(49000, 50000));
	remove_all("./tmp1");
	torrent_handle th = ses.add_torrent(torrent_file, "./tmp1");

	for (int i = 0; i < 70; ++i)
	{
		torrent_status s = th.status();
		std::cerr << s.progress << " " << (s.download_rate / 1000.f) << "\r";
		std::auto_ptr<alert> a;
		a = ses.pop_alert();
		if (a.get())
			std::cerr << a->msg() << "\n";

		if (th.is_seed()) break;
		sleep(999);
	}

	TEST_CHECK(th.is_seed());
}

int test_main()
{
	using namespace libtorrent;
	using namespace boost::filesystem;

	test_transfer();

	remove_all("./tmp1");
	remove_all("./tmp2");

	return 0;
}

