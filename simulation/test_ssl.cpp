/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// this test suite exercises SSL torrents inside the simulator: the matrix of
// which peer has a certificate and whether the connection is made to the
// plain BitTorrent port or the SSL port (mirroring test/test_ssl.cpp's
// test_config_t matrix), and that a peer presenting no/invalid certificate,
// the wrong SNI hostname, or the wrong info-hash is rejected (mirroring
// test/test_ssl.cpp's malicious-peer attacks[] matrix).

#include "libtorrent/config.hpp"
#include "test.hpp"

#if TORRENT_USE_SSL

#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/hex.hpp" // for to_hex
#include "libtorrent/aux_/ssl.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/flags.hpp"

#include "test_utils.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp" // for addr()
#include "disk_io.hpp"
#include "utils.hpp"
#include "http_server.hpp" // for ssl_fixture_path, is_ssl_error

#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <memory>
#include <functional>
#include <utility>
#include <vector>

using namespace lt;
using namespace sim;

namespace {

	// reads the content of a file under test/ssl/ (e.g. to embed a root
	// certificate directly into a torrent, as opposed to passing a path to
	// set_ssl_certificate(), which loads the file itself)
	std::string read_file(std::string const& name)
	{
		std::ifstream f(ssl_fixture_path(name));
		std::stringstream buf;
		buf << f.rdbuf();
		return buf.str();
	}

	// builds the (single-file, 13-piece) test torrent shared by both the
	// transfer-matrix and malicious-peer tests, with the given root
	// certificate embedded in it, ready to be added un-paused and not
	// auto-managed
	lt::add_torrent_params make_ssl_test_torrent(std::string const& root_cert)
	{
		lt::add_torrent_params atp = create_test_torrent(16 * 1024, 13, {}, 1, false, root_cert);
		atp.save_path = ".";
		atp.flags &= ~lt::torrent_flags::auto_managed;
		atp.flags &= ~lt::torrent_flags::paused;
		return atp;
	}

	// records peer_error_alert and peer_disconnected_alert counts, and whether
	// any of them originated from the SSL/TLS layer
	struct disconnect_counters
	{
		int peer_errors = 0;
		int ssl_disconnects = 0;

		void observe(lt::alert const* a)
		{
			// none of this test's expected outcomes are alert_category::error
			// (peer_error_alert is category::peer, peer_disconnected_alert is
			// category::connect), so any error alert here is a genuine bug
			TEST_CHECK(!(a->category() & alert_category::error));

			if (auto const* e = alert_cast<peer_disconnected_alert>(a))
			{
				if (is_ssl_error(e->error))
					++ssl_disconnects;
			}
			else if (auto const* e = alert_cast<peer_error_alert>(a))
			{
				++peer_errors;
				if (is_ssl_error(e->error))
					++ssl_disconnects;
			}
		}
	};

	// flags describing one row of the SSL transfer matrix: which peer has a
	// certificate and which port the connection is made to. The expected
	// outcome (whether the transfer completes, and what kind of disconnect
	// signal it produces) is derived from these in run_ssl_transfer_test()
	// rather than stored here, since it's fully determined by them.
	using ssl_test_flags_t = lt::flags::bitfield_flag<std::uint16_t, struct ssl_test_flags_tag>;

	constexpr ssl_test_flags_t use_ssl_port = 0_bit;
	constexpr ssl_test_flags_t seed_has_cert = 1_bit;
	constexpr ssl_test_flags_t downloader_has_cert = 2_bit;
	constexpr ssl_test_flags_t downloader_has_ssl_port = 3_bit;
	// the downloader adds the torrent as a magnet link (no embedded root
	// certificate) and is given the CA via add_torrent_params::root_certificate
	// instead, exercising that code path rather than the one where the
	// certificate comes from the .torrent file
	constexpr ssl_test_flags_t downloader_magnet = 4_bit;

	struct ssl_test_config
	{
		char const* name;
		ssl_test_flags_t flags;
	};

	// SSL transfer matrix: for every combination of which peer has a
	// certificate and which port the connection is made to, verify the
	// transfer completes exactly when both peers have certificates and the
	// connection is made to the SSL port; otherwise the connection is expected
	// to fail (either as an SSL/TLS error, or, when the target port isn't even
	// listening, as a plain connection failure with no alerts at all).
	ssl_test_config const ssl_configs[] = {
		{"nobody has a cert, connect to regular port", downloader_has_ssl_port},
		{"nobody has a cert, connect to SSL port", use_ssl_port | downloader_has_ssl_port},
		{"only the seed has a cert, connect to regular port",
			seed_has_cert | downloader_has_ssl_port},
		{"only the seed has a cert, connect to SSL port",
			use_ssl_port | seed_has_cert | downloader_has_ssl_port},
		{"only the downloader has a cert, connect to regular port",
			downloader_has_cert | downloader_has_ssl_port},
		{"only the downloader has a cert, connect to SSL port",
			use_ssl_port | downloader_has_cert | downloader_has_ssl_port},
		{"both have a cert, connect to regular port",
			seed_has_cert | downloader_has_cert | downloader_has_ssl_port},
		{"both have a cert, connect to SSL port",
			use_ssl_port | seed_has_cert | downloader_has_cert | downloader_has_ssl_port},
		// the SSL port isn't listening at all, so this is a plain failure to
		// connect, not a peer error or SSL disconnect
		{"both have a cert, downloader has no SSL listen port",
			use_ssl_port | seed_has_cert | downloader_has_cert},
		// the downloader has no .torrent file (and so no embedded root
		// certificate), only a magnet link; it's given the CA directly via
		// add_torrent_params::root_certificate instead
		{"magnet download, seed has a cert",
			use_ssl_port | seed_has_cert | downloader_has_cert | downloader_has_ssl_port
				| downloader_magnet},
		{"magnet download, seed has no cert",
			use_ssl_port | downloader_has_cert | downloader_has_ssl_port | downloader_magnet},
	};

	void run_ssl_transfer_test(ssl_test_config const& cfg,
		bool const use_utp,
		lt::add_torrent_params const& atp_template,
		std::string const& root_cert)
	{
		using asio::ip::address;
		address const peer0 = addr("50.0.0.1"); // seed
		address const peer1 = addr("50.0.0.2"); // downloader

		int const regular_port = 6881;
		int const ssl_port = 6882;
		int const connect_port = (cfg.flags & use_ssl_port) ? ssl_port : regular_port;

		sim::default_config network_cfg;
		sim::simulation sim{network_cfg};
		sim::asio::io_context ios0{sim, peer0};
		sim::asio::io_context ios1{sim, peer1};

		lt::session_proxy zombie[2];

		lt::session_params params;
		lt::settings_pack& pack = params.settings;
		pack = settings();
		pack.set_bool(settings_pack::enable_incoming_utp, use_utp);
		pack.set_bool(settings_pack::enable_outgoing_utp, use_utp);
		pack.set_bool(settings_pack::enable_incoming_tcp, !use_utp);
		pack.set_bool(settings_pack::enable_outgoing_tcp, !use_utp);
		// if a peer fails once, don't try it again
		pack.set_int(settings_pack::max_failcount, 1);

		// the seed is the one that initiates the connection (connect_peer(),
		// below). an outgoing uTP socket multiplexes over one of the
		// session's own listen sockets rather than opening an ad-hoc socket,
		// and it must be a listen socket whose ssl-ness matches the outgoing
		// connection, so the seed needs an SSL-flagged listen socket too, in
		// addition to its plain one
		char seed_iface[50];
		std::snprintf(
			seed_iface, sizeof(seed_iface), "50.0.0.1:%d,50.0.0.1:%ds", regular_port, ssl_port);
		pack.set_str(settings_pack::listen_interfaces, seed_iface);

		std::shared_ptr<lt::session> ses[2];

		params.disk_io_constructor = test_disk().set_seed(true);
		ses[0] = std::make_shared<lt::session>(params, ios0);

		char downloader_iface[50];
		if (cfg.flags & downloader_has_ssl_port)
		{
			std::snprintf(downloader_iface,
				sizeof(downloader_iface),
				"50.0.0.2:%d,50.0.0.2:%ds",
				regular_port,
				ssl_port);
		}
		else
		{
			std::snprintf(downloader_iface, sizeof(downloader_iface), "50.0.0.2:%d", regular_port);
		}
		pack.set_str(settings_pack::listen_interfaces, downloader_iface);

		params.disk_io_constructor = test_disk();
		ses[1] = std::make_shared<lt::session>(params, ios1);

		// harmless when the downloader isn't a magnet download: the seed just
		// never gets a metadata request for it
		ses[0]->add_extension(&lt::create_ut_metadata_plugin);
		ses[1]->add_extension(&lt::create_ut_metadata_plugin);

		lt::add_torrent_params const& atp = atp_template;

		// a magnet-link downloader has no .torrent file, and so no embedded
		// root certificate; it's given the CA directly instead
		lt::add_torrent_params magnet_atp;
		magnet_atp.info_hashes = atp.ti->info_hashes();
		magnet_atp.root_certificate = root_cert;
		magnet_atp.save_path = ".";
		magnet_atp.flags &= ~lt::torrent_flags::auto_managed;
		magnet_atp.flags &= ~lt::torrent_flags::paused;

		lt::add_torrent_params const& downloader_atp =
			(cfg.flags & downloader_magnet) ? magnet_atp : atp;

		std::string const cert = ssl_fixture_path("peer_certificate.pem");
		std::string const key = ssl_fixture_path("peer_private_key.pem");
		std::string const dh = ssl_fixture_path("dhparams.pem");

		torrent_handle tor_seed;
		torrent_handle tor_downloader;
		bool seed_ready = false;
		bool downloader_ready = false;
		disconnect_counters counters;

		auto maybe_connect = [&] {
			if (!seed_ready || !downloader_ready)
				return;
			tor_seed.connect_peer(lt::tcp::endpoint(peer1, std::uint16_t(connect_port)));
		};

		print_alerts(
			*ses[0],
			[&](lt::session&, lt::alert const* a) {
				if (auto* ta = alert_cast<add_torrent_alert>(a))
				{
					tor_seed = ta->handle;
					if (cfg.flags & seed_has_cert)
						tor_seed.set_ssl_certificate(cert, key, dh, "test");
					seed_ready = true;
					maybe_connect();
				}
				counters.observe(a);
			},
			0);

		print_alerts(
			*ses[1],
			[&](lt::session&, lt::alert const* a) {
				if (auto* ta = alert_cast<add_torrent_alert>(a))
				{
					tor_downloader = ta->handle;
					if (cfg.flags & downloader_has_cert)
						tor_downloader.set_ssl_certificate(cert, key, dh, "test");
					downloader_ready = true;
					maybe_connect();
				}
				counters.observe(a);
			},
			1);

		ses[0]->async_add_torrent(atp);
		ses[1]->async_add_torrent(downloader_atp);

		// the transfer only completes when both peers have a certificate and
		// the connection is made to the SSL port; otherwise, if the
		// downloader has an SSL listen port at all, the rejected connection
		// surfaces as an SSL disconnect (and, when it was actually attempted
		// over SSL, also as a peer error)
		bool const expect_complete = bool(cfg.flags & use_ssl_port)
			&& bool(cfg.flags & seed_has_cert) && bool(cfg.flags & downloader_has_cert)
			&& bool(cfg.flags & downloader_has_ssl_port);
		bool const expect_ssl_disconnect =
			bool(cfg.flags & downloader_has_ssl_port) && !expect_complete;
		bool const expect_peer_error = bool(cfg.flags & use_ssl_port) && expect_ssl_disconnect;

		sim::timer t(sim, lt::seconds(30), [&](boost::system::error_code const&) {
			bool const complete = tor_downloader.status().is_seeding;
			std::printf("EXPECT: %s RESULT: %s peer_errors: %d (expect %d) ssl_disconnects: %d "
						"(expect %d)\n",
				expect_complete ? "complete" : "incomplete",
				complete ? "complete" : "incomplete",
				counters.peer_errors,
				int(expect_peer_error),
				counters.ssl_disconnects,
				int(expect_ssl_disconnect));
			TEST_EQUAL(complete, expect_complete);

			// uTP does not surface the same peer_error/ssl_disconnect signal for
			// a rejected connection as TCP does, so only check these for TCP
			if (!use_utp)
			{
				TEST_EQUAL(counters.peer_errors > 0, expect_peer_error);
				TEST_EQUAL(counters.ssl_disconnects > 0, expect_ssl_disconnect);
			}

			int idx = 0;
			for (auto& s : ses)
			{
				zombie[idx++] = s->abort();
				s.reset();
			}
		});

		sim.run();
	}

	using attack_flags_t = lt::flags::bitfield_flag<std::uint16_t, struct attack_flags_tag>;

	constexpr attack_flags_t valid_certificate = 0_bit;
	constexpr attack_flags_t invalid_certificate = 1_bit;
	constexpr attack_flags_t valid_sni_hash = 2_bit;
	constexpr attack_flags_t invalid_sni_hash = 3_bit;
	constexpr attack_flags_t valid_bittorrent_hash = 4_bit;
	constexpr attack_flags_t expect_success = 5_bit;

	// malicious-peer / certificate-validation matrix: a hand-crafted client
	// (bypassing bt_peer_connection entirely) attempts to connect to a seed,
	// varying its client certificate, SNI hostname, and the info-hash in the
	// BitTorrent handshake it sends after the TLS handshake completes.
	struct attack_config
	{
		char const* name;
		attack_flags_t flags;
	};

	attack_config const attacks[] = {
		// positive control
		{"valid certificate, valid SNI, valid info-hash",
			valid_certificate | valid_sni_hash | valid_bittorrent_hash | expect_success},

		// SNI
		{"valid certificate, invalid SNI, valid info-hash",
			valid_certificate | invalid_sni_hash | valid_bittorrent_hash},
		{"valid certificate, no SNI, valid info-hash", valid_certificate | valid_bittorrent_hash},

		// certificate
		{"no certificate, valid SNI, valid info-hash", valid_sni_hash | valid_bittorrent_hash},
		{"invalid certificate, valid SNI, valid info-hash",
			invalid_certificate | valid_sni_hash | valid_bittorrent_hash},

		// bittorrent info-hash
		{"valid certificate, valid SNI, invalid info-hash", valid_certificate | valid_sni_hash},
	};

	// the standard 68-byte BitTorrent handshake, with the info-hash and peer-id
	// left as placeholders, plus a 5-byte "interested" message tacked on (which
	// the response-reading side ignores, exactly like test/test_ssl.cpp's
	// try_connect()). std::to_array() picks up the literal's trailing '\0' as
	// an extra, harmless array element (the peer just sees a stray zero byte
	// after "interested", which it never gets to interpret before we
	// disconnect)
	constexpr auto handshake_template =
		std::to_array("\023BitTorrent protocol\0\0\0\0\0\0\0\x04" // \023 == 0x13
					  "                    " // space for info-hash
					  "aaaaaaaaaaaaaaaaaaaa" // peer-id
					  "\0\0\0\x01\x02"); // interested

	// drives a single malicious-peer connection attempt asynchronously (a
	// simulation cannot block the single simulated thread the way
	// test/test_ssl.cpp's try_connect() blocks a real one), reporting through a
	// callback whether it ended up with a valid BitTorrent handshake echoed back
	struct malicious_peer
	{
		malicious_peer(sim::simulation& sim,
			lt::tcp::endpoint const& target,
			std::shared_ptr<lt::torrent_info const> ti,
			attack_config const& atk)
			: m_ios(sim, addr("50.0.0.3"))
			// the ssl::stream constructor creates its underlying SSL object from
			// the context as it currently stands, so the context's certificate
			// must be fully configured *before* the stream is constructed from
			// it, not afterwards
			, m_ctx(make_context(atk))
			, m_sock(m_ios, m_ctx)
			, m_target(target)
			, m_ti(std::move(ti))
			, m_atk(atk)
		{}

		void start(std::function<void(bool)> on_done)
		{
			m_done = std::move(on_done);
			m_sock.lowest_layer().async_connect(
				m_target, [this](error_code const& ec) { on_connect(ec); });
		}

	private:
		static lt::aux::ssl::context make_context(attack_config const& atk)
		{
			lt::aux::ssl::context ctx(lt::aux::ssl::context::tls);
			error_code ec;
			ctx.set_options(lt::aux::ssl::context::default_workarounds
				| lt::aux::ssl::context::no_sslv2 | lt::aux::ssl::context::no_sslv3
				| lt::aux::ssl::context::single_dh_use);

			// we're a malicious peer, we don't have any interest in verifying
			// the seed's certificate
			ctx.set_verify_mode(lt::aux::ssl::context::verify_none, ec);
			TEST_CHECK(!ec);

			if (atk.flags & (valid_certificate | invalid_certificate))
			{
				bool const invalid = bool(atk.flags & invalid_certificate);
				std::string const certificate = ssl_fixture_path(
					invalid ? "invalid_peer_certificate.pem" : "peer_certificate.pem");
				std::string const private_key = ssl_fixture_path(
					invalid ? "invalid_peer_private_key.pem" : "peer_private_key.pem");
				std::string const dh_params = ssl_fixture_path("dhparams.pem");

				ctx.set_password_callback(
					[](std::size_t, lt::aux::ssl::context::password_purpose) { return "test"; },
					ec);
				TEST_CHECK(!ec);
				ctx.use_certificate_file(certificate, lt::aux::ssl::context::pem, ec);
				TEST_CHECK(!ec);
				ctx.use_private_key_file(private_key, lt::aux::ssl::context::pem, ec);
				TEST_CHECK(!ec);
				ctx.use_tmp_dh_file(dh_params, ec);
				TEST_CHECK(!ec);
			}

			return ctx;
		}

		void on_connect(error_code const& ec)
		{
			// unlike the handshake/write/read stages below, a plain TCP
			// connect failure can never be a legitimate consequence of the
			// attack under test (cert/SNI/info-hash validation all happen
			// later) -- it can only mean the seed's SSL listen socket isn't
			// where we expect it to be, i.e. a test setup bug
			TEST_CHECK(!ec);
			if (ec)
			{
				finish(false);
				return;
			}

			error_code ec2;
			if (m_atk.flags & valid_sni_hash)
			{
				std::string const name = lt::aux::to_hex(m_ti->info_hashes().v1);
				lt::aux::ssl::set_host_name(lt::aux::ssl::get_handle(m_sock), name, ec2);
				TEST_CHECK(!ec2);
			}
			else if (m_atk.flags & invalid_sni_hash)
			{
				std::string const name = lt::aux::to_hex(rand_hash());
				lt::aux::ssl::set_host_name(lt::aux::ssl::get_handle(m_sock), name, ec2);
				TEST_CHECK(!ec2);
			}

			m_sock.async_handshake(lt::aux::ssl::stream_base::client,
				[this](error_code const& ec3) { on_handshake(ec3); });
		}

		void on_handshake(error_code const& ec)
		{
			if (failed(ec))
				return;

			m_handshake = handshake_template;

			if (m_atk.flags & valid_bittorrent_hash)
				std::memcpy(m_handshake.data() + 28, m_ti->info_hashes().v1.data(), 20);
			else
				lt::aux::random_bytes(lt::span<char>(m_handshake).subspan(28, 20));

			lt::aux::random_bytes(lt::span<char>(m_handshake).subspan(48, 20));

			boost::asio::async_write(m_sock,
				boost::asio::buffer(m_handshake),
				[this](error_code const& ec2, std::size_t) { on_write(ec2); });
		}

		void on_write(error_code const& ec)
		{
			if (failed(ec))
				return;
			boost::asio::async_read(m_sock,
				boost::asio::buffer(m_read_buf),
				[this](error_code const& ec2, std::size_t) { on_read(ec2); });
		}

		void on_read(error_code const& ec)
		{
			if (failed(ec))
				return;
			bool const ok = lt::span<char const>(m_read_buf).first(20)
					== lt::span<char const>("\x13"
											"BitTorrent protocol",
						20)
				&& lt::span<char const>(m_read_buf).subspan(28, 20)
					== lt::span<char const>(m_ti->info_hashes().v1);
			finish(ok);
		}

		void finish(bool const ok)
		{
			if (!m_done)
				return;
			auto f = std::exchange(m_done, nullptr);
			f(ok);
		}

		// bails out of the attempt on error, reporting it as a (correctly)
		// rejected connection
		bool failed(error_code const& ec)
		{
			if (!ec)
				return false;
			finish(false);
			return true;
		}

		sim::asio::io_context m_ios;
		lt::aux::ssl::context m_ctx;
		lt::aux::ssl::stream<lt::tcp::socket> m_sock;
		lt::tcp::endpoint m_target;
		std::shared_ptr<lt::torrent_info const> m_ti;
		attack_config m_atk;
		std::array<char, handshake_template.size()> m_handshake{};
		std::array<char, 68> m_read_buf{};
		std::function<void(bool)> m_done;
	};

	// opens a TCP connection to an SSL listener but deliberately never sends a
	// TLS ClientHello. This keeps one incoming handshake pending on the server.
	struct stalled_ssl_peer
	{
		stalled_ssl_peer(
			sim::simulation& sim, address const& source, lt::tcp::endpoint const& target)
			: m_ios(sim, source)
			, m_sock(m_ios)
			, m_target(target)
		{}

		void start(std::function<void()> on_connected)
		{
			m_connected = std::move(on_connected);
			m_sock.async_connect(m_target, [this](error_code const& ec) { on_connect(ec); });
		}

	private:
		void on_connect(error_code const& ec)
		{
			TEST_CHECK(!ec);
			if (ec)
				return;

			if (m_connected)
				m_connected();
		}

		sim::asio::io_context m_ios;
		lt::tcp::socket m_sock;
		lt::tcp::endpoint m_target;
		std::function<void()> m_connected;
	};

	void run_malicious_peer_attack(
		attack_config const& atk, lt::add_torrent_params const& atp_template)
	{
		address const peer0 = addr("50.0.0.1");
		int const port = 6881;

		sim::default_config network_cfg;
		sim::simulation sim{network_cfg};
		sim::asio::io_context ios0{sim, peer0};

		lt::session_proxy zombie;

		lt::session_params params;
		lt::settings_pack& pack = params.settings;
		pack = settings();

		// this session only ever listens on its SSL port -- there's no plain
		// port to fall back to, exactly like test/test_ssl.cpp's
		// test_malicious_peer()
		char iface[50];
		std::snprintf(iface, sizeof(iface), "50.0.0.1:%ds", port);
		pack.set_str(settings_pack::listen_interfaces, iface);

		params.disk_io_constructor = test_disk().set_seed(true);
		auto ses = std::make_shared<lt::session>(params, ios0);

		lt::add_torrent_params const& atp = atp_template;
		std::shared_ptr<lt::torrent_info const> const ti = atp.ti;

		std::unique_ptr<malicious_peer> attempt;
		bool result = false;
		bool done = false;

		print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
			if (auto* ta = alert_cast<add_torrent_alert>(a))
			{
				torrent_handle h = ta->handle;
				h.set_ssl_certificate(ssl_fixture_path("peer_certificate.pem"),
					ssl_fixture_path("peer_private_key.pem"),
					ssl_fixture_path("dhparams.pem"),
					"test");
			}
			else if (alert_cast<torrent_finished_alert>(a))
			{
				attempt = std::make_unique<malicious_peer>(
					sim, lt::tcp::endpoint(peer0, std::uint16_t(port)), ti, atk);
				attempt->start([&](bool const ok) {
					result = ok;
					done = true;
				});
			}
		});

		ses->async_add_torrent(atp);

		sim::timer give_up(sim, lt::seconds(30), [&](boost::system::error_code const&) {
			std::printf("EXPECT: %s RESULT: %s (%s)\n",
				(atk.flags & expect_success) ? "success" : "rejected",
				result ? "success" : "rejected",
				done ? "completed" : "timed out");
			TEST_EQUAL(result, bool(atk.flags & expect_success));

			zombie = ses->abort();
			ses.reset();
		});

		sim.run();
	}

	void run_ssl_handshake_connection_limit_test(lt::add_torrent_params const& atp_template)
	{
		address const peer0 = addr("50.0.0.1");
		address const peer1 = addr("50.0.0.2");
		address const peer2 = addr("50.0.0.3");
		int const port = 6881;

		sim::default_config network_cfg;
		sim::simulation sim{network_cfg};
		sim::asio::io_context ios0{sim, peer0};

		lt::session_proxy zombie;

		lt::session_params params;
		lt::settings_pack& pack = params.settings;
		pack = settings();
		pack.set_int(settings_pack::connections_limit, 1);
		pack.set_int(settings_pack::connections_slack, 0);

		char iface[50];
		std::snprintf(iface, sizeof(iface), "50.0.0.1:%ds", port);
		pack.set_str(settings_pack::listen_interfaces, iface);

		params.disk_io_constructor = test_disk().set_seed(true);
		auto ses = std::make_shared<lt::session>(params, ios0);

		std::unique_ptr<stalled_ssl_peer> first;
		std::unique_ptr<stalled_ssl_peer> second;
		std::unique_ptr<sim::timer> start_second;
		bool connection_rejected = false;

		print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
			if (auto* ta = alert_cast<add_torrent_alert>(a))
			{
				torrent_handle h = ta->handle;
				h.set_ssl_certificate(ssl_fixture_path("peer_certificate.pem"),
					ssl_fixture_path("peer_private_key.pem"),
					ssl_fixture_path("dhparams.pem"),
					"test");
			}
			else if (alert_cast<torrent_finished_alert>(a))
			{
				first = std::make_unique<stalled_ssl_peer>(
					sim, peer1, lt::tcp::endpoint(peer0, std::uint16_t(port)));
				first->start([&] {
					start_second = std::make_unique<sim::timer>(
						sim, lt::seconds(1), [&](boost::system::error_code const& ec) {
							if (ec)
								return;
							second = std::make_unique<stalled_ssl_peer>(
								sim, peer2, lt::tcp::endpoint(peer0, std::uint16_t(port)));
							second->start([] {});
						});
				});
			}
			else if (auto const* e = alert_cast<peer_disconnected_alert>(a))
			{
				if (e->error == error_code(errors::too_many_connections))
					connection_rejected = true;
			}
		});

		ses->async_add_torrent(atp_template);

		sim::timer give_up(sim, lt::seconds(5), [&](boost::system::error_code const&) {
			TEST_CHECK(second);
			TEST_CHECK(connection_rejected);

			zombie = ses->abort();
			ses.reset();
		});

		sim.run();
	}

	// pending SSL handshakes must be bounded by connections_limit (+
	// connections_slack), the same way incoming_connection() already bounds
	// plain TCP accepts synchronously, at accept time. A peer that never starts
	// the TLS handshake must not be able to occupy more than that many slots.
	void run_ssl_pending_handshake_limit()
	{
		address const peer0 = addr("50.0.0.1");
		int const port = 6881;
		int const connections_limit = 5;
		int const connections_slack = 0;
		int const allowed = connections_limit + connections_slack;
		int const num_attackers = 4 * connections_limit;

		sim::default_config network_cfg;
		sim::simulation sim{network_cfg};
		sim::asio::io_context ios0{sim, peer0};

		lt::session_proxy zombie;

		lt::session_params params;
		lt::settings_pack& pack = params.settings;
		pack = settings();
		pack.set_int(settings_pack::connections_limit, connections_limit);
		pack.set_int(settings_pack::connections_slack, connections_slack);

		// this session only ever listens on its SSL port, like
		// run_malicious_peer_attack() above
		char iface[50];
		std::snprintf(iface, sizeof(iface), "50.0.0.1:%ds", port);
		pack.set_str(settings_pack::listen_interfaces, iface);

		params.disk_io_constructor = test_disk().set_seed(true);
		auto ses = std::make_shared<lt::session>(params, ios0);

		std::string const root_cert = read_file("root_ca_cert.pem");
		lt::add_torrent_params const atp = make_ssl_test_torrent(root_cert);

		// attacker sockets: connect to the SSL port, then never send the
		// TLS ClientHello that ssl_stream::async_accept_handshake() is waiting
		// for. Reserved up front so these vectors never reallocate and
		// invalidate async_connect's captured reference below.
		std::size_t const num_attackers_sz = std::size_t(num_attackers);
		std::vector<std::unique_ptr<sim::asio::io_context>> attacker_ios;
		std::vector<lt::tcp::socket> attacker_socks;
		attacker_ios.reserve(num_attackers_sz);
		attacker_socks.reserve(num_attackers_sz);
		int connected = 0;
		int rejected = 0;

		print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
			if (auto* ta = alert_cast<add_torrent_alert>(a))
			{
				torrent_handle h = ta->handle;
				h.set_ssl_certificate(ssl_fixture_path("peer_certificate.pem"),
					ssl_fixture_path("peer_private_key.pem"),
					ssl_fixture_path("dhparams.pem"),
					"test");
			}
			else if (alert_cast<torrent_finished_alert>(a))
			{
				// note: i starts at 1, since make_io_context(sim, 0) would
				// collide with the seed's own address, 50.0.0.1
				for (int i = 1; i <= num_attackers; ++i)
				{
					attacker_ios.push_back(make_io_context(sim, i));
					attacker_socks.emplace_back(*attacker_ios.back());
					attacker_socks.back().async_connect(
						lt::tcp::endpoint(peer0, std::uint16_t(port)),
						[&](error_code const& ec) {
							TEST_CHECK(!ec);
							if (!ec) ++connected;
						});
				}
			}
			else if (auto const* e = alert_cast<peer_disconnected_alert>(a))
			{
				if (e->error == error_code(errors::too_many_connections))
					++rejected;
			}
		});

		ses->async_add_torrent(atp);

		sim::timer give_up(sim, lt::seconds(30), [&](boost::system::error_code const&) {
			std::printf("attackers: %d allowed: %d connected: %d rejected: %d\n"
				, num_attackers, allowed, connected, rejected);

			TEST_CHECK(num_attackers > allowed);

			// the TCP accept itself always succeeds, rejection happens
			// afterwards by closing the socket, so every attacker connects
			// regardless of the limit
			TEST_EQUAL(connected, num_attackers);

			// Assert the rejection alert rather than client-side socket closure.
			// The simulator's socket destructor does not propagate a close to its
			// peer.
			TEST_EQUAL(rejected, num_attackers - allowed);

			zombie = ses->abort();
			ses.reset();
		});

		sim.run();
	}

} // anonymous namespace

TORRENT_TEST(ssl_transfer_matrix)
{
	std::string const root_cert = read_file("root_ca_cert.pem");
	lt::add_torrent_params const atp_template = make_ssl_test_torrent(root_cert);
	for (auto const& cfg : ssl_configs)
	{
		for (bool const use_utp : {false, true})
		{
			::unit_test::reset_output();
			std::cout << "::group::case-" << cfg.name << (use_utp ? " (uTP)" : " (TCP)") << "\n";
			run_ssl_transfer_test(cfg, use_utp, atp_template, root_cert);
			std::cout << "::endgroup::\n";
		}
	}
}

TORRENT_TEST(ssl_malicious_peer)
{
	std::string const root_cert = read_file("root_ca_cert.pem");
	lt::add_torrent_params const atp_template = make_ssl_test_torrent(root_cert);
	for (auto const& atk : attacks)
	{
		::unit_test::reset_output();
		std::cout << "::group::case-" << atk.name << "\n";
		run_malicious_peer_attack(atk, atp_template);
		std::cout << "::endgroup::\n";
	}
}

TORRENT_TEST(ssl_handshake_connection_limit)
{
	std::string const root_cert = read_file("root_ca_cert.pem");
	run_ssl_handshake_connection_limit_test(make_ssl_test_torrent(root_cert));
}

TORRENT_TEST(ssl_pending_handshake_limit)
{
	run_ssl_pending_handshake_limit();
}

#else

TORRENT_TEST(disabled) {}

#endif // TORRENT_USE_SSL
