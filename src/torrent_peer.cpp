/*

Copyright (c) 2012-2018, Arvid Norberg
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

#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/crc32c.hpp"
#include "libtorrent/ip_voter.hpp"
#include "libtorrent/io.hpp" // for write_uint16

namespace libtorrent {

	namespace {

		void apply_mask(std::uint8_t* b, std::uint8_t const* mask, int size)
		{
			for (int i = 0; i < size; ++i)
			{
				*b &= *mask;
				++b;
				++mask;
			}
		}
	}

	// 1. if the IP addresses are identical, hash the ports in 16 bit network-order
	//    binary representation, ordered lowest first.
	// 2. if the IPs are in the same /24, hash the IPs ordered, lowest first.
	// 3. if the IPs are in the ame /16, mask the IPs by 0xffffff55, hash them
	//    ordered, lowest first.
	// 4. if IPs are not in the same /16, mask the IPs by 0xffff5555, hash them
	//    ordered, lowest first.
	//
	// * for IPv6 peers, just use the first 64 bits and widen the masks.
	//   like this: 0xffff5555 -> 0xffffffff55555555
	//   the lower 64 bits are always unmasked
	//
	// * for IPv6 addresses, compare /32 and /48 instead of /16 and /24
	//
	// * the two IP addresses that are used to calculate the rank must
	//   always be of the same address family
	//
	// * all IP addresses are in network byte order when hashed
	std::uint32_t peer_priority(tcp::endpoint e1, tcp::endpoint e2)
	{
		TORRENT_ASSERT(is_v4(e1) == is_v4(e2));

		using std::swap;

		std::uint32_t ret;
		if (e1.address() == e2.address())
		{
			if (e1.port() > e2.port())
				swap(e1, e2);
			std::uint32_t p;
			auto ptr = reinterpret_cast<char*>(&p);
			detail::write_uint16(e1.port(), ptr);
			detail::write_uint16(e2.port(), ptr);
			ret = crc32c_32(p);
		}
		else if (is_v6(e1))
		{
			static const std::uint8_t v6mask[][8] = {
				{ 0xff, 0xff, 0xff, 0xff, 0x55, 0x55, 0x55, 0x55 },
				{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x55, 0x55 },
				{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
			};

			if (e1 > e2) swap(e1, e2);
			address_v6::bytes_type b1 = e1.address().to_v6().to_bytes();
			address_v6::bytes_type b2 = e2.address().to_v6().to_bytes();
			int const mask = std::memcmp(b1.data(), b2.data(), 4) ? 0
				: std::memcmp(b1.data(), b2.data(), 6) ? 1 : 2;
			apply_mask(b1.data(), v6mask[mask], 8);
			apply_mask(b2.data(), v6mask[mask], 8);
			std::uint64_t addrbuf[4];
			memcpy(&addrbuf[0], b1.data(), 16);
			memcpy(&addrbuf[2], b2.data(), 16);
			ret = crc32c(addrbuf, 4);
		}
		else
		{
			static const std::uint8_t v4mask[][4] = {
				{ 0xff, 0xff, 0x55, 0x55 },
				{ 0xff, 0xff, 0xff, 0x55 },
				{ 0xff, 0xff, 0xff, 0xff }
			};

			if (e1 > e2) swap(e1, e2);
			address_v4::bytes_type b1 = e1.address().to_v4().to_bytes();
			address_v4::bytes_type b2 = e2.address().to_v4().to_bytes();
			int mask = memcmp(&b1[0], &b2[0], 2) ? 0
				: memcmp(&b1[0], &b2[0], 3) ? 1 : 2;
			apply_mask(&b1[0], v4mask[mask], 4);
			apply_mask(&b2[0], v4mask[mask], 4);
			std::uint64_t addrbuf;
			memcpy(&addrbuf, &b1[0], 4);
			memcpy(reinterpret_cast<char*>(&addrbuf) + 4, &b2[0], 4);
			ret = crc32c(&addrbuf, 1);
		}

		return ret;
	}

	torrent_peer::torrent_peer(std::uint16_t port_, bool conn
		, peer_source_flags_t const src)
		: prev_amount_upload(0)
		, prev_amount_download(0)
		, connection(nullptr)
		, peer_rank(0)
		, last_optimistically_unchoked(0)
		, last_connected(0)
		, port(port_)
		, hashfails(0)
		, failcount(0)
		, connectable(conn)
		, optimistically_unchoked(false)
		, seed(false)
		, fast_reconnects(0)
		, trust_points(0)
		, source(static_cast<std::uint8_t>(src))
#if !defined TORRENT_DISABLE_ENCRYPTION
		// assume no support in order to
		// prefer opening non-encrypted
		// connections. If it fails, we'll
		// retry with encryption
		, pe_support(false)
#endif
		, is_v6_addr(false)
#if TORRENT_USE_I2P
		, is_i2p_addr(false)
#endif
		, on_parole(false)
		, banned(false)
		, supports_utp(true) // assume peers support utp
		, confirmed_supports_utp(false)
		, supports_holepunch(false)
		, web_seed(false)
	{}

	std::uint32_t torrent_peer::rank(external_ip const& external, int external_port) const
	{
		TORRENT_ASSERT(in_use);
//TODO: how do we deal with our external address changing?
		if (peer_rank == 0)
			peer_rank = peer_priority(
				tcp::endpoint(external.external_address(this->address()), std::uint16_t(external_port))
				, tcp::endpoint(this->address(), this->port));
		return peer_rank;
	}

#ifndef TORRENT_DISABLE_LOGGING
	std::string torrent_peer::to_string() const
	{
		TORRENT_ASSERT(in_use);
#if TORRENT_USE_I2P
		if (is_i2p_addr) return dest().to_string();
#endif // TORRENT_USE_I2P
		error_code ec;
		return address().to_string(ec);
	}
#endif

	std::int64_t torrent_peer::total_download() const
	{
		TORRENT_ASSERT(in_use);
		if (connection != nullptr)
		{
			TORRENT_ASSERT(prev_amount_download == 0);
			return connection->statistics().total_payload_download();
		}
		else
		{
			return std::int64_t(prev_amount_download) << 10;
		}
	}

	std::int64_t torrent_peer::total_upload() const
	{
		TORRENT_ASSERT(in_use);
		if (connection != nullptr)
		{
			TORRENT_ASSERT(prev_amount_upload == 0);
			return connection->statistics().total_payload_upload();
		}
		else
		{
			return std::int64_t(prev_amount_upload) << 10;
		}
	}

	ipv4_peer::ipv4_peer(tcp::endpoint const& ep, bool c
		, peer_source_flags_t const src)
		: torrent_peer(ep.port(), c, src)
		, addr(ep.address().to_v4())
	{
		is_v6_addr = false;
#if TORRENT_USE_I2P
		is_i2p_addr = false;
#endif
	}

	ipv4_peer::ipv4_peer(ipv4_peer const&) = default;
	ipv4_peer& ipv4_peer::operator=(ipv4_peer const& p) = default;

#if TORRENT_USE_I2P
	i2p_peer::i2p_peer(string_view dest, bool connectable_
		, peer_source_flags_t const src)
		: torrent_peer(0, connectable_, src)
		, destination(dest)
	{
		is_v6_addr = false;
		is_i2p_addr = true;
	}
#endif // TORRENT_USE_I2P

	ipv6_peer::ipv6_peer(tcp::endpoint const& ep, bool c
		, peer_source_flags_t const src)
		: torrent_peer(ep.port(), c, src)
		, addr(ep.address().to_v6().to_bytes())
	{
		is_v6_addr = true;
#if TORRENT_USE_I2P
		is_i2p_addr = false;
#endif
	}

	ipv6_peer::ipv6_peer(ipv6_peer const&) = default;

#if TORRENT_USE_I2P
	string_view torrent_peer::dest() const
	{
		if (is_i2p_addr)
			return *static_cast<i2p_peer const*>(this)->destination;
		return "";
	}
#endif

	libtorrent::address torrent_peer::address() const
	{
		if (is_v6_addr)
			return libtorrent::address_v6(
				static_cast<ipv6_peer const*>(this)->addr);
		else
#if TORRENT_USE_I2P
		if (is_i2p_addr) return libtorrent::address();
		else
#endif
		return static_cast<ipv4_peer const*>(this)->addr;
	}

}
