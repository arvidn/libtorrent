/*

Copyright (c) 2012-2017, 2020, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/peer_list.hpp"
#include "setup_transfer.hpp" // for supports_ipv6()
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "test.hpp"

using namespace lt;

namespace {
std::uint32_t hash_buffer(char const* buf, int len)
{
	boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
	crc.process_block(buf, buf + len);
	return crc.checksum();
}
} // anonymous namespace

TORRENT_TEST(peer_priority)
{
	// when the IP is the same, we hash the ports, sorted
	std::uint32_t p = aux::peer_priority(
		ep("230.12.123.3", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("\x01\x2c\x04\xd2", 4));

	// when we're in the same /24, we just hash the IPs
	p = aux::peer_priority(ep("230.12.123.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("\xe6\x0c\x7b\x01\xe6\x0c\x7b\x03", 8));

	// when we're in the same /16, we just hash the IPs masked by
	// 0xffffff55
	p = aux::peer_priority(ep("230.12.23.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("\xe6\x0c\x17\x01\xe6\x0c\x7b\x01", 8));

	// when we're in different /16, we just hash the IPs masked by
	// 0xffff5555
	p = aux::peer_priority(ep("230.120.23.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("\xe6\x0c\x51\x01\xe6\x78\x15\x01", 8));

	// test vectors from BEP 40
	TEST_EQUAL(aux::peer_priority(ep("123.213.32.10", 0), ep("98.76.54.32", 0))
		, 0xec2d7224);

	TEST_EQUAL(aux::peer_priority(
		ep("123.213.32.10", 0), ep("123.213.32.234", 0))
		, 0x99568189);

	if (supports_ipv6())
	{
		// IPv6 has a twice as wide mask, and we only care about the top 64 bits
		// when the IPs are the same, just hash the ports
		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff::1", 0x4d2), ep("ffff:ffff:ffff:ffff::1", 0x12c));
		TEST_EQUAL(p, hash_buffer("\x01\x2c\x04\xd2", 4));

		// these IPs don't belong to the same /32, so apply the full mask
		// 0xffffffff55555555
		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff::1", 0x4d2), ep("ffff:0fff:ffff:ffff::1", 0x12c));
		TEST_EQUAL(p, hash_buffer(
			"\xff\xff\x0f\xff\x55\x55\x55\x55\x00\x00\x00\x00\x00\x00\x00\x01"
			"\xff\xff\xff\xff\x55\x55\x55\x55\x00\x00\x00\x00\x00\x00\x00\x01", 32));
	}
}
