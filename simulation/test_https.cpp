/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// this test suite is only meaningful when built with TLS support, since it
// exercises sim::http_server's https flag and the certificate/hostname/
// protocol validation an HTTPS client is expected to perform.
#include "libtorrent/config.hpp"
#include "test.hpp"

#if TORRENT_USE_SSL

#include "simulator/simulator.hpp"
#include "http_server.hpp"
#include "libtorrent/aux_/http_connection.hpp"
#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/aux_/ssl.hpp"

#include "make_proxy_settings.hpp"

using namespace lt;
using namespace sim;

namespace {

	// "test-hostname.com" is used by the hostname/CN tests: it's the name
	// baked into test/ssl/hostname_cert.pem's subject and SAN.
	struct sim_config : sim::default_config
	{
		chrono::high_resolution_clock::duration hostname_lookup(asio::ip::address const&,
			std::string hostname,
			std::vector<asio::ip::address>& result,
			boost::system::error_code& ec) override
		{
			if (hostname == "test-hostname.com")
			{
				result.push_back(make_address_v4("10.0.0.2"));
				return chrono::milliseconds(100);
			}
			return default_config::hostname_lookup({}, hostname, result, ec);
		}
	};

	// bundles the sim/server/resolver/client-ssl-context scaffolding shared by
	// every test below, and registers the "/test_file" 200-OK handler they
	// all rely on. client_trust_file, if non-empty, is loaded as the client's
	// trust anchor (left empty, ssl_ctx keeps an empty trust store); the
	// client always starts out in verify_peer, since only one test needs
	// verify_none, and that test overrides ssl_ctx itself afterward -- same
	// as the other test-specific tweaks (a restricted protocol range, a fake
	// verification time) that don't fit here because they only apply to one
	// or two tests.
	struct https_fixture
	{
		explicit https_fixture(std::string cert_file = "server.pem",
			std::string key_file = "server.pem",
			std::string client_trust_file = "",
			std::function<void(lt::aux::ssl::context&)> ssl_setup = nullptr)
			: sim(network_cfg)
			, web_server(sim, make_address_v4("10.0.0.2"))
			, client_ios(sim, make_address_v4("10.0.0.1"))
			, res(client_ios)
			, http(web_server,
				  8080,
				  sim::http_server::https,
				  std::move(cert_file),
				  std::move(key_file),
				  std::move(ssl_setup))
			, ssl_ctx(lt::aux::ssl::context::sslv23_client)
		{
			http.register_handler(
				"/test_file", [](std::string, std::string, std::map<std::string, std::string>&) {
					return sim::send_response(200, "OK", 0);
				});

			ssl_ctx.set_verify_mode(lt::aux::ssl::context::verify_peer);
			if (!client_trust_file.empty())
			{
				error_code ec;
				ssl_ctx.load_verify_file(ssl_fixture_path(client_trust_file), ec);
				TEST_CHECK(!ec);
			}
		}

		// starts a single GET request (with no proxy) over ssl_ctx. The
		// outcome is recorded into connected/result_ec/handler_called, which
		// the caller inspects after sim.run(). conn is kept alive as a member
		// (rather than returned) since it must outlive this call, through
		// sim.run(), for as long as the fixture itself does.
		void attempt(std::string const& url)
		{
			lt::aux::proxy_settings const ps = make_proxy_settings(settings_pack::none);

			conn = std::make_shared<lt::aux::http_connection>(
				client_ios,
				res,
				[this, url](error_code const& ec,
					lt::aux::http_parser const&,
					span<char const>,
					lt::aux::http_connection&) {
					++handler_called;
					result_ec = ec;
					std::printf("RESPONSE: %s (%s)\n", url.c_str(), ec.message().c_str());
				},
				1024 * 1024,
				[this, url](lt::aux::http_connection&) {
					connected = true;
					std::printf("CONNECTED: %s\n", url.c_str());
				},
				lt::aux::http_filter_handler(),
				lt::aux::hostname_filter_handler(),
				&ssl_ctx);

			conn->get(url,
				seconds(1),
				&ps,
				5,
				"test/user-agent",
				std::nullopt,
				lt::aux::resolver_flags{},
				std::string(),
#if TORRENT_USE_I2P
				nullptr,
#endif
				false);
		}

		sim_config network_cfg;
		sim::simulation sim;
		sim::asio::io_context web_server;
		sim::asio::io_context client_ios;
		lt::aux::resolver res;
		sim::http_server http;
		lt::aux::ssl::context ssl_ctx;

		std::shared_ptr<lt::aux::http_connection> conn;
		bool connected = false;
		error_code result_ec;
		int handler_called = 0;
	};

} // anonymous namespace

// a well-configured client (trusting the server's cert directly, and
// requesting the hostname the cert actually covers) must succeed. This is
// the positive control for the negative tests below: it proves rejection in
// those tests comes from the specific defect under test, not from some
// unrelated misconfiguration of the harness itself.
TORRENT_TEST(https_accept_valid_connection)
{
	https_fixture f("hostname_cert.pem", "hostname_key.pem", "hostname_cert.pem");
	f.attempt("https://test-hostname.com:8080/test_file");
	f.sim.run();

	TEST_EQUAL(f.handler_called, 1);
	TEST_CHECK(f.connected);
	TEST_CHECK(!f.result_ec);
}

// the server presents a self-signed certificate that isn't signed by any
// CA the client trusts (the client's trust store is empty, as if the
// server's certificate came from an unknown/unrecognized issuer).
TORRENT_TEST(https_reject_unknown_root_cert)
{
	https_fixture f;
	f.attempt("https://10.0.0.2:8080/test_file");
	f.sim.run();

	TEST_EQUAL(f.handler_called, 1);
	TEST_CHECK(!f.connected);
	TEST_CHECK(is_ssl_error(f.result_ec));
}

// the server's certificate does not validate against the CA the client
// trusts (test/ssl/invalid_peer_certificate.pem shares its issuer's name
// with test/ssl/root_ca_cert.pem, but was not actually signed by it, and is
// also expired).
TORRENT_TEST(https_reject_invalid_certificate)
{
	https_fixture f("invalid_peer_certificate.pem",
		"invalid_peer_private_key.pem",
		"root_ca_cert.pem",
		[](lt::aux::ssl::context& ctx) {
			// invalid_peer_private_key.pem is encrypted (passphrase "test",
			// see test/ssl/regenerate_test_certificate.sh)
			error_code ec;
			ctx.set_password_callback(
				[](std::size_t, lt::aux::ssl::context::password_purpose) { return "test"; }, ec);
			TEST_CHECK(!ec);
		});
	f.attempt("https://10.0.0.2:8080/test_file");
	f.sim.run();

	TEST_EQUAL(f.handler_called, 1);
	TEST_CHECK(!f.connected);
	TEST_CHECK(is_ssl_error(f.result_ec));
}

// the certificate chains to a CA the client trusts (it trusts the exact
// self-signed certificate the server presents), but the requested hostname
// isn't the one the certificate covers, a raw IP address, rather than
// test-hostname.com (the certificate's CN/SAN).
TORRENT_TEST(https_reject_hostname_mismatch)
{
	https_fixture f("hostname_cert.pem", "hostname_key.pem", "hostname_cert.pem");
	f.attempt("https://10.0.0.2:8080/test_file");
	f.sim.run();

	TEST_EQUAL(f.handler_called, 1);
	TEST_CHECK(!f.connected);
	TEST_CHECK(is_ssl_error(f.result_ec));
}

#if defined TORRENT_USE_OPENSSL
// the certificate is otherwise valid (trusted directly, hostname matches),
// but is checked against a time far past its notAfter, isolating "expired"
// as the rejection reason independent of the chain-trust and hostname
// checks the other tests exercise. Only run under OpenSSL: overriding the
// verification time requires the raw API in sim::set_verification_time()
// (see http_server.hpp).
TORRENT_TEST(https_reject_expired_certificate)
{
	https_fixture f("hostname_cert.pem", "hostname_key.pem", "hostname_cert.pem");
	// hostname_cert.pem's notAfter is in the year 2300; validate as though
	// it's the year 2400 instead
	set_verification_time(f.ssl_ctx, 13569465600); // 2400-01-01T00:00:00Z
	f.attempt("https://test-hostname.com:8080/test_file");
	f.sim.run();

	TEST_EQUAL(f.handler_called, 1);
	TEST_CHECK(!f.connected);
	TEST_CHECK(is_ssl_error(f.result_ec));
}

// same as above, but with the certificate not yet valid: hostname_cert.pem's
// notBefore is the day this test was written, so validating as though it's
// the year 2020 puts the check time before the certificate's validity
// window starts, rather than after it ends.
TORRENT_TEST(https_reject_not_yet_valid_certificate)
{
	https_fixture f("hostname_cert.pem", "hostname_key.pem", "hostname_cert.pem");
	set_verification_time(f.ssl_ctx, 1577836800); // 2020-01-01T00:00:00Z
	f.attempt("https://test-hostname.com:8080/test_file");
	f.sim.run();

	TEST_EQUAL(f.handler_called, 1);
	TEST_CHECK(!f.connected);
	TEST_CHECK(is_ssl_error(f.result_ec));
}
#endif // TORRENT_USE_OPENSSL

// the client and server have no TLS protocol version in common (the server
// only offers TLS 1.3, the client only offers up to TLS 1.2), so the
// handshake fails during negotiation, before any certificate is even
// exchanged. This stands in for "unsupported crypto algorithm" more broadly:
// a real cipher-suite mismatch surfaces the same way, but restricting cipher
// lists is OpenSSL-version-specific, whereas restricting protocol versions
// is a portable way to force the same class of negotiation failure.
TORRENT_TEST(https_reject_unsupported_protocol)
{
	https_fixture f("server.pem", "server.pem", "", [](lt::aux::ssl::context& ctx) {
		// server: TLS 1.3 only
		ctx.set_options(lt::aux::ssl::context::no_tlsv1 | lt::aux::ssl::context::no_tlsv1_1
			| lt::aux::ssl::context::no_tlsv1_2);
	});

	// certificate validation is irrelevant to this test, only the protocol
	// negotiation is under test
	f.ssl_ctx.set_verify_mode(lt::aux::ssl::context::verify_none);
	// client: at most TLS 1.2
	f.ssl_ctx.set_options(lt::aux::ssl::context::no_tlsv1_3);

	f.attempt("https://10.0.0.2:8080/test_file");
	f.sim.run();

	TEST_EQUAL(f.handler_called, 1);
	TEST_CHECK(!f.connected);
	TEST_CHECK(is_ssl_error(f.result_ec));
}

#else

TORRENT_TEST(disabled) {}

#endif // TORRENT_USE_SSL
