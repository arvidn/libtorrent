#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/thread.hpp"
#include <boost/tuple/tuple.hpp>
#include <iostream>

#include "test.hpp"
#include "setup_transfer.hpp"

void test_swarm()
{
	using namespace libtorrent;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48010, 49000), "0.0.0.0", 0);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49010, 50000), "0.0.0.0", 0);
	session ses3(fingerprint("LT", 0, 1, 0, 0), std::make_pair(50010, 51000), "0.0.0.0", 0);

	ses1.set_alert_mask(alert::all_categories);
	ses2.set_alert_mask(alert::all_categories);
	ses3.set_alert_mask(alert::all_categories);

	// this is to avoid everything finish from a single peer
	// immediately. To make the swarm actually connect all
	// three peers before finishing.
	float rate_limit = 100000;

	session_settings settings;
	settings.allow_multiple_connections_per_ip = true;
	settings.ignore_limits_on_local_network = false;
	settings.choking_algorithm = session_settings::auto_expand_choker;
	settings.upload_rate_limit = rate_limit;
	settings.unchoke_slots_limit = 1;
	ses1.set_settings(settings);

	settings.upload_rate_limit = rate_limit / 10;
	settings.download_rate_limit = rate_limit / 5;
	settings.unchoke_slots_limit = 0;
	ses2.set_settings(settings);
	ses3.set_settings(settings);

#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.out_enc_policy = pe_settings::forced;
	pes.in_enc_policy = pe_settings::forced;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
	ses3.set_pe_settings(pes);
#endif

	torrent_handle tor1;
	torrent_handle tor2;
	torrent_handle tor3;

	boost::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, true, false, true, "_unchoke");	

	session_status st = ses1.status();
	TEST_CHECK(st.allowed_upload_slots == 1);
	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");
		print_alerts(ses3, "ses3");

		st = ses1.status();
		std::cerr << st.allowed_upload_slots << " ";
		if (st.allowed_upload_slots >= 2) break;

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();
		torrent_status st3 = tor3.status();

		std::cerr
			<< "\033[33m" << int(st1.upload_payload_rate / 1000.f) << "kB/s "
			<< st1.num_peers << " " << st.allowed_upload_slots << ": "
			<< "\033[32m" << int(st2.download_payload_rate / 1000.f) << "kB/s "
			<< "\033[31m" << int(st2.upload_payload_rate / 1000.f) << "kB/s "
			<< "\033[0m" << int(st2.progress * 100) << "% "
			<< st2.num_peers << " - "
			<< "\033[32m" << int(st3.download_payload_rate / 1000.f) << "kB/s "
			<< "\033[31m" << int(st3.upload_payload_rate / 1000.f) << "kB/s "
			<< "\033[0m" << int(st3.progress * 100) << "% "
			<< st3.num_peers
			<< std::endl;

		test_sleep(1000);
	}

	TEST_CHECK(st.allowed_upload_slots >= 2);

	// make sure the files are deleted
	ses1.remove_torrent(tor1, session::delete_files);
	ses2.remove_torrent(tor2, session::delete_files);
	ses3.remove_torrent(tor3, session::delete_files);
}

int test_main()
{
	using namespace libtorrent;

	// in case the previous run was t r catch (std::exception&) {}erminated
	error_code ec;
	remove_all("./tmp1_unchoke", ec);
	remove_all("./tmp2_unchoke", ec);
	remove_all("./tmp3_unchoke", ec);

	test_swarm();
	
	test_sleep(2000);
	TEST_CHECK(!exists("./tmp1_unchoke/temporary"));
	TEST_CHECK(!exists("./tmp2_unchoke/temporary"));
	TEST_CHECK(!exists("./tmp3_unchoke/temporary"));

	remove_all("./tmp1_unchoke", ec);
	remove_all("./tmp2_unchoke", ec);
	remove_all("./tmp3_unchoke", ec);

	return 0;
}

