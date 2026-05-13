/*

Copyright (c) 2019-2021, 2026, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Session-level fuzzer for libtorrent's UPnP client state machine.
//
// The fuzzer acts as a UPnP router device. For each input, a fresh UPnP
// instance is created with 127.0.0.1 as the listen address. The fuzzer:
//
//   1. Starts a TCP server on an ephemeral port for all HTTP interactions.
//   2. Calls upnp::start(), polls the io_context so the M-SEARCH is sent,
//      then injects a hardcoded SSDP response pointing to the TCP server.
//   3. Serves a hardcoded XML device description (WANIPConnection:1) whose
//      controlURL points at the same server, ensuring discovery always
//      succeeds and the state machine advances past the SSDP and XML phases.
//   4. Feeds the fuzz input as raw HTTP responses to the subsequent SOAP
//      calls, in order:
//
//        connection 0  XML description   (hardcoded, always valid)
//        connection 1  GetExternalIPAddress
//        connection 2  AddPortMapping (mapping 0 -- tcp/6881)
//        connection 3  AddPortMapping (mapping 1 -- udp/6882)
//        ...           additional connections for error-code retries
//
//      After upnp::close() the remaining DeletePortMapping connections arrive
//      and consume further fuzz messages.
//
// Wire format: length-prefixed messages, 2 bytes big-endian per length
// followed by that many raw bytes.  If fewer messages are supplied than
// connections made, the corresponding TCP connections are closed
// immediately, exercising the incomplete-response error paths.
//
// Interesting code paths exercised:
//   on_upnp_xml                     -- XML body parsing, URL construction
//   on_upnp_get_ip_address_response -- GetExternalIPAddress SOAP parsing
//   on_upnp_map_response            -- AddPortMapping error-code dispatch:
//       error 725 -- retry without lease duration
//       error 727 -- report error immediately
//       error 718 or 501 -- retry with random external port (up to 4 times)
//       any other error  -- report error
//   on_upnp_unmap_response          -- DeletePortMapping response handling

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if DEBUG_LOGGING
#include <iostream>
#endif

#include "libtorrent/upnp.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/portmap.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/io_bytes.hpp"

namespace {

	struct fuzz_portmap_cb final : lt::aux::portmap_callback
	{
		void on_port_mapping(lt::port_mapping_t,
			lt::address const&,
			int,
			lt::portmap_protocol,
			lt::error_code const&,
			lt::portmap_transport,
			lt::aux::listen_socket_handle const&) override
		{}

#ifndef TORRENT_DISABLE_LOGGING
		bool should_log_portmap(lt::portmap_transport) const override { return false; }
		void log_portmap(
			lt::portmap_transport, char const*, lt::aux::listen_socket_handle const&) const override
		{}
#endif
	};

	// Decode the length-prefixed wire format: [2-byte big-endian length][data]...
	std::vector<std::vector<char>> parse_messages(std::uint8_t const* data, std::size_t size)
	{
		std::vector<std::vector<char>> out;
		std::uint8_t const* ptr = data;
		std::uint8_t const* const end = data + size;
		while (end - ptr >= 2)
		{
			std::size_t const len = lt::aux::read_uint16(ptr);
			std::size_t const n = std::min(len, std::size_t(end - ptr));
			out.emplace_back(ptr, ptr + n);
			ptr += n;
		}
		return out;
	}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	auto const responses = parse_messages(data, size);

	lt::io_context ios;
	lt::aux::session_settings settings;
	fuzz_portmap_cb cb;
	auto const ls = lt::aux::listen_socket_handle(std::make_shared<lt::aux::listen_socket_t>());

	// TCP server -- the UPnP client connects here for all HTTP interactions.
	lt::error_code ec;
	lt::tcp::acceptor acc(ios);
	acc.open(lt::tcp::v4(), ec);
	if (ec)
	{
#if DEBUG_LOGGING
		std::cout << "acceptor open failed: " << ec.message() << '\n';
#endif
		return 0;
	}
	acc.set_option(lt::tcp::acceptor::reuse_address(true), ec);
	acc.bind(lt::tcp::endpoint(lt::make_address_v4("127.0.0.1"), 0), ec);
	if (ec)
	{
#if DEBUG_LOGGING
		std::cout << "acceptor bind failed: " << ec.message() << '\n';
#endif
		return 0;
	}
	acc.listen(16, ec);
	if (ec)
	{
#if DEBUG_LOGGING
		std::cout << "acceptor listen failed: " << ec.message() << '\n';
#endif
		return 0;
	}
	int const http_port = acc.local_endpoint(ec).port();

	// Minimal XML device description whose controlURL points at our server.
	// This is served for the first HTTP connection (the "GET /desc.xml"
	// request), ensuring the state machine always advances past XML parsing.
	std::ostringstream xml_oss;
	xml_oss << "<?xml version=\"1.0\"?>" << "<root>" << "<device>"
			<< "<modelName>FuzzRouter</modelName>" << "<serviceList>" << "<service>"
			<< "<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
			<< "<controlURL>http://127.0.0.1:" << http_port << "/ctrl</controlURL>" << "</service>"
			<< "</serviceList>" << "</device>" << "</root>";
	std::string const xml_body = xml_oss.str();

	std::ostringstream xml_http_oss;
	xml_http_oss << "HTTP/1.1 200 OK\r\n"
				 << "Content-Type: text/xml\r\n"
				 << "Content-Length: " << xml_body.size() << "\r\n"
				 << "\r\n"
				 << xml_body;
	std::string const xml_http_resp = xml_http_oss.str();

	// SSDP response with our server's address as the device URL.
	std::ostringstream ssdp_oss;
	ssdp_oss << "HTTP/1.1 200 OK\r\n"
			 << "ST:upnp:rootdevice\r\n"
			 << "USN:uuid:000f-66d6-7296000099dc::upnp:rootdevice\r\n"
			 << "Location: http://127.0.0.1:" << http_port << "/desc.xml\r\n"
			 << "EXT:\r\n"
			 << "\r\n";
	std::string const ssdp_resp = ssdp_oss.str();

	// Self-re-arming accept handler: serves the hardcoded XML description for
	// the first connection, then the fuzz messages for subsequent ones.
	int conn_idx = 0;
	bool stop = false;
	std::function<void()> arm_accept = [&]() {
		if (stop) return;
		acc.async_accept([&](lt::error_code const& accept_ec, lt::tcp::socket s) {
			if (accept_ec || stop)
			{
#if DEBUG_LOGGING
				std::cout << "accept aborted (ec=" << accept_ec.message() << ", stop=" << int(stop)
						  << ")\n";
#endif
				return;
			}

#if DEBUG_LOGGING
			std::array<char, 256> peek{};
			lt::error_code read_ec;
			std::size_t const got = s.read_some(boost::asio::buffer(peek), read_ec);
			std::string line(peek.data(), peek.data() + std::min<std::size_t>(got, peek.size()));
			auto const nl = line.find('\n');
			if (nl != std::string::npos) line.resize(nl);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.pop_back();
			std::cout << "accept conn_idx=" << conn_idx << ", got " << got << " bytes, request: \""
					  << line << "\"\n";
#endif

			lt::error_code write_ec;
			if (conn_idx == 0)
			{
				boost::asio::write(s, boost::asio::buffer(xml_http_resp), write_ec);
#if DEBUG_LOGGING
				std::cout << " -> wrote XML description (" << xml_http_resp.size()
						  << " bytes, ec=" << write_ec.message() << ")\n";
#endif
			}
			else
			{
				auto const ri = static_cast<std::size_t>(conn_idx) - 1;
				if (ri < responses.size() && !responses[ri].empty())
				{
					boost::asio::write(s,
						boost::asio::buffer(responses[ri].data(), responses[ri].size()),
						write_ec);
#if DEBUG_LOGGING
					std::cout << " -> wrote fuzz message " << ri << " (" << responses[ri].size()
							  << " bytes, ec=" << write_ec.message() << ")\n";
#endif
				}
#if DEBUG_LOGGING
				else
				{
					std::cout << " -> no fuzz message for ri=" << ri << ", closing\n";
				}
#endif
			}
			++conn_idx;
			s.close(write_ec);
			arm_accept();
		});
	};
	arm_accept();

	auto up = std::make_shared<lt::upnp>(ios,
		settings,
		cb,
		lt::make_address_v4("127.0.0.1"),
		lt::make_address_v4("255.0.0.0"),
		"",
		ls);

	up->add_mapping(lt::portmap_protocol::tcp,
		6881,
		lt::tcp::endpoint{lt::make_address_v4("127.0.0.1"), 6881},
		"");
	up->add_mapping(lt::portmap_protocol::udp,
		6882,
		lt::tcp::endpoint{lt::make_address_v4("127.0.0.1"), 6882},
		"");

	up->start();
	ios.restart();
	ios.poll();

	// Inject the SSDP response into the UPnP's multicast socket
	// (bound to 127.0.0.1:1900).  The socket receives unicast UDP
	// packets addressed to that endpoint in addition to multicast ones.
	{
		lt::udp::socket sender(ios);
		sender.open(lt::udp::v4(), ec);
		if (ec)
		{
#if DEBUG_LOGGING
			std::cout << "SSDP sender open failed: " << ec.message() << '\n';
#endif
			return 0;
		}
		sender.bind(lt::udp::endpoint(lt::make_address_v4("127.0.0.1"), 0), ec);
		if (ec)
		{
#if DEBUG_LOGGING
			std::cout << "SSDP sender bind failed: " << ec.message() << '\n';
#endif
			return 0;
		}
		sender.send_to(boost::asio::buffer(ssdp_resp),
			lt::udp::endpoint(lt::make_address_v4("127.0.0.1"), 1900),
			0,
			ec);
		if (ec)
		{
#if DEBUG_LOGGING
			std::cout << "SSDP send_to failed: " << ec.message() << '\n';
#endif
			return 0;
		}
	}

	// Process the SSDP response, which triggers the XML GET, which triggers
	// GetExternalIPAddress, which triggers the AddPortMapping chain.
	// With loopback TCP, all async operations complete within a few poll()
	// iterations.
	for (int i = 0; i < 10; ++i)
	{
		ios.restart();
		if (ios.poll() == 0) break;
	}

	// Trigger deletion of any successfully registered port mappings.
	up->close();

	for (int i = 0; i < 10; ++i)
	{
		ios.restart();
		if (ios.poll() == 0) break;
	}

	// Cancel the pending async_accept and flush its completion handler
	// before the captured locals go out of scope.
	stop = true;
	acc.cancel(ec);
	ios.restart();
	ios.poll();

	return 0;
}
