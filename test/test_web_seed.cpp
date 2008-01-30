#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/storage.hpp"
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/operations.hpp>
#include <fstream>

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
void start_web_server()
{
	std::ofstream f("./lighty_config");
	f << "server.modules = (\"mod_access\")\n"
		"server.document-root = \"" << initial_path().string() << "\"\n"
		"server.range-requests = \"enable\"\n"
		"server.port = 8000\n"
		"server.pid-file = \"./lighty.pid\"\n";
	f.close();
	
	system("lighttpd -f lighty_config &");
}

void stop_web_server()
{
	system("kill `cat ./lighty.pid`");
}

void test_transfer()
{
	using namespace libtorrent;
	
	torrent_info torrent_file;

	torrent_file.add_url_seed("http://127.0.0.1:8000/");

	create_directory("test_torrent");
	char random_data[300000];
	std::srand(std::time(0));
	std::generate(random_data, random_data + sizeof(random_data), &std::rand);
	std::ofstream("./test_torrent/test1").write(random_data, 35);
	std::ofstream("./test_torrent/test2").write(random_data, 16536 - 35);
	std::ofstream("./test_torrent/test3").write(random_data, 16536);
	std::ofstream("./test_torrent/test4").write(random_data, 17);
	std::ofstream("./test_torrent/test5").write(random_data, 16536);
	std::ofstream("./test_torrent/test6").write(random_data, 300000);
	std::ofstream("./test_torrent/test7").write(random_data, 300000);

	add_files(torrent_file, complete("."), "test_torrent");

	start_web_server();

	file_pool fp;
	storage st(torrent_file, ".", fp);
	// calculate the hash for all pieces
	int num = torrent_file.num_pieces();
	std::vector<char> buf(torrent_file.piece_length());
	for (int i = 0; i < num; ++i)
	{
		st.read(&buf[0], i, 0, torrent_file.piece_size(i));
		hasher h(&buf[0], torrent_file.piece_size(i));
		torrent_file.set_hash(i, h.final());
	}
	
	// to calculate the info_hash
	torrent_file.create_torrent();

	session ses;
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

	remove_all("./test_torrent");
	stop_web_server();
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

