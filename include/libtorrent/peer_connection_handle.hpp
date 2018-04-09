/*

Copyright (c) 2015-2018, Arvid Norberg, Steven Siloti
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

#ifndef TORRENT_PEER_CONNECTION_HANDLE_HPP_INCLUDED
#define TORRENT_PEER_CONNECTION_HANDLE_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/operations.hpp"
#include "libtorrent/alert_types.hpp"

namespace libtorrent
{

class peer_connection;
class bt_peer_connection;
struct torrent_handle;
struct peer_plugin;
struct peer_info;
struct crypto_plugin;

typedef boost::system::error_code error_code;

// hidden
struct TORRENT_EXPORT peer_connection_handle
{
	peer_connection_handle(boost::weak_ptr<peer_connection> impl)
		: m_connection(impl)
	{}

	int type() const;

	void add_extension(boost::shared_ptr<peer_plugin>);
	peer_plugin const* find_plugin(char const* type);

	bool is_seed() const;

	bool upload_only() const;

	peer_id const& pid() const;
	bool has_piece(int i) const;

	bool is_interesting() const;
	bool is_choked() const;

	bool is_peer_interested() const;
	bool has_peer_choked() const;

	void choke_this_peer();
	void maybe_unchoke_this_peer();

	void get_peer_info(peer_info& p) const;

	torrent_handle associated_torrent() const;

	tcp::endpoint const& remote() const;
	tcp::endpoint local_endpoint() const;

	void disconnect(error_code const& ec, operation_t op, int error = 0);
	bool is_disconnecting() const;
	bool is_connecting() const;
	bool is_outgoing() const;

	bool on_local_network() const;
	bool ignore_unchoke_slots() const;

	bool failed() const;

#ifndef TORRENT_DISABLE_LOGGING

	void peer_log(peer_log_alert::direction_t direction
		, char const* event, char const* fmt = "", ...) const TORRENT_FORMAT(4,5);

#endif // TORRENT_DISABLE_LOGGING

	bool can_disconnect(error_code const& ec) const;

	bool has_metadata() const;

	bool in_handshake() const;

	void send_buffer(char const* begin, int size, int flags = 0);

	time_t last_seen_complete() const;
	time_point time_of_last_unchoke() const;

	bool operator==(peer_connection_handle const& o) const
	{ return !(m_connection < o.m_connection) && !(o.m_connection < m_connection); }
	bool operator!=(peer_connection_handle const& o) const
	{ return m_connection < o.m_connection || o.m_connection < m_connection; }
	bool operator<(peer_connection_handle const& o) const
	{ return m_connection < o.m_connection; }

	boost::shared_ptr<peer_connection> native_handle() const
	{
		return m_connection.lock();
	}

private:
	boost::weak_ptr<peer_connection> m_connection;
};

struct TORRENT_EXPORT bt_peer_connection_handle : public peer_connection_handle
{
	explicit bt_peer_connection_handle(peer_connection_handle pc)
		: peer_connection_handle(pc)
	{}

	bool packet_finished() const;
	bool support_extensions() const;

	bool supports_encryption() const;

	void switch_send_crypto(boost::shared_ptr<crypto_plugin> crypto);
	void switch_recv_crypto(boost::shared_ptr<crypto_plugin> crypto);

	boost::shared_ptr<bt_peer_connection> native_handle() const;
};

} // namespace libtorrent

#endif // TORRENT_PEER_CONNECTION_HANDLE_HPP_INCLUDED
