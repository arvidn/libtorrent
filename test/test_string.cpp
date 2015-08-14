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
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/hex.hpp"
#include "libtorrent/string_util.hpp"
#include <iostream>
#include <string.h> // for strcmp

using namespace libtorrent;

TORRENT_TEST(string)
{
	// test maybe_url_encode
	TEST_EQUAL(maybe_url_encode("http://test:test@abc.com/abc<>abc"), "http://test:test@abc.com/abc%3c%3eabc");
	TEST_EQUAL(maybe_url_encode("http://abc.com/foo bar"), "http://abc.com/foo%20bar");
	TEST_EQUAL(maybe_url_encode("http://abc.com:80/foo bar"), "http://abc.com:80/foo%20bar");
	TEST_EQUAL(maybe_url_encode("http://abc.com:8080/foo bar"), "http://abc.com:8080/foo%20bar");
	TEST_EQUAL(maybe_url_encode("abc"), "abc");
	TEST_EQUAL(maybe_url_encode("http://abc.com/abc"), "http://abc.com/abc");

	// test to/from hex conversion

	char const* str = "0123456789012345678901234567890123456789";
	char bin[20];
	TEST_CHECK(from_hex(str, 40, bin));
	char hex[41];
	to_hex(bin, 20, hex);
	TEST_CHECK(strcmp(hex, str) == 0);

	TEST_CHECK(to_hex("\x55\x73") == "5573");
	TEST_CHECK(to_hex("\xaB\xd0") == "abd0");

	// test is_space

	TEST_CHECK(!is_space('C'));
	TEST_CHECK(!is_space('\b'));
	TEST_CHECK(!is_space('8'));
	TEST_CHECK(!is_space('='));
	TEST_CHECK(is_space(' '));
	TEST_CHECK(is_space('\t'));
	TEST_CHECK(is_space('\n'));
	TEST_CHECK(is_space('\r'));

	// test to_lower

	TEST_CHECK(to_lower('C') == 'c');
	TEST_CHECK(to_lower('c') == 'c');
	TEST_CHECK(to_lower('-') == '-');
	TEST_CHECK(to_lower('&') == '&');

	// test string_equal_no_case

	TEST_CHECK(string_equal_no_case("foobar", "FoobAR"));
	TEST_CHECK(string_equal_no_case("foobar", "foobar"));
	TEST_CHECK(!string_equal_no_case("foobar", "foobar "));
	TEST_CHECK(!string_equal_no_case("foobar", "F00"));

	// test string_begins_no_case

	TEST_CHECK(string_begins_no_case("foobar", "FoobAR --"));
	TEST_CHECK(!string_begins_no_case("foobar", "F00"));

	// test itoa

	TEST_CHECK(to_string(345).elems == std::string("345"));
	TEST_CHECK(to_string(-345).elems == std::string("-345"));
	TEST_CHECK(to_string(0).elems == std::string("0"));
	TEST_CHECK(to_string(1000000000).elems == std::string("1000000000"));

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

	// base32 for i2p
	TEST_CHECK(base32encode("fo", string::no_padding) == "MZXQ");
	TEST_CHECK(base32encode("foob", string::i2p) == "mzxw6yq");
	TEST_CHECK(base32encode("foobar", string::lowercase) == "mzxw6ytboi======");

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

	// make sure invalid encoding returns the empty string
   TEST_CHECK(base32decode("mZXw6yTBO1{#&*()=") == "");

	std::string test;
	for (int i = 0; i < 255; ++i)
		test += char(i);

	TEST_CHECK(base32decode(base32encode(test)) == test);

	// escape_string
	char const* test_string = "!@#$%^&*()-_=+/,. %?";
	TEST_EQUAL(escape_string(test_string, strlen(test_string))
		, "!%40%23%24%25%5e%26*()-_%3d%2b%2f%2c.%20%25%3f");

	// escape_path
	TEST_EQUAL(escape_path(test_string, strlen(test_string))
		, "!%40%23%24%25%5e%26*()-_%3d%2b/%2c.%20%25%3f");

	error_code ec;
	TEST_CHECK(unescape_string(escape_path(test_string, strlen(test_string)), ec) == test_string);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());

	// need_encoding
	char const* test_string2 = "!@$&()-_/,.%?";
	TEST_CHECK(need_encoding(test_string, strlen(test_string)) == true);
	TEST_CHECK(need_encoding(test_string2, strlen(test_string2)) == false);
	TEST_CHECK(need_encoding("\n", 1) == true);

	// maybe_url_encode
	TEST_EQUAL(maybe_url_encode("http://bla.com/\n"), "http://bla.com/%0a");
	TEST_EQUAL(maybe_url_encode("http://bla.com/foo%20bar"), "http://bla.com/foo%20bar");
	TEST_EQUAL(maybe_url_encode("http://bla.com/foo%20bar?k=v&k2=v2"), "http://bla.com/foo%20bar?k=v&k2=v2");
	TEST_EQUAL(maybe_url_encode("?&"), "?&");

	// unescape_string
	TEST_CHECK(unescape_string(escape_string(test_string, strlen(test_string)), ec)
		== test_string);
	std::cerr << unescape_string(escape_string(test_string, strlen(test_string)), ec) << std::endl;
	// prematurely terminated string
	unescape_string("%", ec);
	TEST_CHECK(ec == error_code(errors::invalid_escaped_string));
	unescape_string("%0", ec);
	TEST_CHECK(ec == error_code(errors::invalid_escaped_string));

	// invalid hex character
	unescape_string("%GE", ec);
	TEST_CHECK(ec == error_code(errors::invalid_escaped_string));
	unescape_string("%eg", ec);
	TEST_CHECK(ec == error_code(errors::invalid_escaped_string));
	ec.clear();

	TEST_CHECK(unescape_string("123+abc", ec) == "123 abc");


	// read_until

	char const* test_string1 = "abcdesdf sdgf";
	char const* tmp1 = test_string1;
	TEST_CHECK(read_until(tmp1, 'd', test_string1 + strlen(test_string1)) == "abc");

	tmp1 = test_string1;
	TEST_CHECK(read_until(tmp1, '[', test_string1 + strlen(test_string1)) == "abcdesdf sdgf");

	char hex_chars[] = "0123456789abcdefABCDEF";

	for (int i = 1; i < 255; ++i)
	{
		bool hex = strchr(hex_chars, i) != NULL;
		char c = i;
		TEST_EQUAL(detail::is_hex(&c, 1), hex);
	}

	TEST_EQUAL(detail::hex_to_int('0'), 0);
	TEST_EQUAL(detail::hex_to_int('7'), 7);
	TEST_EQUAL(detail::hex_to_int('a'), 10);
	TEST_EQUAL(detail::hex_to_int('f'), 15);
	TEST_EQUAL(detail::hex_to_int('b'), 11);
	TEST_EQUAL(detail::hex_to_int('t'), -1);
	TEST_EQUAL(detail::hex_to_int('g'), -1);

	std::string path = "a\\b\\c";
	convert_path_to_posix(path);
	TEST_EQUAL(path, "a/b/c");

	// url_has_argument

	TEST_CHECK(url_has_argument("http://127.0.0.1/test", "test") == "");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24", "bar") == "");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24", "foo") == "24");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23", "foo") == "24");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23", "bar") == "23");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "bar") == "23");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "a") == "e");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "b") == "");

	// resolve_file_url

#ifdef TORRENT_WINDOWS
	std::string p = "c:/blah/foo/bar\\";
	convert_path_to_windows(p);
	TEST_EQUAL(p, "c:\\blah\\foo\\bar\\");
	TEST_EQUAL(resolve_file_url("file:///c:/blah/foo/bar"), "c:\\blah\\foo\\bar");
	TEST_EQUAL(resolve_file_url("file:///c:/b%3fah/foo/bar"), "c:\\b?ah\\foo\\bar");
	TEST_EQUAL(resolve_file_url("file://\\c:\\b%3fah\\foo\\bar"), "c:\\b?ah\\foo\\bar");
#else
	TEST_EQUAL(resolve_file_url("file:///c/blah/foo/bar"), "/c/blah/foo/bar");
	TEST_EQUAL(resolve_file_url("file:///c/b%3fah/foo/bar"), "/c/b?ah/foo/bar");
#endif

	std::vector<std::string> list;
	parse_comma_separated_string("  a,b, c, d ,e \t,foobar\n\r,[::1]", list);
	TEST_EQUAL(list.size(), 7);
	TEST_EQUAL(list[0], "a");
	TEST_EQUAL(list[1], "b");
	TEST_EQUAL(list[2], "c");
	TEST_EQUAL(list[3], "d");
	TEST_EQUAL(list[4], "e");
	TEST_EQUAL(list[5], "foobar");
	TEST_EQUAL(list[6], "[::1]");

	std::vector<std::pair<std::string, int> > list2;
	parse_comma_separated_string_port("  a:4,b:35, c : 1000, d: 351 ,e \t:42,foobar:1337\n\r,[2001::1]:6881", list2);
	TEST_EQUAL(list2.size(), 7);
	TEST_EQUAL(list2[0].first, "a");
	TEST_EQUAL(list2[1].first, "b");
	TEST_EQUAL(list2[2].first, "c");
	TEST_EQUAL(list2[3].first, "d");
	TEST_EQUAL(list2[4].first, "e");
	TEST_EQUAL(list2[5].first, "foobar");
	TEST_EQUAL(list2[6].first, "2001::1");

	TEST_EQUAL(list2[0].second, 4);
	TEST_EQUAL(list2[1].second, 35);
	TEST_EQUAL(list2[2].second, 1000);
	TEST_EQUAL(list2[3].second, 351);
	TEST_EQUAL(list2[4].second, 42);
	TEST_EQUAL(list2[5].second, 1337);
	TEST_EQUAL(list2[6].second, 6881);

	// test string_tokenize

	char test_tokenize[] = "a b c \"foo bar\" d\ne f";
	char* next = test_tokenize;
	char* ptr = string_tokenize(next, ' ', &next);
	TEST_EQUAL(ptr, std::string("a"));

	ptr = string_tokenize(next, ' ', &next);
	TEST_EQUAL(ptr, std::string("b"));

	ptr = string_tokenize(next, ' ', &next);
	TEST_EQUAL(ptr, std::string("c"));

	ptr = string_tokenize(next, ' ', &next);
	TEST_EQUAL(ptr, std::string("\"foo bar\""));

	ptr = string_tokenize(next, ' ', &next);
	TEST_EQUAL(ptr, std::string("d\ne"));

	ptr = string_tokenize(next, ' ', &next);
	TEST_EQUAL(ptr, std::string("f"));

	ptr = string_tokenize(next, ' ', &next);
	TEST_EQUAL(ptr, static_cast<char*>(0));

	TEST_EQUAL(std::string("foobar"), convert_from_native(convert_to_native("foobar")));
	TEST_EQUAL(std::string("foobar")
		, convert_from_native(convert_to_native("foo"))
		+ convert_from_native(convert_to_native("bar")));

	TEST_EQUAL(convert_to_native("foobar")
		, convert_to_native("foo") + convert_to_native("bar"));
}

