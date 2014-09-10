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
#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include <fstream>
#include <iostream>
#include <boost/asio/connect.hpp>

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/error.hpp> // for asio::error::get_ssl_category()
#include <boost/asio/ssl.hpp>
#endif

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
	bool expected_to_complete;
	int peer_errors;
	int ssl_disconnects;
};

test_config_t test_config[] =
{
	{"nobody has a cert (connect to regular port)", false, false, false, false, 0, 0},
	{"nobody has a cert (connect to ssl port)", true, false, false, false, 1, 1},
	{"seed has a cert, but not downloader (connect to regular port)", false, true, false, false, 0, 0},
	{"seed has a cert, but not downloader (connect to ssl port)", true, true, false, false, 1, 1},
	{"downloader has a cert, but not seed (connect to regular port)", false, false, true, false, 0, 0},
	{"downloader has a cert, but not seed (connect to ssl port)", true, false, true, false, 1, 1},
	{"both downloader and seed has a cert (connect to regular port)", false, true, true, false, 0, 0},
#ifdef TORRENT_USE_OPENSSL
	{"both downloader and seed has a cert (connect to ssl port)", true, true, true, true, 0, 0},
#else
	{"both downloader and seed has a cert (connect to ssl port)", true, true, true, false, 0, 0},
#endif
};

int peer_disconnects = 0;
int peer_errors = 0;
int ssl_peer_disconnects = 0;

bool on_alert(alert* a)
{
	if (alert_cast<peer_disconnected_alert>(a))
		++peer_disconnects;
	if (peer_error_alert* e = alert_cast<peer_error_alert>(a))
	{
		++peer_disconnects;
		++peer_errors;

#ifdef TORRENT_USE_OPENSSL
		if (e->error.category() == boost::asio::error::get_ssl_category())
			++ssl_peer_disconnects;
#endif
	}
	return false;
}

void test_ssl(int test_idx)
{
	test_config_t const& test = test_config[test_idx];

	fprintf(stderr, "\n%s TEST: %s\n\n", time_now_string(), test.name);

#ifndef TORRENT_USE_OPENSSL
	if (test.use_ssl_ports)
	{
		fprintf(stderr, "N/A\n");
		return;
	}
#endif

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_ssl", ec);
	remove_all("tmp2_ssl", ec);

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48075, 49000), "0.0.0.0", 0, alert_mask);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49075, 50000), "0.0.0.0", 0, alert_mask);

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	session_settings sett;

	sett.ssl_listen = 1024 + rand() % 50000;

	ses1.set_settings(sett);
	sett.ssl_listen += 10;
	ses2.set_settings(sett);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_ssl", ec);
	std::ofstream file("tmp1_ssl/temporary");
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false, "ssl/root_ca_cert.pem");
	file.close();

	add_torrent_params addp;
	addp.save_path = ".";
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;
	ssl_peer_disconnects = 0;
	peer_errors = 0;

	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_ssl", 16 * 1024, &t, false, &addp, true, test.use_ssl_ports);

	if (test.seed_has_cert)
	{
		tor1.set_ssl_certificate(
			combine_path("ssl", "peer_certificate.pem")
			, combine_path("ssl", "peer_private_key.pem")
			, combine_path("ssl", "dhparams.pem")
			, "test");
	}

	if (test.downloader_has_cert)
	{
		tor2.set_ssl_certificate(
			combine_path("ssl", "peer_certificate.pem")
			, combine_path("ssl", "peer_private_key.pem")
			, combine_path("ssl", "dhparams.pem")
			, "test");
	}

	for (int i = 0; i < 15; ++i)
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

		if (peer_disconnects == 2) break;

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

	fprintf(stderr, "peer_errors: %d  expected: %d\n", peer_errors, test.peer_errors);
	TEST_EQUAL(peer_errors, test.peer_errors);
#ifdef TORRENT_USE_OPENSSL
	fprintf(stderr, "ssl_disconnects: %d  expected: %d\n", ssl_peer_disconnects, test.ssl_disconnects);
	TEST_EQUAL(ssl_peer_disconnects, test.ssl_disconnects);
#endif
	fprintf(stderr, "%s: EXPECT: %s\n", time_now_string(), test.expected_to_complete ? "SUCCEESS" : "FAILURE");
	fprintf(stderr, "%s: RESULT: %s\n", time_now_string(), tor2.status().is_seeding ? "SUCCEESS" : "FAILURE");
	TEST_CHECK(tor2.status().is_seeding == test.expected_to_complete);
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

bool try_connect(session& ses1, int port
	, boost::intrusive_ptr<torrent_info> const& t, boost::uint32_t flags)
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

	fprintf(stderr, "\n");

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

	std::string certificate = combine_path("ssl", "peer_certificate.pem");
	std::string private_key = combine_path("ssl", "peer_private_key.pem");
	std::string dh_params = combine_path("ssl", "dhparams.pem");

	if (flags & invalid_certificate)
	{
		certificate = combine_path("ssl", "invalid_peer_certificate.pem");
		private_key = combine_path("ssl", "invalid_peer_private_key.pem");
	}

	// TODO: test using a signed certificate with the wrong info-hash in DN

	if (flags & (valid_certificate | invalid_certificate))
	{
		ctx.set_password_callback(boost::bind(&password_callback, _1, _2, "test"), ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set certificate password callback: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
		ctx.use_certificate_file(certificate, context::pem, ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set certificate file: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
		ctx.use_private_key_file(private_key, context::pem, ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set private key: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
		ctx.use_tmp_dh_file(dh_params, ec);
		if (ec)
		{
			fprintf(stderr, "Failed to set DH params: %s\n"
				, ec.message().c_str());
			TEST_CHECK(!ec);
			return false;
		}
	}

	boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_sock(ios, ctx);

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

	ssl_sock.handshake(asio::ssl::stream_base::client, ec);

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
	boost::asio::write(ssl_sock, libtorrent::asio::buffer(handshake, (sizeof(handshake) - 1)), ec);
	if (ec)
	{
		fprintf(stderr, "failed to write bittorrent handshake: %s\n"
			, ec.message().c_str());
		return false;
	}
	
	char buf[68];
	boost::asio::read(ssl_sock, libtorrent::asio::buffer(buf, sizeof(buf)), ec);
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
	session ses1(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(48075, 49000), "0.0.0.0", 0, alert_mask);
	wait_for_listen(ses1, "ses1");
	session_settings sett;
	sett.ssl_listen = 1024 + rand() % 50000;
	ses1.set_settings(sett);

	// create torrent
	create_directory("tmp3_ssl", ec);
	std::ofstream file("tmp3_ssl/temporary");
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file
		, 16 * 1024, 13, false, "ssl/root_ca_cert.pem");
	file.close();

	add_torrent_params addp;
	addp.save_path = ".";
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.ti = t;

	torrent_handle tor1 = ses1.add_torrent(addp, ec);

	tor1.set_ssl_certificate(
		combine_path("ssl", "peer_certificate.pem")
		, combine_path("ssl", "peer_private_key.pem")
		, combine_path("ssl", "dhparams.pem")
		, "test");

	wait_for_listen(ses1, "ses1");

	for (int i = 0; i < num_attacks; ++i)
	{
		bool success = try_connect(ses1, sett.ssl_listen, t, attacks[i].flags);
		TEST_EQUAL(attacks[i].expect, success);
	}
}

int test_main()
{
	using namespace libtorrent;

	test_malicious_peer();

	for (int i = 0; i < sizeof(test_config)/sizeof(test_config[0]); ++i)
		test_ssl(i);
	
	error_code ec;
	remove_all("tmp1_ssl", ec);
	remove_all("tmp2_ssl", ec);

	return 0;
}



