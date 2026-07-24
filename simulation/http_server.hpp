/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef HTTP_SERVER_HPP_INCLUDED
#define HTTP_SERVER_HPP_INCLUDED

#include "simulator/simulator.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/socket.hpp"
#if TORRENT_USE_SSL
#include "libtorrent/aux_/polymorphic_socket.hpp"
#include "libtorrent/aux_/proxy_base.hpp" // for wrap_allocator, used by ssl_stream
#include "libtorrent/aux_/ssl.hpp"
#include "libtorrent/aux_/ssl_stream.hpp"
#include "libtorrent/error_code.hpp"
#endif
#include <functional>
#include <memory>
#include <optional>
#include <string>
#if defined TORRENT_USE_OPENSSL
#include <ctime>
#endif

using libtorrent::operator""_bit;

namespace sim {
	std::string trim(std::string s);

	std::string lower_case(std::string s);

	std::string normalize(const std::string& s);

	// returns the index to the last byte of the request, or -1 if the buffer
	// does not contain a full http request
	int find_request_len(char const* buf, int len);

	struct http_request
	{
		std::string method;
		std::string req;
		std::string path;
		std::map<std::string, std::string> headers;
	};

	http_request parse_request(char const* start, int len);

	// builds an HTTP response buffer
	std::string send_response(
		int code, char const* status_message, int len = 0, char const** extra_header = NULL);

	using http_server_flags_t =
		lt::flags::bitfield_flag<std::uint8_t, struct http_server_flags_tag>;

#if TORRENT_USE_SSL
	// resolves a filename under test/ssl/ to a path usable from a
	// simulation test binary's working directory.
	std::string ssl_fixture_path(std::string const& name);

	// true if ec is an SSL/TLS-layer error (certificate verification,
	// handshake negotiation, etc), as opposed to e.g. a plain connection
	// failure.
	bool is_ssl_error(lt::error_code const& ec);

#if defined TORRENT_USE_OPENSSL
	// overrides the time used to check a peer certificate's notBefore/
	// notAfter, letting a test validate an otherwise-valid certificate as
	// though it were some other date. This is raw OpenSSL API (there's no
	// portable equivalent through boost::asio::ssl::context or the aux::ssl
	// abstraction), so it isn't available when built against GnuTLS.
	void set_verification_time(lt::aux::ssl::context& ctx, std::time_t t);
#endif
#endif

	// This is a very simple http server that only supports a single concurrent
	// connection
	struct http_server
	{
		static constexpr http_server_flags_t keep_alive = 0_bit;
		// behave like an HTTP/1.0 server: respond with an HTTP/1.0 status line
		// and close the connection after each response. HTTP/1.0 has no
		// persistent connections, and the Connection header is an HTTP/1.1
		// mechanism, so no "Connection: close" is sent, the client must detect
		// the close from the protocol version (and the socket closing).
		static constexpr http_server_flags_t http_1_0 = 1_bit;
		// wrap the connection in TLS, using a self-signed test certificate
		// (test/ssl/server.pem). Only available when built with
		// TORRENT_USE_SSL.
		static constexpr http_server_flags_t https = 2_bit;

		http_server(asio::io_context& ios,
			unsigned short listen_port,
			http_server_flags_t flags = http_server::keep_alive
#if TORRENT_USE_SSL
			// only meaningful together with the https flag. cert_file and
			// key_file each name a PEM file under test/ssl/ (by default both
			// point at server.pem, which bundles a certificate and its
			// private key in one file; use_certificate_file()/
			// use_private_key_file() each just scan for their own PEM block
			// type and ignore the rest, so pointing both at the same bundle
			// works). ssl_setup, when set, is called with the freshly
			// constructed ssl::context before the certificate and private
			// key are loaded, so tests can install a password callback
			// (needed by some encrypted test/ssl/ key files) or apply other
			// restrictions (e.g. supported TLS versions) ahead of time.
			,
			std::string cert_file = "server.pem",
			std::string key_file = "server.pem",
			std::function<void(lt::aux::ssl::context&)> ssl_setup = nullptr
#endif
		);

		void stop();

		using handler_t = std::function<std::string(
			std::string, std::string, std::map<std::string, std::string>&)>;
		using generator_t = std::function<std::string(std::int64_t, std::int64_t)>;

		void register_handler(std::string const& path, handler_t h);
		void register_content(std::string const& path, std::int64_t const size, generator_t gen);
		void register_redirect(std::string const& path, std::string const& target);
		void register_stall_handler(std::string const& path);

		// the number of TCP connections that have been accepted so far
		int accepted_connections() const { return m_accepted_connections; }

	private:
#if TORRENT_USE_SSL
		// a single-connection HTTP(S) server only ever needs to distinguish
		// between a plain TCP connection and one wrapped in TLS, so this uses
		// a smaller variant than libtorrent::aux::socket_type (which also
		// covers proxies, uTP, i2p and WebRTC).
		using connection_t =
			lt::aux::polymorphic_socket<lt::tcp::socket, lt::aux::ssl_stream<lt::tcp::socket>>;
#else
		using connection_t = lt::tcp::socket;
#endif

		void on_accept(boost::system::error_code const& ec, sim::asio::ip::tcp::socket peer);
#if TORRENT_USE_SSL
		void on_handshake(boost::system::error_code const& ec);
#endif
		void read();
		void on_read(boost::system::error_code const& ec, size_t bytes_transferred);
		void on_write(boost::system::error_code const& ec, size_t bytes_transferred, bool close);
		void close_connection();
		void finish_close(
			boost::system::error_code const& shutdown_ec = boost::system::error_code());

		asio::io_context& m_ios;

		asio::ip::tcp::acceptor m_listen_socket;

#if TORRENT_USE_SSL
		std::unique_ptr<lt::aux::ssl::context> m_ssl_ctx;
#endif
		// left empty until the first connection is accepted: the active
		// alternative is constructed in place (emplace), since the underlying
		// std::variant has no move-assignment operator when one of its
		// alternatives (ssl_stream) is move-constructible but not
		// move-assignable.
		std::optional<connection_t> m_connection;
		asio::ip::tcp::endpoint m_ep;

		std::unordered_map<std::string, handler_t> m_handlers;
		std::set<std::string> m_stall_handlers;

		// read buffer, we receive bytes into this buffer for the connection
		std::string m_recv_buffer;

		// the number of bytes of m_recv_buffer that we've actually read data into.
		// The remaining is uninitialized, possibly being read into in an async call
		int m_bytes_used;

		std::string m_send_buffer;

		// set to true when shutting down
		bool m_close;

		http_server_flags_t m_flags;

		// counts the number of accepted TCP connections
		int m_accepted_connections = 0;
	};

}

#endif
