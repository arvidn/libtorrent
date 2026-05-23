/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <memory>

#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/aux_/receive_buffer.hpp"
#include "libtorrent/span.hpp"

#if !defined TORRENT_DISABLE_ENCRYPTION

namespace {

	std::array<char, 20> make_key(std::uint8_t const* data, std::size_t size, std::size_t salt)
	{
		std::array<char, 20> ret{};
		for (std::size_t i = 0; i < ret.size(); ++i)
			ret[i] = (size == 0) ? char(i + salt)
								 : char(data[(i + salt) % size] ^ std::uint8_t(i * 13 + salt));
		return ret;
	}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	lt::aux::dh_key_exchange a;
	lt::aux::dh_key_exchange b;

	auto const b_pub = lt::aux::export_key(b.get_local_key());
	a.compute_secret(reinterpret_cast<std::uint8_t const*>(b_pub.data()));
	auto const a_pub = lt::aux::export_key(a.get_local_key());
	b.compute_secret(reinterpret_cast<std::uint8_t const*>(a_pub.data()));

	auto key_in = make_key(data, size, 1);
	auto key_out = make_key(data, size, 7);

	auto rc4 = std::make_shared<lt::aux::rc4_handler>();
	rc4->set_incoming_key({key_in.data(), static_cast<std::ptrdiff_t>(key_in.size())});
	rc4->set_outgoing_key({key_out.data(), static_cast<std::ptrdiff_t>(key_out.size())});

	lt::aux::encryption_handler enc;
	enc.switch_send_crypto(rc4, 0);

	std::vector<char> payload(std::min<std::size_t>(size, 512));
	for (std::size_t i = 0; i < payload.size(); ++i)
		payload[i] = char(data[i]);

	lt::span<char> out_buf(payload.data(), static_cast<std::ptrdiff_t>(payload.size()));
	std::array<lt::span<char>, 1> out_iov{{out_buf}};
	enc.encrypt({out_iov.data(), static_cast<std::ptrdiff_t>(out_iov.size())});
	enc.switch_send_crypto(nullptr, 0);

	auto const cap = std::min<int>(int(payload.size()), 4096);
	lt::aux::receive_buffer rb;
	if (cap > 0)
	{
		rb.reset(cap);
		auto dst = rb.reserve(cap);
		std::copy_n(payload.data(), cap, dst.data());
		rb.received(cap);
		rb.advance_pos(cap);
	}

	lt::aux::crypto_receive_buffer crb(rb);
	enc.switch_recv_crypto(rc4, crb);
	if (cap > 0)
	{
		std::size_t transferred = std::size_t(std::min<int>(rb.pos(), 256));
		enc.decrypt(crb, transferred);
	}
	crb.crypto_reset((size > 0) ? int(data[0] % 32) : 0);

	return 0;
}

#else

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const*, std::size_t) { return 0; }

#endif
