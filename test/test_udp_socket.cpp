/*

Copyright (c) 2026, Arvid Norberg
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
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/span.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace lt;

namespace {

// build a SOCKS5 UDP forwarded packet for an IPv4 destination.
// header layout (RFC 1928 section 7):
//   2 bytes RSV (0x0000)
//   1 byte  FRAG
//   1 byte  ATYP (0x01 = IPv4)
//   4 bytes addr
//   2 bytes port
//   N bytes payload
std::vector<char> make_v4_packet(std::uint8_t const frag
	, std::array<std::uint8_t, 4> const addr
	, std::uint16_t const port
	, span<char const> payload)
{
	std::vector<char> buf;
	buf.push_back(0); buf.push_back(0); // RSV
	buf.push_back(char(frag));
	buf.push_back(0x01); // ATYP = IPv4
	for (auto b : addr) buf.push_back(char(b));
	buf.push_back(char(port >> 8));
	buf.push_back(char(port & 0xff));
	buf.insert(buf.end(), payload.begin(), payload.end());
	return buf;
}

// build a SOCKS5 UDP forwarded packet for an IPv6 destination.
// header layout: 2 RSV + 1 FRAG + 1 ATYP(0x04) + 16 addr + 2 port + payload
std::vector<char> make_v6_packet(std::uint8_t const frag
	, std::array<std::uint8_t, 16> const addr
	, std::uint16_t const port
	, span<char const> payload)
{
	std::vector<char> buf;
	buf.push_back(0); buf.push_back(0);
	buf.push_back(char(frag));
	buf.push_back(0x04);
	for (auto b : addr) buf.push_back(char(b));
	buf.push_back(char(port >> 8));
	buf.push_back(char(port & 0xff));
	buf.insert(buf.end(), payload.begin(), payload.end());
	return buf;
}

// build a SOCKS5 UDP forwarded packet for a domain-name destination.
// header layout: 2 RSV + 1 FRAG + 1 ATYP(0x03) + 1 LEN + LEN hostname + 2 port + payload
std::vector<char> make_hostname_packet(std::uint8_t const frag
	, string_view const hostname
	, std::uint16_t const port
	, span<char const> payload)
{
	std::vector<char> buf;
	buf.push_back(0); buf.push_back(0);
	buf.push_back(char(frag));
	buf.push_back(0x03);
	buf.push_back(char(hostname.size()));
	buf.insert(buf.end(), hostname.begin(), hostname.end());
	buf.push_back(char(port >> 8));
	buf.push_back(char(port & 0xff));
	buf.insert(buf.end(), payload.begin(), payload.end());
	return buf;
}

} // anonymous namespace

TORRENT_TEST(socks5_unwrap_ipv4)
{
	std::array<char, 4> const payload{{'a', 'b', 'c', 'd'}};
	auto buf = make_v4_packet(0, {{1, 2, 3, 4}}, 6881, payload);

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.from.address().to_string(), "1.2.3.4");
	TEST_EQUAL(pack.from.port(), 6881);
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_unwrap_ipv6)
{
	std::array<std::uint8_t, 16> const a{{
		0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01}};
	std::array<char, 3> const payload{{'x', 'y', 'z'}};
	auto buf = make_v6_packet(0, a, 1234, payload);

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.from.address().is_v6(), true);
	TEST_EQUAL(pack.from.port(), 1234);
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_unwrap_hostname_resolvable)
{
	// a hostname that parses as a valid address goes into pack.from
	std::array<char, 2> const payload{{'h', 'i'}};
	auto buf = make_hostname_packet(0, "5.6.7.8", 9000, payload);

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.from.address().to_string(), "5.6.7.8");
	TEST_EQUAL(pack.from.port(), 9000);
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_unwrap_hostname_unresolvable)
{
	// a hostname that does not parse as an address goes into pack.hostname
	std::array<char, 2> const payload{{'h', 'i'}};
	auto buf = make_hostname_packet(0, "example.org", 80, payload);

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.hostname, "example.org");
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_unwrap_reject_fragmented)
{
	std::array<char, 1> const payload{{'!'}};
	auto buf = make_v4_packet(1, {{1, 2, 3, 4}}, 1, payload);

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

TORRENT_TEST(socks5_unwrap_reject_too_short)
{
	// the IPv4 minimum is 10 bytes of header, plus at least one payload byte
	std::array<char, 10> buf{};
	buf[3] = 0x01; // ATYP = IPv4

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

// regression: an IPv6 forwarded packet shorter than the minimum 22-byte
// header (4 byte preamble + 16 byte address + 2 byte port) was previously
// only rejected by the IPv4-sized "size <= 10" check, which let unwrap()
// read past the end of the buffer.
TORRENT_TEST(socks5_unwrap_reject_truncated_ipv6)
{
	// 11 bytes total: passes the size > 10 check, but is well short of the
	// 22 bytes required to read a v6 endpoint.
	std::array<char, 11> buf{};
	buf[3] = 0x04; // ATYP = IPv6

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

TORRENT_TEST(socks5_unwrap_reject_ipv6_one_short)
{
	// exactly 22 bytes is still a header with no payload. The implementation
	// requires size > 22 to leave room for at least one byte of payload.
	std::array<char, 22> buf{};
	buf[3] = 0x04;

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

// regression: a hostname-ATYP packet whose length byte reaches the end of
// the buffer leaves no room for the trailing 2-byte port. The previous
// bounds check only required the hostname to fit, so unwrap() would read
// 2 bytes past the buffer for the port.
TORRENT_TEST(socks5_unwrap_reject_hostname_missing_port)
{
	// 4 byte preamble + 1 byte LEN + 5 byte hostname = 10 bytes, no port.
	std::array<char, 10> buf{};
	buf[3] = 0x03; // ATYP = hostname
	buf[4] = 5;    // LEN
	buf[5] = 'h'; buf[6] = 'e'; buf[7] = 'l'; buf[8] = 'l'; buf[9] = 'o';
	// Must be > 10 bytes to reach the hostname branch, so add one trailing
	// byte that is too few for a port.
	std::vector<char> v(buf.begin(), buf.end());
	v.push_back(0); // only one of the two needed port bytes

	udp_socket::packet pack;
	pack.data = span<char>{v.data(), int(v.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

TORRENT_TEST(socks5_unwrap_reject_hostname_overflow)
{
	// LEN claims more bytes than are present in the buffer
	std::array<char, 12> buf{};
	buf[3] = 0x03;
	buf[4] = 50; // LEN much larger than what's in the buffer

	udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}
