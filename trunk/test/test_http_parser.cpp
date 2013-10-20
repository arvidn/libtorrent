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

#include "test.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/size_type.hpp"

#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

using namespace libtorrent;
using boost::tuple;
using boost::make_tuple;
using boost::tie;

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
			ret.get<2>() |= error;
//			std::cerr << payload << ", " << protocol << ", " << chunk_size << std::endl;
			TORRENT_ASSERT(payload + protocol == chunk_size);
		}
		TEST_CHECK(prev == make_tuple(0, 0, false) || ret == prev);
		TEST_EQUAL(ret.get<0>() + ret.get<1>(), strlen(str));
		prev = ret;
	}
	return ret;
}

int test_main()
{
	// HTTP request parser
	http_parser parser;
	boost::tuple<int, int, bool> received;

	received = feed_bytes(parser
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
	TEST_EQUAL(parser.connection_close(), false);

	// test connection close
	parser.reset();

	TEST_CHECK(!parser.finished());

	char const* http1_response =
		"HTTP/1.0 200 OK\r\n"
		"Cache-Control: max-age=180\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, http1_response);

	TEST_CHECK(received == make_tuple(0, int(strlen(http1_response)), false));
	TEST_CHECK(parser.get_body().left() == 0);
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");
	TEST_EQUAL(parser.connection_close(), true);

	parser.reset();

	TEST_CHECK(!parser.finished());

	char const* close_response =
		"HTTP/1.1 200 OK\r\n"
		"Connection: close\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, close_response);

	TEST_CHECK(received == make_tuple(0, int(strlen(close_response)), false));
	TEST_CHECK(parser.get_body().left() == 0);
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");
	TEST_EQUAL(parser.connection_close(), true);

	parser.reset();

	TEST_CHECK(!parser.finished());

	char const* keep_alive_response =
		"HTTP/1.1 200 OK\r\n"
		"Connection: keep-alive\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, keep_alive_response);

	TEST_CHECK(received == make_tuple(0, int(strlen(keep_alive_response)), false));
	TEST_CHECK(parser.get_body().left() == 0);
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");
	TEST_EQUAL(parser.connection_close(), false);

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
		"\r\n";

	received = feed_bytes(parser, bt_lsd);

	TEST_CHECK(received == make_tuple(0, int(strlen(bt_lsd)), false));
	TEST_CHECK(parser.method() == "bt-search");
	TEST_CHECK(parser.path() == "*");
	TEST_CHECK(atoi(parser.header("port").c_str()) == 6881);
	TEST_CHECK(parser.header("infohash") == "12345678901234567890");

	TEST_CHECK(parser.finished());

	parser.reset();
	TEST_CHECK(!parser.finished());

	// test chunked encoding
	char const* chunked_test = "HTTP/1.1 200 OK\r\n"
		"Content-Length: 20\r\n"
		"Content-Type: text/plain\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"4\r\n"
		"test\r\n"
		"10\r\n"
		"0123456789abcdef\r\n"
		"0\r\n"
		"Test-header: foobar\r\n"
		"\r\n";

	received = feed_bytes(parser, chunked_test);

	printf("payload: %d protocol: %d\n", received.get<0>(), received.get<1>());
	TEST_CHECK(received == make_tuple(20, strlen(chunked_test) - 20, false));
	TEST_CHECK(parser.finished());
	TEST_CHECK(std::equal(parser.get_body().begin, parser.get_body().end
		, "4\r\ntest\r\n10\r\n0123456789abcdef"));
	TEST_CHECK(parser.header("test-header") == "foobar");
	TEST_CHECK(parser.header("content-type") == "text/plain");
	TEST_CHECK(atoi(parser.header("content-length").c_str()) == 20);
	TEST_CHECK(parser.chunked_encoding());
	typedef std::pair<size_type, size_type> chunk_range;
	std::vector<chunk_range> cmp;
	cmp.push_back(chunk_range(96, 100));
	cmp.push_back(chunk_range(106, 122));
	TEST_CHECK(cmp == parser.chunks());

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

	parser.reset();

	// make sure we support content-range responses
	// and that we're case insensitive
	char const* web_seed_response =
		"HTTP/1.1 206 OK\n"
		"contEnt-rAngE: bYTes 0-4\n"
		"conTent-TyPe: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, web_seed_response);

	TEST_CHECK(received == make_tuple(5, int(strlen(web_seed_response) - 5), false));
	TEST_CHECK(parser.content_range() == (std::pair<size_type, size_type>(0, 4)));
	TEST_CHECK(parser.content_length() == 5);

	parser.reset();

	// make sure we support content-range responses
	// and that we're case insensitive
	char const* one_hundred_response =
		"HTTP/1.1 100 Continue\n"
		"\r\n"
		"HTTP/1.1 200 OK\n"
		"Content-Length: 4\r\n"
		"Content-Type: test/plain\r\n"
		"\r\n"
		"test";

	received = feed_bytes(parser, one_hundred_response);

	TEST_CHECK(received == make_tuple(4, int(strlen(one_hundred_response) - 4), false));
	TEST_EQUAL(parser.content_length(), 4);

	{
		// test chunked encoding parser
		char const chunk_header1[] = "f;this is a comment\r\n";
		size_type chunk_size;
		int header_size;
		bool ret = parser.parse_chunk_header(buffer::const_interval(chunk_header1, chunk_header1 + 10)
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, false);
		ret = parser.parse_chunk_header(buffer::const_interval(chunk_header1, chunk_header1 + sizeof(chunk_header1))
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, true);
		TEST_EQUAL(chunk_size, 15);
		TEST_EQUAL(header_size, sizeof(chunk_header1) - 1);

		char const chunk_header2[] =
			"0;this is a comment\r\n"
			"test1: foo\r\n"
			"test2: bar\r\n"
			"\r\n";

		ret = parser.parse_chunk_header(buffer::const_interval(chunk_header2, chunk_header2 + sizeof(chunk_header2))
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, true);
		TEST_EQUAL(chunk_size, 0);
		TEST_EQUAL(header_size, sizeof(chunk_header2) - 1);

		TEST_EQUAL(parser.headers().find("test1")->second, "foo");
		TEST_EQUAL(parser.headers().find("test2")->second, "bar");
	}

	// test url parsing

	error_code ec;
	TEST_CHECK(parse_url_components("http://foo:bar@host.com:80/path/to/file", ec)
		== make_tuple("http", "foo:bar", "host.com", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path/to/file", ec)
		== make_tuple("http", "", "host.com", -1, "/path/to/file"));

	TEST_CHECK(parse_url_components("ftp://host.com:21/path/to/file", ec)
		== make_tuple("ftp", "", "host.com", 21, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path?foo:bar@foo:", ec)
		== make_tuple("http", "", "host.com", -1, "/path?foo:bar@foo:"));

	TEST_CHECK(parse_url_components("http://192.168.0.1/path/to/file", ec)
		== make_tuple("http", "", "192.168.0.1", -1, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://[2001:ff00::1]:42/path/to/file", ec)
		== make_tuple("http", "", "[2001:ff00::1]", 42, "/path/to/file"));

	// leading spaces are supposed to be stripped
	TEST_CHECK(parse_url_components(" \thttp://[2001:ff00::1]:42/path/to/file", ec)
		== make_tuple("http", "", "[2001:ff00::1]", 42, "/path/to/file"));

	parse_url_components("http://[2001:ff00::1:42/path/to/file", ec);
	TEST_CHECK(ec == error_code(errors::expected_close_bracket_in_address));

	parse_url_components("http:/", ec);
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));
	ec.clear();

	parse_url_components("http:", ec);
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));
	ec.clear();
	return 0;
}

