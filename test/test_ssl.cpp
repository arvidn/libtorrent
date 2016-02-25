/*

Copyright (c) 2013, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/torrent_info.hpp"

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#include <fstream>
#include <iostream>
#include <boost/asio/connect.hpp>

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/error.hpp> // for asio::error::get_ssl_category()
#include <boost/asio/ssl.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace libtorrent;
using boost::tuples::ignore;

int const alert_mask = alert::all_categories
& ~alert::progress_notification
& ~alert::stats_notification;

struct test_config_t
{
	char const* name;
	bool use_ssl_ports;
	bool seed_has_cert;
	bool downloader_has_cert;
	bool downloader_has_ssl_listen_port;
	bool expected_to_complete;
	int peer_errors;
	int ssl_disconnects;
};

test_config_t test_config[] =
{
	// name                                                              sslport sd-cert dl-cert dl-port expect peer-error ssl-disconn
	{"nobody has a cert (connect to regular port)",                      false,  false,  false,  true,   false, 0, 1},
	{"nobody has a cert (connect to ssl port)",                          true,   false,  false,  true,   false, 1, 1},
	{"seed has a cert, but not downloader (connect to regular port)",    false,  true,   false,  true,   false, 0, 1},
	{"seed has a cert, but not downloader (connect to ssl port)",        true,   true,   false,  true,   false, 1, 1},
	{"downloader has a cert, but not seed (connect to regular port)",    false,  false,  true,   true,   false, 0, 1},
	{"downloader has a cert, but not seed (connect to ssl port)",        true,   false,  true,   true,   false, 1, 1},
	{"both downloader and seed has a cert (connect to regular port)",    false,  true,   true,   true,   false, 0, 1},
	{"both downloader and seed has a cert (connect to ssl port)",        true,   true,   true,   true,   true,  0, 0},
	// there is a disconnect (or failed connection attempt), that's not a peer
	// error though, so both counters stay 0
	{"both downloader and seed has a cert (downloader has no SSL port)", true,   true,   true,   false,  false, 0, 0},
};

int peer_disconnects = 0;
int peer_errors = 0;
int ssl_peer_disconnects = 0;

bool on_alert(alert const* a)
{
	if (peer_disconnected_alert const* e = alert_cast<peer_disconnected_alert>(a))
	{
		++peer_disconnects;
		if (e->error.category() == boost::asio::error::get_ssl_category())
			++ssl_peer_disconnects;
	}

	if (peer_error_alert const* e = alert_cast<peer_error_alert>(a))
	{
		++peer_disconnects;
		++peer_errors;

		if (e->error.category() == boost::asio::error::get_ssl_category())
			++ssl_peer_disconnects;
	}
	return false;
}

void test_ssl(int test_idx, bool use_utp)
{
	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	test_config_t const& test = test_config[test_idx];

	fprintf(stderr, "\n%s TEST: %s Protocol: %s\n\n", time_now_string(), test.name, use_utp ? "uTP": "TCP");

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_ssl", ec);
	remove_all("tmp2_ssl", ec);

	int ssl_port = 1024 + rand() % 50000;
	settings_pack sett;
	sett.set_int(settings_pack::alert_mask, alert_mask);
	sett.set_int(settings_pack::max_retry_port_bind, 100);
	sett.set_str(settings_pack::listen_interfaces, "0.0.0.0:48075");
	sett.set_bool(settings_pack::enable_incoming_utp, use_utp);
	sett.set_bool(settings_pack::enable_outgoing_utp, use_utp);
	sett.set_bool(settings_pack::enable_incoming_tcp, !use_utp);
	sett.set_bool(settings_pack::enable_outgoing_tcp, !use_utp);
	sett.set_bool(settings_pack::enable_dht, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);
	// if a peer fails once, don't try it again
	sett.set_int(settings_pack::max_failcount, 1);
	sett.set_int(settings_pack::ssl_listen, ssl_port);

	libtorrent::session ses1(sett, 0);

	if (test.downloader_has_ssl_listen_port)
		sett.set_int(settings_pack::ssl_listen, ssl_port + 20);
	else
		sett.set_int(settings_pack::ssl_listen, 0);

	libtorrent::session ses2(sett, 0);

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_ssl", ec);
	std::ofstream file("tmp1_ssl/temporary");
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary"
		, 16 * 1024, 13, false, combine_path("..", combine_path("ssl", "root_ca_cert.pem")));
	file.close();

	add_torrent_params addp;
	addp.save_path = "tmp1_ssl";
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	peer_disconnects = 0;
	ssl_peer_disconnects = 0;
	peer_errors = 0;

	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, false, "_ssl", 16 * 1024, &t, false, &addp, true);

	if (test.seed_has_cert)
	{
		tor1.set_ssl_certificate(
			combine_path("..", combine_path("ssl", "peer_certificate.pem"))
			, combine_path("..", combine_path("ssl", "peer_private_key.pem"))
			, combine_path("..", combine_path("ssl", "dhparams.pem"))
			, "test");
	}

	if (test.downloader_has_cert)
	{
		tor2.set_ssl_certificate(
			combine_path("..", combine_path("ssl", "peer_certificate.pem"))
			, combine_path("..", combine_path("ssl", "peer_private_key.pem"))
			, combine_path("..", combine_path("ssl", "dhparams.pem"))
			, "test");
	}

	// make sure they've taken effect
	if (test.downloader_has_cert || test.seed_has_cert)
	{
		// this will cause a round-trip to the main thread, and make sure the
		// previous async. calls have completed
		ses1.listen_port();
		ses2.listen_port();
	}

	wait_for_alert(ses1, torrent_finished_alert::alert_type, "ses1");
	wait_for_downloading(ses2, "ses2");

	// connect the peers after setting the certificates
	int port = 0;
	if (test.use_ssl_ports)
		if (test.downloader_has_ssl_listen_port)
			port = ses2.ssl_listen_port();
		else
			port = 13512;
	else
		port = ses2.listen_port();

	fprintf(stderr, "\n\n%s: ses1: connecting peer port: %d\n\n\n"
		, time_now_string(), port);
	tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
		, port));

#ifdef TORRENT_USE_VALGRIND
	const int timeout = 100;
#else
	const int timeout = 40;
#endif
	for (int i = 0; i < timeout; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			std::cerr << time_now_string() << " "
				<< "\033[32m" << int(st1.download_payload_rate / 1000.f) << "kB/s "
				<< "\033[33m" << int(st1.upload_payload_rate / 1000.f) << "kB/s "
				<< "\033[0m" << int(st1.progress * 100) << "% "
				<< st1.num_peers
				<< ": "
				<< "\033[32m" << int(st2.download_payload_rate / 1000.f) << "kB/s "
				<< "\033[31m" << int(st2.upload_payload_rate / 1000.f) << "kB/s "
				<< "\033[0m" << int(st2.progress * 100) << "% "
				<< st2.num_peers
				<< " cc: " << st2.connect_candidates
				<< std::endl;
		}

		if (peer_disconnects >= 2)
		{
			fprintf(stderr, "too many disconnects (%d), breaking\n", peer_disconnects);
			break;
		}

		if (st2.is_finished) break;

		if (st2.state != torrent_status::downloading)
		{
			static char const* state_str[] =
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data);

		test_sleep(100);
	}

	fprintf(stderr, "peer_errors: %d expected_errors: %d\n"
		, peer_errors, test.peer_errors);
	TEST_EQUAL(peer_errors > 0, test.peer_errors > 0);

	fprintf(stderr, "ssl_disconnects: %d  expected: %d\n", ssl_peer_disconnects, test.ssl_disconnects);
	TEST_EQUAL(ssl_peer_disconnects > 0, test.ssl_disconnects > 0);

	fprintf(stderr, "%s: EXPECT: %s\n", time_now_string(), test.expected_to_complete ? "SUCCEESS" : "FAILURE");
	fprintf(stderr, "%s: RESULT: %s\n", time_now_string(), tor2.status().is_seeding ? "SUCCEESS" : "FAILURE");
	TEST_EQUAL(tor2.status().is_seeding, test.expected_to_complete);

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
}

std::string password_callback(int length, boost::asio::ssl::context::password_purpose p
	, std::string pw)
{
	if (p != boost::asio::ssl::context::for_reading) return "";
	return pw;
}

struct attack_t
{
	// flags controlling the connection attempt
	boost::uint32_t flags;
	// whether or not we expect to be able to connect
	bool expect;
};

enum attack_flags_t
{
	valid_certificate = 1,
	invalid_certificate = 2,
	valid_sni_hash = 4,
	invalid_sni_hash = 8,
	valid_bittorrent_hash = 16,
};

attack_t attacks[] =
{
	// positive test
	{ valid_certificate | valid_sni_hash | valid_bittorrent_hash, true},

	// SNI
	{ valid_certificate | invalid_sni_hash | valid_bittorrent_hash, false},
	{ valid_certificate | valid_bittorrent_hash, false},

	// certificate
	{ valid_sni_hash | valid_bittorrent_hash, false},
	{ invalid_certificate | valid_sni_hash | valid_bittorrent_hash, false},

	// bittorrent hash
	{ valid_certificate | valid_sni_hash, false},
};

const int num_attacks = sizeof(attacks)/sizeof(attacks[0]);

bool try_connect(libtorrent::session& ses1, int port
	, boost::shared_ptr<torrent_info> const& t, boost::uint32_t flags)
{
	using boost::asio::ssl::context;

	fprintf(stderr, "\nMALICIOUS PEER TEST: ");
	if (flags & invalid_certificate) fprintf(stderr, "invalid-certificate ");
	else if (flags & valid_certificate) fprintf(stderr, "valid-certificate ");
	else fprintf(stderr, "no-certificate ");

	if (flags & invalid_sni_hash) fprintf(stderr, "invalid-SNI-hash ");
	else if (flags & valid_sni_hash) fprintf(stderr, "valid-SNI-hash ");
	else fprintf(stderr, "no-SNI-hash ");

	if (flags & valid_bittorrent_hash) fprintf(stderr, "valid-bittorrent-hash ");
	else fprintf(stderr, "invalid-bittorrent-hash ");

	fprintf(stderr, " port: %d\n", port);

	error_code ec;
	boost::asio::io_service ios;

	// create the SSL context for this torrent. We need to
	// inject the root certificate, and no other, to
	// verify other peers against
	context ctx(ios, context::sslv23);

	ctx.set_options(context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);

	// we're a malicious peer, we don't have any interest
	// in verifying peers
	ctx.set_verify_mode(context::verify_none, ec);
	if (ec)
	{
		fprintf(stderr, "Failed to set SSL verify mode: %s\n"
			, ec.message().c_str());
		TEST_CHECK(!ec);
		return false;
	}

	std::string certificate = combine_path("..", combine_path("ssl", "peer_certificate.pem"));
	std::string private_key = combine_path("..", combine_path("ssl", "peer_private_key.pem"));
	std::string dh_params = combine_path("..", combine_path("ssl", "dhparams.pem"));

	if (flags & invalid_certificate)
	{
		certificate = combine_path("..", combine_path("ssl", "invalid_peer_certificate.pem"));
		private_key = combine_path("..", combine_path("ssl", "invalid_peer_private_key.pem"));
	}

	// TODO: test using a signed certificate with the wrong info-hash in DN

	if (flags & (valid_certificate | invalid_certificate))
	{
		fprintf(stderr, "set_password_callback\n");
		ctx.set_password_callback(boost::bind(&password_callback, _1, _2, "test"), ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set certificate password callback: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
		fprintf(stderr, "use_certificate_file \"%s\"\n", certificate.c_str());
		ctx.use_certificate_file(certificate, context::pem, ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set certificate file: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
		fprintf(stderr, "use_private_key_file \"%s\"\n", private_key.c_str());
		ctx.use_private_key_file(private_key, context::pem, ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set private key: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
		fprintf(stderr, "use_tmp_dh_file \"%s\"\n", dh_params.c_str());
		ctx.use_tmp_dh_file(dh_params, ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set DH params: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
	}

	boost::asio::ssl::stream<tcp::socket> ssl_sock(ios, ctx);

	fprintf(stderr, "connecting 127.0.0.1:%d\n", port);
	ssl_sock.lowest_layer().connect(tcp::endpoint(
		address_v4::from_string("127.0.0.1"), port), ec);
	print_alerts(ses1, "ses1", true, true, true, &on_alert);

	if (ec)
	{
		fprintf(stderr, "Failed to connect: %s\n"
			, ec.message().c_str());
		TEST_CHECK(!ec);
		return false;
	}

	if (flags & valid_sni_hash)
	{
		std::string name = to_hex(t->info_hash().to_string());
		fprintf(stderr, "SNI: %s\n", name.c_str());
		SSL_set_tlsext_host_name(ssl_sock.native_handle(), name.c_str());
	}
	else if (flags & invalid_sni_hash)
	{
		char const hex_alphabet[] = "0123456789abcdef";
		std::string name;
		name.reserve(40);
		for (int i = 0; i < 40; ++i)
			name += hex_alphabet[rand() % 16];

		fprintf(stderr, "SNI: %s\n", name.c_str());
		SSL_set_tlsext_host_name(ssl_sock.native_handle(), name.c_str());
	}

	fprintf(stderr, "SSL handshake\n");
	ssl_sock.handshake(boost::asio::ssl::stream_base::client, ec);

	print_alerts(ses1, "ses1", true, true, true, &on_alert);
	if (ec)
	{
		fprintf(stderr, "Failed SSL handshake: %s\n"
			, ec.message().c_str());
		return false;
	}

	char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
		"                    " // space for info-hash
		"aaaaaaaaaaaaaaaaaaaa" // peer-id
		"\0\0\0\x01\x02"; // interested

	// fill in the info-hash
	if (flags & valid_bittorrent_hash)
	{
		std::memcpy(handshake + 28, &t->info_hash()[0], 20);
	}
	else
	{
		// TODO: also test using a hash that refers to a valid torrent
		// but that differs from the SNI hash
		std::generate(handshake + 28, handshake + 48, &rand);
	}

	// fill in the peer-id
	std::generate(handshake + 48, handshake + 68, &rand);

	fprintf(stderr, "bittorrent handshake\n");
	boost::asio::write(ssl_sock, boost::asio::buffer(handshake, (sizeof(handshake) - 1)), ec);
	print_alerts(ses1, "ses1", true, true, true, &on_alert);
	if (ec)
	{
		fprintf(stderr, "failed to write bittorrent handshake: %s\n"
			, ec.message().c_str());
		return false;
	}

	char buf[68];
	fprintf(stderr, "read bittorrent handshake\n");
	boost::asio::read(ssl_sock, boost::asio::buffer(buf, sizeof(buf)), ec);
	print_alerts(ses1, "ses1", true, true, true, &on_alert);
	if (ec)
	{
		fprintf(stderr, "failed to read bittorrent handshake: %s\n"
			, ec.message().c_str());
		return false;
	}

	if (memcmp(buf, "\x13" "BitTorrent protocol", 20) != 0)
	{
		fprintf(stderr, "invalid bittorrent handshake\n");
		return false;
	}

	if (memcmp(buf + 28, &t->info_hash()[0], 20) != 0)
	{
		fprintf(stderr, "invalid info-hash in bittorrent handshake\n");
		return false;
	}

	fprintf(stderr, "successfully connected over SSL and shook hand over bittorrent\n");

	return true;
}

void test_malicious_peer()
{
	error_code ec;
	remove_all("tmp3_ssl", ec);

	// set up session
	int ssl_port = 1024 + rand() % 50000;
	settings_pack sett;
	sett.set_int(settings_pack::alert_mask, alert_mask);
	sett.set_int(settings_pack::max_retry_port_bind, 100);
	sett.set_str(settings_pack::listen_interfaces, "0.0.0.0:48075");
	sett.set_int(settings_pack::ssl_listen, ssl_port);
	sett.set_bool(settings_pack::enable_dht, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);

	libtorrent::session ses1(sett, 0);
	wait_for_listen(ses1, "ses1");

	// create torrent
	create_directory("tmp3_ssl", ec);
	std::ofstream file("tmp3_ssl/temporary");
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary"
		, 16 * 1024, 13, false, combine_path("..", combine_path("ssl", "root_ca_cert.pem")));
	file.close();

	TEST_CHECK(!t->ssl_cert().empty());

	add_torrent_params addp;
	addp.save_path = "tmp3_ssl";
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.ti = t;

	torrent_handle tor1 = ses1.add_torrent(addp, ec);

	tor1.set_ssl_certificate(
		combine_path("..", combine_path("ssl", "peer_certificate.pem"))
		, combine_path("..", combine_path("ssl", "peer_private_key.pem"))
		, combine_path("..", combine_path("ssl", "dhparams.pem"))
		, "test");

	alert const* a = wait_for_alert(ses1
		, torrent_finished_alert::alert_type, "ses1");
	TEST_CHECK(a);
	if (a)
	{
		TEST_EQUAL(a->type(), torrent_finished_alert::alert_type);
	}

	for (int i = 0; i < num_attacks; ++i)
	{
		bool success = try_connect(ses1, ssl_port, t, attacks[i].flags);
		TEST_EQUAL(success, attacks[i].expect);
	}
}
#endif // TORRENT_USE_OPENSSL

TORRENT_TEST(malicious_peer)
{
#ifdef TORRENT_USE_OPENSSL
	test_malicious_peer();
#endif
}

#ifdef TORRENT_USE_OPENSSL
TORRENT_TEST(utp_config0) { test_ssl(0, true); }
TORRENT_TEST(utp_config1) { test_ssl(1, true); }
TORRENT_TEST(utp_config2) { test_ssl(2, true); }
TORRENT_TEST(utp_config3) { test_ssl(3, true); }
TORRENT_TEST(utp_config4) { test_ssl(4, true); }
TORRENT_TEST(utp_config5) { test_ssl(5, true); }
TORRENT_TEST(utp_config6) { test_ssl(6, true); }
TORRENT_TEST(utp_config7) { test_ssl(7, true); }
TORRENT_TEST(utp_config8) { test_ssl(8, true); }

TORRENT_TEST(tcp_config0) { test_ssl(0, false); }
TORRENT_TEST(tcp_config1) { test_ssl(1, false); }
TORRENT_TEST(tcp_config2) { test_ssl(2, false); }
TORRENT_TEST(tcp_config3) { test_ssl(3, false); }
TORRENT_TEST(tcp_config4) { test_ssl(4, false); }
TORRENT_TEST(tcp_config5) { test_ssl(5, false); }
TORRENT_TEST(tcp_config6) { test_ssl(6, false); }
TORRENT_TEST(tcp_config7) { test_ssl(7, false); }
TORRENT_TEST(tcp_config8) { test_ssl(8, false); }
#endif

