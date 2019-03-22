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

#include "libtorrent/peer_connection_handle.hpp"
#include "libtorrent/bt_peer_connection.hpp"

#ifndef TORRENT_DISABLE_LOGGING
#include <cstdarg> // for va_start, va_end
#endif

namespace libtorrent {

connection_type peer_connection_handle::type() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->type();
}

void peer_connection_handle::add_extension(std::shared_ptr<peer_plugin> ext)
{
#ifndef TORRENT_DISABLE_EXTENSIONS
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->add_extension(std::move(ext));
#else
	TORRENT_UNUSED(ext);
#endif
}

peer_plugin const* peer_connection_handle::find_plugin(string_view type) const
{
#ifndef TORRENT_DISABLE_EXTENSIONS
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->find_plugin(type);
#else
	TORRENT_UNUSED(type);
	return nullptr;
#endif
}

bool peer_connection_handle::is_seed() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->is_seed();
}

bool peer_connection_handle::upload_only() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->upload_only();
}

peer_id const& peer_connection_handle::pid() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->pid();
}

bool peer_connection_handle::has_piece(piece_index_t i) const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->has_piece(i);
}

bool peer_connection_handle::is_interesting() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->is_interesting();
}

bool peer_connection_handle::is_choked() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->is_choked();
}

bool peer_connection_handle::is_peer_interested() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->is_peer_interested();
}

bool peer_connection_handle::has_peer_choked() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->has_peer_choked();
}

void peer_connection_handle::choke_this_peer()
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->choke_this_peer();
}

void peer_connection_handle::maybe_unchoke_this_peer()
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->maybe_unchoke_this_peer();
}

void peer_connection_handle::get_peer_info(peer_info& p) const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->get_peer_info(p);
}

torrent_handle peer_connection_handle::associated_torrent() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	if (!pc) return torrent_handle();
	std::shared_ptr<torrent> t = pc->associated_torrent().lock();
	if (!t) return torrent_handle();
	return t->get_handle();
}

tcp::endpoint const& peer_connection_handle::remote() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->remote();
}

tcp::endpoint peer_connection_handle::local_endpoint() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->local_endpoint();
}

void peer_connection_handle::disconnect(error_code const& ec, operation_t const op
	, disconnect_severity_t const error)
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->disconnect(ec, op, error);
}

bool peer_connection_handle::is_disconnecting() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->is_disconnecting();
}

bool peer_connection_handle::is_connecting() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->is_connecting();
}

bool peer_connection_handle::is_outgoing() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->is_outgoing();
}

bool peer_connection_handle::on_local_network() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->on_local_network();
}

bool peer_connection_handle::ignore_unchoke_slots() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->ignore_unchoke_slots();
}

bool peer_connection_handle::failed() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->failed();
}

bool peer_connection_handle::should_log(peer_log_alert::direction_t direction) const
{
#ifndef TORRENT_DISABLE_LOGGING
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->should_log(direction);
#else
	TORRENT_UNUSED(direction);
	return false;
#endif
}

TORRENT_FORMAT(4,5)
void peer_connection_handle::peer_log(peer_log_alert::direction_t direction
	, char const* event, char const* fmt, ...) const
{
#ifndef TORRENT_DISABLE_LOGGING
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	va_list v;
	va_start(v, fmt);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wclass-varargs"
#endif
	pc->peer_log(direction, event, fmt, v);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	va_end(v);
#else // TORRENT_DISABLE_LOGGING
	TORRENT_UNUSED(direction);
	TORRENT_UNUSED(event);
	TORRENT_UNUSED(fmt);
#endif
}

bool peer_connection_handle::can_disconnect(error_code const& ec) const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->can_disconnect(ec);
}

bool peer_connection_handle::has_metadata() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->has_metadata();
}

bool peer_connection_handle::in_handshake() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->in_handshake();
}

void peer_connection_handle::send_buffer(char const* begin, int size)
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->send_buffer({begin, size});
}

std::time_t peer_connection_handle::last_seen_complete() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->last_seen_complete();
}

time_point peer_connection_handle::time_of_last_unchoke() const
{
	std::shared_ptr<peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->time_of_last_unchoke();
}

bool bt_peer_connection_handle::packet_finished() const
{
	std::shared_ptr<bt_peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->packet_finished();
}

bool bt_peer_connection_handle::support_extensions() const
{
	std::shared_ptr<bt_peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->support_extensions();
}

bool bt_peer_connection_handle::supports_encryption() const
{
#if !defined TORRENT_DISABLE_ENCRYPTION
	std::shared_ptr<bt_peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	return pc->supports_encryption();
#else
	return false;
#endif
}

void bt_peer_connection_handle::switch_send_crypto(std::shared_ptr<crypto_plugin> crypto)
{
#if !defined TORRENT_DISABLE_ENCRYPTION
	std::shared_ptr<bt_peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->switch_send_crypto(std::move(crypto));
#else
	TORRENT_UNUSED(crypto);
#endif
}

void bt_peer_connection_handle::switch_recv_crypto(std::shared_ptr<crypto_plugin> crypto)
{
#if !defined TORRENT_DISABLE_ENCRYPTION
	std::shared_ptr<bt_peer_connection> pc = native_handle();
	TORRENT_ASSERT(pc);
	pc->switch_recv_crypto(std::move(crypto));
#else
	TORRENT_UNUSED(crypto);
#endif
}

std::shared_ptr<bt_peer_connection> bt_peer_connection_handle::native_handle() const
{
	return std::static_pointer_cast<bt_peer_connection>(
		peer_connection_handle::native_handle());
}

} // namespace libtorrent
