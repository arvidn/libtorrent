/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/upnp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp" // print_endpoint
#include "libtorrent/http_parser.hpp"
#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/aux_/path.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>

using namespace lt;

using lt::portmap_protocol;

namespace {

broadcast_socket* sock = nullptr;
int g_port = 0;

char const* soap_add_response[] = {
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
	"</u:AddPortMapping></s:Body></s:Envelope>",
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:2\">"
	"</u:AddPortMapping></s:Body></s:Envelope>"};

char const* soap_delete_response[] = {
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
	"</u:DeletePortMapping></s:Body></s:Envelope>",
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:2\">"
	"</u:DeletePortMapping></s:Body></s:Envelope>"};

void incoming_msearch(udp::endpoint const& from, span<char const> buffer)
{
	http_parser p;
	bool error = false;
	p.incoming(buffer, error);
	if (error || !p.header_finished())
	{
		std::cout << "*** malformed HTTP from "
			<< print_endpoint(from) << std::endl;
		return;
	}

	if (p.method() != "m-search") return;

	std::cout << "< incoming m-search from " << from << std::endl;

	char msg[] = "HTTP/1.1 200 OK\r\n"
		"ST:upnp:rootdevice\r\n"
		"USN:uuid:000f-66d6-7296000099dc::upnp:rootdevice\r\n"
		"Location: http://127.0.0.1:%d/upnp.xml\r\n"
		"Server: Custom/1.0 UPnP/1.0 Proc/Ver\r\n"
		"EXT:\r\n"
		"Cache-Control:max-age=180\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	TORRENT_ASSERT(g_port != 0);
	char buf[sizeof(msg) + 30];
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	int const len = std::snprintf(buf, sizeof(buf), msg, g_port);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	error_code ec;
	sock->send(buf, len, ec);

	if (ec) std::cout << "*** error sending " << ec.message() << std::endl;
}

struct callback_info
{
	port_mapping_t mapping;
	int port;
	error_code ec;
	bool operator==(callback_info const& e)
	{ return mapping == e.mapping && port == e.port && !ec == !e.ec; }
};

std::list<callback_info> callbacks;

namespace // TODO: remove this nested namespace
{
	struct upnp_callback final : aux::portmap_callback
	{
		void on_port_mapping(port_mapping_t const mapping
			, address const& ip, int port
			, portmap_protocol const protocol, error_code const& err
			, portmap_transport) override
		{
			callback_info info = {mapping, port, err};
			callbacks.push_back(info);
			std::cout << "mapping: " << static_cast<int>(mapping)
				<< ", port: " << port << ", IP: " << ip
				<< ", proto: " << static_cast<int>(protocol)
				<< ", error: \"" << err.message() << "\"\n";
		}
	#ifndef TORRENT_DISABLE_LOGGING
		bool should_log_portmap(portmap_transport) const override
		{
			return true;
		}

		void log_portmap(portmap_transport, char const* msg) const override
		{
			std::cout << "UPnP: " << msg << std::endl;
			//TODO: store the log and verify that some key messages are there
		}
	#endif
	};
}

void run_upnp_test(char const* root_filename, char const* control_name, int igd_version)
{
	lt::io_context ios;

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
		return;
	}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	std::fprintf(xml_file, &buf[0], g_port);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	fclose(xml_file);

	std::ofstream xml(control_name, std::ios::trunc);
	xml.write(soap_add_response[igd_version-1], sizeof(soap_add_response[igd_version-1])-1);
	xml.close();

	sock = new broadcast_socket(uep("239.255.255.250", 1900));

	sock->open(&incoming_msearch, ios, ec);

	std::string user_agent = "test agent";

	upnp_callback cb;
	auto upnp_handler = std::make_shared<upnp>(ios, user_agent, cb, false);
	upnp_handler->start();
	upnp_handler->discover_device();

	for (int i = 0; i < 20; ++i)
	{
		ios.restart();
		ios.poll();
		if (!upnp_handler->router_model().empty()) break;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	std::cout << "router: " << upnp_handler->router_model() << std::endl;
	TEST_CHECK(!upnp_handler->router_model().empty());

	auto const mapping1 = upnp_handler->add_mapping(portmap_protocol::tcp, 500, ep("127.0.0.1", 500));
	auto const mapping2 = upnp_handler->add_mapping(portmap_protocol::udp, 501, ep("127.0.0.1", 501));

	for (int i = 0; i < 40; ++i)
	{
		ios.restart();
		ios.poll();
		if (callbacks.size() >= 2) break;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	callback_info expected1 = {mapping1, 500, error_code()};
	callback_info expected2 = {mapping2, 501, error_code()};
	TEST_EQUAL(std::count(callbacks.begin(), callbacks.end(), expected1), 1);
	TEST_EQUAL(std::count(callbacks.begin(), callbacks.end(), expected2), 1);

	xml.open(control_name, std::ios::trunc);
	xml.write(soap_delete_response[igd_version-1], sizeof(soap_delete_response[igd_version-1])-1);
	xml.close();

	upnp_handler->close();
	sock->close();

	for (int i = 0; i < 40; ++i)
	{
		ios.restart();
		ios.poll();
		if (callbacks.size() >= 4) break;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	// there should have been two DeleteMapping calls
	TEST_EQUAL(callbacks.size(), 4);

	stop_web_server();

	callbacks.clear();

	delete sock;
}

} // anonymous namespace

TORRENT_TEST(upnp)
{
	run_upnp_test(combine_path("..", "root1.xml").c_str(), "wipconn", 1);
	run_upnp_test(combine_path("..", "root2.xml").c_str(), "WANIPConnection", 1);
	run_upnp_test(combine_path("..", "root3.xml").c_str(), "WANIPConnection_2", 2);
}

TORRENT_TEST(upnp_max_mappings)
{
	lt::io_context ios;
	upnp_callback cb;
	auto upnp_handler = std::make_shared<upnp>(ios, "test agent", cb, false);

	for (int i = 0; i < 50; ++i)
	{
		auto const mapping = upnp_handler->add_mapping(portmap_protocol::tcp
			, 500 + i, ep("127.0.0.1", 500 + i));

		TEST_CHECK(mapping != port_mapping_t{-1});
	}
}
