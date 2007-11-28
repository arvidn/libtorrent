#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/bencode.hpp"
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

// proxy: 0=none, 1=socks4, 2=socks5, 3=socks5_pw 4=http 5=http_pw
void test_transfer(torrent_info torrent_file, int proxy)
{
	using namespace libtorrent;

	session ses;
	ses.set_severity_level(alert::debug);
	ses.listen_on(std::make_pair(49000, 50000));
	remove_all("./tmp1");

	char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	std::cerr << "  ==== TESTING " << test_name[proxy] << " proxy ====" << std::endl;
	
	if (proxy)
	{
		start_proxy(8002, proxy);
		proxy_settings ps;
		ps.hostname = "127.0.0.1";
		ps.port = 8002;
		ps.username = "testuser";
		ps.password = "testpass";
		ps.type = (proxy_settings::proxy_type)proxy;
		ses.set_web_seed_proxy(ps);
	}

	torrent_handle th = ses.add_torrent(torrent_file, "./tmp1");

	std::vector<announce_entry> empty;
	th.replace_trackers(empty);

	for (int i = 0; i < 30; ++i)
	{
		torrent_status s = th.status();
		std::cerr << s.progress << " " << (s.download_rate / 1000.f) << std::endl;
		std::auto_ptr<alert> a;
		a = ses.pop_alert();
		if (a.get())
			std::cerr << a->msg() << "\n";

		if (th.is_seed()) break;
		test_sleep(1000);
	}

	TEST_CHECK(th.is_seed());

	if (proxy) stop_proxy(8002);

	remove_all("./tmp1");
}

int test_main()
{
	using namespace libtorrent;
	using namespace boost::filesystem;

	boost::intrusive_ptr<torrent_info> torrent_file(new torrent_info);
	torrent_file->add_url_seed("http://127.0.0.1:8000/");

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

	add_files(*torrent_file, complete("."), "test_torrent");

	start_web_server(8000);

	file_pool fp;
	boost::scoped_ptr<storage_interface> s(default_storage_constructor(
		torrent_file, ".", fp));
	// calculate the hash for all pieces
	int num = torrent_file->num_pieces();
	std::vector<char> buf(torrent_file->piece_length());
	for (int i = 0; i < num; ++i)
	{
		s->read(&buf[0], i, 0, torrent_file->piece_size(i));
		hasher h(&buf[0], torrent_file->piece_size(i));
		torrent_file->set_hash(i, h.final());
	}
	
	// to calculate the info_hash
	entry te = torrent_file->create_torrent();


	for (int i = 0; i < 6; ++i)
		test_transfer(*torrent_file, i);


	
	stop_web_server(8000);
	remove_all("./test_torrent");
	return 0;
}

