/*

Copyright (c) 2012-2016, Arvid Norberg
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

#include <boost/detail/endian.hpp> // for BIG_ENDIAN and LITTLE_ENDIAN macros

namespace libtorrent
{
	namespace {

		void apply_mask(boost::uint8_t* b, boost::uint8_t const* mask, int size)
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
	boost::uint32_t peer_priority(tcp::endpoint e1, tcp::endpoint e2)
	{
		TORRENT_ASSERT(e1.address().is_v4() == e2.address().is_v4());

		using std::swap;

		boost::uint32_t ret;
		if (e1.address() == e2.address())
		{
			if (e1.port() > e2.port())
				swap(e1, e2);
			boost::uint32_t p;
#if defined BOOST_BIG_ENDIAN
			p = e1.port() << 16;
			p |= e2.port();
#elif defined BOOST_LITTLE_ENDIAN
			p = aux::host_to_network(e2.port()) << 16;
			p |= aux::host_to_network(e1.port());
#else
#error unsupported endianness
#endif
			ret = crc32c_32(p);
		}
#if TORRENT_USE_IPV6
		else if (e1.address().is_v6())
		{
			static const boost::uint8_t v6mask[][8] = {
				{ 0xff, 0xff, 0xff, 0xff, 0x55, 0x55, 0x55, 0x55 },
				{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x55, 0x55 },
				{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
			};

			if (e1 > e2) swap(e1, e2);
			address_v6::bytes_type b1 = e1.address().to_v6().to_bytes();
			address_v6::bytes_type b2 = e2.address().to_v6().to_bytes();
			int mask = memcmp(&b1[0], &b2[0], 4) ? 0
				: memcmp(&b1[0], &b2[0], 6) ? 1 : 2;
			apply_mask(&b1[0], v6mask[mask], 8);
			apply_mask(&b2[0], v6mask[mask], 8);
			boost::uint64_t addrbuf[4];
			memcpy(&addrbuf[0], &b1[0], 16);
			memcpy(&addrbuf[2], &b2[0], 16);
			ret = crc32c(addrbuf, 4);
		}
#endif
		else
		{
			static const boost::uint8_t v4mask[][4] = {
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
			boost::uint64_t addrbuf;
			memcpy(&addrbuf, &b1[0], 4);
			memcpy(reinterpret_cast<char*>(&addrbuf) + 4, &b2[0], 4);
			ret = crc32c(&addrbuf, 1);
		}

		return ret;
	}

	torrent_peer::torrent_peer(boost::uint16_t port_, bool conn, int src)
		: prev_amount_upload(0)
		, prev_amount_download(0)
		, connection(0)
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
		, source(src)
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		// assume no support in order to
		// prefer opening non-encrypted
		// connections. If it fails, we'll
		// retry with encryption
		, pe_support(false)
#endif
#if TORRENT_USE_IPV6
		, is_v6_addr(false)
#endif
#if TORRENT_USE_I2P
		, is_i2p_addr(false)
#endif
		, on_parole(false)
		, banned(false)
		, supports_utp(true) // assume peers support utp
		, confirmed_supports_utp(false)
		, supports_holepunch(false)
		, web_seed(false)
#if TORRENT_USE_ASSERTS
		, in_use(false)
#endif
	{
		TORRENT_ASSERT((src & 0xff) == src);
	}

	boost::uint32_t torrent_peer::rank(external_ip const& external, int external_port) const
	{
//TODO: how do we deal with our external address changing?
		if (peer_rank == 0)
			peer_rank = peer_priority(
				tcp::endpoint(external.external_address(this->address()), external_port)
				, tcp::endpoint(this->address(), this->port));
		return peer_rank;
	}

#ifndef TORRENT_DISABLE_LOGGING
	std::string torrent_peer::to_string() const
	{
#if TORRENT_USE_I2P
		if (is_i2p_addr) return dest();
#endif // TORRENT_USE_I2P
		error_code ec;
		return address().to_string(ec);
	}
#endif

	boost::uint64_t torrent_peer::total_download() const
	{
		if (connection != 0)
		{
			TORRENT_ASSERT(prev_amount_download == 0);
			return connection->statistics().total_payload_download();
		}
		else
		{
			return boost::uint64_t(prev_amount_download) << 10;
		}
	}

	boost::uint64_t torrent_peer::total_upload() const
	{
		if (connection != 0)
		{
			TORRENT_ASSERT(prev_amount_upload == 0);
			return connection->statistics().total_payload_upload();
		}
		else
		{
			return boost::uint64_t(prev_amount_upload) << 10;
		}
	}

	ipv4_peer::ipv4_peer(
		tcp::endpoint const& ep, bool c, int src
	)
		: torrent_peer(ep.port(), c, src)
		, addr(ep.address().to_v4())
	{
#if TORRENT_USE_IPV6
		is_v6_addr = false;
#endif
#if TORRENT_USE_I2P
		is_i2p_addr = false;
#endif
	}

	ipv4_peer::ipv4_peer(ipv4_peer const& p)
		: torrent_peer(p), addr(p.addr) {}

#if TORRENT_USE_I2P
	i2p_peer::i2p_peer(char const* dest, bool connectable, int src)
		: torrent_peer(0, connectable, src), destination(allocate_string_copy(dest))
	{
#if TORRENT_USE_IPV6
		is_v6_addr = false;
#endif
		is_i2p_addr = true;
	}

	i2p_peer::~i2p_peer()
	{ free(destination); }
#endif // TORRENT_USE_I2P

#if TORRENT_USE_IPV6
	ipv6_peer::ipv6_peer(
		tcp::endpoint const& ep, bool c, int src
	)
		: torrent_peer(ep.port(), c, src)
		, addr(ep.address().to_v6().to_bytes())
	{
		is_v6_addr = true;
#if TORRENT_USE_I2P
		is_i2p_addr = false;
#endif
	}

#endif // TORRENT_USE_IPV6

#if TORRENT_USE_I2P
	char const* torrent_peer::dest() const
	{
		if (is_i2p_addr)
			return static_cast<i2p_peer const*>(this)->destination;
		return "";
	}
#endif

	libtorrent::address torrent_peer::address() const
	{
#if TORRENT_USE_IPV6
		if (is_v6_addr)
			return libtorrent::address_v6(
				static_cast<ipv6_peer const*>(this)->addr);
		else
#endif
#if TORRENT_USE_I2P
		if (is_i2p_addr) return libtorrent::address();
		else
#endif
		return static_cast<ipv4_peer const*>(this)->addr;
	}

}

