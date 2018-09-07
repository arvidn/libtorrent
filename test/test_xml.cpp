/*

Copyright (c) 2012, Arvid Norberg
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

#include "libtorrent/xml_parse.hpp"
#include "libtorrent/upnp.hpp"
#include "test.hpp"
#include <iostream>
#include <functional>

namespace {

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

char upnp_xml3[] =
"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
"<s:Body>"
"<s:Fault>"
"<faultcode>s:Client</faultcode>"
"<faultstring>UPnPError</faultstring>"
"<detail>"
"<UPnPErrorxmlns=\"urn:schemas-upnp-org:control-1-0\">"
"<errorCode>402</errorCode>"
"<errorDescription>Invalid Args</errorDescription>"
"</UPnPError>"
"</detail>"
"</s:Fault>"
"</s:Body>"
"</s:Envelope>";

char upnp_xml4[] =
"<?xml version=\"1.0\"?>"
"<s:Envelope"
" xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
"<s:Body>"
"<u:GetExternalIPAddressResponse"
" xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
"<NewExternalIPAddress>123.10.20.30</NewExternalIPAddress>"
"</u:GetExternalIPAddressResponse>"
"</s:Body>"
"</s:Envelope>";

using namespace lt;
using namespace std::placeholders;

void parser_callback(std::string& out, int token, string_view s
	, string_view val)
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
		case xml_tag_content: out += "T"; break;
		default: TEST_CHECK(false);
	}
	out.append(s.begin(), s.end());
	if (token == xml_attribute)
	{
		TEST_CHECK(!val.empty());
		out += "V";
		out.append(val.begin(), val.end());
	}
	else
	{
		TEST_CHECK(val.empty());
	}
}

void test_parse(char const* in, char const* expected)
{
	std::string out;
	xml_parse(in, std::bind(&parser_callback
		, std::ref(out), _1, _2, _3));
	std::printf("in: %s\n     out: %s\nexpected: %s\n"
		, in, out.c_str(), expected);
	TEST_EQUAL(out, expected);
}

} // anonymous namespace

TORRENT_TEST(upnp_parser1)
{
	parse_state xml_s;
	xml_parse(upnp_xml, std::bind(&find_control_url, _1, _2, std::ref(xml_s)));

	std::cout << "namespace " << xml_s.service_type << std::endl;
	std::cout << "url_base: " << xml_s.url_base << std::endl;
	std::cout << "control_url: " << xml_s.control_url << std::endl;
	std::cout << "model: " << xml_s.model << std::endl;
	TEST_EQUAL(xml_s.url_base, "http://192.168.0.1:5678");
	TEST_EQUAL(xml_s.control_url, "/WANIPConnection");
	TEST_EQUAL(xml_s.model, "D-Link Router");
}

TORRENT_TEST(upnp_parser2)
{
	parse_state xml_s;
	xml_parse(upnp_xml2, std::bind(&find_control_url, _1, _2, std::ref(xml_s)));

	std::cout << "namespace " << xml_s.service_type << std::endl;
	std::cout << "url_base: " << xml_s.url_base << std::endl;
	std::cout << "control_url: " << xml_s.control_url << std::endl;
	std::cout << "model: " << xml_s.model << std::endl;
	TEST_EQUAL(xml_s.url_base, "http://192.168.1.1:49152");
	TEST_EQUAL(xml_s.control_url, "/upnp/control/WANPPPConn1");
	TEST_EQUAL(xml_s.model, "Wireless-G ADSL Home Gateway");
}

TORRENT_TEST(upnp_parser3)
{
	error_code_parse_state xml_s;
	xml_parse(upnp_xml3, std::bind(&find_error_code, _1, _2, std::ref(xml_s)));

	std::cout << "error_code " << xml_s.error_code << std::endl;
	TEST_EQUAL(xml_s.error_code, 402);
}

TORRENT_TEST(upnp_parser4)
{
	ip_address_parse_state xml_s;
	xml_parse(upnp_xml4, std::bind(&find_ip_address, _1, _2, std::ref(xml_s)));

	std::cout << "error_code " << xml_s.error_code << std::endl;
	std::cout << "ip_address " << xml_s.ip_address << std::endl;
	TEST_EQUAL(xml_s.error_code, -1);
	TEST_EQUAL(xml_s.ip_address, "123.10.20.30");
}

TORRENT_TEST(tags)
{
	test_parse("<a>foo<b/>bar</a>", "BaSfooEbSbarFa");
}

TORRENT_TEST(xml_tag_comment)
{
	test_parse("<?xml version = \"1.0\"?><c x=\"1\" \t y=\"3\"/><d foo='bar'></d boo='foo'><!--comment-->"
		, "DxmlAversionV1.0EcAxV1AyV3BdAfooVbarFdAbooVfooCcomment");
}

TORRENT_TEST(empty_tag)
{
	test_parse("<foo/>", "Efoo");
}

TORRENT_TEST(empty_tag_whitespace)
{
	test_parse("<foo  />", "Efoo");
}

TORRENT_TEST(xml_tag_no_attribute)
{
	test_parse("<?xml?>", "Dxml");
}

TORRENT_TEST(xml_tag_no_attribute_whitespace)
{
	test_parse("<?xml  ?>", "Dxml");
}

TORRENT_TEST(attribute_missing_qoute)
{
	test_parse("<a f=1>foo</a f='b>"
		, "BaPunquoted attribute valueSfooFaPmissing end quote on attribute");
}

TORRENT_TEST(attribute_whitespace)
{
	test_parse("<a  f>foo</a  v  >", "BaTfSfooFaTv  ");
}

TORRENT_TEST(unterminated_cdata)
{
	// test unterminated CDATA tags
	test_parse("<![CDATA[foo", "Punexpected end of file");
}

TORRENT_TEST(cdata)
{
	// test CDATA tag
	test_parse("<![CDATA[verbatim tag that can have > and < in it]]>"
		, "Sverbatim tag that can have > and < in it");
}

TORRENT_TEST(unterminated_tag)
{
	// test unterminated tags
	test_parse("<foo", "Punexpected end of file");
}

TORRENT_TEST(unqouted_attribute_value)
{
	// test unquoted attribute values
	test_parse("<foo a=bar>", "BfooPunquoted attribute value");
}

TORRENT_TEST(unterminated_attribute)
{
	// test unterminated attribute value
	test_parse("<foo a=\"bar>", "BfooPmissing end quote on attribute");
}

TORRENT_TEST(unterminated_tag_with_attribute)
{
	// test unterminated tag
	test_parse("<foo a=\"bar", "Punexpected end of file");
}

