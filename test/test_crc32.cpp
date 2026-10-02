/*

Copyright (c) 2014-2017, 2020-2021, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/crc32c.hpp"
#include "libtorrent/aux_/cpuid.hpp"
#include "libtorrent/aux_/string_util.hpp" // for to_lower
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/byteswap.hpp"
#include "test.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <array>
#include <cstring>

namespace {

// independent reference implementation, used to cross-check every
// aux::crc32c*() entry point below
std::uint32_t crc32c_buffer(char const* buf, int const len)
{
	boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
	crc.process_block(buf, buf + len);
	return crc.checksum();
}

std::uint32_t crc32c_lowercase_buffer(char const* buf, int const len)
{
	boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
	for (int i = 0; i < len; ++i)
		crc.process_byte(static_cast<std::uint8_t>(lt::aux::to_lower(buf[i])));
	return crc.checksum();
}

std::uint32_t crc32c_mix_buffer(char const* buf, int const len)
{
	std::uint32_t state = lt::aux::crc32c_init;
	for (int i = 0; i < len; ++i)
		state = lt::aux::crc32c_mix(state, static_cast<std::uint8_t>(buf[i]));
	return lt::aux::crc32c_finish(state);
}
}

TORRENT_TEST(crc32)
{
	using namespace lt;

	std::uint32_t in1 = aux::host_to_network(0xeffea55a);
	std::uint32_t out = aux::crc32c(in1);
	TEST_EQUAL(out, 0x5ee3b9d5);

	auto const check_block = [](unsigned char const* raw, std::uint32_t const expected) {
		std::array<std::uint64_t, 4> buf;
		std::memcpy(buf.data(), raw, 32);

		std::uint32_t const block_out = aux::crc32c(buf);
		TEST_EQUAL(block_out, expected);
		TEST_EQUAL(block_out, crc32c_buffer(reinterpret_cast<char const*>(raw), 32));
		TEST_EQUAL(block_out, crc32c_mix_buffer(reinterpret_cast<char const*>(raw), 32));

		// the single-uint64_t overload, checked against the leading 8 bytes
		std::uint64_t word0;
		std::memcpy(&word0, raw, 8);
		TEST_EQUAL(aux::crc32c(word0), crc32c_buffer(reinterpret_cast<char const*>(raw), 8));
	};

	// https://tools.ietf.org/html/rfc3720#appendix-B.4
	unsigned char zeros[32] = {};
	check_block(zeros, 0x8a9136aaU);

	unsigned char ones[32];
	std::memset(ones, 0xff, 32);
	check_block(ones, 0x62a8ab43U);

	unsigned char ramp[32];
	for (int i = 0; i < 32; ++i)
		ramp[i] = static_cast<unsigned char>(i);
	check_block(ramp, 0x46dd794eU);

#if !TORRENT_HAS_ARM
	TORRENT_ASSERT(!aux::arm_crc32c_support);
#endif
}

TORRENT_TEST(crc32c_mix_lowercase)
{
	using namespace lt;

	// every length from 0 through past two word-sized chunks, to exercise
	// every possible tail size alongside the batched word path inside
	// crc32c_mix_lowercase()
	std::string s;
	for (int len = 0; len < 20; ++len)
	{
		s += char('A' + (len % 26));
		std::uint32_t const got =
			aux::crc32c_finish(aux::crc32c_mix_lowercase(aux::crc32c_init, s));
		std::uint32_t const want = crc32c_lowercase_buffer(s.data(), int(s.size()));
		TEST_EQUAL(got, want);
	}
}

TORRENT_TEST(crc32c_mix_string)
{
	using namespace lt;

	// lengths past two words cover both the batched 8-byte path and every
	// tail size; mixed case catches any accidental folding
	std::string s;
	for (int len = 0; len < 20; ++len)
	{
		s += char((len % 2) ? 'A' + (len % 26) : 'a' + (len % 26));
		std::uint32_t const got = aux::crc32c_finish(aux::crc32c_mix(aux::crc32c_init, s));
		TEST_EQUAL(got, crc32c_mix_buffer(s.data(), int(s.size())));
	}

	TEST_CHECK(
		aux::crc32c_mix(aux::crc32c_init, "Foo") != aux::crc32c_mix(aux::crc32c_init, "foo"));
	TEST_EQUAL(aux::crc32c_mix(aux::crc32c_init, "foo"),
		aux::crc32c_mix_lowercase(aux::crc32c_init, "Foo"));
}

TORRENT_TEST(crc32c_mix_compose)
{
	using namespace lt;

	// file_storage::compute_element_hashes() extends a parent directory's
	// already-computed running state independently per child, so
	// continuing from a saved intermediate state must match feeding the
	// whole sequence to crc32c_mix_lowercase() in one call. The state is a
	// plain value with no hidden identity, so re-deriving the same prefix
	// from scratch and continuing must give the same result too.
	std::uint32_t const prefix = aux::crc32c_mix_lowercase(aux::crc32c_init, "ab");

	std::uint32_t const child1 = aux::crc32c_mix_lowercase(prefix, "c");
	std::uint32_t const child2 = aux::crc32c_mix_lowercase(prefix, "d");

	TEST_EQUAL(aux::crc32c_finish(child1), crc32c_lowercase_buffer("abc", 3));
	TEST_EQUAL(aux::crc32c_finish(child2), crc32c_lowercase_buffer("abd", 3));
	TEST_CHECK(child1 != child2);

	std::uint32_t const prefix_again = aux::crc32c_mix_lowercase(aux::crc32c_init, "ab");
	TEST_EQUAL(aux::crc32c_mix_lowercase(prefix_again, "c"), child1);

	// a single byte mixed in with crc32c_mix() (the path element separator
	// in compute_element_hashes()) composes the same way
	std::uint32_t const with_sep = aux::crc32c_mix(prefix, '/');
	std::uint32_t const with_sep_and_child = aux::crc32c_mix_lowercase(with_sep, "c");
	TEST_EQUAL(aux::crc32c_finish(with_sep_and_child), crc32c_lowercase_buffer("ab/c", 4));
}
