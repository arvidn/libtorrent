#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#include "test.hpp"

using namespace libtorrent;
using namespace boost::tuples;

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

	return 0;
}

