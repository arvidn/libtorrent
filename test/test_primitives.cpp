/*

Copyright (c) 2008-2012, Arvid Norberg
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

#include "libtorrent/entry.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/socket_io.hpp" // for print_endpoint
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/hex.hpp" // from_hex
#include "libtorrent/fingerprint.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace lt;

TORRENT_TEST(retry_interval)
{
	// make sure the retry interval keeps growing
	// on failing announces
	announce_entry ae("dummy");
	ae.endpoints.emplace_back(aux::listen_socket_handle(), false);
	int last = 0;
	auto const tracker_backoff = 250;
	for (int i = 0; i < 10; ++i)
	{
		ae.endpoints.front().failed(tracker_backoff, seconds32(5));
		int const delay = static_cast<int>(total_seconds(ae.endpoints.front().next_announce - clock_type::now()));
		TEST_CHECK(delay > last);
		last = delay;
		std::printf("%d, ", delay);
	}
	std::printf("\n");
}

TORRENT_TEST(error_code)
{
	TEST_CHECK(error_code(errors::http_error).message() == "HTTP error");
	TEST_CHECK(error_code(errors::missing_file_sizes).message()
		== "missing or invalid 'file sizes' entry");
#if TORRENT_ABI_VERSION == 1
	TEST_CHECK(error_code(errors::unsupported_protocol_version).message()
		== "unsupported protocol version");
#endif
	TEST_CHECK(error_code(errors::no_i2p_router).message() == "no i2p router is set up");
	TEST_CHECK(error_code(errors::http_parse_error).message() == "Invalid HTTP header");
	TEST_CHECK(error_code(errors::error_code_max).message() == "Unknown error");

	TEST_CHECK(error_code(errors::unauthorized, http_category()).message()
		== "401 Unauthorized");
	TEST_CHECK(error_code(errors::service_unavailable, http_category()).message()
		== "503 Service Unavailable");
}

#if defined __GNUC__ && __GNUC__ >= 7
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation="
#endif

TORRENT_TEST(snprintf)
{
	char msg[10];
	std::snprintf(msg, sizeof(msg), "too %s format string", "long");
	TEST_CHECK(strcmp(msg, "too long ") == 0);
}

#if defined __GNUC__ && __GNUC__ >= 7
#pragma GCC diagnostic pop
#endif

TORRENT_TEST(address_to_from_string)
{
	if (!supports_ipv6()) return;

	error_code ec;
	// make sure the assumption we use in peer list hold
	std::multimap<address, int> peers;
	std::multimap<address, int>::iterator i;
	peers.insert(std::make_pair(address::from_string("::1", ec), 0));
	peers.insert(std::make_pair(address::from_string("::2", ec), 3));
	peers.insert(std::make_pair(address::from_string("::3", ec), 5));
	i = peers.find(address::from_string("::2", ec));
	TEST_CHECK(i != peers.end());
	if (i != peers.end())
	{
		TEST_CHECK(i->first == address::from_string("::2", ec));
		TEST_CHECK(i->second == 3);
	}
}

TORRENT_TEST(address_endpoint_io)
{
	// test print_endpoint, print_address
	TEST_EQUAL(print_endpoint(ep("127.0.0.1", 23)), "127.0.0.1:23");
	TEST_EQUAL(print_address(addr4("241.124.23.5")), "241.124.23.5");

	TEST_EQUAL(print_endpoint(ep("ff::1", 1214)), "[ff::1]:1214");
	TEST_EQUAL(print_address(addr6("2001:ff::1")), "2001:ff::1");

	// test address_to_bytes
	TEST_EQUAL(address_to_bytes(addr4("10.11.12.13")), "\x0a\x0b\x0c\x0d");
	TEST_EQUAL(address_to_bytes(addr4("16.5.127.1")), "\x10\x05\x7f\x01");

	// test endpoint_to_bytes
	TEST_EQUAL(endpoint_to_bytes(uep("10.11.12.13", 8080)), "\x0a\x0b\x0c\x0d\x1f\x90");
	TEST_EQUAL(endpoint_to_bytes(uep("16.5.127.1", 12345)), "\x10\x05\x7f\x01\x30\x39");
}

TORRENT_TEST(gen_fingerprint)
{
	TEST_EQUAL(generate_fingerprint("AB", 1, 2, 3, 4), "-AB1234-");
	TEST_EQUAL(generate_fingerprint("AB", 1, 2), "-AB1200-");
	TEST_EQUAL(generate_fingerprint("..", 1, 10), "-..1A00-");
	TEST_EQUAL(generate_fingerprint("CZ", 1, 15), "-CZ1F00-");
	TEST_EQUAL(generate_fingerprint("CZ", 1, 15, 16, 17), "-CZ1FGH-");
}

TORRENT_TEST(printf_int64)
{
	char buffer[100];
	std::int64_t val = 345678901234567ll;
	std::snprintf(buffer, sizeof(buffer), "%" PRId64 " %s", val, "end");
	TEST_EQUAL(buffer, std::string("345678901234567 end"));
}

TORRENT_TEST(printf_uint64)
{
	char buffer[100];
	std::uint64_t val = 18446744073709551615ull;
	std::snprintf(buffer, sizeof(buffer), "%" PRIu64 " %s", val, "end");
	TEST_EQUAL(buffer, std::string("18446744073709551615 end"));
}

#if defined __GNUC__ && __GNUC__ >= 7
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation="
#endif

TORRENT_TEST(printf_trunc)
{
	char buffer[4];
	int val = 184;
	std::snprintf(buffer, sizeof(buffer), "%d %s", val, "end");
	TEST_EQUAL(buffer, std::string("184"));
}

#if defined __GNUC__ && __GNUC__ >= 7
#pragma GCC diagnostic pop
#endif


TORRENT_TEST(error_condition)
{
#ifdef TORRENT_WINDOWS
	error_code ec(ERROR_FILE_NOT_FOUND, system_category());
#else
	error_code ec(ENOENT, system_category());
#endif
	TEST_CHECK(ec == boost::system::errc::no_such_file_or_directory);
}

