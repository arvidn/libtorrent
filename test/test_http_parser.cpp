/*

Copyright (c) 2013-2021, Arvid Norberg
Copyright (c) 2018, Alden Torres
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
#include "libtorrent/string_view.hpp"

#include <tuple>

using namespace lt;

namespace {

std::tuple<int, int, bool> feed_bytes(http_parser& parser, string_view str)
{
	std::tuple<int, int, bool> ret(0, 0, false);
	std::tuple<int, int, bool> prev(0, 0, false);
	for (int chunks = 1; chunks < 70; ++chunks)
	{
		ret = std::make_tuple(0, 0, false);
		parser.reset();
		string_view recv_buf;
		for (;;)
		{
			int const chunk_size = std::min(chunks, int(str.size() - recv_buf.size()));
			if (chunk_size == 0) break;
			recv_buf = str.substr(0, recv_buf.size() + std::size_t(chunk_size));
			int payload, protocol;
			bool error = false;
			std::tie(payload, protocol) = parser.incoming(recv_buf, error);
			std::get<0>(ret) += payload;
			std::get<1>(ret) += protocol;

#ifdef _MSC_VER
#pragma warning(push, 1)
// this ia a buggy diagnostic on msvc
// warning C4834: discarding return value of function with 'nodiscard' attribute
#pragma warning( disable : 4834 )
#endif
			std::get<2>(ret) |= error;

#ifdef _MSC_VER
#pragma warning(pop)
#endif
			TORRENT_ASSERT(payload + protocol == chunk_size || std::get<2>(ret));
		}
		TEST_CHECK(prev == std::make_tuple(0, 0, false) || ret == prev || std::get<2>(ret));
		if (!std::get<2>(ret))
		{
			TEST_EQUAL(std::get<0>(ret) + std::get<1>(ret), int(str.size()));
		}

		prev = ret;
	}
	return ret;
}

} // anonymous namespace

TORRENT_TEST(http_parser)
{
	// HTTP request parser
	http_parser parser;
	std::tuple<int, int, bool> received;

	received = feed_bytes(parser
		, "HTTP/1.1 200 OK\r\n"
		"Content-Length: 4\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"test");

	TEST_CHECK(received == std::make_tuple(4, 64, false));
	TEST_CHECK(parser.finished());
	span<char const> body = parser.get_body();
	TEST_CHECK(std::equal(body.begin(), body.end(), "test"));
	TEST_CHECK(parser.header("content-type") == "text/plain");
	TEST_CHECK(atoi(parser.header("content-length").c_str()) == 4);
	TEST_CHECK(*parser.header_duration("content-length") == lt::seconds32(4));
	TEST_CHECK(parser.header_duration("content-length-x") == boost::none);

	parser.reset();

	TEST_CHECK(!parser.finished());

	char const upnp_response[] =
		"HTTP/1.1 200 OK\r\n"
		"ST:upnp:rootdevice\r\n"
		"USN:uuid:000f-66d6-7296000099dc::upnp:rootdevice\r\n"
		"Location: http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc\r\n"
		"Server: Custom/1.0 UPnP/1.0 Proc/Ver\r\n"
		"EXT:\r\n"
		"Cache-Control:max-age=180\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, upnp_response);

	TEST_CHECK(received == std::make_tuple(0, int(strlen(upnp_response)), false));
	TEST_CHECK(parser.get_body().empty());
	TEST_CHECK(parser.header("st") == "upnp:rootdevice");
	TEST_CHECK(parser.header("location")
		== "http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc");
	TEST_CHECK(parser.header("ext") == "");
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");
	TEST_EQUAL(parser.connection_close(), false);

	// test connection close
	parser.reset();

	TEST_CHECK(!parser.finished());

	char const http1_response[] =
		"HTTP/1.0 200 OK\r\n"
		"Cache-Control: max-age=180\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, http1_response);

	TEST_CHECK(received == std::make_tuple(0, int(strlen(http1_response)), false));
	TEST_CHECK(parser.get_body().empty());
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");
	TEST_EQUAL(parser.connection_close(), true);

	parser.reset();

	TEST_CHECK(!parser.finished());

	char const close_response[] =
		"HTTP/1.1 200 OK\r\n"
		"Connection: close\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, close_response);

	TEST_CHECK(received == std::make_tuple(0, int(strlen(close_response)), false));
	TEST_CHECK(parser.get_body().empty());
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");
	TEST_EQUAL(parser.connection_close(), true);

	parser.reset();

	TEST_CHECK(!parser.finished());

	char const keep_alive_response[] =
		"HTTP/1.1 200 OK\r\n"
		"Connection: keep-alive\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, keep_alive_response);

	TEST_CHECK(received == std::make_tuple(0, int(strlen(keep_alive_response)), false));
	TEST_CHECK(parser.get_body().empty());
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");
	TEST_EQUAL(parser.connection_close(), false);

	parser.reset();
	TEST_CHECK(!parser.finished());

	char const upnp_notify[] =
		"NOTIFY * HTTP/1.1\r\n"
		"Host:239.255.255.250:1900\r\n"
		"NT:urn:schemas-upnp-org:device:MediaServer:1\r\n"
		"NTS:ssdp:alive\r\n"
		"Location:http://10.0.1.15:2353/upnphost/udhisapi.dll?"
			"content=uuid:c17f2c31-d19b-4912-af94-651945c8a84e\r\n"
		"USN:uuid:c17f0c32-d1db-4be8-ae94-25f94583026e::urn:schemas-upnp-org:device:MediaServer:1\r\n"
		"Cache-Control:max-age=900\r\n"
		"Server:Microsoft-Windows-NT/5.1 UPnP/1.0 UPnP-Device-Host/1.0\r\n";

	received = feed_bytes(parser, upnp_notify);

	TEST_CHECK(received == std::make_tuple(0, int(strlen(upnp_notify)), false));
	TEST_CHECK(parser.method() == "notify");
	TEST_CHECK(parser.path() == "*");

	parser.reset();
	TEST_CHECK(!parser.finished());

	char const bt_lsd[] = "BT-SEARCH * HTTP/1.1\r\n"
		"Host: 239.192.152.143:6771\r\n"
		"Port: 6881\r\n"
		"Infohash: 12345678901234567890\r\n"
		"\r\n";

	received = feed_bytes(parser, bt_lsd);

	TEST_CHECK(received == std::make_tuple(0, int(strlen(bt_lsd)), false));
	TEST_CHECK(parser.method() == "bt-search");
	TEST_CHECK(parser.path() == "*");
	TEST_CHECK(atoi(parser.header("port").c_str()) == 6881);
	TEST_CHECK(parser.header("infohash") == "12345678901234567890");

	TEST_CHECK(parser.finished());

	parser.reset();
	TEST_CHECK(!parser.finished());

	// test chunked encoding
	char const chunked_test[] = "HTTP/1.1 200 OK\r\n"
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

	std::printf("payload: %d protocol: %d\n", std::get<0>(received), std::get<1>(received));
	TEST_CHECK(received == std::make_tuple(20, int(strlen(chunked_test)) - 20, false));
	TEST_CHECK(parser.finished());
	TEST_CHECK(std::equal(parser.get_body().begin(), parser.get_body().end()
		, "4\r\ntest\r\n10\r\n0123456789abcdef"));
	TEST_CHECK(parser.header("test-header") == "foobar");
	TEST_CHECK(parser.header("content-type") == "text/plain");
	TEST_CHECK(atoi(parser.header("content-length").c_str()) == 20);
	TEST_CHECK(parser.chunked_encoding());
	using chunk_range = std::pair<std::int64_t, std::int64_t>;
	std::vector<chunk_range> cmp;
	cmp.push_back(chunk_range(96, 100));
	cmp.push_back(chunk_range(106, 122));
	TEST_CHECK(cmp == parser.chunks());

	// make sure we support trackers with incorrect line endings
	char const tracker_response[] =
		"HTTP/1.1 200 OK\n"
		"content-length: 5\n"
		"content-type: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, tracker_response);

	TEST_CHECK(received == std::make_tuple(5, int(strlen(tracker_response) - 5), false));
	TEST_CHECK(parser.get_body().size() == 5);

	parser.reset();

	// make sure we support content-range responses
	// and that we're case insensitive
	char const web_seed_response[] =
		"HTTP/1.1 206 OK\n"
		"contEnt-rAngE: bYTes 0-4\n"
		"conTent-TyPe: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, web_seed_response);

	TEST_CHECK(received == std::make_tuple(5, int(strlen(web_seed_response) - 5), false));
	TEST_CHECK(parser.content_range() == (std::pair<std::int64_t, std::int64_t>(0, 4)));
	TEST_CHECK(parser.content_length() == 5);

	parser.reset();

	// test invalid content range
	char const invalid_range_response[] =
		"HTTP/1.1 206 OK\n"
		"content-range: bytes 4-0\n"
		"content-type: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, invalid_range_response);

	TEST_CHECK(std::get<2>(received) == true);

	parser.reset();

	// test invalid status line
	char const invalid_status_response[] =
		"HTTP/1.1 206\n"
		"content-range: bytes 4-0\n"
		"content-type: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, invalid_status_response);

	TEST_CHECK(std::get<2>(received) == true);

	parser.reset();

	// test invalid status line 2
	char const invalid_status_response2[] =
		"HTTP/1.1\n"
		"content-range: bytes 4-0\n"
		"content-type: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, invalid_status_response2);

	TEST_CHECK(std::get<2>(received) == true);

	parser.reset();

	// make sure we support content-range responses
	// and that we're case insensitive
	char const one_hundred_response[] =
		"HTTP/1.1 100 Continue\n"
		"\r\n"
		"HTTP/1.1 200 OK\n"
		"Content-Length: 4\r\n"
		"Content-Type: test/plain\r\n"
		"\r\n"
		"test";

	received = feed_bytes(parser, one_hundred_response);

	TEST_CHECK(received == std::make_tuple(4, int(strlen(one_hundred_response) - 4), false));
	TEST_EQUAL(parser.content_length(), 4);

	{
		// test chunked encoding parser
		char const chunk_header1[] = "f;this is a comment\r\n";
		std::int64_t chunk_size;
		int header_size;
		bool ret = parser.parse_chunk_header(span<char const>(chunk_header1, 10)
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, false);
		ret = parser.parse_chunk_header(span<char const>(chunk_header1, sizeof(chunk_header1))
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, true);
		TEST_EQUAL(chunk_size, 15);
		TEST_EQUAL(header_size, sizeof(chunk_header1) - 1);

		char const chunk_header2[] =
			"0;this is a comment\r\n"
			"test1: foo\r\n"
			"test2: bar\r\n"
			"\r\n";

		ret = parser.parse_chunk_header(span<char const>(chunk_header2, sizeof(chunk_header2))
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
		== std::make_tuple("http", "foo:bar", "host.com", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path/to/file", ec)
		== std::make_tuple("http", "", "host.com", -1, "/path/to/file"));

	TEST_CHECK(parse_url_components("ftp://host.com:21/path/to/file", ec)
		== std::make_tuple("ftp", "", "host.com", 21, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path?foo:bar@foo:", ec)
		== std::make_tuple("http", "", "host.com", -1, "/path?foo:bar@foo:"));

	TEST_CHECK(parse_url_components("http://192.168.0.1/path/to/file", ec)
		== std::make_tuple("http", "", "192.168.0.1", -1, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://[2001:ff00::1]:42/path/to/file", ec)
		== std::make_tuple("http", "", "2001:ff00::1", 42, "/path/to/file"));

	// if there is no path component, "/" is added
	TEST_CHECK(parse_url_components("http://test.com:42", ec)
		== std::make_tuple("http", "", "test.com", 42, "/"));

	TEST_CHECK(parse_url_components("http://test.com:42/", ec)
		== std::make_tuple("http", "", "test.com", 42, "/"));

	TEST_CHECK(parse_url_components("http://test.com:42?query=string", ec)
		== std::make_tuple("http", "", "test.com", 42, "/?query=string"));

	TEST_CHECK(parse_url_components("http://test.com:42/?query=string", ec)
		== std::make_tuple("http", "", "test.com", 42, "/?query=string"));

	TEST_CHECK(parse_url_components("http://test.com:42#fragment", ec)
		== std::make_tuple("http", "", "test.com", 42, "/#fragment"));

	TEST_CHECK(parse_url_components("http://test.com:42/#fragment", ec)
		== std::make_tuple("http", "", "test.com", 42, "/#fragment"));

	// leading spaces are supposed to be stripped
	TEST_CHECK(parse_url_components(" \thttp://[2001:ff00::1]:42/path/to/file", ec)
		== std::make_tuple("http", "", "2001:ff00::1", 42, "/path/to/file"));

	parse_url_components("http://[2001:ff00::1:42/path/to/file", ec);
	TEST_CHECK(ec == error_code(errors::expected_close_bracket_in_address));

	parse_url_components("http:/", ec);
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));
	ec.clear();

	parse_url_components("http:", ec);
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));
	ec.clear();

	parse_url_components("http://test.com:42abc", ec);
	TEST_CHECK(ec == error_code(errors::invalid_port));

	// test split_url

	TEST_CHECK(split_url("http://foo:bar@host.com:80/path/to/file", ec)
		== std::make_tuple("http://foo:bar@host.com:80", "/path/to/file"));

	TEST_CHECK(split_url("http://host.com/path/to/file", ec)
		== std::make_tuple("http://host.com", "/path/to/file"));

	TEST_CHECK(split_url("ftp://host.com:21/path/to/file", ec)
		== std::make_tuple("ftp://host.com:21", "/path/to/file"));

	TEST_CHECK(split_url("http://host.com/path?foo:bar@foo:", ec)
		== std::make_tuple("http://host.com", "/path?foo:bar@foo:"));

	TEST_CHECK(split_url("http://192.168.0.1/path/to/file", ec)
		== std::make_tuple("http://192.168.0.1", "/path/to/file"));

	TEST_CHECK(split_url("http://[2001:ff00::1]:42/path/to/file", ec)
		== std::make_tuple("http://[2001:ff00::1]:42", "/path/to/file"));

	TEST_CHECK(split_url("http://[2001:ff00::1]:42", ec)
		== std::make_tuple("http://[2001:ff00::1]:42", ""));

	TEST_CHECK(split_url("bla://[2001:ff00::1]:42/path/to/file", ec)
		== std::make_tuple("bla://[2001:ff00::1]:42", "/path/to/file"));

	ec.clear();
	TEST_CHECK(split_url("foo:/[2001:ff00::1]:42/path/to/file", ec)
		== std::make_tuple("foo:/[2001:ff00::1]:42/path/to/file", ""));
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));

	ec.clear();
	TEST_CHECK(split_url("foo:/", ec)
		== std::make_tuple("foo:/", ""));
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));

	ec.clear();
	TEST_CHECK(split_url("//[2001:ff00::1]:42/path/to/file", ec)
		== std::make_tuple("//[2001:ff00::1]:42/path/to/file", ""));
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));

	ec.clear();
	TEST_CHECK(split_url("//host.com/path?foo:bar@foo:", ec)
		== std::make_tuple("//host.com/path?foo:bar@foo:", ""));
	TEST_CHECK(ec == error_code(errors::unsupported_url_protocol));

	// test resolve_redirect_location

	TEST_EQUAL(resolve_redirect_location("http://example.com/a/b", "a")
		, "http://example.com/a/a");

	TEST_EQUAL(resolve_redirect_location("http://example.com/a/b", "c/d/e/")
		, "http://example.com/a/c/d/e/");

	TEST_EQUAL(resolve_redirect_location("http://example.com/a/b", "../a")
		, "http://example.com/a/../a");

	TEST_EQUAL(resolve_redirect_location("http://example.com/a/b", "/c")
		, "http://example.com/c");

	TEST_EQUAL(resolve_redirect_location("http://example.com/a/b", "http://test.com/d")
		, "http://test.com/d");

	TEST_EQUAL(resolve_redirect_location("my-custom-scheme://example.com/a/b", "http://test.com/d")
		, "http://test.com/d");

	TEST_EQUAL(resolve_redirect_location("http://example.com", "/d")
		, "http://example.com/d");

	TEST_EQUAL(resolve_redirect_location("http://example.com", "d")
		, "http://example.com/d");

	TEST_EQUAL(resolve_redirect_location("my-custom-scheme://example.com/a/b", "/d")
		, "my-custom-scheme://example.com/d");

	TEST_EQUAL(resolve_redirect_location("my-custom-scheme://example.com/a/b", "c/d")
		, "my-custom-scheme://example.com/a/c/d");

	// if the referrer is invalid, just respond the verbatim location

	TEST_EQUAL(resolve_redirect_location("example.com/a/b", "/c/d")
		, "/c/d");

	// is_ok_status

	TEST_EQUAL(is_ok_status(200), true);
	TEST_EQUAL(is_ok_status(206), true);
	TEST_EQUAL(is_ok_status(299), false);
	TEST_EQUAL(is_ok_status(300), true);
	TEST_EQUAL(is_ok_status(399), true);
	TEST_EQUAL(is_ok_status(400), false);
	TEST_EQUAL(is_ok_status(299), false);

	// is_redirect

	TEST_EQUAL(is_redirect(299), false);
	TEST_EQUAL(is_redirect(100), false);
	TEST_EQUAL(is_redirect(300), true);
	TEST_EQUAL(is_redirect(399), true);
	TEST_EQUAL(is_redirect(400), false);
}

namespace {
std::string test_collapse_chunks(std::string chunked_input, bool const expect_error = false)
{
	http_parser parser;

	std::string input = "HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n";

	input += chunked_input;

	bool error = false;
	int payload = 0;
	int protocol = 0;
	std::tie(payload, protocol) = parser.incoming(input, error);

	TEST_CHECK(protocol > 0);
	TEST_CHECK(payload > 0);
	if (expect_error)
	{
		TEST_CHECK(std::size_t(protocol + payload) <= input.size());
	}
	else
	{
		TEST_EQUAL(std::size_t(protocol + payload), input.size());
		TEST_CHECK(parser.finished());
	}

	std::vector<char> mutable_buffer;
	span<char const> body = parser.get_body();
	std::copy(body.begin(), body.end(), std::back_inserter(mutable_buffer));
	body = parser.collapse_chunk_headers(mutable_buffer);
	return std::string(body.data(), std::size_t(body.size()));
}
}

TORRENT_TEST(chunked_encoding)
{
	auto const collapsed = test_collapse_chunks(
		"4\r\ntest\r\n4\r\n1234\r\n10\r\n0123456789abcdef\r\n"
		"0\r\n\r\n");
	TEST_EQUAL(collapsed, "test12340123456789abcdef");
}

TORRENT_TEST(chunked_encoding_beyond_end)
{
	auto const collapsed = test_collapse_chunks(
		"4\r\ntest\r\n4\r\n1234\r\n20\r\n0123456789abcdef\r\n"
		"0\r\n\r\n", true);
	TEST_EQUAL(collapsed, "test1234");
}

TORRENT_TEST(chunked_encoding_end_of_buffer)
{
	auto const collapsed = test_collapse_chunks(
		"4\r\ntest\r\n4\r\n1234\r\n17\r\n0123456789abcdef\r\n"
		"0\r\n\r\n", true);
	TEST_EQUAL(collapsed, "test12340123456789abcdef\r\n0\r\n\r\n");
}

TORRENT_TEST(chunked_encoding_past_end)
{
	auto const collapsed = test_collapse_chunks(
		"4\r\ntest\r\n4\r\n1234\r\n18\r\n0123456789abcdef\r\n"
		"0\r\n\r\n", true);
	TEST_EQUAL(collapsed, "test1234");
}

TORRENT_TEST(chunked_encoding_negative)
{
	auto const collapsed = test_collapse_chunks(
		"4\r\ntest\r\n4\r\n1234\r\n-10\r\n0123456789abcdef\r\n"
		"0\r\n\r\n", true);
	TEST_EQUAL(collapsed, "");
}

TORRENT_TEST(chunked_encoding_end)
{
	auto const collapsed = test_collapse_chunks(
		"4\r\ntest\r\n4\r\n1234\r\n11\r\n0123456789abcdef\r\n"
		"0\r\n\r\n", true);
	TEST_EQUAL(collapsed, "test12340123456789abcdef\r");
}

TORRENT_TEST(chunked_encoding_overflow)
{
	char const chunked_input[] =
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"7FFFFFFFFFFFFFBF\r\n";

	http_parser parser;
	int payload;
	int protocol;
	bool error = false;
	std::tie(payload, protocol) = parser.incoming(chunked_input, error);

	// it should have encountered an error
	TEST_CHECK(error == true);
}

TORRENT_TEST(invalid_content_length)
{
	char const chunked_input[] =
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"Content-Length: -45345\r\n"
		"\r\n";

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, chunked_input);

	TEST_CHECK(std::get<2>(received) == true);
}

TORRENT_TEST(invalid_chunked)
{
	char const chunked_input[] =
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"-53465234545\r\n"
		"foobar";

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, chunked_input);

	TEST_CHECK(std::get<2>(received) == true);
}

TORRENT_TEST(invalid_content_range_start)
{
	char const chunked_input[] =
		"HTTP/1.1 206 OK\n"
		"Content-Range: bYTes -3-4\n"
		"\n";

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, chunked_input);

	TEST_CHECK(std::get<2>(received) == true);
}

TORRENT_TEST(invalid_content_range_end)
{
	char const chunked_input[] =
		"HTTP/1.1 206 OK\n"
		"Content-Range: bYTes 3--434\n"
		"\n";

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, chunked_input);

	TEST_CHECK(std::get<2>(received) == true);
}

TORRENT_TEST(overflow_content_length)
{
	char const* chunked_input =
		"HTTP/1.1 200 OK\r\n"
		"Content-Length: 9999999999999999999999999999\r\n"
		"\r\n";

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, chunked_input);

	TEST_CHECK(std::get<2>(received) == true);
}

TORRENT_TEST(overflow_content_range_end)
{
	char const* chunked_input =
		"HTTP/1.1 206 OK\n"
		"Content-Range: bytes 0-999999999999999999999999\n"
		"\n";

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, chunked_input);

	TEST_CHECK(std::get<2>(received) == true);
}

TORRENT_TEST(overflow_content_range_begin)
{
	char const* chunked_input =
		"HTTP/1.1 206 OK\n"
		"Content-Range: bytes 999999999999999999999999-0\n"
		"\n";

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, chunked_input);

	TEST_CHECK(std::get<2>(received) == true);
}

TORRENT_TEST(missing_chunked_header)
{
	char const input[] =
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"\n";

	// make the inpout not be null terminated. If the parser reads off the end,
	// address sanitizer will trigger
	char chunked_input[sizeof(input)-1];
	std::memcpy(chunked_input, input, sizeof(chunked_input));

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, {chunked_input, sizeof(chunked_input)});

	TEST_CHECK(std::get<2>(received) == false);
}

TORRENT_TEST(invalid_chunk_1)
{
	std::uint8_t const invalid_chunked_input[] = {
		0x48, 0x6f, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, // HoTP/1.1 200 OK
		0x20, 0x32, 0x30, 0x30, 0x20, 0x4f, 0x4b, 0x0d, // Cont-Length: 20
		0x0a, 0x43, 0x6f, 0x6e, 0x74, 0x2d, 0x4c, 0x65, // Contente: tn
		0x6e, 0x67, 0x74, 0x68, 0x3a, 0x20, 0x32, 0x30, // Transfer-Encoding: chunked
		0x0d, 0x0a, 0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e,
		0x74, 0x65, 0x3a, 0x20, 0x74, 0x6e, 0x0d, 0x0a,
		0x54, 0x72, 0x61, 0x6e, 0x73, 0x66, 0x65, 0x72,
		0x2d, 0x45, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e,
		0x67, 0x3a, 0x20, 0x63, 0x68, 0x75, 0x6e, 0x6b,
		0x65, 0x64, 0x0d, 0x0a, 0x0d, 0x0d, 0x0a, 0x0d,
		0x0a, 0x0a, 0x2d, 0x38, 0x39, 0x61, 0x62, 0x63,
		0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x0d,
		0x0a, 0xd6, 0x0d, 0x0a, 0x54, 0xbd, 0xbd, 0xbd,
		0xbd, 0xbd, 0xbd, 0xbd, 0xbd, 0xbd, 0xbd, 0x64,
		0x65, 0x66, 0x0d, 0x0a, 0xd6, 0x0d, 0x0a, 0x54,
		0xbd, 0xbd, 0xbd, 0xbd, 0xbd, 0xbd, 0xbd, 0xbd,
		0xbd, 0xbd, 0xbd, 0x65, 0x73, 0x74, 0x2d, 0x68,
		0x65, 0x61, 0x64, 0x79, 0x72, 0x3a, 0x20, 0x66,
		0x6f, 0x6f, 0x62, 0x61, 0x72, 0x0d, 0x0a, 0x0d,
		0x0a, 0x00
	};

	http_parser parser;
	std::tuple<int, int, bool> const received
		= feed_bytes(parser, {reinterpret_cast<char const*>(invalid_chunked_input), sizeof(invalid_chunked_input)});

	TEST_CHECK(std::get<2>(received) == false);
}

TORRENT_TEST(invalid_chunk_2)
{
	std::uint8_t const invalid_chunked_input[] = {
		0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31,
		0x20, 0x32, 0x30, 0x30, 0x20, 0x4f, 0x4b, 0x0a, // HTTP/1.1 20x00, OK.
		0x54, 0x72, 0x61, 0x6e, 0x73, 0x66, 0x65, 0x72, // Transfer-Encodin
		0x2d, 0x45, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e, // g: chunked.Date:
		0x67, 0x3a, 0x20, 0x63, 0x68, 0x75, 0x6e, 0x6b, //  Sat, 0x31, Aug 200
		0x65, 0x64, 0x0a, 0x44, 0x61, 0x74, 0x65, 0x3a, // 2 00:24:0x08, GMT.C
		0x20, 0x53, 0x61, 0x74, 0x2c, 0x20, 0x33, 0x31, // onnection: close
		0x20, 0x41, 0x75, 0x67, 0x20, 0x32, 0x30, 0x30,
		0x32, 0x20, 0x30, 0x30, 0x3a, 0x32, 0x34, 0x3a,
		0x30, 0x38, 0x20, 0x47, 0x4d, 0x54, 0x0a, 0x43,
		0x6f, 0x6e, 0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f,
		0x6e, 0x3a, 0x20, 0x63, 0x6c, 0x6f, 0x73, 0x65,
		0x0a, 0x0a, 0x31, 0x0a, 0x72, 0x0a, 0x30, 0x0a,
		0x0a
	};

	http_parser parser;
	feed_bytes(parser, {reinterpret_cast<char const*>(invalid_chunked_input), sizeof(invalid_chunked_input)});
}

TORRENT_TEST(invalid_chunk_3)
{
	std::uint8_t const invalid_chunked_input[] = {
		0x48, 0xff, 0xff, 0xff, 0xfd, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0x20, 0x32, 0x30, 0x30,  // H....TTP/1.1 200
		0x20, 0x4f, 0x4b, 0x0a, 0x54, 0x72, 0x61, 0x6e, 0x73, 0x66, 0x65, 0x72, 0x2d, 0x45, 0x6e, 0x63,  //  OK.Transfer-Enc
		0x6f, 0x64, 0x69, 0x6e, 0x67, 0x3a, 0x20, 0x63, 0x68, 0x75, 0x6e, 0x6b, 0x65, 0x64, 0x0a, 0x44,  // oding: chunked.D
		0x61, 0x74, 0x65, 0x3a, 0x20, 0x53, 0x61, 0x74, 0x2c, 0x20, 0x33, 0x31, 0x20, 0x41, 0x75, 0x67,  // ate: Sat, 0x31, Aug
		0x20, 0x32, 0x30, 0x30, 0x32, 0x20, 0x30, 0x30, 0x3a, 0x32, 0x34, 0x3a, 0x30, 0x38, 0x20, 0x47,  //  2002 0x00,:0x24,:0x08, G
		0x4d, 0x54, 0x0a, 0x43, 0x6f, 0x6e, 0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x3a, 0x20, 0x63,  // MT.Connection: c
		0x6c, 0x6f, 0x73, 0x65, 0x0a, 0x0a, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,  // lose..0000000000
		0x36, 0x34, 0x0a, 0x6c, 0x72, 0x79, 0x6d, 0x68, 0x74, 0x67, 0x6b, 0x7a, 0x71, 0x74, 0x71, 0x71,  // 0x64,.lrymhtgkzqtqq
		0x62, 0x75, 0x71, 0x69, 0x66, 0x76, 0x69, 0x74, 0x6b, 0x70, 0x69, 0x63, 0x6f, 0x64, 0x67, 0x77,  // buqifvitkpicodgw
		0x6d, 0x69, 0x6a, 0x64, 0x67, 0x76, 0x6b, 0x63, 0x65, 0x78, 0x64, 0x75, 0x71, 0x74, 0x6e, 0x74,  // mijdgvkcexduqtnt
		0x6e, 0x66, 0x62, 0x75, 0x64, 0x6d, 0x6e, 0x6e, 0x62, 0x78, 0x72, 0x72, 0x63, 0x78, 0x6e, 0x70,  // nfbudmnnbxrrcxnp
		0x66, 0x79, 0x73, 0x6f, 0x74, 0x66, 0x71, 0x7a, 0x63, 0x74, 0x77, 0x75, 0x6d, 0x6a, 0x6e, 0x63,  // fysotfqzctwumjnc
		0x6f, 0x71, 0x77, 0x72, 0x63, 0x6d, 0x67, 0x64, 0x6c, 0x78, 0x77, 0x6f, 0x78, 0x6c, 0x64, 0x65,  // oqwrcmgdlxwoxlde
		0x6a, 0x76, 0x73, 0x66, 0x63, 0x6b, 0x65, 0x0a, 0x30, 0x0a, 0x0a, 0x0a,              // jvsfcke.0...
	};

	http_parser parser;
	feed_bytes(parser, {reinterpret_cast<char const*>(invalid_chunked_input), sizeof(invalid_chunked_input)});
}

TORRENT_TEST(idna)
{
	TEST_CHECK(!is_idna("a.b.com"));
	TEST_CHECK(!is_idna("example.com"));
	TEST_CHECK(!is_idna("exn--ample.com"));

	// xn-- is the ACE introducer for a punycoded label. It can appear at the
	// start of any label, but not in the middle (if it does, it's not
	// interpreted as an ACE). Since hostnames are case insensitive, the ACE
	// introducer has to be as well
	TEST_CHECK(is_idna("xn--example.com"));
	TEST_CHECK(is_idna("xN--example.com"));
	TEST_CHECK(is_idna("xn--example-.com"));
	TEST_CHECK(is_idna("subdomain.xn--example-.com"));
	TEST_CHECK(is_idna("subdomain.example.xn--com-"));

	// this isn't valid IDNA, but it's suspicious
	TEST_CHECK(is_idna("xn--.com"));

	// some weird edge-cases
	TEST_CHECK(!is_idna(".............."));
	TEST_CHECK(is_idna(".....xn--........."));
	TEST_CHECK(is_idna(".....Xn--........."));
	TEST_CHECK(is_idna(".....xN--........."));
	TEST_CHECK(is_idna(".....XN--........."));
	TEST_CHECK(!is_idna(".....-xn--........."));
	TEST_CHECK(is_idna("xn--"));
	TEST_CHECK(is_idna("Xn--"));
	TEST_CHECK(is_idna("XN--"));
	TEST_CHECK(is_idna("xN--"));
	TEST_CHECK(is_idna("xn--.xn--"));
	TEST_CHECK(is_idna(".....xn--"));
	TEST_CHECK(is_idna("xn--..."));
	TEST_CHECK(is_idna("xN--..."));
	TEST_CHECK(!is_idna(""));
	TEST_CHECK(!is_idna("."));

	TEST_CHECK(!is_idna("x"));
	TEST_CHECK(!is_idna("xn"));
	TEST_CHECK(!is_idna("xn-"));
	TEST_CHECK(!is_idna("-xn--"));

	TEST_CHECK(!is_idna(".x"));
	TEST_CHECK(!is_idna(".xn"));
	TEST_CHECK(!is_idna(".xn-"));
	TEST_CHECK(!is_idna(".-xn--"));

	TEST_CHECK(!is_idna("x."));
	TEST_CHECK(!is_idna("xn."));
	TEST_CHECK(!is_idna("xn-."));
	TEST_CHECK(!is_idna("-xn--."));
}

TORRENT_TEST(has_tracker_query_string)
{
	TEST_CHECK(has_tracker_query_string("foo=bar&info_hash=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("info_hash=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("&&info_hash=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("foo=bar&info_hash=abc"_sv));

	TEST_CHECK(has_tracker_query_string("foo=bar&port=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("foo=bar&event=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("foo=bar&downloaded=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("foo=bar&key=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("foo=bar&uploaded=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("foo=bar&corrupt=abc&a=b"_sv));
	TEST_CHECK(has_tracker_query_string("foo=bar&peer_id=abc&a=b"_sv));

	TEST_CHECK(!has_tracker_query_string("foo=bar&a=b"_sv));
	TEST_CHECK(!has_tracker_query_string(""_sv));
	TEST_CHECK(!has_tracker_query_string("info_hash1=abc"_sv));
	TEST_CHECK(!has_tracker_query_string("&1port=abc"_sv));
}
