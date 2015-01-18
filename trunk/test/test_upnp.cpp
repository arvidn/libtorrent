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
#include "test.hpp"
#include "setup_transfer.hpp"
#include <fstream>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/smart_ptr.hpp>
#include <iostream>

using namespace libtorrent;

broadcast_socket* sock = 0;
int g_port = 0;

char soap_add_response[] =
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
	"</u:AddPortMapping></s:Body></s:Envelope>";

char soap_delete_response[] =
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
	"<s:Body><u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
	"</u:DeletePortMapping></s:Body></s:Envelope>";

void incoming_msearch(udp::endpoint const& from, char* buffer
	, int size)
{
	http_parser p;
	bool error = false;
	p.incoming(buffer::const_interval(buffer, buffer + size), error);
	if (error || !p.header_finished())
	{
		std::cerr << "*** malformed HTTP from "
			<< print_endpoint(from) << std::endl;
		return;
	}

	if (p.method() != "m-search") return;

	std::cerr << "< incoming m-search from " << from << std::endl;

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
	int len = snprintf(buf, sizeof(buf), msg, g_port);

	error_code ec;
	sock->send(buf, len, ec);

	if (ec) std::cerr << "*** error sending " << ec.message() << std::endl;
}

void log_callback(char const* err)
{
	std::cerr << "UPnP: " << err << std::endl;
	//TODO: store the log and verify that some key messages are there
}

struct callback_info
{
	int mapping;
	int port;
	error_code ec;
	bool operator==(callback_info const& e)
	{ return mapping == e.mapping && port == e.port && !ec == !e.ec; }
};

std::list<callback_info> callbacks;

void callback(int mapping, address const& ip, int port, error_code const& err)
{
	callback_info info = {mapping, port, err};
	callbacks.push_back(info);
	std::cerr << "mapping: " << mapping << ", port: " << port << ", IP: " << ip
		<< ", error: \"" << err.message() << "\"\n";
}

int run_upnp_test(char const* root_filename, char const* router_model, char const* control_name)
{
	libtorrent::io_service ios;
	
	g_port = start_web_server();

	std::vector<char> buf;
	error_code ec;
	load_file(root_filename, buf, ec);
	buf.push_back(0);

	FILE* xml_file = fopen("upnp.xml", "w+");
	if (xml_file == NULL)
	{
		fprintf(stderr, "failed to open file 'upnp.xml': %s\n", strerror(errno));
		TEST_CHECK(false);
		return 1;
	}
	fprintf(xml_file, &buf[0], g_port);
	fclose(xml_file);

	std::ofstream xml(control_name, std::ios::trunc);
	xml.write(soap_add_response, sizeof(soap_add_response)-1);
	xml.close();

	sock = new broadcast_socket(udp::endpoint(address_v4::from_string("239.255.255.250")
		, 1900));

	sock->open(&incoming_msearch, ios, ec);

	std::string user_agent = "test agent";

	boost::shared_ptr<upnp> upnp_handler = boost::make_shared<upnp>(boost::ref(ios)
		, address_v4::from_string("127.0.0.1")
		, user_agent, &callback, &log_callback, false);
	upnp_handler->start();
	upnp_handler->discover_device();

	for (int i = 0; i < 20; ++i)
	{
		ios.reset();
		ios.poll(ec);
		if (ec)
		{
			fprintf(stderr, "io_service::run(): %s\n", ec.message().c_str());
			ec.clear();
			break;
		}
		if (upnp_handler->router_model() != "") break;
		test_sleep(100);
	}

	std::cerr << "router: " << upnp_handler->router_model() << std::endl;
	TEST_EQUAL(upnp_handler->router_model(), router_model);

	int mapping1 = upnp_handler->add_mapping(upnp::tcp, 500, 500);
	int mapping2 = upnp_handler->add_mapping(upnp::udp, 501, 501);

	for (int i = 0; i < 40; ++i)
	{
		ios.reset();
		ios.poll(ec);
		if (ec)
		{
			fprintf(stderr, "io_service::run(): %s\n", ec.message().c_str());
			ec.clear();
			break;
		}
		if (callbacks.size() >= 2) break;
		test_sleep(100);
	}

	callback_info expected1 = {mapping1, 500, error_code()};
	callback_info expected2 = {mapping2, 501, error_code()};
	TEST_EQUAL(std::count(callbacks.begin(), callbacks.end(), expected1), 1);
	TEST_EQUAL(std::count(callbacks.begin(), callbacks.end(), expected2), 1);

	xml.open(control_name, std::ios::trunc);
	xml.write(soap_delete_response, sizeof(soap_delete_response)-1);
	xml.close();

	upnp_handler->close();
	sock->close();

	for (int i = 0; i < 40; ++i)
	{
		ios.reset();
		ios.poll(ec);
		if (ec)
		{
			fprintf(stderr, "io_service::run(): %s\n", ec.message().c_str());
			ec.clear();
			break;
		}
		if (callbacks.size() >= 4) break;
		test_sleep(100);
	}

	// there should have been two DeleteMapping calls
	TEST_EQUAL(callbacks.size(), 4);

	stop_web_server();

	callbacks.clear();

	delete sock;
	return 0;
}

int test_main()
{
	run_upnp_test(combine_path("..", "root1.xml").c_str(), "Xtreme N GIGABIT Router", "wipconn");
	run_upnp_test(combine_path("..", "root2.xml").c_str(), "D-Link Router", "WANIPConnection");
	return 0;
}

