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
#include "libtorrent/connection_queue.hpp"
#include "test.hpp"
#include "setup_transfer.hpp"
#include <fstream>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/intrusive_ptr.hpp>
#include <iostream>

using namespace libtorrent;

broadcast_socket* sock = 0;
int g_port = 0;

char upnp_xml[] = 
"<root>"
"<specVersion>"
"<major>1</major>"
"<minor>0</minor>"
"</specVersion>"
"<URLBase>http://127.0.0.1:%d</URLBase>"
"<device>"
"<deviceType>"
"urn:schemas-upnp-org:device:InternetGatewayDevice:1"
"</deviceType>"
"<presentationURL>http://192.168.0.1:80</presentationURL>"
"<friendlyName>D-Link Router</friendlyName>"
"<manufacturer>D-Link</manufacturer>"
"<manufacturerURL>http://www.dlink.com</manufacturerURL>"
"<modelDescription>Internet Access Router</modelDescription>"
"<modelName>D-Link Router</modelName>"
"<UDN>uuid:upnp-InternetGatewayDevice-1_0-12345678900001</UDN>"
"<UPC>123456789001</UPC>"
"<serviceList>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:L3Forwarding1</serviceId>"
"<controlURL>/Layer3Forwarding</controlURL>"
"<eventSubURL>/Layer3Forwarding</eventSubURL>"
"<SCPDURL>/Layer3Forwarding.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>"
"<friendlyName>WANDevice</friendlyName>"
"<manufacturer>D-Link</manufacturer>"
"<manufacturerURL>http://www.dlink.com</manufacturerURL>"
"<modelDescription>Internet Access Router</modelDescription>"
"<modelName>D-Link Router</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://support.dlink.com</modelURL>"
"<serialNumber>12345678900001</serialNumber>"
"<UDN>uuid:upnp-WANDevice-1_0-12345678900001</UDN>"
"<UPC>123456789001</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANCommonInterfaceConfig</serviceId>"
"<controlURL>/WANCommonInterfaceConfig</controlURL>"
"<eventSubURL>/WANCommonInterfaceConfig</eventSubURL>"
"<SCPDURL>/WANCommonInterfaceConfig.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>"
"<friendlyName>WAN Connection Device</friendlyName>"
"<manufacturer>D-Link</manufacturer>"
"<manufacturerURL>http://www.dlink.com</manufacturerURL>"
"<modelDescription>Internet Access Router</modelDescription>"
"<modelName>D-Link Router</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://support.dlink.com</modelURL>"
"<serialNumber>12345678900001</serialNumber>"
"<UDN>uuid:upnp-WANConnectionDevice-1_0-12345678900001</UDN>"
"<UPC>123456789001</UPC>"
"<serviceList>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANIPConnection</serviceId>"
"<controlURL>/WANIPConnection</controlURL>"
"<eventSubURL>/WANIPConnection</eventSubURL>"
"<SCPDURL>/WANIPConnection.xml</SCPDURL>"
"</service>"
"</serviceList>"
"</device>"
"</deviceList>"
"</device>"
"</deviceList>"
"</device>"
"</root>";

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
	//TODO: store the callbacks and verify that the ports were successful
}

int test_main()
{
	libtorrent::io_service ios;
	
	g_port = start_web_server();
	FILE* xml_file = fopen("upnp.xml", "w+");
	fprintf(xml_file, upnp_xml, g_port);
	fclose(xml_file);

	std::ofstream xml("WANIPConnection", std::ios::trunc);
	xml.write(soap_add_response, sizeof(soap_add_response)-1);
	xml.close();

	sock = new broadcast_socket(udp::endpoint(address_v4::from_string("239.255.255.250"), 1900)
		, &incoming_msearch);

	error_code ec;
	sock->open(ios, ec);

	std::string user_agent = "test agent";

	connection_queue cc(ios);
	boost::intrusive_ptr<upnp> upnp_handler = new upnp(ios, cc, address_v4::from_string("127.0.0.1")
		, user_agent, &callback, &log_callback, false);
	upnp_handler->discover_device();

	libtorrent::deadline_timer timer(ios);
	timer.expires_from_now(seconds(10), ec);
	timer.async_wait(boost::bind(&libtorrent::io_service::stop, boost::ref(ios)));

	ios.reset();
	ios.run(ec);

	int mapping1 = upnp_handler->add_mapping(upnp::tcp, 500, 500);
	int mapping2 = upnp_handler->add_mapping(upnp::udp, 501, 501);
	timer.expires_from_now(seconds(10), ec);
	timer.async_wait(boost::bind(&libtorrent::io_service::stop, boost::ref(ios)));

	ios.reset();
	ios.run(ec);

	xml.open("WANIPConnection", std::ios::trunc);
	xml.write(soap_delete_response, sizeof(soap_delete_response)-1);
	xml.close();

	std::cerr << "router: " << upnp_handler->router_model() << std::endl;
	TEST_CHECK(upnp_handler->router_model() == "D-Link Router");
	upnp_handler->close();
	sock->close();

	ios.reset();
	ios.run(ec);

	callback_info expected1 = {mapping1, 500, error_code()};
	callback_info expected2 = {mapping2, 501, error_code()};
	TEST_CHECK(std::count(callbacks.begin(), callbacks.end(), expected1) == 1);
	TEST_CHECK(std::count(callbacks.begin(), callbacks.end(), expected2) == 1);

	stop_web_server();

	delete sock;
	return 0;
}


