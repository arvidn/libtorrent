/*

Copyright (c) 2012-2017, 2020-2021, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/peer_list.hpp"
#include "setup_transfer.hpp" // for supports_ipv6()
#include "libtorrent/hex.hpp"
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "test.hpp"

using namespace lt;

namespace {
std::uint32_t hash_buffer(std::string const& hex)
{
	std::vector<char> buffer(hex.size() / 2);
	aux::from_hex(hex, buffer.data());
	boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
	crc.process_block(buffer.data(), buffer.data() + buffer.size());
	return crc.checksum();
}
} // anonymous namespace

TORRENT_TEST(peer_priority)
{
	// when the IP is the same, we hash the ports, sorted
	std::uint32_t p = aux::peer_priority(
		ep("230.12.123.3", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("012c04d2"));

	// when we're in the same /24, we just hash the IPs
	p = aux::peer_priority(ep("230.12.123.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("e60c7b01e60c7b03"));

	// when we're in the same /16, we just hash the IPs masked by
	// 0xffffff55
	p = aux::peer_priority(ep("230.12.23.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("e60c1701e60c7b01"));

	// when we're in different /16, we just hash the IPs masked by
	// 0xffff5555
	p = aux::peer_priority(ep("230.120.23.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("e60c5101e6781501"));

	// test vectors from BEP 40
	TEST_EQUAL(aux::peer_priority(ep("123.213.32.10", 0), ep("98.76.54.32", 0))
		, 0xec2d7224);

	TEST_EQUAL(aux::peer_priority(
		ep("123.213.32.10", 0), ep("123.213.32.234", 0))
		, 0x99568189);

	if (supports_ipv6())
	{
		// if the IPs are identical, order and hash the ports
		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer("012c04d2"));
		// the order doesn't matter
		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x12c)
			, ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
		);
		TEST_EQUAL(p, hash_buffer("012c04d2"));

		// these IPs don't belong to the same /32, so apply the full mask
		// 0xffffffffffff55555555555555555555
		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:0fff:ffff:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffff0fffffff55555555555555555555"
			"ffffffffffff55555555555555555555")
		);

		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:0fff:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffffffff0fff55555555555555555555"
			"ffffffffffff55555555555555555555")
		);

		// these share the same /48
		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:ff0f:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffffffffff0fff555555555555555555"
			"ffffffffffffff555555555555555555")
		);

		// these share the same /56
		p = aux::peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:ffff:0fff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffffffffffff0fff5555555555555555"
			"ffffffffffffffff5555555555555555")
		);
	}
}
