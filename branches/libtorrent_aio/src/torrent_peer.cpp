
#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/peer_connection.hpp"

namespace libtorrent
{
	torrent_peer::torrent_peer(boost::uint16_t port, bool conn, int src)
		: prev_amount_upload(0)
		, prev_amount_download(0)
		, connection(0)
#ifndef TORRENT_DISABLE_GEO_IP
		, inet_as(0)
#endif
		, last_optimistically_unchoked(0)
		, last_connected(0)
		, port(port)
		, hashfails(0)
		, failcount(0)
		, connectable(conn)
		, optimistically_unchoked(false)
		, seed(false)
		, fast_reconnects(0)
		, trust_points(0)
		, source(src)
#ifndef TORRENT_DISABLE_ENCRYPTION
		, pe_support(true)
#endif
#if TORRENT_USE_IPV6
		, is_v6_addr(false)
#endif
#if TORRENT_USE_I2P
		, is_i2p_addr(false)
#endif
		, on_parole(false)
		, banned(false)
#ifndef TORRENT_DISABLE_DHT
		, added_to_dht(false)
#endif
		, supports_utp(true) // assume peers support utp
		, confirmed_supports_utp(false)
		, supports_holepunch(false)
		, web_seed(false)
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		, in_use(false)
#endif
	{
		TORRENT_ASSERT((src & 0xff) == src);
	}

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

