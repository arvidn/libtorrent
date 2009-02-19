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

#include "libtorrent/parse_url.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/broadcast_socket.hpp"
#ifndef TORRENT_DISABLE_DHT
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#endif
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/bind.hpp>

#include "test.hpp"

using namespace libtorrent;
using namespace boost::tuples;
using boost::bind;

tuple<int, int, bool> feed_bytes(http_parser& parser, char const* str)
{
	tuple<int, int, bool> ret(0, 0, false);
	tuple<int, int, bool> prev(0, 0, false);
	for (int chunks = 1; chunks < 70; ++chunks)
	{
		ret = make_tuple(0, 0, false);
		parser.reset();
		buffer::const_interval recv_buf(str, str);
		for (; *str;)
		{
			int chunk_size = (std::min)(chunks, int(strlen(recv_buf.end)));
			if (chunk_size == 0) break;
			recv_buf.end += chunk_size;
			int payload, protocol;
			bool error = false;
			tie(payload, protocol) = parser.incoming(recv_buf, error);
			ret.get<0>() += payload;
			ret.get<1>() += protocol;
			ret.get<2>() += error;
//			std::cerr << payload << ", " << protocol << ", " << chunk_size << std::endl;
			TORRENT_ASSERT(payload + protocol == chunk_size);
		}
		TEST_CHECK(prev == make_tuple(0, 0, false) || ret == prev);
		prev = ret;
	}
	return ret;
}

void parser_callback(std::string& out, int token, char const* s, char const* val)
{
	switch (token)
	{
		case xml_start_tag: out += "B"; break;
		case xml_end_tag: out += "F"; break;
		case xml_empty_tag: out += "E"; break;
		case xml_declaration_tag: out += "D"; break;
		case xml_comment: out += "C"; break;
		case xml_string: out += "S"; break;
		case xml_attribute: out += "A"; break;
		case xml_parse_error: out += "P"; break;
		default: TEST_CHECK(false);
	}
	out += s;
	if (token == xml_attribute)
	{
		TEST_CHECK(val != 0);
		out += "V";
		out += val;
	}
	else
	{
		TEST_CHECK(val == 0);
	}
}

#ifndef TORRENT_DISABLE_DHT	
void add_and_replace(libtorrent::dht::node_id& dst, libtorrent::dht::node_id const& add)
{
	bool carry = false;
	for (int k = 19; k >= 0; --k)
	{
		int sum = dst[k] + add[k] + (carry?1:0);
		dst[k] = sum & 255;
		carry = sum > 255;
	}
}
#endif

char upnp_xml[] = 
"<root>"
"<specVersion>"
"<major>1</major>"
"<minor>0</minor>"
"</specVersion>"
"<URLBase>http://192.168.0.1:5678</URLBase>"
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

char upnp_xml2[] =
"<root>"
"<specVersion>"
"<major>1</major>"
"<minor>0</minor>"
"</specVersion>"
"<URLBase>http://192.168.1.1:49152</URLBase>"
"<device>"
"<deviceType>"
"urn:schemas-upnp-org:device:InternetGatewayDevice:1"
"</deviceType>"
"<friendlyName>LINKSYS WAG200G Gateway</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com</manufacturerURL>"
"<modelDescription>LINKSYS WAG200G Gateway</modelDescription>"
"<modelName>Wireless-G ADSL Home Gateway</modelName>"
"<modelNumber>WAG200G</modelNumber>"
"<modelURL>http://www.linksys.com</modelURL>"
"<serialNumber>123456789</serialNumber>"
"<UDN>uuid:8d401597-1dd2-11b2-a7d4-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:L3Forwarding1</serviceId>"
"<controlURL>/upnp/control/L3Forwarding1</controlURL>"
"<eventSubURL>/upnp/event/L3Forwarding1</eventSubURL>"
"<SCPDURL>/l3frwd.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>"
"<friendlyName>WANDevice</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com/</manufacturerURL>"
"<modelDescription>Residential Gateway</modelDescription>"
"<modelName>Internet Connection Sharing</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://www.linksys.com/</modelURL>"
"<serialNumber>0000001</serialNumber>"
"<UDN>uuid:8d401596-1dd2-11b2-a7d4-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANCommonIFC1</serviceId>"
"<controlURL>/upnp/control/WANCommonIFC1</controlURL>"
"<eventSubURL>/upnp/event/WANCommonIFC1</eventSubURL>"
"<SCPDURL>/cmnicfg.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>"
"<friendlyName>WANConnectionDevice</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com/</manufacturerURL>"
"<modelDescription>Residential Gateway</modelDescription>"
"<modelName>Internet Connection Sharing</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://www.linksys.com/</modelURL>"
"<serialNumber>0000001</serialNumber>"
"<UDN>uuid:8d401597-1dd2-11b2-a7d3-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:WANEthernetLinkConfig:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANEthLinkC1</serviceId>"
"<controlURL>/upnp/control/WANEthLinkC1</controlURL>"
"<eventSubURL>/upnp/event/WANEthLinkC1</eventSubURL>"
"<SCPDURL>/wanelcfg.xml</SCPDURL>"
"</service>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANPPPConn1</serviceId>"
"<controlURL>/upnp/control/WANPPPConn1</controlURL>"
"<eventSubURL>/upnp/event/WANPPPConn1</eventSubURL>"
"<SCPDURL>/pppcfg.xml</SCPDURL>"
"</service>"
"</serviceList>"
"</device>"
"</deviceList>"
"</device>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:LANDevice:1</deviceType>"
"<friendlyName>LANDevice</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com/</manufacturerURL>"
"<modelDescription>Residential Gateway</modelDescription>"
"<modelName>Residential Gateway</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://www.linksys.com/</modelURL>"
"<serialNumber>0000001</serialNumber>"
"<UDN>uuid:8d401596-1dd2-11b2-a7d3-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:LANHostConfigManagement:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:LANHostCfg1</serviceId>"
"<controlURL>/upnp/control/LANHostCfg1</controlURL>"
"<eventSubURL>/upnp/event/LANHostCfg1</eventSubURL>"
"<SCPDURL>/lanhostc.xml</SCPDURL>"
"</service>"
"</serviceList>"
"</device>"
"</deviceList>"
"<presentationURL>http://192.168.1.1/index.htm</presentationURL>"
"</device>"
"</root>";

struct parse_state
{
	parse_state(): in_service(false) {}
	void reset(char const* st)
	{
		in_service = false;
		service_type = st;
		tag_stack.clear();
		control_url.clear();
		model.clear();
		url_base.clear();
	}
	bool in_service;
	std::list<std::string> tag_stack;
	std::string control_url;
	char const* service_type;
	std::string model;
	std::string url_base;
};

void find_control_url(int type, char const* string, parse_state& state);

int test_main()
{
	using namespace libtorrent;

	// test itoa

	TEST_CHECK(to_string(345).elems == std::string("345"));
	TEST_CHECK(to_string(-345).elems == std::string("-345"));
	TEST_CHECK(to_string(0).elems == std::string("0"));
	TEST_CHECK(to_string(1000000000).elems == std::string("1000000000"));

	// test url parsing

	TEST_CHECK(parse_url_components("http://foo:bar@host.com:80/path/to/file")
		== make_tuple("http", "foo:bar", "host.com", 80, "/path/to/file", (char const*)0));

	TEST_CHECK(parse_url_components("http://host.com/path/to/file")
		== make_tuple("http", "", "host.com", 80, "/path/to/file", (char const*)0));

	TEST_CHECK(parse_url_components("ftp://host.com:21/path/to/file")
		== make_tuple("ftp", "", "host.com", 21, "/path/to/file", (char const*)0));

	TEST_CHECK(parse_url_components("http://host.com/path?foo:bar@foo:")
		== make_tuple("http", "", "host.com", 80, "/path?foo:bar@foo:", (char const*)0));

	TEST_CHECK(parse_url_components("http://192.168.0.1/path/to/file")
		== make_tuple("http", "", "192.168.0.1", 80, "/path/to/file", (char const*)0));

	TEST_CHECK(parse_url_components("http://[::1]/path/to/file")
		== make_tuple("http", "", "[::1]", 80, "/path/to/file", (char const*)0));

	// base64 test vectors from http://www.faqs.org/rfcs/rfc4648.html

	TEST_CHECK(base64encode("") == "");
	TEST_CHECK(base64encode("f") == "Zg==");
	TEST_CHECK(base64encode("fo") == "Zm8=");
	TEST_CHECK(base64encode("foo") == "Zm9v");
	TEST_CHECK(base64encode("foob") == "Zm9vYg==");
	TEST_CHECK(base64encode("fooba") == "Zm9vYmE=");
	TEST_CHECK(base64encode("foobar") == "Zm9vYmFy");

	// base32 test vectors from http://www.faqs.org/rfcs/rfc4648.html

   TEST_CHECK(base32encode("") == "");
   TEST_CHECK(base32encode("f") == "MY======");
   TEST_CHECK(base32encode("fo") == "MZXQ====");
   TEST_CHECK(base32encode("foo") == "MZXW6===");
   TEST_CHECK(base32encode("foob") == "MZXW6YQ=");
   TEST_CHECK(base32encode("fooba") == "MZXW6YTB");
   TEST_CHECK(base32encode("foobar") == "MZXW6YTBOI======");

   TEST_CHECK(base32decode("") == "");
   TEST_CHECK(base32decode("MY======") == "f");
   TEST_CHECK(base32decode("MZXQ====") == "fo");
   TEST_CHECK(base32decode("MZXW6===") == "foo");
   TEST_CHECK(base32decode("MZXW6YQ=") == "foob");
   TEST_CHECK(base32decode("MZXW6YTB") == "fooba");
   TEST_CHECK(base32decode("MZXW6YTBOI======") == "foobar");

   TEST_CHECK(base32decode("MY") == "f");
   TEST_CHECK(base32decode("MZXW6YQ") == "foob");
   TEST_CHECK(base32decode("MZXW6YTBOI") == "foobar");
   TEST_CHECK(base32decode("mZXw6yTBO1======") == "foobar");

	std::string test;
	for (int i = 0; i < 255; ++i)
		test += char(i);

	TEST_CHECK(base32decode(base32encode(test)) == test);

	// url_has_argument

	TEST_CHECK(!url_has_argument("http://127.0.0.1/test", "test"));
	TEST_CHECK(!url_has_argument("http://127.0.0.1/test?foo=24", "bar"));
	TEST_CHECK(*url_has_argument("http://127.0.0.1/test?foo=24", "foo") == "24");
	TEST_CHECK(*url_has_argument("http://127.0.0.1/test?foo=24&bar=23", "foo") == "24");
	TEST_CHECK(*url_has_argument("http://127.0.0.1/test?foo=24&bar=23", "bar") == "23");
	TEST_CHECK(*url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "bar") == "23");
	TEST_CHECK(*url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "a") == "e");
	TEST_CHECK(!url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "b"));

	// HTTP request parser

	http_parser parser;
	boost::tuple<int, int, bool> received = feed_bytes(parser
		, "HTTP/1.1 200 OK\r\n"
		"Content-Length: 4\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"test");

	TEST_CHECK(received == make_tuple(4, 64, false));
	TEST_CHECK(parser.finished());
	TEST_CHECK(std::equal(parser.get_body().begin, parser.get_body().end, "test"));
	TEST_CHECK(parser.header("content-type") == "text/plain");
	TEST_CHECK(atoi(parser.header("content-length").c_str()) == 4);

	parser.reset();

	TEST_CHECK(!parser.finished());

	char const* upnp_response =
		"HTTP/1.1 200 OK\r\n"
		"ST:upnp:rootdevice\r\n"
		"USN:uuid:000f-66d6-7296000099dc::upnp:rootdevice\r\n"
		"Location: http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc\r\n"
		"Server: Custom/1.0 UPnP/1.0 Proc/Ver\r\n"
		"EXT:\r\n"
		"Cache-Control:max-age=180\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, upnp_response);

	TEST_CHECK(received == make_tuple(0, int(strlen(upnp_response)), false));
	TEST_CHECK(parser.get_body().left() == 0);
	TEST_CHECK(parser.header("st") == "upnp:rootdevice");
	TEST_CHECK(parser.header("location")
		== "http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc");
	TEST_CHECK(parser.header("ext") == "");
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");

	parser.reset();
	TEST_CHECK(!parser.finished());

	char const* upnp_notify =
		"NOTIFY * HTTP/1.1\r\n"
		"Host:239.255.255.250:1900\r\n"
		"NT:urn:schemas-upnp-org:device:MediaServer:1\r\n"
		"NTS:ssdp:alive\r\n"
		"Location:http://10.0.1.15:2353/upnphost/udhisapi.dll?content=uuid:c17f2c31-d19b-4912-af94-651945c8a84e\r\n"
		"USN:uuid:c17f0c32-d1db-4be8-ae94-25f94583026e::urn:schemas-upnp-org:device:MediaServer:1\r\n"
		"Cache-Control:max-age=900\r\n"
		"Server:Microsoft-Windows-NT/5.1 UPnP/1.0 UPnP-Device-Host/1.0\r\n";

	received = feed_bytes(parser, upnp_notify);

	TEST_CHECK(received == make_tuple(0, int(strlen(upnp_notify)), false));
	TEST_CHECK(parser.method() == "notify");
	TEST_CHECK(parser.path() == "*");

	parser.reset();
	TEST_CHECK(!parser.finished());

	char const* bt_lsd = "BT-SEARCH * HTTP/1.1\r\n"
		"Host: 239.192.152.143:6771\r\n"
		"Port: 6881\r\n"
		"Infohash: 12345678901234567890\r\n"
		"\r\n\r\n";

	received = feed_bytes(parser, bt_lsd);

	TEST_CHECK(received == make_tuple(2, int(strlen(bt_lsd) - 2), false));
	TEST_CHECK(parser.method() == "bt-search");
	TEST_CHECK(parser.path() == "*");
	TEST_CHECK(atoi(parser.header("port").c_str()) == 6881);
	TEST_CHECK(parser.header("infohash") == "12345678901234567890");

	TEST_CHECK(!parser.finished());

	parser.reset();
	TEST_CHECK(!parser.finished());

	// make sure we support trackers with incorrect line endings
	char const* tracker_response =
		"HTTP/1.1 200 OK\n"
		"content-length: 5\n"
		"content-type: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, tracker_response);

	TEST_CHECK(received == make_tuple(5, int(strlen(tracker_response) - 5), false));
	TEST_CHECK(parser.get_body().left() == 5);

	// test xml parser

	char xml1[] = "<a>foo<b/>bar</a>";
	std::string out1;

	xml_parse(xml1, xml1 + sizeof(xml1) - 1, bind(&parser_callback
		, boost::ref(out1), _1, _2, _3));
	std::cerr << out1 << std::endl;
	TEST_CHECK(out1 == "BaSfooEbSbarFa");

	char xml2[] = "<?xml version = \"1.0\"?><c x=\"1\" \t y=\"3\"/><d foo='bar'></d boo='foo'><!--comment-->";
	std::string out2;

	xml_parse(xml2, xml2 + sizeof(xml2) - 1, bind(&parser_callback
		, boost::ref(out2), _1, _2, _3));
	std::cerr << out2 << std::endl;
	TEST_CHECK(out2 == "DxmlAversionV1.0EcAxV1AyV3BdAfooVbarFdAbooVfooCcomment");

	char xml3[] = "<a f=1>foo</a f='b>";
	std::string out3;

	xml_parse(xml3, xml3 + sizeof(xml3) - 1, bind(&parser_callback
		, boost::ref(out3), _1, _2, _3));
	std::cerr << out3 << std::endl;
	TEST_CHECK(out3 == "BaPunquoted attribute valueSfooFaPmissing end quote on attribute");

	char xml4[] = "<a  f>foo</a  v  >";
	std::string out4;

	xml_parse(xml4, xml4 + sizeof(xml4) - 1, bind(&parser_callback
		, boost::ref(out4), _1, _2, _3));
	std::cerr << out4 << std::endl;
	TEST_CHECK(out4 == "BaPgarbage inside element bracketsSfooFaPgarbage inside element brackets");

	// test upnp xml parser

	parse_state xml_s;
	xml_s.reset("urn:schemas-upnp-org:service:WANIPConnection:1");
	xml_parse((char*)upnp_xml, (char*)upnp_xml + sizeof(upnp_xml)
		, bind(&find_control_url, _1, _2, boost::ref(xml_s)));

	std::cerr << "namespace " << xml_s.service_type << std::endl;
	std::cerr << "url_base: " << xml_s.url_base << std::endl;
	std::cerr << "control_url: " << xml_s.control_url << std::endl;
	std::cerr << "model: " << xml_s.model << std::endl;
	TEST_CHECK(xml_s.url_base == "http://192.168.0.1:5678");
	TEST_CHECK(xml_s.control_url == "/WANIPConnection");
	TEST_CHECK(xml_s.model == "D-Link Router");

	xml_s.reset("urn:schemas-upnp-org:service:WANPPPConnection:1");
	xml_parse((char*)upnp_xml2, (char*)upnp_xml2 + sizeof(upnp_xml2)
		, bind(&find_control_url, _1, _2, boost::ref(xml_s)));

	std::cerr << "namespace " << xml_s.service_type << std::endl;
	std::cerr << "url_base: " << xml_s.url_base << std::endl;
	std::cerr << "control_url: " << xml_s.control_url << std::endl;
	std::cerr << "model: " << xml_s.model << std::endl;
	TEST_CHECK(xml_s.url_base == "http://192.168.1.1:49152");
	TEST_CHECK(xml_s.control_url == "/upnp/control/WANPPPConn1");
	TEST_CHECK(xml_s.model == "Wireless-G ADSL Home Gateway");

	// test network functions

	error_code ec;
	TEST_CHECK(is_local(address::from_string("192.168.0.1", ec)));
	TEST_CHECK(is_local(address::from_string("10.1.1.56", ec)));
	TEST_CHECK(!is_local(address::from_string("14.14.251.63", ec)));
	TEST_CHECK(is_loopback(address::from_string("127.0.0.1", ec)));
	TEST_CHECK(is_loopback(address::from_string("::1", ec)));
	TEST_CHECK(is_any(address_v6::any()));
	TEST_CHECK(is_any(address_v4::any()));
	TEST_CHECK(!is_any(address::from_string("31.53.21.64", ec)));

	// test torrent parsing

	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name.utf-8"] = "test1";
	info["name"] = "test__";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry torrent;
	torrent["info"] = info;

	torrent_info ti(torrent);
	std::cerr << ti.name() << std::endl;
	TEST_CHECK(ti.name() == "test1");

#ifdef TORRENT_WINDOWS
	info["name.utf-8"] = "c:/test1/test2/test3";
#else
	info["name.utf-8"] = "/test1/test2/test3";
#endif
	torrent["info"] = info;
	torrent_info ti2(torrent);
	std::cerr << ti2.name() << std::endl;
	TEST_CHECK(ti2.name() == "test3");

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	torrent_info ti3(torrent);
	std::cerr << ti3.name() << std::endl;
	TEST_CHECK(ti3.name() == "test2/test3/test4");

#ifndef TORRENT_DISABLE_DHT	
	// test kademlia functions

	using namespace libtorrent::dht;

	for (int i = 0; i < 160; i += 4)
	{
		for (int j = 0; j < 160; j += 4)
		{
			node_id a(0);
			a[(159-i) / 8] = 1 << (i & 7);
			node_id b(0);
			b[(159-j) / 8] = 1 << (j & 7);
			int dist = distance_exp(a, b);

			TEST_CHECK(dist >= 0 && dist < 160);
			TEST_CHECK(dist == ((i == j)?0:(std::max)(i, j)));

			for (int k = 0; k < 160; k += 4)
			{
				node_id c(0);
				c[(159-k) / 8] = 1 << (k & 7);

				bool cmp = compare_ref(a, b, c);
				TEST_CHECK(cmp == (distance(a, c) < distance(b, c)));
			}
		}
	}

	// test kademlia routing table
	dht_settings s;
	node_id id = boost::lexical_cast<sha1_hash>("6123456789abcdef01232456789abcdef0123456");
	dht::routing_table table(id, 10, s);
	table.node_seen(id, udp::endpoint(address_v4::any(), rand()));

	node_id tmp;
	node_id diff = boost::lexical_cast<sha1_hash>("00000f7459456a9453f8719b09547c11d5f34064");
	std::vector<node_entry> nodes;
	for (int i = 0; i < 1000000; ++i)
	{
		table.node_seen(tmp, udp::endpoint(address_v4::any(), rand()));
		add_and_replace(tmp, diff);
	}

	std::copy(table.begin(), table.end(), std::back_inserter(nodes));

	std::cout << "nodes: " << nodes.size() << std::endl;

	std::vector<node_entry> temp;

	std::generate(tmp.begin(), tmp.end(), &std::rand);
	table.find_node(tmp, temp, false, nodes.size() + 1);
	std::cout << "returned: " << temp.size() << std::endl;
	TEST_CHECK(temp.size() == nodes.size());

	std::generate(tmp.begin(), tmp.end(), &std::rand);
	table.find_node(tmp, temp, true, nodes.size() + 1);
	std::cout << "returned: " << temp.size() << std::endl;
	TEST_CHECK(temp.size() == nodes.size() + 1);

	std::generate(tmp.begin(), tmp.end(), &std::rand);
	table.find_node(tmp, temp, false, 7);
	std::cout << "returned: " << temp.size() << std::endl;
	TEST_CHECK(temp.size() == 7);

	std::sort(nodes.begin(), nodes.end(), bind(&compare_ref
		, bind(&node_entry::id, _1)
		, bind(&node_entry::id, _2), tmp));

	int hits = 0;
	for (std::vector<node_entry>::iterator i = temp.begin()
		, end(temp.end()); i != end; ++i)
	{
		int hit = std::find_if(nodes.begin(), nodes.end()
			, bind(&node_entry::id, _1) == i->id) - nodes.begin();
//		std::cerr << hit << std::endl;
		if (hit < int(temp.size())) ++hits;
	}
	TEST_CHECK(hits > int(temp.size()) / 2);

	std::generate(tmp.begin(), tmp.end(), &std::rand);
	table.find_node(tmp, temp, false, 15);
	std::cout << "returned: " << temp.size() << std::endl;
	TEST_CHECK(temp.size() == 15);

	std::sort(nodes.begin(), nodes.end(), bind(&compare_ref
		, bind(&node_entry::id, _1)
		, bind(&node_entry::id, _2), tmp));

	hits = 0;
	for (std::vector<node_entry>::iterator i = temp.begin()
		, end(temp.end()); i != end; ++i)
	{
		int hit = std::find_if(nodes.begin(), nodes.end()
			, bind(&node_entry::id, _1) == i->id) - nodes.begin();
//		std::cerr << hit << std::endl;
		if (hit < int(temp.size())) ++hits;
	}
	TEST_CHECK(hits > int(temp.size()) / 2);

#endif



	// test peer_id/sha1_hash type

	sha1_hash h1(0);
	sha1_hash h2(0);
	TEST_CHECK(h1 == h2);
	TEST_CHECK(!(h1 != h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(h1.is_all_zeros());

	h1 = boost::lexical_cast<sha1_hash>("0123456789012345678901234567890123456789");
	h2 = boost::lexical_cast<sha1_hash>("0113456789012345678901234567890123456789");

	TEST_CHECK(h2 < h1);
	TEST_CHECK(h2 == h2);
	TEST_CHECK(h1 == h1);
	h2.clear();
	TEST_CHECK(h2.is_all_zeros());
	
	h2 = boost::lexical_cast<sha1_hash>("ffffffffff0000000000ffffffffff0000000000");
	h1 = boost::lexical_cast<sha1_hash>("fffff00000fffff00000fffff00000fffff00000");
	h1 &= h2;
	TEST_CHECK(h1 == boost::lexical_cast<sha1_hash>("fffff000000000000000fffff000000000000000"));

	h2 = boost::lexical_cast<sha1_hash>("ffffffffff0000000000ffffffffff0000000000");
	h1 = boost::lexical_cast<sha1_hash>("fffff00000fffff00000fffff00000fffff00000");
	h1 |= h2;
	TEST_CHECK(h1 == boost::lexical_cast<sha1_hash>("fffffffffffffff00000fffffffffffffff00000"));
	
	h2 = boost::lexical_cast<sha1_hash>("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
	h1 ^= h2;
	std::cerr << h1 << std::endl;
	TEST_CHECK(h1 == boost::lexical_cast<sha1_hash>("f0f0f0f0f0f0f0ff0f0ff0f0f0f0f0f0f0ff0f0f"));
	TEST_CHECK(h1 != h2);

	h2 = sha1_hash("                    ");
	TEST_CHECK(h2 == boost::lexical_cast<sha1_hash>("2020202020202020202020202020202020202020"));
	
	// CIDR distance test
	h1 = boost::lexical_cast<sha1_hash>("0123456789abcdef01232456789abcdef0123456");
	h2 = boost::lexical_cast<sha1_hash>("0123456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 160);
	h2 = boost::lexical_cast<sha1_hash>("0120456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 14);
	h2 = boost::lexical_cast<sha1_hash>("012f456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 12);
	h2 = boost::lexical_cast<sha1_hash>("0123456789abcdef11232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 16 * 4 + 3);


	// test bitfield
	bitfield test1(10, false);
	TEST_CHECK(test1.count() == 0);
	test1.set_bit(9);
	TEST_CHECK(test1.count() == 1);
	test1.clear_bit(9);
	TEST_CHECK(test1.count() == 0);
	test1.set_bit(2);
	TEST_CHECK(test1.count() == 1);
	test1.set_bit(1);
	test1.set_bit(9);
	TEST_CHECK(test1.count() == 3);
	test1.clear_bit(2);
	TEST_CHECK(test1.count() == 2);
	int distance = std::distance(test1.begin(), test1.end());
	std::cerr << distance << std::endl;
	TEST_CHECK(distance == 10);

	test1.set_all();
	TEST_CHECK(test1.count() == 10);

	test1.clear_all();
	TEST_CHECK(test1.count() == 0);

	test1.resize(2);
	test1.set_bit(0);
	test1.resize(16, true);
	TEST_CHECK(test1.count() == 15);
	test1.resize(20, true);
	TEST_CHECK(test1.count() == 19);
	test1.set_bit(1);
	test1.resize(1);
	TEST_CHECK(test1.count() == 1);
	return 0;
}

