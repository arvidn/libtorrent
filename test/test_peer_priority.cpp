/*

Copyright (c) 2012-2017, 2019-2020, Arvid Norberg
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

#include "libtorrent/peer_list.hpp"
#include "libtorrent/hasher.hpp"
#include "setup_transfer.hpp" // for supports_ipv6()
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
	std::uint32_t p = peer_priority(
		ep("230.12.123.3", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("012c04d2"));

	// when we're in the same /24, we just hash the IPs
	p = peer_priority(ep("230.12.123.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("e60c7b01e60c7b03"));

	// when we're in the same /16, we just hash the IPs masked by
	// 0xffffff55
	p = peer_priority(ep("230.12.23.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("e60c1701e60c7b01"));

	// when we're in different /16, we just hash the IPs masked by
	// 0xffff5555
	p = peer_priority(ep("230.120.23.1", 0x4d2), ep("230.12.123.3", 0x12c));
	TEST_EQUAL(p, hash_buffer("e60c5101e6781501"));

	// test vectors from BEP 40
	TEST_EQUAL(peer_priority(ep("123.213.32.10", 0), ep("98.76.54.32", 0))
		, 0xec2d7224);

	TEST_EQUAL(peer_priority(
		ep("123.213.32.10", 0), ep("123.213.32.234", 0))
		, 0x99568189);

	if (supports_ipv6())
	{
		// if the IPs are identical, order and hash the ports
		p = peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer("012c04d2"));
		// the order doesn't matter
		p = peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x12c)
			, ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
		);
		TEST_EQUAL(p, hash_buffer("012c04d2"));

		// these IPs don't belong to the same /32, so apply the full mask
		// 0xffffffffffff55555555555555555555
		p = peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:0fff:ffff:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffff0fffffff55555555555555555555"
			"ffffffffffff55555555555555555555")
		);

		p = peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:0fff:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffffffff0fff55555555555555555555"
			"ffffffffffff55555555555555555555")
		);

		// these share the same /48
		p = peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:ff0f:ffff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffffffffff0fff555555555555555555"
			"ffffffffffffff555555555555555555")
		);

		// these share the same /56
		p = peer_priority(
			ep("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0x4d2)
			, ep("ffff:ffff:ffff:0fff:ffff:ffff:ffff:ffff", 0x12c)
		);
		TEST_EQUAL(p, hash_buffer(
			"ffffffffffff0fff5555555555555555"
			"ffffffffffffffff5555555555555555")
		);
	}
}
