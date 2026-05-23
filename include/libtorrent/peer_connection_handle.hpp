/*

Copyright (c) 2015, 2017, Steven Siloti
Copyright (c) 2016-2018, 2020-2021, Alden Torres
Copyright (c) 2016-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_CONNECTION_HANDLE_HPP_INCLUDED
#define TORRENT_PEER_CONNECTION_HANDLE_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/operations.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/peer_connection.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent {

namespace aux { struct bt_peer_connection; }

// the peer_connection_handle class provides a handle to the internal peer
// connection object, to be used by plugins. This is a low level interface that
// may not be stable across libtorrent versions
struct TORRENT_EXPORT peer_connection_handle
{
	// construct a handle wrapping the given internal peer_connection.
	// Typically created by libtorrent and passed to plugins; not
	// intended to be constructed by user code.
	explicit peer_connection_handle(std::weak_ptr<aux::peer_connection> impl)
		: m_connection(std::move(impl))
	{}

	// the protocol type of this connection (bittorrent, url_seed,
	// http_seed)
	connection_type type() const;

	// attach a peer_plugin to this connection. ``find_plugin()`` looks
	// up an attached plugin by its ``type()`` string and returns nullptr
	// if none is registered.
	void add_extension(std::shared_ptr<peer_plugin>);
	peer_plugin const* find_plugin(string_view type) const;

	// returns true if the remote peer is a seed
	bool is_seed() const;

	// returns true if the remote peer has indicated upload-only (it
	// is a seed or has chosen not to download anything more from us)
	bool upload_only() const;

	// the peer id reported by the remote during handshake.
	// ``has_piece(i)`` returns true if the remote peer has piece ``i``.
	peer_id const& pid() const;
	bool has_piece(piece_index_t i) const;

	// is_interesting() returns true if we are interested in the peer
	// (it has pieces we want). is_choked() returns true if we have
	// choked the peer (we are not currently sending data to it).
	bool is_interesting() const;
	bool is_choked() const;

	// is_peer_interested() returns true if the peer has indicated it
	// is interested in us. has_peer_choked() returns true if the peer
	// is choking us.
	bool is_peer_interested() const;
	bool has_peer_choked() const;

	// choke or possibly unchoke this peer immediately, bypassing the
	// regular unchoke selection. ``maybe_unchoke_this_peer()`` only
	// unchokes if there is an unchoke slot available.
	void choke_this_peer();
	void maybe_unchoke_this_peer();

	// fills in ``p`` with statistics and state for this connection,
	// equivalent to the corresponding entry returned by
	// torrent_handle::get_peer_info().
	void get_peer_info(peer_info& p) const;

	// the torrent this peer is associated with. Returns an invalid
	// handle if the peer is not yet associated with a torrent.
	torrent_handle associated_torrent() const;

	// the remote peer's address and port, and the local endpoint of
	// the socket on our side.
	tcp::endpoint const& remote() const;
	tcp::endpoint local_endpoint() const;

	// drop the connection with the given error code and operation
	// type. The disconnect is asynchronous; ``is_disconnecting()``
	// becomes true immediately and the actual socket close happens
	// later. ``is_connecting()`` reports whether the outgoing socket
	// is still in the connect phase; ``is_outgoing()`` whether the
	// connection was initiated by us.
	void disconnect(error_code const& ec, operation_t op
		, disconnect_severity_t = peer_connection_interface::normal);
	bool is_disconnecting() const;
	bool is_connecting() const;
	bool is_outgoing() const;

	// returns true if the peer's address is on the same local network
	// as us. ``ignore_unchoke_slots()`` returns true if this peer is
	// excluded from regular unchoke slot accounting.
	bool on_local_network() const;
	bool ignore_unchoke_slots() const;

	// returns true if the connection has been closed because of an
	// error (rather than because of a normal disconnect)
	bool failed() const;

	// should_log() returns true if peer logging is enabled in the
	// given direction. peer_log() emits a ``peer_log_alert`` for this
	// connection, with printf-style formatting.
	bool should_log(peer_log_alert::direction_t direction) const;
	void peer_log(peer_log_alert::direction_t direction
		, peer_log_alert::event_t event, char const* fmt = "", ...) const TORRENT_FORMAT(4,5);

	// returns true if a disconnect with error ``ec`` would be
	// permitted at this point (some errors are suppressed during
	// startup).
	bool can_disconnect(error_code const& ec) const;

	// returns true if we have received the metadata (info dict) for
	// this torrent from the peer or otherwise
	bool has_metadata() const;

	// returns true if the connection is still in the BitTorrent
	// handshake phase
	bool in_handshake() const;

	// queue ``size`` bytes of data to be sent to the peer. The data
	// is copied; the buffer does not need to outlive the call.
	void send_buffer(char const* begin, int size);

	// the most recent ``time_of_completion`` reported by the peer
	// (BEP 21), and the wall-clock time when we last unchoked it.
	std::time_t last_seen_complete() const;
	time_point time_of_last_unchoke() const;

	bool operator==(peer_connection_handle const& o) const
	{ return !lt(m_connection, o.m_connection) && !lt(o.m_connection, m_connection); }
	bool operator!=(peer_connection_handle const& o) const
	{ return lt(m_connection, o.m_connection) || lt(o.m_connection, m_connection); }
	bool operator<(peer_connection_handle const& o) const
	{ return lt(m_connection, o.m_connection); }

	// returns a shared_ptr to the underlying peer_connection. The
	// pointer may be null if the connection has already been
	// destroyed.
	std::shared_ptr<aux::peer_connection> native_handle() const
	{
		return m_connection.lock();
	}

private:
	std::weak_ptr<aux::peer_connection> m_connection;

	// copied from boost::weak_ptr
	bool lt(std::weak_ptr<aux::peer_connection> const& a
		, std::weak_ptr<aux::peer_connection> const& b) const
	{
		return a.owner_before(b);
	}
};

// The bt_peer_connection_handle provides a handle to the internal bittorrent
// peer connection object to plugins. It's low level and may not be a stable API
// across libtorrent versions.
struct TORRENT_EXPORT bt_peer_connection_handle : peer_connection_handle
{
	// construct from a generic ``peer_connection_handle``. Behavior
	// is undefined if the underlying connection is not actually a
	// bittorrent peer connection.
	explicit bt_peer_connection_handle(peer_connection_handle pc)
		: peer_connection_handle(std::move(pc))
	{}

	// packet_finished() returns true if the receive buffer holds a
	// complete BitTorrent message. support_extensions() returns true
	// if the remote peer announced support for the LTEP extension
	// protocol (BEP 10).
	bool packet_finished() const;
	bool support_extensions() const;

	// returns true if MSE (Message Stream Encryption) is in use on
	// this connection
	bool supports_encryption() const;

	// install a custom ``crypto_plugin`` on the send or receive path.
	// Used to inject MSE / PE-style stream cipher into the peer
	// connection.
	void switch_send_crypto(std::shared_ptr<crypto_plugin> crypto);
	void switch_recv_crypto(std::shared_ptr<crypto_plugin> crypto);

	// returns a shared_ptr to the underlying bt_peer_connection. May
	// be null if the connection has been destroyed.
	std::shared_ptr<aux::bt_peer_connection> native_handle() const;
};

} // namespace libtorrent

#endif // TORRENT_PEER_CONNECTION_HANDLE_HPP_INCLUDED
