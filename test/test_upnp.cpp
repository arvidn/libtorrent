/*

Copyright (c) 2007-2010, 2012-2013, 2015-2022, Arvid Norberg
Copyright (c) 2015, Mike Tzou
Copyright (c) 2016-2018, 2020-2021, Alden Torres
Copyright (c) 2018, d-komarov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/upnp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_io.hpp" // print_endpoint
#include "libtorrent/aux_/http_parser.hpp"
#include "broadcast_socket.hpp"
#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/aux_/path.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>

using namespace lt;
using namespace std::chrono_literals;

using lt::portmap_protocol;

namespace {

int g_port = 0;
std::string g_local_address;

// when non-empty, used as the host in the advertised SSDP Location header
// instead of g_local_address, to let tests inject hosts upnp.cpp's SSRF
// check is expected to reject
std::string g_location_host_override;

#ifndef TORRENT_DISABLE_LOGGING
std::vector<std::string> g_log_messages;
#endif

// TEST-NET-3 (RFC 5737), never a real ISP-assigned address. Embedded in the
// fake device's GetExternalIPAddress response so a mapping callback can be
// proven to have come from the fake test device rather than from an
// unrelated, real UPnP router that happens to also be reachable on the same
// network the test runs on.
char const* const test_external_ip = "203.0.113.1";

string_view soap_add_response[] = {
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
	"<NewExternalIPAddress>203.0.113.1</NewExternalIPAddress>"
	"</u:AddPortMapping></s:Body></s:Envelope>"_sv,
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:2\">"
	"<NewExternalIPAddress>203.0.113.1</NewExternalIPAddress>"
	"</u:AddPortMapping></s:Body></s:Envelope>"_sv};

string_view soap_delete_response[] = {
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
	"</u:DeletePortMapping></s:Body></s:Envelope>"_sv,
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:2\">"
	"</u:DeletePortMapping></s:Body></s:Envelope>"_sv};

void send_rootdevice_response(broadcast_socket& sock, udp::endpoint const& to);

void incoming_msearch(broadcast_socket& sock, udp::endpoint const& from, span<char const> buffer)
{
	aux::http_parser p;
	bool error = false;
	p.incoming(buffer, error);
	if (error || !p.header_finished())
	{
		std::cout << "*** malformed HTTP from "
			<< aux::print_endpoint(from) << std::endl;
		return;
	}

	if (p.method() != "m-search") return;

	std::cout << "< incoming m-search from " << from << std::endl;

	send_rootdevice_response(sock, from);
}

void send_rootdevice_response(broadcast_socket& sock, udp::endpoint const& to)
{
	constexpr string_view msg = "HTTP/1.1 200 OK\r\n"
								"ST:upnp:rootdevice\r\n"
								"USN:uuid:000f-66d6-7296000099dc::upnp:rootdevice\r\n"
								"Location: http://%s:%d/upnp.xml\r\n"
								"Server: Custom/1.0 UPnP/1.0 Proc/Ver\r\n"
								"EXT:\r\n"
								"Cache-Control:max-age=180\r\n"
								"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n"_sv;

	TORRENT_ASSERT(g_port != 0);
	TORRENT_ASSERT(!g_local_address.empty());
	std::string const& location_host =
		g_location_host_override.empty() ? g_local_address : g_location_host_override;
	std::array<char, msg.size() + 65> buf;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	int const len =
		std::snprintf(buf.data(), buf.size(), msg.data(), location_host.c_str(), g_port);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	error_code ec;
	sock.send_to(buf.data(), len, to, ec);

	std::cout << "> sending response to " << aux::print_endpoint(to) << std::endl;

	if (ec) std::cout << "*** error sending " << ec.message() << std::endl;
}

struct callback_info
{
	port_mapping_t mapping;
	int port;
	error_code ec;
	address ip;
	bool operator==(callback_info const& e)
	{
		return mapping == e.mapping && port == e.port && !ec == !e.ec && ip == e.ip;
	}
};

std::list<callback_info> callbacks;

// polls the io_context in short bursts until either condition() returns
// true or timeout elapses, so a stuck expectation fails the test with a
// clear message instead of relying on the external per-test time limit.
bool wait_for(
	lt::io_context& ios, lt::seconds const timeout, std::function<bool()> const& condition)
{
	auto const deadline = lt::clock_type::now() + timeout;
	for (;;)
	{
		if (condition())
			return true;
		if (lt::clock_type::now() >= deadline)
			return false;
		ios.restart();
		ios.run_for(100ms);
	}
}

struct upnp_callback final : aux::portmap_callback
{
	void on_port_mapping(port_mapping_t const mapping,
		address const& ip,
		int port,
		portmap_protocol const protocol,
		error_code const& err,
		portmap_transport,
		aux::listen_socket_handle const&) override
	{
		callback_info info = {mapping, port, err, ip};
		callbacks.push_back(info);
		std::cout << "mapping: " << static_cast<int>(mapping) << ", port: " << port
				  << ", IP: " << ip << ", proto: " << static_cast<int>(protocol) << ", error: \""
				  << err.message() << "\"\n";
	}
#ifndef TORRENT_DISABLE_LOGGING
	bool should_log_portmap(portmap_transport) const override { return true; }

	void log_portmap(
		portmap_transport, char const* msg, aux::listen_socket_handle const&) const override
	{
		std::cout << "UPnP: " << msg << std::endl;
		g_log_messages.emplace_back(msg);
	}
#endif
};

aux::ip_interface pick_upnp_interface()
{
	lt::io_context ios;
	error_code ec;
	auto const routes = aux::enum_routes(ios, ec);
	if (ec)
	{
		std::cerr << "failed to enumerate routes: " << ec.message() << '\n';
		TEST_CHECK(false);
		return {};
	}
	auto const ifs = aux::enum_net_interfaces(ios, ec);
	if (ec)
	{
		std::cerr << "failed to enumerate network interfaces: " << ec.message() << '\n';
		TEST_CHECK(false);
		return {};
	}
	int idx = 0;
	for (auto const& face : ifs)
	{
		if (!face.interface_address.is_v4()) continue;
		std::cout << " - " << idx
			<< ' ' << face.interface_address.to_string()
			<< ' ' << int(static_cast<std::uint8_t>(face.state))
			<< ' ' << static_cast<std::uint32_t>(face.flags)
			<< ' ' << face.name << '\n';
		++idx;
	}

	std::printf("%-17s%-17s%s\n", "destination", "network", "interface");
	for (auto const& r : routes)
	{
		if (!r.destination.is_v4()) continue;
		std::printf("%-17s%-17s%s\n"
			, r.destination.to_string().c_str()
			, r.netmask.to_string().c_str()
			, r.name);
	}

	auto const iface = std::find_if(ifs.begin(), ifs.end(), [&](aux::ip_interface const& face)
		{
			if (!face.interface_address.is_v4()) return false;
			if (!(face.flags & aux::if_flags::up)) return false;
			if (!(face.flags & aux::if_flags::multicast)) return false;
			if (face.state != aux::if_state::up && face.state != aux::if_state::unknown) return false;

			auto const route = std::find_if(routes.begin(), routes.end(), [&](aux::ip_route const& r)
			{
				if (!r.destination.is_v4()) return false;
				if (string_view(face.name) != r.name) return false;
				return aux::match_addr_mask(make_address_v4("239.255.255.250")
					, r.destination.to_v4()
					, r.netmask);
			});
			if (route == routes.end()) return false;
			return true;
		});

	if (iface == ifs.end())
	{
		std::cerr << "could not find an IPv4 interface to run UPnP test over!\n";
		TEST_CHECK(false);
		return {};
	}
	std::cout << "starting upnp on: " << iface->interface_address.to_string() << ' ' << iface->name << '\n';
	return *iface;
}

void run_upnp_test(char const* root_filename, char const* control_name, int igd_version)
{
	// advertise the fake device on a real interface, not loopback: upnp.cpp's
	// SSRF check rejects loopback/link-local/multicast hosts. web_server.py
	// binds INADDR_ANY, so it answers on that address too.
	auto const ipf = pick_upnp_interface();
	g_local_address = ipf.interface_address.to_string();

	g_port = start_web_server();

	std::vector<char> buf;
	error_code ec;
	load_file(root_filename, buf, ec);
	buf.push_back(0);

	FILE* xml_file = fopen("upnp.xml", "w+");
	if (xml_file == nullptr)
	{
		std::printf("failed to open file 'upnp.xml': %s\n", strerror(errno));
		TEST_CHECK(false);
		stop_web_server();
		return;
	}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	std::fprintf(xml_file, &buf[0], g_local_address.c_str(), g_port);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	fclose(xml_file);

	std::ofstream xml(control_name, std::ios::trunc);
	xml.write(soap_add_response[igd_version - 1].data(),
		static_cast<std::streamsize>(soap_add_response[igd_version - 1].length()));
	xml.close();

	lt::io_context ios;
	// its udp::sockets reference ios during teardown, so must precede it here
	broadcast_socket sock(uep("239.255.255.250", 1900));
	aux::session_settings sett;

	sock.open([&sock](udp::endpoint const& from,
				  span<char const> buffer) { incoming_msearch(sock, from, buffer); },
		ios,
		ec);

	upnp_callback cb;
	auto upnp_handler = std::make_shared<upnp>(ios, sett, cb
		, ipf.interface_address.to_v4(), ipf.netmask.to_v4(), ipf.name, aux::listen_socket_handle());
	upnp_handler->start();

	// real NICs often don't loop multicast back to the sender (WiFi client
	// isolation, non-hairpinning switches), unlike CI's network, so unicast
	// the advertisement straight to upnp.cpp's SSDP port instead.
	send_rootdevice_response(sock, udp::endpoint(ipf.interface_address.to_v4(), 1900));

	// give up cleanly (rather than relying on the external per-test time
	// limit) if an expected outcome never materializes, so a regression
	// shows up as a clear, fast failure instead of an apparent hang.
	auto const bail = [&](char const* msg) {
		TEST_ERROR(msg);
		upnp_handler->close();
		sock.close();
		stop_web_server();
		callbacks.clear();
	};

	if (!wait_for(ios, 5s, [&] { return !upnp_handler->router_model().empty(); }))
	{
		bail("timed out waiting for router discovery");
		return;
	}

	std::cout << "router: " << upnp_handler->router_model() << std::endl;

	// the internal-client endpoint becomes the source address upnp.cpp binds
	// the outgoing AddPortMapping connection to (see bind_info_t in
	// upnp::create_port_mapping); it must be reachable from the control URL's
	// host, which is now a real interface address rather than loopback.
	auto const mapping1 = upnp_handler->add_mapping(
		portmap_protocol::tcp, 500, tcp::endpoint(ipf.interface_address.to_v4(), 500), "");
	auto const mapping2 = upnp_handler->add_mapping(
		portmap_protocol::udp, 501, tcp::endpoint(ipf.interface_address.to_v4(), 501), "");

	// the external IP in the expected callbacks pins these mappings to the
	// fake device: pick_upnp_interface() runs on a real, non-loopback
	// interface (required by the SSRF check below), so a real UPnP router
	// reachable on the same network may also answer the discovery broadcast
	// and complete these same mappings on its own. Without the IP check, its
	// callbacks would be indistinguishable from the fake device's.
	callback_info const expected1 = {mapping1, 500, error_code(), make_address(test_external_ip)};
	callback_info const expected2 = {mapping2, 501, error_code(), make_address(test_external_ip)};

	if (!wait_for(ios, 10s, [&] {
			return std::count(callbacks.begin(), callbacks.end(), expected1) >= 1
				&& std::count(callbacks.begin(), callbacks.end(), expected2) >= 1;
		}))
	{
		bail("timed out waiting for AddPortMapping responses");
		return;
	}

	TEST_EQUAL(std::count(callbacks.begin(), callbacks.end(), expected1), 1);
	TEST_EQUAL(std::count(callbacks.begin(), callbacks.end(), expected2), 1);

	std::size_t const callbacks_after_add = callbacks.size();

	xml.open(control_name, std::ios::trunc);
	xml.write(soap_delete_response[igd_version - 1].data(),
		static_cast<std::streamsize>(soap_delete_response[igd_version - 1].length()));
	xml.close();

	upnp_handler->close();
	sock.close();

	// unlike add-mapping, on_upnp_unmap_response() reports every device's
	// completion identically (an unspecified address and no device-specific
	// error code), so a real router that also completed the add above cannot
	// be told apart from the fake device here. Only require our own two
	// deletes to have arrived, not an exact total.
	if (!wait_for(ios, 10s, [&] { return callbacks.size() >= callbacks_after_add + 2; }))
	{
		TEST_ERROR("timed out waiting for DeletePortMapping responses");
		stop_web_server();
		callbacks.clear();
		return;
	}

	TEST_CHECK(callbacks.size() >= callbacks_after_add + 2);

	stop_web_server();

	callbacks.clear();
}

// regression test for the SSRF check in invalid_upnp_host()/invalid_upnp_address():
// a v4-mapped IPv6 literal (e.g. "::ffff:127.0.0.1") must be normalized to its
// embedded IPv4 address before the loopback/link-local/multicast checks run.
// advertise such a host in the SSDP Location header and confirm upnp.cpp
// rejects the rootdevice outright, without ever connecting to it.
// relies on log_portmap() to observe the rejection, so it can't run when
// logging is compiled out.
#ifndef TORRENT_DISABLE_LOGGING
void run_upnp_ssrf_test()
{
	auto const ipf = pick_upnp_interface();
	g_local_address = ipf.interface_address.to_string();

	error_code ec;
	lt::io_context ios;
	// its udp::sockets reference ios during teardown, so must precede it here
	broadcast_socket sock(uep("239.255.255.250", 1900));
	aux::session_settings sett;

	sock.open([&sock](udp::endpoint const& from,
				  span<char const> buffer) { incoming_msearch(sock, from, buffer); },
		ios,
		ec);

	g_port = 6666; // arbitrary; the malicious host is rejected before connecting
	g_location_host_override = "[::ffff:127.0.0.1]";

	upnp_callback cb;
	auto upnp_handler = std::make_shared<upnp>(ios,
		sett,
		cb,
		ipf.interface_address.to_v4(),
		ipf.netmask.to_v4(),
		ipf.name,
		aux::listen_socket_handle());
	upnp_handler->start();

	// real NICs often don't loop multicast back to the sender (WiFi client
	// isolation, non-hairpinning switches), unlike CI's network, so unicast
	// the advertisement straight to upnp.cpp's SSDP port instead.
	send_rootdevice_response(sock, udp::endpoint(ipf.interface_address.to_v4(), 1900));

	// note: router_model() is not checked here, real UPnP routers on the test
	// network may legitimately answer the multicast search and get accepted;
	// this test only cares whether the malicious host was rejected.
	auto const found_rejection = [&] {
		return std::any_of(
			g_log_messages.begin(), g_log_messages.end(), [](std::string const& msg) {
				return msg.find("invalid address") != std::string::npos
					&& msg.find("::ffff:127.0.0.1") != std::string::npos;
			});
	};

	if (!wait_for(ios, 5s, found_rejection))
		TEST_ERROR("timed out waiting for the malicious control URL to be rejected");

	upnp_handler->close();
	sock.close();

	g_location_host_override.clear();
	g_log_messages.clear();
}
#endif // TORRENT_DISABLE_LOGGING

// runs a device description advertising a malformed control URL (an
// injection attempt, or an unsupported scheme) for its WANIPConnection
// service, and verifies upnp.cpp rejects it outright rather than acting on
// it: no port mapping ever succeeds through that device.
void run_upnp_reject_test(char const* root_filename)
{
	auto const ipf = pick_upnp_interface();
	g_local_address = ipf.interface_address.to_string();

	g_port = start_web_server();

	std::vector<char> buf;
	error_code ec;
	load_file(root_filename, buf, ec);
	buf.push_back(0);

	FILE* xml_file = fopen("upnp.xml", "w+");
	if (xml_file == nullptr)
	{
		std::printf("failed to open file 'upnp.xml': %s\n", strerror(errno));
		TEST_CHECK(false);
		stop_web_server();
		return;
	}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	std::fprintf(xml_file, &buf[0], g_local_address.c_str(), g_port);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	fclose(xml_file);

	lt::io_context ios;
	// its udp::sockets reference ios during teardown, so must precede it here
	broadcast_socket sock(uep("239.255.255.250", 1900));
	aux::session_settings sett;

	sock.open([&sock](udp::endpoint const& from,
				  span<char const> buffer) { incoming_msearch(sock, from, buffer); },
		ios,
		ec);

	upnp_callback cb;
	auto upnp_handler = std::make_shared<upnp>(ios,
		sett,
		cb,
		ipf.interface_address.to_v4(),
		ipf.netmask.to_v4(),
		ipf.name,
		aux::listen_socket_handle());
	upnp_handler->start();

	// real NICs often don't loop multicast back to the sender (WiFi client
	// isolation, non-hairpinning switches), unlike CI's network, so unicast
	// the advertisement straight to upnp.cpp's SSDP port instead.
	send_rootdevice_response(sock, udp::endpoint(ipf.interface_address.to_v4(), 1900));

	if (!wait_for(ios, 5s, [&] { return !upnp_handler->router_model().empty(); }))
	{
		TEST_ERROR("timed out waiting for router discovery");
		upnp_handler->close();
		sock.close();
		stop_web_server();
		return;
	}

	// the device itself is discovered (router_model() is populated from the
	// device description before the control URL is validated), but its
	// WANIPConnection control URL is rejected, so no mapping can ever be
	// requested through it
	std::cout << "router: " << upnp_handler->router_model() << std::endl;

	auto const mapping1 = upnp_handler->add_mapping(
		portmap_protocol::tcp, 500, tcp::endpoint(ipf.interface_address.to_v4(), 500), "");
	TORRENT_UNUSED(mapping1);

#ifndef TORRENT_DISABLE_LOGGING
	// the malformed control URL must have been rejected. This is checked via
	// the log rather than the callback/mapping count: on a machine where a
	// real UPnP router is reachable on the same network, that router also
	// answers the discovery broadcast and may complete a real mapping of its
	// own, which would make a callback-count assertion depend on unrelated
	// network state instead of on this fake device's (rejected) response.
	auto const rejected = [&] {
		return std::any_of(g_log_messages.begin(), g_log_messages.end(), [](std::string const& m) {
			return m.find("rejecting invalid control URL") != std::string::npos
				&& m.find("udp://") != std::string::npos;
		});
	};
	if (!wait_for(ios, 5s, rejected))
		TEST_ERROR("timed out waiting for the malformed control URL to be rejected");
#else
	for (int i = 0; i < 40; ++i)
	{
		ios.restart();
		ios.run_for(100ms);
	}
#endif

	upnp_handler->close();
	sock.close();
	stop_web_server();

	callbacks.clear();
#ifndef TORRENT_DISABLE_LOGGING
	g_log_messages.clear();
#endif
}

} // anonymous namespace

TORRENT_TEST(upnp_invalid_control_url_scheme)
{
	// the WANIPConnection controlURL uses udp://, which is meaningless for
	// UPnP SOAP control requests (always plain HTTP(S)); upnp.cpp must
	// reject it rather than trying to connect to it
	run_upnp_reject_test(combine_path("..", "root_bad_control_url.xml").c_str());
}

TORRENT_TEST(upnp_wipconn)
{
	run_upnp_test(combine_path("..", "root1.xml").c_str(), "wipconn", 1);
}

TORRENT_TEST(upnp_wanipconnection)
{
	run_upnp_test(combine_path("..", "root2.xml").c_str(), "WANIPConnection", 1);
}

TORRENT_TEST(upnp_wanipconnection2)
{
	run_upnp_test(combine_path("..", "root3.xml").c_str(), "WANIPConnection", 2);
}

#ifndef TORRENT_DISABLE_LOGGING
TORRENT_TEST(upnp_ssrf_v4_mapped_loopback) { run_upnp_ssrf_test(); }
#endif

TORRENT_TEST(upnp_max_mappings)
{
	lt::io_context ios;

	// pick an appropriate interface to run this test on
	auto const ipf = pick_upnp_interface();
	aux::session_settings sett;

	upnp_callback cb;
	auto upnp_handler = std::make_shared<upnp>(ios, sett, cb
		, ipf.interface_address.to_v4(), ipf.netmask.to_v4(), ipf.name, aux::listen_socket_handle());

	for (int i = 0; i < 50; ++i)
	{
		auto const mapping = upnp_handler->add_mapping(portmap_protocol::tcp
			, 500 + i, ep("127.0.0.1", 500 + i), "");

		TEST_CHECK(mapping != port_mapping_t{-1});
	}
}

TORRENT_TEST(upnp_content_type)
{
	TEST_CHECK(aux::is_upnp_xml_content_type("text/xml"));
	TEST_CHECK(aux::is_upnp_xml_content_type("Text/XML"));
	TEST_CHECK(aux::is_upnp_xml_content_type(" text/xml; charset=\"utf-8\""));
	TEST_CHECK(aux::is_upnp_xml_content_type("application/soap+xml; charset=utf-8"));
	TEST_CHECK(aux::is_upnp_xml_content_type("application/xml"));
	TEST_CHECK(aux::is_upnp_xml_content_type("text/soap+xml"));

	TEST_CHECK(!aux::is_upnp_xml_content_type("text/html;charset=utf-8"));
	TEST_CHECK(!aux::is_upnp_xml_content_type("x-text/xml"));
	TEST_CHECK(!aux::is_upnp_xml_content_type("application/json"));
}
