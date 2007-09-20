#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/upnp.hpp"
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/bind.hpp>

#include "test.hpp"

using namespace libtorrent;
using namespace boost::tuples;
using boost::bind;

tuple<int, int> feed_bytes(http_parser& parser, char const* str)
{
	tuple<int, int> ret(0, 0);
	buffer::const_interval recv_buf(str, str + 1);
	for (; *str; ++str)
	{
		recv_buf.end = str + 1;
		int payload, protocol;
		tie(payload, protocol) = parser.incoming(recv_buf);
		ret.get<0>() += payload;
		ret.get<1>() += protocol;
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

int test_main()
{
	using namespace libtorrent;

	TEST_CHECK(parse_url_components("http://foo:bar@host.com:80/path/to/file")
		== make_tuple("http", "foo:bar", "host.com", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path/to/file")
		== make_tuple("http", "", "host.com", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("ftp://host.com:21/path/to/file")
		== make_tuple("ftp", "", "host.com", 21, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path?foo:bar@foo:")
		== make_tuple("http", "", "host.com", 80, "/path?foo:bar@foo:"));

	TEST_CHECK(parse_url_components("http://192.168.0.1/path/to/file")
		== make_tuple("http", "", "192.168.0.1", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://[::1]/path/to/file")
		== make_tuple("http", "", "[::1]", 80, "/path/to/file"));

	// base64 test vectors from http://www.faqs.org/rfcs/rfc4648.html

	TEST_CHECK(base64encode("") == "");
	TEST_CHECK(base64encode("f") == "Zg==");
	TEST_CHECK(base64encode("fo") == "Zm8=");
	TEST_CHECK(base64encode("foo") == "Zm9v");
	TEST_CHECK(base64encode("foob") == "Zm9vYg==");
	TEST_CHECK(base64encode("fooba") == "Zm9vYmE=");
	TEST_CHECK(base64encode("foobar") == "Zm9vYmFy");

	// HTTP request parser

	http_parser parser;
	boost::tuple<int, int> received = feed_bytes(parser
		, "HTTP/1.1 200 OK\r\n"
		"Content-Length: 4\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"test");

	TEST_CHECK(received == make_tuple(4, 64));
	TEST_CHECK(parser.finished());
	TEST_CHECK(std::equal(parser.get_body().begin, parser.get_body().end, "test"));
	TEST_CHECK(parser.header<std::string>("content-type") == "text/plain");
	TEST_CHECK(parser.header<int>("content-length") == 4);

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

	TEST_CHECK(received == make_tuple(0, int(strlen(upnp_response))));
	TEST_CHECK(parser.get_body().left() == 0);
	TEST_CHECK(parser.header<std::string>("st") == "upnp:rootdevice");
	TEST_CHECK(parser.header<std::string>("location")
		== "http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc");
	TEST_CHECK(parser.header<std::string>("ext") == "");
	TEST_CHECK(parser.header<std::string>("date") == "Fri, 02 Jan 1970 08:10:38 GMT");

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

	TEST_CHECK(received == make_tuple(0, int(strlen(upnp_notify))));
	TEST_CHECK(parser.method() == "notify");
	TEST_CHECK(parser.path() == "*");

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

	// test network functions

	TEST_CHECK(is_local(address::from_string("192.168.0.1")));
	TEST_CHECK(is_local(address::from_string("10.1.1.56")));
	TEST_CHECK(!is_local(address::from_string("14.14.251.63")));
	
	return 0;
}

