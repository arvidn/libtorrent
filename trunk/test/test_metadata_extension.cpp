#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/operations.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/extensions/metadata_transfer.hpp"

using boost::filesystem::remove_all;
using boost::tuples::ignore;

void test_transfer(bool clear_files = true, bool disconnect = false)
{
	using namespace libtorrent;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48000, 49000));
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49000, 50000));
	ses1.add_extension(&create_metadata_plugin);
	ses2.add_extension(&create_metadata_plugin);
	torrent_handle tor1;
	torrent_handle tor2;

	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0, clear_files);	

	for (int i = 0; i < 50; ++i)
	{
		// make sure this function can be called on
		// torrents without metadata
		if (!disconnect) tor2.status();
		std::auto_ptr<alert> a;
		a = ses1.pop_alert();
		if (a.get())
			std::cerr << "ses1: " << a->msg() << "\n";

		a = ses2.pop_alert();
		if (a.get())
			std::cerr << "ses2: " << a->msg() << "\n";

		if (disconnect && tor2.is_valid()) ses2.remove_torrent(tor2);
		if (!disconnect && tor2.has_metadata()) break;
		test_sleep(100);
	}

	if (disconnect) return;

	TEST_CHECK(tor2.has_metadata());
	std::cerr << "waiting for transfer to complete\n";

	for (int i = 0; i < 50; ++i)
	{
		tor2.status();
		if (tor2.is_seed()) break;
		test_sleep(100);
	}

	TEST_CHECK(tor2.is_seed());
	if (tor2.is_seed()) std::cerr << "done\n";
}

int test_main()
{
	using namespace libtorrent;
	using namespace boost::filesystem;

	// test to disconnect one client prematurely
	test_transfer(true, true);
	
	// test where one has data and one doesn't
	test_transfer(true);

	// test where both have data (to trigger the file check)
	test_transfer(false);

	remove_all("./tmp1");
	remove_all("./tmp2");

	return 0;
}

