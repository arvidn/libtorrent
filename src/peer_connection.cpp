/*

Copyright (c) 2016, tnextday
Copyright (c) 2003-2020, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2018, 2020, Alden Torres
Copyright (c) 2016-2018, Steven Siloti
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2017-2018, Pavel Pimenov
Copyright (c) 2020, Viktor Elofsson
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

#include <vector>
#include <functional>
#include <cstdint>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/logic/tribool.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/peer_list.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/bandwidth_manager.hpp"
#include "libtorrent/request_blocks.hpp" // for request_a_block
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/aux_/alert_manager.hpp" // for alert_manager
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/close_reason.hpp"
#include "libtorrent/aux_/has_block.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/aux_/buffer.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/set_socket_buffer.hpp"

#if TORRENT_USE_ASSERTS
#include <set>
#endif

#ifndef TORRENT_DISABLE_LOGGING
#include <cstdarg> // for va_start, va_end
#include <cstdio> // for vsnprintf
#include "libtorrent/socket_io.hpp"
#include "libtorrent/hex.hpp" // to_hex
#endif

#include "libtorrent/aux_/torrent_impl.hpp"

//#define TORRENT_CORRUPT_DATA

using namespace std::placeholders;

namespace libtorrent {

	constexpr request_flags_t peer_connection::time_critical;
	constexpr request_flags_t peer_connection::busy;

	namespace {

	// the limits of the download queue size
	constexpr int min_request_queue = 2;

	bool pending_block_in_buffer(pending_block const& pb)
	{
		return pb.send_buffer_offset != pending_block::not_in_buffer;
	}

	}

	constexpr piece_index_t piece_block_progress::invalid_index;

	constexpr disconnect_severity_t peer_connection_interface::normal;
	constexpr disconnect_severity_t peer_connection_interface::failure;
	constexpr disconnect_severity_t peer_connection_interface::peer_error;

#if TORRENT_USE_ASSERTS
	bool peer_connection::is_single_thread() const
	{
#ifdef TORRENT_USE_INVARIANT_CHECKS
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return true;
		return t->is_single_thread();
#else
		return true;
#endif
	}
#endif

	peer_connection::peer_connection(peer_connection_args& pack)
		: peer_connection_hot_members(pack.tor, *pack.ses, *pack.sett)
		, m_socket(std::move(pack.s))
		, m_peer_info(pack.peerinfo)
		, m_counters(*pack.stats_counters)
		, m_num_pieces(0)
		, m_max_out_request_queue(aux::clamp_assign<std::uint16_t>(m_settings.get_int(settings_pack::max_out_request_queue)))
		, m_remote(pack.endp)
		, m_disk_thread(*pack.disk_thread)
		, m_ios(*pack.ios)
		, m_work(make_work_guard(m_ios))
		, m_outstanding_piece_verification(0)
		, m_outgoing(!pack.tor.expired())
		, m_received_listen_port(false)
		, m_fast_reconnect(false)
		, m_failed(false)
		, m_connected(pack.tor.expired())
		, m_request_large_blocks(false)
#ifndef TORRENT_DISABLE_SHARE_MODE
		, m_share_mode(false)
#endif
		, m_upload_only(false)
		, m_bitfield_received(false)
		, m_no_download(false)
		, m_holepunch_mode(false)
		, m_peer_choked(true)
		, m_have_all(false)
		, m_peer_interested(false)
		, m_need_interest_update(false)
		, m_has_metadata(true)
		, m_exceeded_limit(false)
		, m_slow_start(true)
	{
		m_counters.inc_stats_counter(counters::num_tcp_peers
			+ static_cast<std::uint8_t>(socket_type_idx(m_socket)));
		std::shared_ptr<torrent> t = m_torrent.lock();

		// the protocol_v2 flag should not be set for non-v2 torrents
		TORRENT_ASSERT(!t || t->info_hash().has_v2() || !m_peer_info->protocol_v2);

		if (m_connected)
			m_counters.inc_stats_counter(counters::num_peers_connected);
		else if (m_connecting)
			m_counters.inc_stats_counter(counters::num_peers_half_open);

		// if t is nullptr, we better not be connecting, since
		// we can't decrement the connecting counter
		TORRENT_ASSERT(t || !m_connecting);

		m_channel_state[upload_channel] = peer_info::bw_idle;
		m_channel_state[download_channel] = peer_info::bw_idle;

		m_quota[0] = 0;
		m_quota[1] = 0;

		TORRENT_ASSERT(pack.peerinfo == nullptr || pack.peerinfo->banned == false);
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(m_outgoing ? peer_log_alert::outgoing : peer_log_alert::incoming))
		{
			error_code ec;
			TORRENT_ASSERT(m_socket.remote_endpoint(ec) == m_remote || ec);
			tcp::endpoint local_ep = m_socket.local_endpoint(ec);

			peer_log(m_outgoing ? peer_log_alert::outgoing : peer_log_alert::incoming
				, m_outgoing ? "OUTGOING_CONNECTION" : "INCOMING_CONNECTION"
				, "ep: %s type: %s seed: %d p: %p local: %s"
				, print_endpoint(m_remote).c_str()
				, socket_type_name(m_socket)
				, m_peer_info ? m_peer_info->seed : 0
				, static_cast<void*>(m_peer_info)
				, print_endpoint(local_ep).c_str());
		}
#endif

		// this counter should not be incremented until we know constructing this
		// peer object can't fail anymore
		if (m_connecting && t) t->inc_num_connecting(m_peer_info);

#if TORRENT_USE_ASSERTS
		piece_failed = false;
		m_in_constructor = false;
#endif
	}

	template <typename Fun, typename... Args>
	void peer_connection::wrap(Fun f, Args&&... a)
#ifndef BOOST_NO_EXCEPTIONS
		try
#endif
	{
		(this->*f)(std::forward<Args>(a)...);
	}
#ifndef BOOST_NO_EXCEPTIONS
	catch (std::bad_alloc const&) {
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "EXCEPTION", "bad_alloc");
#endif
		disconnect(make_error_code(boost::system::errc::not_enough_memory)
			, operation_t::unknown);
	}
	catch (system_error const& e) {
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "EXCEPTION", "(%d %s) %s"
			, e.code().value()
			, e.code().message().c_str()
			, e.what());
#endif
		disconnect(e.code(), operation_t::unknown);
	}
	catch (std::exception const& e) {
		TORRENT_UNUSED(e);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "EXCEPTION", "%s", e.what());
#endif
		disconnect(make_error_code(boost::system::errc::not_enough_memory)
			, operation_t::sock_write);
	}
#endif // BOOST_NO_EXCEPTIONS

	int peer_connection::timeout() const
	{
		TORRENT_ASSERT(is_single_thread());
		int ret = m_settings.get_int(settings_pack::peer_timeout);
#if TORRENT_USE_I2P
		if (m_peer_info && m_peer_info->is_i2p_addr)
		{
			// quadruple the timeout for i2p peers
			ret *= 4;
		}
#endif
		return ret;
	}

	void peer_connection::on_exception(std::exception const& e)
	{
		TORRENT_UNUSED(e);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "PEER_ERROR", "ERROR: %s"
			, e.what());
#endif
		disconnect(error_code(), operation_t::unknown, peer_error);
	}

	void peer_connection::on_error(error_code const& ec)
	{
		disconnect(ec, operation_t::unknown, peer_error);
	}

	int peer_connection::get_priority(int const channel) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(channel >= 0 && channel < 2);
		int prio = 1;
		for (int i = 0; i < num_classes(); ++i)
		{
			int class_prio = m_ses.peer_classes().at(class_at(i))->priority[channel];
			if (prio < class_prio) prio = class_prio;
		}

		std::shared_ptr<torrent> t = associated_torrent().lock();

		if (t)
		{
			for (int i = 0; i < t->num_classes(); ++i)
			{
				int class_prio = m_ses.peer_classes().at(t->class_at(i))->priority[channel];
				if (prio < class_prio) prio = class_prio;
			}
		}
		return prio;
	}

	void peer_connection::reset_choke_counters()
	{
		TORRENT_ASSERT(is_single_thread());
		m_downloaded_at_last_round= m_statistics.total_payload_download();
		m_uploaded_at_last_round = m_statistics.total_payload_upload();
	}

	void peer_connection::start()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_peer_info == nullptr || m_peer_info->connection == this);
		std::shared_ptr<torrent> t = m_torrent.lock();

		if (!m_outgoing)
		{
			error_code ec;
			m_socket.non_blocking(true, ec);
			if (ec)
			{
				disconnect(ec, operation_t::iocontrol);
				return;
			}
			m_remote = m_socket.remote_endpoint(ec);
			if (ec)
			{
				disconnect(ec, operation_t::getpeername);
				return;
			}
			m_local = m_socket.local_endpoint(ec);
			if (ec)
			{
				disconnect(ec, operation_t::getname);
				return;
			}
			if (aux::is_v4(m_remote) && m_settings.get_int(settings_pack::peer_tos) != 0)
			{
				m_socket.set_option(type_of_service(char(m_settings.get_int(settings_pack::peer_tos))), ec);
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log(peer_log_alert::outgoing))
				{
					peer_log(peer_log_alert::outgoing, "SET_TOS", "tos: %d e: %s"
						, m_settings.get_int(settings_pack::peer_tos), ec.message().c_str());
				}
#endif
			}
#if defined IPV6_TCLASS
			else if (aux::is_v6(m_remote) && m_settings.get_int(settings_pack::peer_tos) != 0)
			{
				m_socket.set_option(traffic_class(char(m_settings.get_int(settings_pack::peer_tos))), ec);
			}
#endif
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "SET_PEER_CLASS", "a: %s"
				, print_address(m_remote.address()).c_str());
		}
#endif

		m_ses.set_peer_classes(this, m_remote.address(), socket_type_idx(m_socket));

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			std::string classes;
			for (int i = 0; i < num_classes(); ++i)
			{
				classes += m_ses.peer_classes().at(class_at(i))->label;
				classes += ' ';
			}
			peer_log(peer_log_alert::info, "CLASS", "%s"
				, classes.c_str());
		}
#endif

		if (t && t->ready_for_connections())
		{
			init();
		}

		// if this is an incoming connection, we're done here
		if (!m_connecting)
		{
			error_code err;
			aux::set_socket_buffer_size(m_socket, m_settings, err);
#ifndef TORRENT_DISABLE_LOGGING
			if (err && should_log(peer_log_alert::incoming))
			{
				peer_log(peer_log_alert::incoming, "SOCKET_BUFFER", "%s %s"
					, print_endpoint(m_remote).c_str()
					, print_error(err).c_str());
			}
#endif

			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing))
		{
			peer_log(peer_log_alert::outgoing, "OPEN", "protocol: %s"
				, (aux::is_v4(m_remote) ? "IPv4" : "IPv6"));
		}
#endif
		error_code ec;
		m_socket.open(m_remote.protocol(), ec);
		if (ec)
		{
			disconnect(ec, operation_t::sock_open);
			return;
		}

		tcp::endpoint const bound_ip = m_ses.bind_outgoing_socket(m_socket
			, m_remote.address(), ec);
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing))
		{
			peer_log(peer_log_alert::outgoing, "BIND", "dst: %s ec: %s"
				, print_endpoint(bound_ip).c_str()
				, ec.message().c_str());
		}
#else
		TORRENT_UNUSED(bound_ip);
#endif
		if (ec)
		{
			disconnect(ec, operation_t::sock_bind);
			return;
		}

		{
			error_code err;
			aux::set_socket_buffer_size(m_socket, m_settings, err);
#ifndef TORRENT_DISABLE_LOGGING
			if (err && should_log(peer_log_alert::outgoing))
			{
				peer_log(peer_log_alert::outgoing, "SOCKET_BUFFER", "%s %s"
					, print_endpoint(m_remote).c_str()
					, print_error(err).c_str());
			}
#endif
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing))
		{
			peer_log(peer_log_alert::outgoing, "ASYNC_CONNECT", "dst: %s"
				, print_endpoint(m_remote).c_str());
		}
#endif
		ADD_OUTSTANDING_ASYNC("peer_connection::on_connection_complete");

		auto conn = self();
		m_socket.async_connect(m_remote
			, [conn](error_code const& e) { conn->wrap(&peer_connection::on_connection_complete, e); });
		m_connect = aux::time_now();

		sent_syn(aux::is_v6(m_remote));

		if (t && t->alerts().should_post<peer_connect_alert>())
		{
			t->alerts().emplace_alert<peer_connect_alert>(
				t->get_handle(), remote(), pid(), socket_type_idx(m_socket), peer_connect_alert::direction_t::out);
		}
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "LOCAL ENDPOINT", "e: %s"
				, print_endpoint(m_socket.local_endpoint(ec)).c_str());
		}
#endif
	}

	void peer_connection::update_interest()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_need_interest_update)
		{
			// we're the first to request an interest update
			// post a message in order to delay it enough for
			// any potential other messages already in the queue
			// to not trigger another one. This effectively defer
			// the update until the current message queue is
			// flushed
			auto conn = self();
			post(m_ios, [conn] { conn->wrap(&peer_connection::do_update_interest); });
		}
		m_need_interest_update = true;
	}

	void peer_connection::do_update_interest()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_need_interest_update);
		m_need_interest_update = false;

		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;

		// if m_have_piece is 0, it means the connections
		// have not been initialized yet. The interested
		// flag will be updated once they are.
		if (m_have_piece.empty())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UPDATE_INTEREST", "connections not initialized");
#endif
			return;
		}
		if (!t->ready_for_connections())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UPDATE_INTEREST", "not ready for connections");
#endif
			return;
		}

		bool interested = false;
		if (!t->is_upload_only())
		{
			t->need_picker();
			piece_picker const& p = t->picker();
			piece_index_t const end_piece(p.num_pieces());
			for (piece_index_t j(0); j != end_piece; ++j)
			{
				if (m_have_piece[j]
					&& t->piece_priority(j) > dont_download
					&& !p.has_piece_passed(j))
				{
					interested = true;
#ifndef TORRENT_DISABLE_LOGGING
					peer_log(peer_log_alert::info, "UPDATE_INTEREST", "interesting, piece: %d"
						, static_cast<int>(j));
#endif
					break;
				}
			}
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (!interested)
			peer_log(peer_log_alert::info, "UPDATE_INTEREST", "not interesting");
#endif

		if (!interested) send_not_interested();
		else t->peer_is_interesting(*this);

		TORRENT_ASSERT(in_handshake() || is_interesting() == interested);

		disconnect_if_redundant();
	}

#ifndef TORRENT_DISABLE_LOGGING
	bool peer_connection::should_log(peer_log_alert::direction_t) const
	{
		return m_ses.alerts().should_post<peer_log_alert>();
	}

	void peer_connection::peer_log(peer_log_alert::direction_t direction
		, char const* event) const noexcept
	{
		peer_log(direction, event, "");
	}

	TORRENT_FORMAT(4,5)
	void peer_connection::peer_log(peer_log_alert::direction_t direction
		, char const* event, char const* fmt, ...) const noexcept try
	{
		TORRENT_ASSERT(is_single_thread());

		if (!m_ses.alerts().should_post<peer_log_alert>()) return;

		va_list v;
		va_start(v, fmt);

		torrent_handle h;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (t) h = t->get_handle();

		m_ses.alerts().emplace_alert<peer_log_alert>(
			h, m_remote, m_peer_id, direction, event, fmt, v);

		va_end(v);

	}
	catch (std::exception const&) {}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
	void peer_connection::add_extension(std::shared_ptr<peer_plugin> ext)
	{
		TORRENT_ASSERT(is_single_thread());
		m_extensions.push_back(ext);
	}

	peer_plugin const* peer_connection::find_plugin(string_view type)
	{
		TORRENT_ASSERT(is_single_thread());
		auto p = std::find_if(m_extensions.begin(), m_extensions.end()
			, [&](std::shared_ptr<peer_plugin> const& e) { return e->type() == type; });
		return p != m_extensions.end() ? p->get() : nullptr;
	}
#endif

	void peer_connection::send_allowed_set()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (!t->valid_metadata())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ALLOWED", "skipping allowed set because we don't have metadata");
#endif
			return;
		}

#ifndef TORRENT_DISABLE_SUPERSEEDING
		if (t->super_seeding())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ALLOWED", "skipping allowed set because of super seeding");
#endif
			return;
		}
#endif

		if (upload_only())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ALLOWED", "skipping allowed set because peer is upload only");
#endif
			return;
		}

		int const num_allowed_pieces = m_settings.get_int(settings_pack::allowed_fast_set_size);
		if (num_allowed_pieces <= 0) return;

		if (!t->valid_metadata()) return;

		int const num_pieces = t->torrent_file().num_pieces();

		if (num_allowed_pieces >= num_pieces)
		{
			// this is a special case where we have more allowed
			// fast pieces than pieces in the torrent. Just send
			// an allowed fast message for every single piece
			for (auto const i : t->torrent_file().piece_range())
			{
				// there's no point in offering fast pieces
				// that the peer already has
				if (has_piece(i)) continue;

				write_allow_fast(i);
				TORRENT_ASSERT(std::find(m_accept_fast.begin()
					, m_accept_fast.end(), i)
					== m_accept_fast.end());
				if (m_accept_fast.empty())
				{
					m_accept_fast.reserve(10);
					m_accept_fast_piece_cnt.reserve(10);
				}
				m_accept_fast.push_back(i);
				m_accept_fast_piece_cnt.push_back(0);
			}
			return;
		}

		std::string x;
		address const& addr = m_remote.address();
		if (addr.is_v4())
		{
			address_v4::bytes_type bytes = addr.to_v4().to_bytes();
			x.assign(reinterpret_cast<char*>(bytes.data()), bytes.size());
		}
		else
		{
			address_v6::bytes_type bytes = addr.to_v6().to_bytes();
			x.assign(reinterpret_cast<char*>(bytes.data()), bytes.size());
		}
		x.append(associated_info_hash().data(), 20);

		sha1_hash hash = hasher(x).final();
		int attempts = 0;
		int loops = 0;
		for (;;)
		{
			char const* p = hash.data();
			for (int i = 0; i < int(hash.size() / sizeof(std::uint32_t)); ++i)
			{
				++loops;
				TORRENT_ASSERT(num_pieces > 0);
				piece_index_t const piece(int(aux::read_uint32(p) % std::uint32_t(num_pieces)));
				if (std::find(m_accept_fast.begin(), m_accept_fast.end(), piece)
					!= m_accept_fast.end())
				{
					// this is our safety-net to make sure this loop terminates, even
					// under the worst conditions
					if (++loops > 500) return;
					continue;
				}

				if (!has_piece(piece))
				{
					write_allow_fast(piece);
					if (m_accept_fast.empty())
					{
						m_accept_fast.reserve(10);
						m_accept_fast_piece_cnt.reserve(10);
					}
					m_accept_fast.push_back(piece);
					m_accept_fast_piece_cnt.push_back(0);
				}
				if (++attempts >= num_allowed_pieces) return;
			}
			hash = hasher(hash).final();
		}
	}

	void peer_connection::on_metadata_impl()
	{
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<torrent> t = associated_torrent().lock();
		m_have_piece.resize(t->torrent_file().num_pieces(), m_have_all);
		m_num_pieces = m_have_piece.count();

		piece_index_t const limit(m_num_pieces);

		// now that we know how many pieces there are
		// remove any invalid allowed_fast and suggest pieces
		// now that we know what the number of pieces are
		m_allowed_fast.erase(std::remove_if(m_allowed_fast.begin(), m_allowed_fast.end()
			, [=](piece_index_t const p) { return p >= limit; })
			, m_allowed_fast.end());

		// remove any piece suggested to us whose index is invalid
		// now that we know how many pieces there are
		m_suggested_pieces.erase(
			std::remove_if(m_suggested_pieces.begin(), m_suggested_pieces.end()
				, [=](piece_index_t const p) { return p >= limit; })
			, m_suggested_pieces.end());

		on_metadata();
		if (m_disconnecting) return;
	}

	void peer_connection::init()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->valid_metadata());
		TORRENT_ASSERT(t->ready_for_connections());

		m_have_piece.resize(t->torrent_file().num_pieces(), m_have_all);

		if (m_have_all)
		{
			m_num_pieces = t->torrent_file().num_pieces();
			m_have_piece.set_all();
		}
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(!m_initialized);
		m_initialized = true;
#endif
		// now that we have a piece_picker,
		// update it with this peer's pieces

		TORRENT_ASSERT(m_num_pieces == m_have_piece.count());

		if (m_num_pieces == m_have_piece.size())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INIT", "this is a seed p: %p"
				, static_cast<void*>(m_peer_info));
#endif

			TORRENT_ASSERT(m_have_piece.all_set());
			TORRENT_ASSERT(m_have_piece.count() == m_have_piece.size());
			TORRENT_ASSERT(m_have_piece.size() == t->torrent_file().num_pieces());

			// if this is a web seed. we don't have a peer_info struct
			t->set_seed(m_peer_info, true);
			m_upload_only = true;

			t->peer_has_all(this);

#if TORRENT_USE_INVARIANT_CHECKS
			if (t && t->has_picker())
				t->picker().check_peer_invariant(m_have_piece, peer_info_struct());
#endif
			if (t->is_upload_only()) send_not_interested();
			else t->peer_is_interesting(*this);
			disconnect_if_redundant();
			return;
		}

		// if we're a seed, we don't keep track of piece availability
		if (t->has_picker())
		{
			TORRENT_ASSERT(m_have_piece.size() == t->torrent_file().num_pieces());
			t->peer_has(m_have_piece, this);
			bool interesting = false;
			for (auto const i : m_have_piece.range())
			{
				if (!m_have_piece[i]) continue;
				// if the peer has a piece and we don't, the peer is interesting
				if (!t->have_piece(i)
					&& t->picker().piece_priority(i) != dont_download)
					interesting = true;
			}
			if (interesting) t->peer_is_interesting(*this);
			else send_not_interested();
		}
		else
		{
			update_interest();
		}
	}

	peer_connection::~peer_connection()
	{
		m_counters.inc_stats_counter(counters::num_tcp_peers
			+ static_cast<std::uint8_t>(socket_type_idx(m_socket)), -1);

//		INVARIANT_CHECK;
		TORRENT_ASSERT(!m_in_constructor);
		TORRENT_ASSERT(!m_destructed);
#if TORRENT_USE_ASSERTS
		m_destructed = true;
#endif

#if TORRENT_USE_ASSERTS
		m_in_use = 0;
#endif

		// decrement the stats counter
		set_endgame(false);

		if (m_interesting)
			m_counters.inc_stats_counter(counters::num_peers_down_interested, -1);
		if (m_peer_interested)
			m_counters.inc_stats_counter(counters::num_peers_up_interested, -1);
		if (!m_choked)
		{
			m_counters.inc_stats_counter(counters::num_peers_up_unchoked_all, -1);
			if (!ignore_unchoke_slots())
				m_counters.inc_stats_counter(counters::num_peers_up_unchoked, -1);
		}
		if (!m_peer_choked)
			m_counters.inc_stats_counter(counters::num_peers_down_unchoked, -1);
		if (m_connected)
			m_counters.inc_stats_counter(counters::num_peers_connected, -1);
		m_connected = false;
		if (!m_download_queue.empty())
			m_counters.inc_stats_counter(counters::num_peers_down_requests, -1);

		// defensive
		std::shared_ptr<torrent> t = m_torrent.lock();
		// if t is nullptr, we better not be connecting, since
		// we can't decrement the connecting counter
		TORRENT_ASSERT(t || !m_connecting);

		// we should really have dealt with this already
		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t) t->dec_num_connecting(m_peer_info);
			m_connecting = false;
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		m_extensions.clear();
#endif

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "CONNECTION CLOSED");
#endif
		TORRENT_ASSERT(m_request_queue.empty());
		TORRENT_ASSERT(m_download_queue.empty());
	}

	bool peer_connection::on_parole() const
	{ return peer_info_struct() && peer_info_struct()->on_parole; }

	picker_options_t peer_connection::picker_options() const
	{
		TORRENT_ASSERT(is_single_thread());
		picker_options_t ret = m_picker_options;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		if (!t) return {};

		if (t->num_time_critical_pieces() > 0)
		{
			ret |= piece_picker::time_critical_mode;
		}

		if (t->is_sequential_download())
		{
			ret |= piece_picker::sequential;
		}
		else if (t->num_have() < m_settings.get_int(settings_pack::initial_picker_threshold))
		{
			// if we have fewer pieces than a certain threshold
			// don't pick rare pieces, just pick random ones,
			// and prioritize finishing them
			ret |= piece_picker::prioritize_partials;
		}
		else
		{
			ret |= piece_picker::rarest_first;

			if (m_snubbed)
			{
				// snubbed peers should request
				// the common pieces first, just to make
				// it more likely for all snubbed peers to
				// request blocks from the same piece
				ret |= piece_picker::reverse;
			}
			else
			{
				if (m_settings.get_bool(settings_pack::piece_extent_affinity)
					&& t->num_time_critical_pieces() == 0)
					ret |= piece_picker::piece_extent_affinity;
			}
		}

		if (m_settings.get_bool(settings_pack::prioritize_partial_pieces))
			ret |= piece_picker::prioritize_partials;

		if (on_parole()) ret |= piece_picker::on_parole
			| piece_picker::prioritize_partials;

		// only one of rarest_first and sequential can be set. i.e. the sum of
		// whether the bit is set or not may only be 0 or 1 (never 2)
		TORRENT_ASSERT(((ret & piece_picker::rarest_first) ? 1 : 0)
			+ ((ret & piece_picker::sequential) ? 1 : 0) <= 1);
		return ret;
	}

	void peer_connection::fast_reconnect(bool r)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!peer_info_struct() || peer_info_struct()->fast_reconnects > 1)
			return;
		m_fast_reconnect = r;
		peer_info_struct()->last_connected = std::uint16_t(m_ses.session_time());
		int const rewind = m_settings.get_int(settings_pack::min_reconnect_time)
			* m_settings.get_int(settings_pack::max_failcount);
		if (int(peer_info_struct()->last_connected) < rewind) peer_info_struct()->last_connected = 0;
		else peer_info_struct()->last_connected -= std::uint16_t(rewind);

		if (peer_info_struct()->fast_reconnects < 15)
			++peer_info_struct()->fast_reconnects;
	}

	void peer_connection::received_piece(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		// dont announce during handshake
		if (in_handshake()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming, "RECEIVED", "piece: %d"
			, static_cast<int>(index));
#endif

		// remove suggested pieces once we have them
		auto i = std::find(m_suggested_pieces.begin(), m_suggested_pieces.end(), index);
		if (i != m_suggested_pieces.end()) m_suggested_pieces.erase(i);

		// remove allowed fast pieces
		i = std::find(m_allowed_fast.begin(), m_allowed_fast.end(), index);
		if (i != m_allowed_fast.end()) m_allowed_fast.erase(i);

		if (has_piece(index))
		{
			// if we got a piece that this peer has
			// it might have been the last interesting
			// piece this peer had. We might not be
			// interested anymore
			update_interest();
			if (is_disconnecting()) return;
		}

		if (disconnect_if_redundant()) return;

#if TORRENT_USE_ASSERTS
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
#endif
	}

	void peer_connection::announce_piece(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		// dont announce during handshake
		if (in_handshake()) return;

		// optimization, don't send have messages
		// to peers that already have the piece
		if (!m_settings.get_bool(settings_pack::send_redundant_have)
			&& has_piece(index))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing_message, "HAVE", "piece: %d SUPPRESSED"
				, static_cast<int>(index));
#endif
			return;
		}

		if (disconnect_if_redundant()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "HAVE", "piece: %d"
			, static_cast<int>(index));
#endif
		write_have(index);
#if TORRENT_USE_ASSERTS
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
#endif
	}

	bool peer_connection::has_piece(piece_index_t const i) const
	{
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->valid_metadata());
		TORRENT_ASSERT(i >= piece_index_t(0));
		TORRENT_ASSERT(i < t->torrent_file().end_piece());
		return m_have_piece[i];
	}

	std::vector<pending_block> const& peer_connection::request_queue() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_request_queue;
	}

	std::vector<pending_block> const& peer_connection::download_queue() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_download_queue;
	}

	std::vector<peer_request> const& peer_connection::upload_queue() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_requests;
	}

	time_duration peer_connection::download_queue_time(int const extra_bytes) const
	{
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		int rate = 0;

		// if we haven't received any data recently, the current download rate
		// is not representative
		if (aux::time_now() - m_last_piece.get(m_connect) > seconds(30) && m_download_rate_peak > 0)
		{
			rate = m_download_rate_peak;
		}
		else if (aux::time_now() - m_last_unchoked.get(m_connect) < seconds(5)
			&& m_statistics.total_payload_upload() < 2 * 0x4000)
		{
			// if we're have only been unchoked for a short period of time,
			// we don't know what rate we can get from this peer. Instead of assuming
			// the lowest possible rate, assume the average.

			int peers_with_requests = int(stats_counters()[counters::num_peers_down_requests]);
			// avoid division by 0
			if (peers_with_requests == 0) peers_with_requests = 1;

			// TODO: this should be the global download rate
			rate = t->statistics().transfer_rate(stat::download_payload) / peers_with_requests;
		}
		else
		{
			// current download rate in bytes per seconds
			rate = m_statistics.transfer_rate(stat::download_payload);
		}

		// avoid division by zero
		if (rate < 50) rate = 50;

		// average of current rate and peak
//		rate = (rate + m_download_rate_peak) / 2;

		return milliseconds((m_outstanding_bytes + extra_bytes
			+ m_queued_time_critical * t->block_size() * 1000) / rate);
	}

	void peer_connection::add_stat(std::int64_t const downloaded, std::int64_t const uploaded)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.add_stat(downloaded, uploaded);
	}

	sha1_hash peer_connection::associated_info_hash() const
	{
		std::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		auto const& ih = t->info_hash();
		// if protocol_v2 is set on the peer, this better be a v2 torrent,
		// otherwise something isn't right
		TORRENT_ASSERT(ih.has_v2() || !peer_info_struct()->protocol_v2);
		return ih.get((ih.has_v2() && peer_info_struct()->protocol_v2)
			? protocol_version::V2 : protocol_version::V1);
	}

	void peer_connection::received_bytes(int const bytes_payload, int const bytes_protocol)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.received_bytes(bytes_payload, bytes_protocol);
		if (m_ignore_stats) return;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->received_bytes(bytes_payload, bytes_protocol);
	}

	void peer_connection::sent_bytes(int const bytes_payload, int const bytes_protocol)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.sent_bytes(bytes_payload, bytes_protocol);
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (bytes_payload)
		{
			for (auto const& e : m_extensions)
			{
				e->sent_payload(bytes_payload);
			}
		}
#endif
		if (bytes_payload > 0) m_last_sent_payload.set(m_connect, clock_type::now());
		if (m_ignore_stats) return;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->sent_bytes(bytes_payload, bytes_protocol);
	}

	void peer_connection::trancieve_ip_packet(int const bytes, bool const ipv6)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.trancieve_ip_packet(bytes, ipv6);
		if (m_ignore_stats) return;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->trancieve_ip_packet(bytes, ipv6);
	}

	void peer_connection::sent_syn(bool const ipv6)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.sent_syn(ipv6);
		if (m_ignore_stats) return;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->sent_syn(ipv6);
	}

	void peer_connection::received_synack(bool const ipv6)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.received_synack(ipv6);
		if (m_ignore_stats) return;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->received_synack(ipv6);
	}

	typed_bitfield<piece_index_t> const& peer_connection::get_bitfield() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_have_piece;
	}

	void peer_connection::received_valid_data(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		// this fails because we haven't had time to disconnect
		// seeds yet, and we might have just become one
//		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			e->on_piece_pass(index);
		}
#else
		TORRENT_UNUSED(index);
#endif
	}

	// single_peer is true if the entire piece was received by a single
	// peer
	bool peer_connection::received_invalid_data(piece_index_t const index, bool single_peer)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;
		TORRENT_UNUSED(single_peer);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			e->on_piece_failed(index);
		}
#else
		TORRENT_UNUSED(index);
#endif
		return true;
	}

	// verifies a piece to see if it is valid (is within a valid range)
	// and if it can correspond to a request generated by libtorrent.
	bool peer_connection::verify_piece(peer_request const& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(t->valid_metadata());
		torrent_info const& ti = t->torrent_file();

		return p.piece >= piece_index_t(0)
			&& p.piece < ti.end_piece()
			&& p.start >= 0
			&& p.start < ti.piece_length()
			&& t->to_req(piece_block(p.piece, p.start / t->block_size())) == p;
	}

	void peer_connection::attach_to_torrent(info_hash_t const& ih)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "ATTACH", "attached to torrent");
#endif

		TORRENT_ASSERT(!m_disconnecting);
		TORRENT_ASSERT(m_torrent.expired());
		std::weak_ptr<torrent> wpt = m_ses.find_torrent(ih);
		std::shared_ptr<torrent> t = wpt.lock();

		if (t && t->is_aborted())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ATTACH", "the torrent has been aborted");
#endif
			t.reset();
		}

		if (!t)
		{
			t = m_ses.delay_load_torrent(ih, this);
#ifndef TORRENT_DISABLE_LOGGING
			if (t && should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "ATTACH"
					, "Delay loaded torrent: %s:"
					, aux::to_hex(t->info_hash().get_best()).c_str());
			}
#endif
		}

		if (!t)
		{
			// we couldn't find the torrent!
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "ATTACH"
					, "couldn't find a torrent with the given info_hash: %s torrents:"
					, aux::to_hex(ih.get_best()).c_str());
			}
#endif

#ifndef TORRENT_DISABLE_DHT
			ih.for_each([&](sha1_hash const& e, protocol_version)
			{
				if (dht::verify_secret_id(e))
				{
					// this means the hash was generated from our generate_secret_id()
					// as part of DHT traffic. The fact that we got an incoming
					// connection on this info-hash, means the other end, making this
					// connection fished it out of the DHT chatter. That's suspicious.
					m_ses.ban_ip(m_remote.address());
				}
			});
#endif
			disconnect(errors::invalid_info_hash, operation_t::bittorrent, failure);
			return;
		}

		// if this peer supports v2, this better be a v2 torrent
		TORRENT_ASSERT(t->info_hash().has_v2() || !(peer_info_struct() && peer_info_struct()->protocol_v2));

		if (t->is_paused()
			&& t->is_auto_managed()
			&& m_settings.get_bool(settings_pack::incoming_starts_queued_torrents)
			&& !t->is_aborted())
		{
			t->resume();
		}

		if (t->is_paused() || t->is_aborted() || t->graceful_pause())
		{
			// paused torrents will not accept
			// incoming connections unless they are auto managed
			// and incoming_starts_queued_torrents is true
			// torrents that have errors should always reject
			// incoming peers
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ATTACH", "rejected connection to paused torrent");
#endif
			disconnect(errors::torrent_paused, operation_t::bittorrent, peer_error);
			return;
		}

#if TORRENT_USE_I2P
		auto* i2ps = boost::get<i2p_stream>(&m_socket);
		if (!i2ps && t->torrent_file().is_i2p()
			&& !m_settings.get_bool(settings_pack::allow_i2p_mixed))
		{
			// the torrent is an i2p torrent, the peer is a regular peer
			// and we don't allow mixed mode. Disconnect the peer.
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ATTACH", "rejected regular connection to i2p torrent");
#endif
			disconnect(errors::peer_banned, operation_t::bittorrent, peer_error);
			return;
		}
#endif // TORRENT_USE_I2P

		TORRENT_ASSERT(m_torrent.expired());

		// check to make sure we don't have another connection with the same
		// info_hash and peer_id. If we do. close this connection.
		t->attach_peer(this);
		if (m_disconnecting) return;
		// it's important to assign the torrent after successfully attaching.
		// if the peer disconnects while attaching, it's not a proper member
		// of the torrent and peer_connection::disconnect() will fail if it
		// think it is
		m_torrent = t;

		if (t && t->alerts().should_post<peer_connect_alert>())
		{
			t->alerts().emplace_alert<peer_connect_alert>(
				t->get_handle(), remote(), pid(), socket_type_idx(m_socket), peer_connect_alert::direction_t::in);
		}

		if (t->info_hash().has_v2() && (t->info_hash().get(protocol_version::V2) == ih.v1
			|| t->info_hash().v2 == ih.v2))
		{
			peer_info_struct()->protocol_v2 = true;
			TORRENT_ASSERT(t->info_hash().has_v2());
		}

		if (m_exceeded_limit)
		{
			// find a peer in some torrent (presumably the one with most peers)
			// and disconnect the lowest ranking peer
			std::weak_ptr<torrent> torr = m_ses.find_disconnect_candidate_torrent();
			std::shared_ptr<torrent> other_t = torr.lock();

			if (other_t)
			{
				if (other_t->num_peers() <= t->num_peers())
				{
					disconnect(errors::too_many_connections, operation_t::bittorrent);
					return;
				}
				// find the lowest ranking peer and disconnect that
				peer_connection* p = other_t->find_lowest_ranking_peer();
				if (p != nullptr)
				{
					p->disconnect(errors::too_many_connections, operation_t::bittorrent);
					peer_disconnected_other();
				}
				else
				{
					disconnect(errors::too_many_connections, operation_t::bittorrent);
					return;
				}
			}
			else
			{
				disconnect(errors::too_many_connections, operation_t::bittorrent);
				return;
			}
		}

		TORRENT_ASSERT(!m_torrent.expired());

		// if the torrent isn't ready to accept
		// connections yet, we'll have to wait with
		// our initialization
		if (t->ready_for_connections()) init();

		TORRENT_ASSERT(!m_torrent.expired());

		// assume the other end has no pieces
		// if we don't have valid metadata yet,
		// leave the vector unallocated
		TORRENT_ASSERT(m_num_pieces == 0);
		m_have_piece.clear_all();
		TORRENT_ASSERT(!m_torrent.expired());
	}

	std::uint32_t peer_connection::peer_rank() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_peer_info == nullptr ? 0
			: m_peer_info->rank(m_ses.external_address(), m_ses.listen_port());
	}

	// message handlers

	// -----------------------------
	// --------- KEEPALIVE ---------
	// -----------------------------

	void peer_connection::incoming_keepalive()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "KEEPALIVE");
#endif
	}

	// -----------------------------
	// ----------- CHOKE -----------
	// -----------------------------

	void peer_connection::set_endgame(bool b)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_endgame_mode == b) return;
		m_endgame_mode = b;
		if (m_endgame_mode)
			m_counters.inc_stats_counter(counters::num_peers_end_game);
		else
			m_counters.inc_stats_counter(counters::num_peers_end_game, -1);
	}

	void peer_connection::incoming_choke()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_choke()) return;
		}
#endif
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "CHOKE");
#endif
		if (m_peer_choked == false)
			m_counters.inc_stats_counter(counters::num_peers_down_unchoked, -1);

		m_peer_choked = true;
		set_endgame(false);

		clear_request_queue();
	}

	void peer_connection::clear_request_queue()
	{
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		if (!t->has_picker())
		{
			m_request_queue.clear();
			return;
		}

		// clear the requests that haven't been sent yet
		if (peer_info_struct() == nullptr || !peer_info_struct()->on_parole)
		{
			// if the peer is not in parole mode, clear the queued
			// up block requests
			piece_picker& p = t->picker();
			for (auto const& r : m_request_queue)
			{
				p.abort_download(r.block, peer_info_struct());
			}
			m_request_queue.clear();
			m_queued_time_critical = 0;
		}
	}

	void peer_connection::clear_download_queue()
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		piece_picker& picker = t->picker();
		torrent_peer* self_peer = peer_info_struct();
		while (!m_download_queue.empty())
		{
			pending_block& qe = m_download_queue.back();
			if (!qe.timed_out && !qe.not_wanted)
				picker.abort_download(qe.block, self_peer);
			m_outstanding_bytes -= t->to_req(qe.block).length;
			if (m_outstanding_bytes < 0) m_outstanding_bytes = 0;
			m_download_queue.pop_back();
		}
	}

	// -----------------------------
	// -------- REJECT PIECE -------
	// -----------------------------

	void peer_connection::incoming_reject_request(peer_request const& r)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "REJECT_PIECE", "piece: %d s: %x l: %x"
			, static_cast<int>(r.piece), r.start, r.length);
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_reject(r)) return;
		}
#endif

		if (is_disconnecting()) return;

		int const block_size = t->block_size();
		if (r.piece < piece_index_t{}
			|| r.piece >= t->torrent_file().files().end_piece()
			|| r.start < 0
			|| r.start >= t->torrent_file().piece_length()
			|| (r.start % block_size) != 0
			|| r.length != std::min(t->torrent_file().piece_size(r.piece) - r.start, block_size))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "REJECT_PIECE", "invalid reject message (%d, %d, %d)"
				, int(r.piece), int(r.start), int(r.length));
#endif
			return;
		}

		auto const dlq_iter = std::find_if(
			m_download_queue.begin(), m_download_queue.end()
			, [&r, block_size](pending_block const& pb)
			{
				auto const& b = pb.block;
				if (b.piece_index != r.piece) return false;
				if (b.block_index != r.start / block_size) return false;
				return true;
			});

		if (dlq_iter != m_download_queue.end())
		{
			pending_block const b = *dlq_iter;
			bool const remove_from_picker = !dlq_iter->timed_out && !dlq_iter->not_wanted;
			m_download_queue.erase(dlq_iter);
			TORRENT_ASSERT(m_outstanding_bytes >= r.length);
			m_outstanding_bytes -= r.length;
			if (m_outstanding_bytes < 0) m_outstanding_bytes = 0;

			if (m_download_queue.empty())
				m_counters.inc_stats_counter(counters::num_peers_down_requests, -1);

			// if the peer is in parole mode, keep the request
			if (peer_info_struct() && peer_info_struct()->on_parole)
			{
				// we should only add it if the block is marked as
				// busy in the piece-picker
				if (remove_from_picker)
					m_request_queue.insert(m_request_queue.begin(), b);
			}
			else if (!t->is_seed() && remove_from_picker)
			{
				piece_picker& p = t->picker();
				p.abort_download(b.block, peer_info_struct());
			}
#if TORRENT_USE_INVARIANT_CHECKS
			check_invariant();
#endif
		}
#ifndef TORRENT_DISABLE_LOGGING
		else
		{
			peer_log(peer_log_alert::info, "REJECT_PIECE", "piece not in request queue (%d, %d, %d)"
				, int(r.piece), int(r.start), int(r.length));
		}
#endif
		if (has_peer_choked())
		{
			// if we're choked and we got a rejection of
			// a piece in the allowed fast set, remove it
			// from the allow fast set.
			auto const i = std::find(m_allowed_fast.begin(), m_allowed_fast.end(), r.piece);
			if (i != m_allowed_fast.end()) m_allowed_fast.erase(i);
		}
		else
		{
			auto const i = std::find(m_suggested_pieces.begin(), m_suggested_pieces.end(), r.piece);
			if (i != m_suggested_pieces.end()) m_suggested_pieces.erase(i);
		}

		check_graceful_pause();
		if (is_disconnecting()) return;

		if (m_request_queue.empty() && m_download_queue.size() < 2)
		{
			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::reject_piece_picks);
		}

		send_block_requests();
	}

	// -----------------------------
	// ------- SUGGEST PIECE -------
	// -----------------------------

	void peer_connection::incoming_suggest(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "SUGGEST_PIECE"
			, "piece: %d", static_cast<int>(index));
#endif
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_suggest(index)) return;
		}
#endif

		if (is_disconnecting()) return;
		if (index < piece_index_t(0))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "INVALID_SUGGEST_PIECE"
				, "%d", static_cast<int>(index));
#endif
			return;
		}

		if (t->valid_metadata())
		{
			if (index >= m_have_piece.end_index())
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::incoming_message, "INVALID_SUGGEST"
					, "%d s: %d", static_cast<int>(index), m_have_piece.size());
#endif
				return;
			}

			// if we already have the piece, we can
			// ignore this message
			if (t->have_piece(index))
				return;
		}

		// the piece picker will prioritize the pieces from the beginning to end.
		// the later the suggestion is received, the higher priority we should
		// ascribe to it, so we need to insert suggestions at the front of the
		// queue.
		if (m_suggested_pieces.end_index() > m_settings.get_int(settings_pack::max_suggest_pieces))
			m_suggested_pieces.resize(m_settings.get_int(settings_pack::max_suggest_pieces) - 1);

		m_suggested_pieces.insert(m_suggested_pieces.begin(), index);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "SUGGEST_PIECE", "piece: %d added to set: %d"
			, static_cast<int>(index), m_suggested_pieces.end_index());
#endif
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void peer_connection::incoming_unchoke()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_unchoke()) return;
		}
#endif

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "UNCHOKE");
#endif
		if (m_peer_choked)
			m_counters.inc_stats_counter(counters::num_peers_down_unchoked);

		m_peer_choked = false;
		m_last_unchoked.set(m_connect, aux::time_now());
		if (is_disconnecting()) return;

		if (is_interesting())
		{
			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::unchoke_piece_picks);
			send_block_requests();
		}
	}

	// -----------------------------
	// -------- INTERESTED ---------
	// -----------------------------

	void peer_connection::incoming_interested()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_interested()) return;
		}
#endif

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "INTERESTED");
#endif
		if (m_peer_interested == false)
			m_counters.inc_stats_counter(counters::num_peers_up_interested);

		m_peer_interested = true;
		if (is_disconnecting()) return;

		// if the peer is ready to download stuff, it must have metadata
		m_has_metadata = true;

		disconnect_if_redundant();
		if (is_disconnecting()) return;

		if (t->graceful_pause())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UNCHOKE"
				, "did not unchoke, graceful pause mode");
#endif
			return;
		}

		if (!is_choked())
		{
			// the reason to send an extra unchoke message here is that
			// because of the handshake-round-trip optimization, we may
			// end up sending an unchoke before the other end sends us
			// an interested message. This may confuse clients, not reacting
			// to the first unchoke, and then not check whether it's unchoked
			// when sending the interested message. If the other end's client
			// has this problem, sending another unchoke here will kick it
			// to react to the fact that it's unchoked.
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UNCHOKE", "sending redundant unchoke");
#endif
			write_unchoke();
			return;
		}

		maybe_unchoke_this_peer();
	}

	void peer_connection::maybe_unchoke_this_peer()
	{
		TORRENT_ASSERT(is_single_thread());
		if (ignore_unchoke_slots())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UNCHOKE", "about to unchoke, peer ignores unchoke slots");
#endif
			// if this peer is exempted from the choker
			// just unchoke it immediately
			send_unchoke();
		}
		else if (m_ses.preemptive_unchoke())
		{
			// if the peer is choked and we have upload slots left,
			// then unchoke it.

			std::shared_ptr<torrent> t = m_torrent.lock();
			TORRENT_ASSERT(t);

			t->unchoke_peer(*this);
		}
#ifndef TORRENT_DISABLE_LOGGING
		else if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "UNCHOKE", "did not unchoke, the number of uploads (%d) "
				"is more than or equal to the available slots (%d), limit (%d)"
				, int(m_counters[counters::num_peers_up_unchoked])
				, int(m_counters[counters::num_unchoke_slots])
				, m_settings.get_int(settings_pack::unchoke_slots_limit));
		}
#endif
	}

	// -----------------------------
	// ------ NOT INTERESTED -------
	// -----------------------------

	void peer_connection::incoming_not_interested()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_not_interested()) return;
		}
#endif

		m_became_uninterested.set(m_connect, aux::time_now());

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "NOT_INTERESTED");
#endif
		if (m_peer_interested)
			m_counters.inc_stats_counter(counters::num_peers_up_interested, -1);

		m_peer_interested = false;
		if (is_disconnecting()) return;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		choke_this_peer();
	}

	void peer_connection::choke_this_peer()
	{
		TORRENT_ASSERT(is_single_thread());
		if (is_choked()) return;
		if (ignore_unchoke_slots())
		{
			send_choke();
			return;
		}

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (m_peer_info && m_peer_info->optimistically_unchoked)
		{
			m_peer_info->optimistically_unchoked = false;
			m_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic, -1);
			t->trigger_optimistic_unchoke();
		}
		t->choke_peer(*this);
		t->trigger_unchoke();
	}

	// -----------------------------
	// ----------- HAVE ------------
	// -----------------------------

	void peer_connection::incoming_have(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_have(index)) return;
		}
#endif

		if (is_disconnecting()) return;

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();

		// if this peer is choked, there's no point in sending suggest messages to
		// it. They would just be out-of-date by the time we unchoke the peer
		// anyway.
		if (m_settings.get_int(settings_pack::suggest_mode) == settings_pack::suggest_read_cache
			&& !is_choked()
			&& std::any_of(m_suggest_pieces.begin(), m_suggest_pieces.end()
				, [=](piece_index_t const idx) { return idx == index; }))
		{
			send_piece_suggestions(2);
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "HAVE", "piece: %d"
			, static_cast<int>(index));
#endif

		if (is_disconnecting()) return;

		if (!t->valid_metadata() && index >= m_have_piece.end_index())
		{
			if (index <= piece_index_t(m_settings.get_int(settings_pack::max_piece_count)))
			{
				// if we don't have metadata
				// and we might not have received a bitfield
				// extend the bitmask to fit the new
				// have message
				m_have_piece.resize(static_cast<int>(index) + 1, false);
			}
			else
			{
				// unless the index > 64k, in which case
				// we just ignore it
				return;
			}
		}

		// if we got an invalid message, abort
		if (index >= m_have_piece.end_index() || index < piece_index_t(0))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ERROR", "have-metadata have_piece: %d size: %d"
				, static_cast<int>(index), m_have_piece.size());
#endif
			disconnect(errors::invalid_have, operation_t::bittorrent, peer_error);
			return;
		}

#ifndef TORRENT_DISABLE_SUPERSEEDING
		if (t->super_seeding()
#if TORRENT_ABI_VERSION == 1
			&& !m_settings.get_bool(settings_pack::strict_super_seeding)
#endif
			)
		{
			// if we're super-seeding and the peer just told
			// us that it completed the piece we're super-seeding
			// to it, change the super-seeding piece for this peer
			// if the peer optimizes out redundant have messages
			// this will be handled when the peer sends not-interested
			// instead.
			if (super_seeded_piece(index))
			{
				superseed_piece(index, t->get_piece_to_super_seed(m_have_piece));
			}
		}
#endif

		if (m_have_piece[index])
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "HAVE"
				, "got redundant HAVE message for index: %d"
				, static_cast<int>(index));
#endif
			return;
		}

		m_have_piece.set_bit(index);
		++m_num_pieces;

		// if the peer is downloading stuff, it must have metadata
		m_has_metadata = true;

		// only update the piece_picker if
		// we have the metadata and if
		// we're not a seed (in which case
		// we won't have a piece picker)
		if (!t->valid_metadata()) return;

		t->peer_has(index, this);

		// it's important to not disconnect before we have
		// updated the piece picker, otherwise we will incorrectly
		// decrement the piece count without first incrementing it
		if (is_seed())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SEED", "this is a seed. p: %p"
				, static_cast<void*>(m_peer_info));
#endif

			TORRENT_ASSERT(m_have_piece.all_set());
			TORRENT_ASSERT(m_have_piece.count() == m_have_piece.size());
			TORRENT_ASSERT(m_have_piece.size() == t->torrent_file().num_pieces());

			t->seen_complete();
			t->set_seed(m_peer_info, true);
			m_upload_only = true;

#if TORRENT_USE_INVARIANT_CHECKS
			if (t && t->has_picker())
				t->picker().check_peer_invariant(m_have_piece, peer_info_struct());
#endif
			if (disconnect_if_redundant()) return;
		}

		// it's important to update whether we're interested in this peer before
		// calling disconnect_if_redundant, otherwise we may disconnect even if
		// we are interested
		if (!t->has_piece_passed(index)
			&& !t->is_upload_only()
			&& !is_interesting()
			&& (!t->has_picker() || t->picker().piece_priority(index) != dont_download))
			t->peer_is_interesting(*this);

		disconnect_if_redundant();
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_SUPERSEEDING
#if TORRENT_ABI_VERSION == 1
		// if we're super seeding, this might mean that somebody
		// forwarded this piece. In which case we need to give
		// a new piece to that peer
		if (t->super_seeding()
			&& m_settings.get_bool(settings_pack::strict_super_seeding)
			&& (!super_seeded_piece(index) || t->num_peers() == 1))
		{
			for (auto& p : *t)
			{
				if (!p->super_seeded_piece(index)) continue;
				if (!p->has_piece(index)) continue;
				p->superseed_piece(index, t->get_piece_to_super_seed(p->get_bitfield()));
			}
		}
#endif // TORRENT_ABI_VERSION
#endif // TORRENT_DISABLE_SUPERSEEDING
	}

	// -----------------------------
	// -------- DONT HAVE ----------
	// -----------------------------

	void peer_connection::incoming_dont_have(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (index < piece_index_t{}
			|| index >= t->torrent_file().end_piece())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "DONT_HAVE"
				, "invalid piece: %d", static_cast<int>(index));
#endif
			return;
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_dont_have(index)) return;
		}
#endif

		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "DONT_HAVE", "piece: %d"
			, static_cast<int>(index));
#endif

		// if we got an invalid message, abort
		if (index >= m_have_piece.end_index() || index < piece_index_t(0))
		{
			disconnect(errors::invalid_dont_have, operation_t::bittorrent, peer_error);
			return;
		}

		if (!m_have_piece[index])
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "DONT_HAVE"
				, "got redundant DONT_HAVE message for index: %d"
				, static_cast<int>(index));
#endif
			return;
		}

		bool was_seed = is_seed();
		m_have_piece.clear_bit(index);
		TORRENT_ASSERT(m_num_pieces > 0);
		--m_num_pieces;

		// only update the piece_picker if
		// we have the metadata and if
		// we're not a seed (in which case
		// we won't have a piece picker)
		if (!t->valid_metadata()) return;

		t->peer_lost(index, this);

		if (was_seed)
			t->set_seed(m_peer_info, false);
	}

	// -----------------------------
	// --------- BITFIELD ----------
	// -----------------------------

	void peer_connection::incoming_bitfield(typed_bitfield<piece_index_t> const& bits)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_bitfield(bits)) return;
		}
#endif

		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::incoming_message))
		{
			std::string bitfield_str;
			bitfield_str.resize(aux::numeric_cast<std::size_t>(bits.size()));
			for (auto const i : bits.range())
				bitfield_str[std::size_t(static_cast<int>(i))] = bits[i] ? '1' : '0';
			peer_log(peer_log_alert::incoming_message, "BITFIELD"
				, "%s", bitfield_str.c_str());
		}
#endif

		// if we don't have the metadata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& bits.size() != m_have_piece.size())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::incoming_message))
			{
				peer_log(peer_log_alert::incoming_message, "BITFIELD"
					, "invalid size: %d expected %d", bits.size()
					, m_have_piece.size());
			}
#endif
			disconnect(errors::invalid_bitfield_size, operation_t::bittorrent, peer_error);
			return;
		}

		if (m_bitfield_received)
		{
			// if we've already received a bitfield message
			// we first need to count down all the pieces
			// we believe the peer has first
			t->peer_lost(m_have_piece, this);
		}

		m_bitfield_received = true;

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!t->ready_for_connections())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_num_pieces == bits.size())
				peer_log(peer_log_alert::info, "SEED", "this is a seed. p: %p"
					, static_cast<void*>(m_peer_info));
#endif
			m_have_piece = bits;
			m_num_pieces = bits.count();
			t->set_seed(m_peer_info, m_num_pieces == bits.size());

#if TORRENT_USE_INVARIANT_CHECKS
			if (t && t->has_picker())
				t->picker().check_peer_invariant(m_have_piece, peer_info_struct());
#endif
			return;
		}

		TORRENT_ASSERT(t->valid_metadata());

		int num_pieces = bits.count();
		if (num_pieces == m_have_piece.size())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SEED", "this is a seed. p: %p"
				, static_cast<void*>(m_peer_info));
#endif

			t->set_seed(m_peer_info, true);
			m_upload_only = true;

			m_have_piece.set_all();
			m_num_pieces = num_pieces;
			t->peer_has_all(this);

			TORRENT_ASSERT(m_have_piece.all_set());
			TORRENT_ASSERT(m_have_piece.count() == m_have_piece.size());
			TORRENT_ASSERT(m_have_piece.size() == t->torrent_file().num_pieces());

#if TORRENT_USE_INVARIANT_CHECKS
			if (t && t->has_picker())
				t->picker().check_peer_invariant(m_have_piece, peer_info_struct());
#endif

			// this will cause us to send the INTERESTED message
			if (!t->is_upload_only())
				t->peer_is_interesting(*this);

			disconnect_if_redundant();

			return;
		}

		// let the torrent know which pieces the peer has if we're a seed, we
		// don't keep track of piece availability
		t->peer_has(bits, this);

		m_have_piece = bits;
		m_num_pieces = num_pieces;

		update_interest();
	}

	bool peer_connection::disconnect_if_redundant()
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_disconnecting) return false;
		if (m_need_interest_update) return false;

		// we cannot disconnect in a constructor
		TORRENT_ASSERT(m_in_constructor == false);
		if (!m_settings.get_bool(settings_pack::close_redundant_connections)) return false;

		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return false;

		// if we don't have the metadata yet, don't disconnect
		// also, if the peer doesn't have metadata we shouldn't
		// disconnect it, since it may want to request the
		// metadata from us
		if (!t->valid_metadata() || !has_metadata()) return false;

#ifndef TORRENT_DISABLE_SHARE_MODE
		// don't close connections in share mode, we don't know if we need them
		if (t->share_mode()) return false;
#endif

		if (m_upload_only && t->is_upload_only()
			&& can_disconnect(errors::upload_upload_connection))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UPLOAD_ONLY", "the peer is upload-only and our torrent is also upload-only");
#endif
			disconnect(errors::upload_upload_connection, operation_t::bittorrent);
			return true;
		}

		if (m_upload_only
			&& !m_interesting
			&& m_bitfield_received
			&& t->are_files_checked()
			&& can_disconnect(errors::uninteresting_upload_peer))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UPLOAD_ONLY", "the peer is upload-only and we're not interested in it");
#endif
			disconnect(errors::uninteresting_upload_peer, operation_t::bittorrent);
			return true;
		}

		return false;
	}

	bool peer_connection::can_disconnect(error_code const& ec) const
	{
		TORRENT_ASSERT(is_single_thread());
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (!e->can_disconnect(ec)) return false;
		}
#else
		TORRENT_UNUSED(ec);
#endif
		return true;
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void peer_connection::incoming_request(peer_request const& r)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		torrent_info const& ti = t->torrent_file();

		m_counters.inc_stats_counter(counters::piece_requests);

#ifndef TORRENT_DISABLE_LOGGING
		const bool valid_piece_index
			= r.piece >= piece_index_t(0)
			&& r.piece < t->torrent_file().end_piece();

		peer_log(peer_log_alert::incoming_message, "REQUEST"
			, "piece: %d s: %x l: %x", static_cast<int>(r.piece), r.start, r.length);
#endif

#ifndef TORRENT_DISABLE_SUPERSEEDING
		if (t->super_seeding()
			&& !super_seeded_piece(r.piece))
		{
			m_counters.inc_stats_counter(counters::invalid_piece_requests);
			if (m_num_invalid_requests < std::numeric_limits<decltype(m_num_invalid_requests)>::max())
				++m_num_invalid_requests;
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "INVALID_REQUEST", "piece not super-seeded "
					"i: %d t: %d n: %d h: %d ss1: %d ss2: %d"
					, m_peer_interested
					, valid_piece_index
						? t->torrent_file().piece_size(r.piece) : -1
					, t->torrent_file().num_pieces()
					, valid_piece_index ? t->has_piece_passed(r.piece) : 0
					, static_cast<int>(m_superseed_piece[0])
					, static_cast<int>(m_superseed_piece[1]));
			}
#endif

			write_reject_request(r);

			if (t->alerts().should_post<invalid_request_alert>())
			{
				// msvc 12 appears to deduce the rvalue reference template
				// incorrectly for bool temporaries. So, create a dummy instance
				bool const peer_interested = bool(m_peer_interested);
				t->alerts().emplace_alert<invalid_request_alert>(
					t->get_handle(), m_remote, m_peer_id, r
					, t->has_piece_passed(r.piece), peer_interested, true);
			}
			return;
		}
#endif // TORRENT_DISABLE_SUPERSEEDING

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_request(r)) return;
		}
		if (is_disconnecting()) return;
#endif

		if (!t->valid_metadata())
		{
			m_counters.inc_stats_counter(counters::invalid_piece_requests);
			// if we don't have valid metadata yet,
			// we shouldn't get a request
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST", "we don't have metadata yet");
#endif
			write_reject_request(r);
			return;
		}

		if (int(m_requests.size()) > m_settings.get_int(settings_pack::max_allowed_in_request_queue))
		{
			m_counters.inc_stats_counter(counters::max_piece_requests);
			// don't allow clients to abuse our
			// memory consumption.
			// ignore requests if the client
			// is making too many of them.
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST", "incoming request queue full %d"
				, int(m_requests.size()));
#endif
			write_reject_request(r);
			return;
		}

		int fast_idx = -1;
		auto const fast_iter = std::find(m_accept_fast.begin()
			, m_accept_fast.end(), r.piece);
		if (fast_iter != m_accept_fast.end()) fast_idx = int(fast_iter - m_accept_fast.begin());

		if (!m_peer_interested)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "INVALID_REQUEST", "peer is not interested "
					" t: %d n: %d block_limit: %d"
					, valid_piece_index
						? t->torrent_file().piece_size(r.piece) : -1
					, t->torrent_file().num_pieces()
					, t->block_size());
				peer_log(peer_log_alert::info, "INTERESTED", "artificial incoming INTERESTED message");
			}
#endif
			if (t->alerts().should_post<invalid_request_alert>())
			{
				t->alerts().emplace_alert<invalid_request_alert>(
					t->get_handle(), m_remote, m_peer_id, r
					, t->has_piece_passed(r.piece)
					, false, false);
			}

			// be lenient and pretend that the peer said it was interested
			incoming_interested();
		}

		// make sure this request
		// is legal and that the peer
		// is not choked
		if (r.piece < piece_index_t(0)
			|| r.piece >= t->torrent_file().end_piece()
			|| (!t->has_piece_passed(r.piece)
#ifndef TORRENT_DISABLE_PREDICTIVE_PIECES
				&& !t->is_predictive_piece(r.piece)
#endif
				&& !t->seed_mode())
			|| r.start < 0
			|| r.start >= ti.piece_size(r.piece)
			|| r.length <= 0
			|| r.length + r.start > ti.piece_size(r.piece)
			|| r.length > t->block_size())
		{
			m_counters.inc_stats_counter(counters::invalid_piece_requests);

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "INVALID_REQUEST"
					, "i: %d t: %d n: %d h: %d block_limit: %d"
					, m_peer_interested
					, valid_piece_index
						? t->torrent_file().piece_size(r.piece) : -1
					, ti.num_pieces()
					, t->has_piece_passed(r.piece)
					, t->block_size());
			}
#endif

			write_reject_request(r);
			if (m_num_invalid_requests < std::numeric_limits<decltype(m_num_invalid_requests)>::max())
				++m_num_invalid_requests;

			if (t->alerts().should_post<invalid_request_alert>())
			{
				// msvc 12 appears to deduce the rvalue reference template
				// incorrectly for bool temporaries. So, create a dummy instance
				bool const peer_interested = bool(m_peer_interested);
				t->alerts().emplace_alert<invalid_request_alert>(
					t->get_handle(), m_remote, m_peer_id, r
					, t->has_piece_passed(r.piece), peer_interested, false);
			}

			// every ten invalid request, remind the peer that it's choked
			if (!m_peer_interested && m_num_invalid_requests % 10 == 0 && m_choked)
			{
				// TODO: 2 this should probably be based on time instead of number
				// of request messages. For a very high throughput connection, 300
				// may be a legitimate number of requests to have in flight when
				// getting choked
				if (m_num_invalid_requests > 300 && !m_peer_choked
					&& can_disconnect(errors::too_many_requests_when_choked))
				{
					disconnect(errors::too_many_requests_when_choked, operation_t::bittorrent, peer_error);
					return;
				}
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::outgoing_message, "CHOKE");
#endif
				write_choke();
			}

			return;
		}

		// if we have choked the client
		// ignore the request
		int const blocks_per_piece =
			(ti.piece_length() + t->block_size() - 1) / t->block_size();

		// disconnect peers that downloads more than foo times an allowed
		// fast piece
		if (m_choked && fast_idx != -1 && m_accept_fast_piece_cnt[fast_idx] >= 3 * blocks_per_piece
			&& can_disconnect(errors::too_many_requests_when_choked))
		{
			disconnect(errors::too_many_requests_when_choked, operation_t::bittorrent, peer_error);
			return;
		}

		if (m_choked && fast_idx == -1)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "REJECTING REQUEST", "peer choked and piece not in allowed fast set");
#endif
			m_counters.inc_stats_counter(counters::choked_piece_requests);
			write_reject_request(r);

			// allow peers to send request up to 2 seconds after getting choked,
			// then disconnect them
			if (aux::time_now() - seconds(2) > m_last_choke.get(m_connect)
				&& can_disconnect(errors::too_many_requests_when_choked))
			{
				disconnect(errors::too_many_requests_when_choked, operation_t::bittorrent, peer_error);
				return;
			}
		}
		else
		{
			// increase the allowed fast set counter
			if (fast_idx != -1)
				++m_accept_fast_piece_cnt[fast_idx];

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests);

			TORRENT_ASSERT(t->valid_metadata());
			TORRENT_ASSERT(r.piece >= piece_index_t(0));
			TORRENT_ASSERT(r.piece < t->torrent_file().end_piece());
			TORRENT_ASSERT(r.length <= default_block_size);
			TORRENT_ASSERT(r.length > 0);

			m_requests.push_back(r);

			if (t->alerts().should_post<incoming_request_alert>())
			{
				t->alerts().emplace_alert<incoming_request_alert>(r, t->get_handle()
					, m_remote, m_peer_id);
			}

			m_last_incoming_request.set(m_connect, aux::time_now());
			fill_send_buffer();
		}
	}

	// reject all requests to this piece
	void peer_connection::reject_piece(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		for (auto i = m_requests.begin(), end(m_requests.end()); i != end; ++i)
		{
			peer_request const& r = *i;
			if (r.piece != index) continue;
			write_reject_request(r);
			i = m_requests.erase(i);

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests, -1);
		}
	}

	void peer_connection::incoming_piece_fragment(int const bytes)
	{
		TORRENT_ASSERT(is_single_thread());
		m_last_piece.set(m_connect, aux::time_now());
		TORRENT_ASSERT_VAL(m_outstanding_bytes >= bytes, m_outstanding_bytes - bytes);
		m_outstanding_bytes -= bytes;
		if (m_outstanding_bytes < 0) m_outstanding_bytes = 0;
		std::shared_ptr<torrent> t = associated_torrent().lock();
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_received_in_piece + bytes <= t->block_size());
		m_received_in_piece += bytes;
#endif

		// progress of this torrent increased
		t->state_updated();

#if TORRENT_USE_INVARIANT_CHECKS
		check_invariant();
#endif
	}

	void peer_connection::start_receive_piece(peer_request const& r)
	{
		TORRENT_ASSERT(is_single_thread());
#if TORRENT_USE_INVARIANT_CHECKS
		check_invariant();
#endif
#if TORRENT_USE_ASSERTS
		span<char const> recv_buffer = m_recv_buffer.get();
		int recv_pos = int(recv_buffer.end() - recv_buffer.begin());
		TORRENT_ASSERT(recv_pos >= 9);
#endif

		std::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		if (!verify_piece(r))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_PIECE", "piece: %d s: %d l: %d"
				, static_cast<int>(r.piece), r.start, r.length);
#endif
			disconnect(errors::invalid_piece, operation_t::bittorrent, peer_error);
			return;
		}

		piece_block const b(r.piece, r.start / t->block_size());
		m_receiving_block = b;

		bool in_req_queue = false;
		for (auto const& pb : m_download_queue)
		{
			if (pb.block != b) continue;
			in_req_queue = true;
			break;
		}

		// if this is not in the request queue, we have to
		// assume our outstanding bytes includes this piece too
		// if we're disconnecting, we shouldn't add pieces
		if (!in_req_queue && !m_disconnecting)
		{
			for (auto i = m_request_queue.begin()
				, end(m_request_queue.end()); i != end; ++i)
			{
				if (i->block != b) continue;
				in_req_queue = true;
				if (i - m_request_queue.begin() < m_queued_time_critical)
					--m_queued_time_critical;
				m_request_queue.erase(i);
				break;
			}

			if (m_download_queue.empty())
				m_counters.inc_stats_counter(counters::num_peers_down_requests);

			m_download_queue.insert(m_download_queue.begin(), b);
			if (!in_req_queue)
			{
				if (t->alerts().should_post<unwanted_block_alert>())
				{
					t->alerts().emplace_alert<unwanted_block_alert>(t->get_handle()
						, m_remote, m_peer_id, b.block_index, b.piece_index);
				}
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "INVALID_REQUEST"
					, "The block we just got was not in the request queue");
#endif
				TORRENT_ASSERT(m_download_queue.front().block == b);
				m_download_queue.front().not_wanted = true;
			}
			m_outstanding_bytes += r.length;
		}
	}

#if TORRENT_USE_INVARIANT_CHECKS
	struct check_postcondition
	{
		explicit check_postcondition(std::shared_ptr<torrent> const& t_
			, bool init_check = true): t(t_) { if (init_check) check(); }

		~check_postcondition() { check(); }

		void check()
		{
			if (!t->is_seed())
			{
				const int blocks_per_piece = static_cast<int>(
					(t->torrent_file().piece_length() + t->block_size() - 1) / t->block_size());

				std::vector<piece_picker::downloading_piece> const& dl_queue
					= t->picker().get_download_queue();

				for (std::vector<piece_picker::downloading_piece>::const_iterator i =
					dl_queue.begin(); i != dl_queue.end(); ++i)
				{
					TORRENT_ASSERT(i->finished <= blocks_per_piece);
				}
			}
		}

		std::shared_ptr<torrent> t;
	};
#endif


	// -----------------------------
	// ----------- PIECE -----------
	// -----------------------------

	void peer_connection::incoming_piece(peer_request const& p, char const* data)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		// we're not receiving any block right now
		m_receiving_block = piece_block::invalid;

#ifdef TORRENT_CORRUPT_DATA
		// corrupt all pieces from certain peers
		if (aux::is_v4(m_remote)
			&& (m_remote.address().to_v4().to_uint() & 0xf) == 0)
		{
			data[0] = ~data[0];
		}
#endif

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();
		if (is_disconnecting()) return;

		// slow-start
		if (m_slow_start)
			m_desired_queue_size += 1;

		update_desired_queue_size();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_piece(p, {data, p.length}))
			{
#if TORRENT_USE_ASSERTS
				TORRENT_ASSERT(m_received_in_piece == p.length);
				m_received_in_piece = 0;
#endif
				return;
			}
		}
#endif
		if (is_disconnecting()) return;

#if TORRENT_USE_INVARIANT_CHECKS
		check_postcondition post_checker_(t);
#if defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		t->check_invariant();
#endif
#endif

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::incoming_message))
		{
			peer_log(peer_log_alert::incoming_message, "PIECE", "piece: %d s: %x l: %x ds: %d qs: %d q: %d"
				, static_cast<int>(p.piece), p.start, p.length, statistics().download_rate()
				, int(m_desired_queue_size), int(m_download_queue.size()));
		}
#endif

		if (p.length == 0)
		{
			if (t->alerts().should_post<peer_error_alert>())
			{
				t->alerts().emplace_alert<peer_error_alert>(t->get_handle(), m_remote
					, m_peer_id, operation_t::bittorrent, errors::peer_sent_empty_piece);
			}
			// This is used as a reject-request by bitcomet
			incoming_reject_request(p);
			return;
		}

		// if we're already seeding, don't bother,
		// just ignore it
		if (t->is_seed())
		{
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(m_received_in_piece == p.length);
			m_received_in_piece = 0;
#endif
			if (!m_download_queue.empty())
			{
				m_download_queue.erase(m_download_queue.begin());
				if (m_download_queue.empty())
					m_counters.inc_stats_counter(counters::num_peers_down_requests, -1);
			}
			t->add_redundant_bytes(p.length, waste_reason::piece_seed);
			return;
		}

		time_point const now = clock_type::now();

		t->need_picker();

		piece_picker& picker = t->picker();

		piece_block block_finished(p.piece, p.start / t->block_size());
		TORRENT_ASSERT(verify_piece(p));

		auto const b = std::find_if(m_download_queue.begin()
			, m_download_queue.end(), aux::has_block(block_finished));

		if (b == m_download_queue.end())
		{
			if (t->alerts().should_post<unwanted_block_alert>())
			{
				t->alerts().emplace_alert<unwanted_block_alert>(t->get_handle()
					, m_remote, m_peer_id, block_finished.block_index
					, block_finished.piece_index);
			}
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST", "The block we just got was not in the request queue");
#endif
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT_VAL(m_received_in_piece == p.length, m_received_in_piece);
			m_received_in_piece = 0;
#endif
			t->add_redundant_bytes(p.length, waste_reason::piece_unknown);

			// the bytes of the piece we just completed have been deducted from
			// m_outstanding_bytes as we received it, in incoming_piece_fragment.
			// however, it now turns out the piece we received wasn't in the
			// download queue, so we still have the same number of pieces in the
			// download queue, which is why we need to add the bytes back.
			m_outstanding_bytes += p.length;
#if TORRENT_USE_INVARIANT_CHECKS
			check_invariant();
#endif
			return;
		}

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT_VAL(m_received_in_piece == p.length, m_received_in_piece);
		m_received_in_piece = 0;
#endif
		// if the block we got is already finished, then ignore it
		if (picker.is_downloaded(block_finished))
		{
			waste_reason const reason
				= (b->timed_out) ? waste_reason::piece_timed_out
				: (b->not_wanted) ? waste_reason::piece_cancelled
				: (b->busy) ? waste_reason::piece_end_game
				: waste_reason::piece_unknown;

			t->add_redundant_bytes(p.length, reason);

			m_download_queue.erase(b);
			if (m_download_queue.empty())
				m_counters.inc_stats_counter(counters::num_peers_down_requests, -1);

			if (m_disconnecting) return;

			m_request_time.add_sample(int(total_milliseconds(now - m_requested.get(m_connect))));
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "REQUEST_TIME", "%d +- %d ms"
					, m_request_time.mean(), m_request_time.avg_deviation());
			}
#endif

			// we completed an incoming block, and there are still outstanding
			// requests. The next block we expect to receive now has another
			// timeout period until we time out. So, reset the timer.
			if (!m_download_queue.empty())
				m_requested.set(m_connect, now);

			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::incoming_redundant_piece_picks);
			send_block_requests();
			return;
		}

		// we received a request within the timeout, make sure this peer is
		// not snubbed anymore
		if (total_seconds(now - m_requested.get(m_connect)) < request_timeout()
			&& m_snubbed)
		{
			m_snubbed = false;
			if (t->alerts().should_post<peer_unsnubbed_alert>())
			{
				t->alerts().emplace_alert<peer_unsnubbed_alert>(t->get_handle()
					, m_remote, m_peer_id);
			}
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "FILE_ASYNC_WRITE", "piece: %d s: %x l: %x"
			, static_cast<int>(p.piece), p.start, p.length);
#endif
		m_download_queue.erase(b);
		if (m_download_queue.empty())
			m_counters.inc_stats_counter(counters::num_peers_down_requests, -1);

		if (t->is_deleted()) return;

		bool const exceeded = m_disk_thread.async_write(t->storage(), p, data, self()
			, [conn = self(), p, t] (storage_error const& e)
			{ conn->wrap(&peer_connection::on_disk_write_complete, e, p, t); });
		m_ses.deferred_submit_jobs();

		// every peer is entitled to have two disk blocks allocated at any given
		// time, regardless of whether the cache size is exceeded or not. If this
		// was not the case, when the cache size setting is very small, most peers
		// would be blocked most of the time, because the disk cache would
		// continuously be in exceeded state. Only rarely would it actually drop
		// down to 0 and unblock all peers.
		if (exceeded && m_outstanding_writing_bytes > 0)
		{
			if (!(m_channel_state[download_channel] & peer_info::bw_disk))
				m_counters.inc_stats_counter(counters::num_peers_down_disk);
			m_channel_state[download_channel] |= peer_info::bw_disk;
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "DISK", "exceeded disk buffer watermark");
#endif
		}

		std::int64_t const write_queue_size = m_counters.inc_stats_counter(
			counters::queued_write_bytes, p.length);
		m_outstanding_writing_bytes += p.length;

		std::int64_t const max_queue_size = m_settings.get_int(
			settings_pack::max_queued_disk_bytes);
		if (write_queue_size > max_queue_size
			&& write_queue_size - p.length < max_queue_size
			&& t->alerts().should_post<performance_alert>())
		{
			t->alerts().emplace_alert<performance_alert>(t->get_handle()
				, performance_alert::too_high_disk_queue_limit);
		}

		m_request_time.add_sample(int(total_milliseconds(now - m_requested.get(m_connect))));
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "REQUEST_TIME", "%d +- %d ms"
				, m_request_time.mean(), m_request_time.avg_deviation());
		}
#endif

		// we completed an incoming block, and there are still outstanding
		// requests. The next block we expect to receive now has another
		// timeout period until we time out. So, reset the timer.
		if (!m_download_queue.empty())
			m_requested.set(m_connect, now);

		bool const was_finished = picker.is_piece_finished(p.piece);
		// did we request this block from any other peers?
		bool const multi = picker.num_peers(block_finished) > 1;
//		std::fprintf(stderr, "peer_connection mark_as_writing peer: %p piece: %d block: %d\n"
//			, peer_info_struct(), block_finished.piece_index, block_finished.block_index);
		picker.mark_as_writing(block_finished, peer_info_struct());

		// this is for a future per-block request feature
#if 0
		if (t->info_hashes().has_v2())
		{
			t->picker().started_hash_job(p.piece);
			m_disk_thread.async_hash2(t->storage(), p.piece, p.start, {}
				, [conn = self(), p](piece_index_t, sha256_hash const& h, storage_error const& e)
			{
				conn->wrap(&peer_connection::on_hash2_complete, e, p, h);
			});
			m_ses.deferred_submit_jobs();
		}
#endif

		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);
		// if we requested this block from other peers, cancel it now
		if (multi) t->cancel_block(block_finished);

#ifndef TORRENT_DISABLE_PREDICTIVE_PIECES
		if (m_settings.get_int(settings_pack::predictive_piece_announce))
		{
			piece_index_t const piece = block_finished.piece_index;
			piece_picker::downloading_piece st;
			t->picker().piece_info(piece, st);

			int const num_blocks = t->picker().blocks_in_piece(piece);
			if (st.requested > 0 && st.writing + st.finished + st.requested == num_blocks)
			{
				std::vector<torrent_peer*> const d = t->picker().get_downloaders(piece);
				if (d.size() == 1)
				{
					// only make predictions if all remaining
					// blocks are requested from the same peer
					torrent_peer* const peer = d[0];
					if (peer->connection)
					{
						// we have a connection. now, what is the current
						// download rate from this peer, and how many blocks
						// do we have left to download?
						std::int64_t const rate = peer->connection->statistics().download_payload_rate();
						std::int64_t const bytes_left = std::int64_t(st.requested) * t->block_size();
						// the settings unit is milliseconds, so calculate the
						// number of milliseconds worth of bytes left in the piece
						if (rate > 1000
							&& (bytes_left * 1000) / rate < m_settings.get_int(settings_pack::predictive_piece_announce))
						{
							// we predict we will complete this piece very soon.
							t->predicted_have_piece(piece, int((bytes_left * 1000) / rate));
						}
					}
				}
			}
		}
#endif // TORRENT_DISABLE_PREDICTIVE_PIECES

		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);

#if TORRENT_USE_INVARIANT_CHECKS \
	&& defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		t->check_invariant();
#endif

#if TORRENT_USE_ASSERTS
		piece_picker::downloading_piece pi;
		picker.piece_info(p.piece, pi);
		int num_blocks = picker.blocks_in_piece(p.piece);
		TORRENT_ASSERT(pi.writing + pi.finished + pi.requested <= num_blocks);
		TORRENT_ASSERT(picker.is_piece_finished(p.piece) == (pi.writing + pi.finished == num_blocks));
#endif

		// did we just finish the piece?
		// this means all blocks are either written
		// to disk or are in the disk write cache
		if (picker.is_piece_finished(p.piece) && !was_finished)
		{
#if TORRENT_USE_INVARIANT_CHECKS
			check_postcondition post_checker2_(t, false);
#endif
			t->verify_piece(p.piece);
		}

		check_graceful_pause();

		if (is_disconnecting()) return;

		if (request_a_block(*t, *this))
			m_counters.inc_stats_counter(counters::incoming_piece_picks);
		send_block_requests();
	}

	void peer_connection::check_graceful_pause()
	{
		// TODO: 3 instead of having to ask the torrent whether it's in graceful
		// pause mode or not, the peers should keep that state (and the torrent
		// should update them when it enters graceful pause). When a peer enters
		// graceful pause mode, it should cancel all outstanding requests and
		// clear its request queue.
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || !t->graceful_pause()) return;

		if (m_outstanding_bytes > 0) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "GRACEFUL_PAUSE", "NO MORE DOWNLOAD");
#endif
		disconnect(errors::torrent_paused, operation_t::bittorrent);
	}

	void peer_connection::on_disk_write_complete(storage_error const& error
		, peer_request const& p, std::shared_ptr<torrent> t)
	{
		TORRENT_ASSERT(is_single_thread());
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "FILE_ASYNC_WRITE_COMPLETE", "piece: %d s: %x l: %x e: %s"
				, static_cast<int>(p.piece), p.start, p.length, error.ec.message().c_str());
		}
#endif

		m_counters.inc_stats_counter(counters::queued_write_bytes, -p.length);
		m_outstanding_writing_bytes -= p.length;

		TORRENT_ASSERT(m_outstanding_writing_bytes >= 0);

		// every peer is entitled to allocate a disk buffer if it has no writes outstanding
		// see the comment in incoming_piece
		if (m_outstanding_writing_bytes == 0
			&& m_channel_state[download_channel] & peer_info::bw_disk)
		{
			m_counters.inc_stats_counter(counters::num_peers_down_disk, -1);
			m_channel_state[download_channel] &= ~peer_info::bw_disk;
		}

		INVARIANT_CHECK;

		if (!t)
		{
			disconnect(error.ec, operation_t::file_write);
			return;
		}

		// in case the outstanding bytes just dropped down
		// to allow to receive more data
		setup_receive();

		piece_block const block_finished(p.piece, p.start / t->block_size());

		if (error)
		{
			// we failed to write the piece to disk tell the piece picker
			// this will block any other peer from issuing requests
			// to this piece, until we've cleared it.
			if (error.ec == boost::asio::error::operation_aborted)
			{
				if (t->has_picker())
					t->picker().mark_as_canceled(block_finished, nullptr);
			}
			else
			{
				// if any other peer has a busy request to this block, we need
				// to cancel it too
				t->cancel_block(block_finished);
				if (t->has_picker())
					t->picker().write_failed(block_finished);

				if (t->has_storage())
				{
					// when this returns, all outstanding jobs to the
					// piece are done, and we can restore it, allowing
					// new requests to it
					m_disk_thread.async_clear_piece(t->storage(), p.piece
						, [t, block_finished] (piece_index_t pi)
						{ t->wrap(&torrent::on_piece_fail_sync, pi, block_finished); });
				}
				else
				{
					// is m_abort true? if so, we should probably just
					// exit this function early, no need to keep the picker
					// state up-to-date, right?
					t->on_piece_fail_sync(p.piece, block_finished);
				}
				m_ses.deferred_submit_jobs();
			}
			t->update_gauge();
			// handle_disk_error may disconnect us
			t->handle_disk_error("write", error, this, torrent::disk_class::write);
			return;
		}

		if (!t->has_picker()) return;

		piece_picker& picker = t->picker();

		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);

//		std::fprintf(stderr, "peer_connection mark_as_finished peer: %p piece: %d block: %d\n"
//			, peer_info_struct(), block_finished.piece_index, block_finished.block_index);
		picker.mark_as_finished(block_finished, peer_info_struct());

		t->maybe_done_flushing();

		if (t->alerts().should_post<block_finished_alert>())
		{
			t->alerts().emplace_alert<block_finished_alert>(t->get_handle(),
				remote(), pid(), block_finished.block_index
				, block_finished.piece_index);
		}

		disconnect_if_redundant();

		if (m_disconnecting) return;

#if TORRENT_USE_ASSERTS
		if (t->has_picker())
		{
			auto const& q = picker.get_download_queue();

			for (auto const& dp : q)
			{
				if (dp.index != block_finished.piece_index) continue;
				auto const info = picker.blocks_for_piece(dp);
				TORRENT_ASSERT(info[block_finished.block_index].state
					== piece_picker::block_info::state_finished);
			}
		}
#endif
		if (t->is_aborted()) return;
	}

	// -----------------------------
	// ---------- CANCEL -----------
	// -----------------------------

	void peer_connection::incoming_cancel(peer_request const& r)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_cancel(r)) return;
		}
#endif
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "CANCEL"
			, "piece: %d s: %x l: %x", static_cast<int>(r.piece), r.start, r.length);
#endif

		auto const i = std::find(m_requests.begin(), m_requests.end(), r);

		if (i != m_requests.end())
		{
			m_counters.inc_stats_counter(counters::cancelled_piece_requests);
			m_requests.erase(i);

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests, -1);

			write_reject_request(r);
		}
		else
		{
			// TODO: 2 since we throw away the queue entry once we issue
			// the disk job, this may happen. Instead, we should keep the
			// queue entry around, mark it as having been requested from
			// disk and once the disk job comes back, discard it if it has
			// been cancelled. Maybe even be able to cancel disk jobs?
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_CANCEL", "got cancel not in the queue");
#endif
		}
	}

	// -----------------------------
	// --------- DHT PORT ----------
	// -----------------------------

	void peer_connection::incoming_dht_port(int const listen_port)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "DHT_PORT", "p: %d", listen_port);
#endif
#ifndef TORRENT_DISABLE_DHT
		m_ses.add_dht_node({m_remote.address(), std::uint16_t(listen_port)});
#else
		TORRENT_UNUSED(listen_port);
#endif
	}

	// -----------------------------
	// --------- HAVE ALL ----------
	// -----------------------------

	void peer_connection::incoming_have_all()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		// we cannot disconnect in a constructor, and
		// this function may end up doing that
		TORRENT_ASSERT(m_in_constructor == false);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "HAVE_ALL");
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_have_all()) return;
		}
#endif
		if (is_disconnecting()) return;

		if (m_bitfield_received)
			t->peer_lost(m_have_piece, this);

		m_have_all = true;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "SEED", "this is a seed p: %p"
			, static_cast<void*>(m_peer_info));
#endif

		t->set_seed(m_peer_info, true);
		m_upload_only = true;
		m_bitfield_received = true;

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!t->ready_for_connections())
		{
			// assume seeds are interesting when we
			// don't even have the metadata
			t->peer_is_interesting(*this);

			disconnect_if_redundant();
			return;
		}

		TORRENT_ASSERT(!m_have_piece.empty());
		m_have_piece.set_all();
		m_num_pieces = m_have_piece.size();

		t->peer_has_all(this);

#if TORRENT_USE_INVARIANT_CHECKS
		if (t && t->has_picker())
			t->picker().check_peer_invariant(m_have_piece, peer_info_struct());
#endif

		TORRENT_ASSERT(m_have_piece.all_set());
		TORRENT_ASSERT(m_have_piece.count() == m_have_piece.size());
		TORRENT_ASSERT(m_have_piece.size() == t->torrent_file().num_pieces());

		// if we're finished, we're not interested
		if (t->is_upload_only()) send_not_interested();
		else t->peer_is_interesting(*this);

		disconnect_if_redundant();
	}

	// -----------------------------
	// --------- HAVE NONE ---------
	// -----------------------------

	void peer_connection::incoming_have_none()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "HAVE_NONE");
#endif

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_have_none()) return;
		}
#endif
		if (is_disconnecting()) return;

		if (m_bitfield_received)
			t->peer_lost(m_have_piece, this);

		t->set_seed(m_peer_info, false);
		m_bitfield_received = true;

		m_have_piece.clear_all();
		m_num_pieces = 0;

		// if the peer is ready to download stuff, it must have metadata
		m_has_metadata = true;

		// we're never interested in a peer that doesn't have anything
		send_not_interested();

		TORRENT_ASSERT(!m_have_piece.empty() || !t->ready_for_connections());
		disconnect_if_redundant();
	}

	// -----------------------------
	// ------- ALLOWED FAST --------
	// -----------------------------

	void peer_connection::incoming_allowed_fast(piece_index_t const index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "ALLOWED_FAST", "%d"
			, static_cast<int>(index));
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			if (e->on_allowed_fast(index)) return;
		}
#endif
		if (is_disconnecting()) return;

		if (index < piece_index_t(0))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "INVALID_ALLOWED_FAST"
				, "%d", static_cast<int>(index));
#endif
			return;
		}

		if (t->valid_metadata())
		{
			if (index >= m_have_piece.end_index())
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::incoming_message, "INVALID_ALLOWED_FAST"
					, "%d s: %d", static_cast<int>(index), m_have_piece.size());
#endif
				return;
			}

			// if we already have the piece, we can
			// ignore this message
			if (t->have_piece(index))
				return;
		}

		// if we don't have the metadata, we'll verify
		// this piece index later
		m_allowed_fast.push_back(index);

		// if the peer has the piece and we want
		// to download it, request it
		if (index < m_have_piece.end_index()
			&& m_have_piece[index]
			&& !t->has_piece_passed(index)
			&& t->valid_metadata()
			&& t->has_picker()
			&& t->picker().piece_priority(index) > dont_download)
		{
			t->peer_is_interesting(*this);
		}
	}

	std::vector<piece_index_t> const& peer_connection::allowed_fast()
	{
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		// TODO: sort the allowed fast set in priority order
		return m_allowed_fast;
	}

	bool peer_connection::can_request_time_critical() const
	{
		TORRENT_ASSERT(is_single_thread());
		if (has_peer_choked() || !is_interesting()) return false;
		if (int(m_download_queue.size()) + int(m_request_queue.size())
			> m_desired_queue_size * 2) return false;
		if (on_parole()) return false;
		if (m_disconnecting) return false;
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		if (t->upload_mode()) return false;

		// ignore snubbed peers, since they're not likely to return pieces in a
		// timely manner anyway
		if (m_snubbed) return false;
		return true;
	}

	bool peer_connection::make_time_critical(piece_block const& block)
	{
		TORRENT_ASSERT(is_single_thread());
		auto const rit = std::find_if(m_request_queue.begin()
			, m_request_queue.end(), aux::has_block(block));
		if (rit == m_request_queue.end()) return false;
#if TORRENT_USE_ASSERTS
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->has_picker());
		TORRENT_ASSERT(t->picker().is_requested(block));
#endif
		// ignore it if it's already time critical
		if (rit - m_request_queue.begin() < int(m_queued_time_critical)) return false;
		pending_block b = *rit;
		m_request_queue.erase(rit);
		m_request_queue.insert(m_request_queue.begin() + int(m_queued_time_critical), b);

		if (m_queued_time_critical < std::numeric_limits<decltype(m_queued_time_critical)>::max())
			++m_queued_time_critical;
		return true;
	}

	bool peer_connection::add_request(piece_block const& block
		, request_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(!m_disconnecting);
		TORRENT_ASSERT(t->valid_metadata());

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < t->torrent_file().end_piece());
		TORRENT_ASSERT(block.block_index < t->torrent_file().piece_size(block.piece_index));
		TORRENT_ASSERT(!t->picker().is_requested(block) || (t->picker().num_peers(block) > 0));
		TORRENT_ASSERT(!t->have_piece(block.piece_index));
		TORRENT_ASSERT(std::find_if(m_download_queue.begin(), m_download_queue.end()
			, aux::has_block(block)) == m_download_queue.end());
		TORRENT_ASSERT(std::find(m_request_queue.begin(), m_request_queue.end()
			, block) == m_request_queue.end());

		if (t->upload_mode())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "PIECE_PICKER"
				, "not_picking: %d,%d upload_mode"
				, static_cast<int>(block.piece_index), block.block_index);
#endif
			return false;
		}
		if (m_disconnecting)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "PIECE_PICKER"
				, "not_picking: %d,%d disconnecting"
				, static_cast<int>(block.piece_index), block.block_index);
#endif
			return false;
		}

		if ((flags & busy) && !(flags & time_critical))
		{
			// this block is busy (i.e. it has been requested
			// from another peer already). Only allow one busy
			// request in the pipeline at the time
			// this rule does not apply to time critical pieces,
			// in which case we are allowed to pick more than one
			// busy blocks
			if (std::any_of(m_download_queue.begin(), m_download_queue.end()
				, [](pending_block const& i) { return i.busy; }))
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "PIECE_PICKER"
					, "not_picking: %d,%d already in download queue & busy"
					, static_cast<int>(block.piece_index), block.block_index);
#endif
				return false;
			}

			if (std::any_of(m_request_queue.begin(), m_request_queue.end()
				, [](pending_block const& i) { return i.busy; }))
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "PIECE_PICKER"
					, "not_picking: %d,%d already in request queue & busy"
					, static_cast<int>(block.piece_index), block.block_index);
#endif
				return false;
			}
		}

		if (!t->picker().mark_as_downloading(block, peer_info_struct()
			, picker_options()))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "PIECE_PICKER"
				, "not_picking: %d,%d failed to mark_as_downloading"
				, static_cast<int>(block.piece_index), block.block_index);
#endif
			return false;
		}

		if (t->alerts().should_post<block_downloading_alert>())
		{
			t->alerts().emplace_alert<block_downloading_alert>(t->get_handle()
				, remote(), pid(), block.block_index, block.piece_index);
		}

		pending_block pb(block);
		pb.busy = (flags & busy) ? true : false;
		if (flags & time_critical)
		{
			m_request_queue.insert(m_request_queue.begin() + m_queued_time_critical
				, pb);
			++m_queued_time_critical;
		}
		else
		{
			m_request_queue.push_back(pb);
		}
		return true;
	}

	void peer_connection::cancel_all_requests()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		// this peer might be disconnecting
		if (!t) return;

		TORRENT_ASSERT(t->valid_metadata());

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "CANCEL_ALL_REQUESTS");
#endif

		while (!m_request_queue.empty())
		{
			t->picker().abort_download(m_request_queue.back().block, peer_info_struct());
			m_request_queue.pop_back();
		}
		m_queued_time_critical = 0;

		// make a local temporary copy of the download queue, since it
		// may be modified when we call write_cancel (for peers that don't
		// support the FAST extensions).
		std::vector<pending_block> temp_copy = m_download_queue;

		for (auto const& pb : temp_copy)
		{
			piece_block const b = pb.block;

			int const block_offset = b.block_index * t->block_size();
			int const block_size
				= std::min(t->torrent_file().piece_size(b.piece_index)-block_offset,
					t->block_size());
			TORRENT_ASSERT(block_size > 0);
			TORRENT_ASSERT(block_size <= t->block_size());

			// we can't cancel the piece if we've started receiving it
			if (m_receiving_block == b) continue;

			peer_request r;
			r.piece = b.piece_index;
			r.start = block_offset;
			r.length = block_size;

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing_message, "CANCEL"
				, "piece: %d s: %d l: %d b: %d"
				, static_cast<int>(b.piece_index), block_offset, block_size, b.block_index);
#endif
			write_cancel(r);
		}
	}

	void peer_connection::cancel_request(piece_block const& block, bool const force)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		// this peer might be disconnecting
		if (!t) return;

		TORRENT_ASSERT(t->valid_metadata());

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < t->torrent_file().end_piece());
		TORRENT_ASSERT(block.block_index < t->torrent_file().piece_size(block.piece_index));

		// if all the peers that requested this block has been
		// cancelled, then just ignore the cancel.
		if (!t->picker().is_requested(block)) return;

		auto const it = std::find_if(m_download_queue.begin(), m_download_queue.end()
			, aux::has_block(block));
		if (it == m_download_queue.end())
		{
			auto const rit = std::find_if(m_request_queue.begin()
				, m_request_queue.end(), aux::has_block(block));

			// when a multi block is received, it is cancelled
			// from all peers, so if this one hasn't requested
			// the block, just ignore to cancel it.
			if (rit == m_request_queue.end()) return;

			if (rit - m_request_queue.begin() < m_queued_time_critical)
				--m_queued_time_critical;

			t->picker().abort_download(block, peer_info_struct());
			m_request_queue.erase(rit);
			// since we found it in the request queue, it means it hasn't been
			// sent yet, so we don't have to send a cancel.
			return;
		}

		int const block_offset = block.block_index * t->block_size();
		int const block_size
			= std::min(t->torrent_file().piece_size(block.piece_index) - block_offset,
			t->block_size());
		TORRENT_ASSERT(block_size > 0);
		TORRENT_ASSERT(block_size <= t->block_size());

		it->not_wanted = true;

		if (force) t->picker().abort_download(block, peer_info_struct());

		if (m_outstanding_bytes < block_size) return;

		peer_request r;
		r.piece = block.piece_index;
		r.start = block_offset;
		r.length = block_size;

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing_message, "CANCEL"
				, "piece: %d s: %d l: %d b: %d"
				, static_cast<int>(block.piece_index), block_offset, block_size, block.block_index);
#endif
		write_cancel(r);
	}

	bool peer_connection::send_choke()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(!is_connecting());

		if (m_choked)
		{
			TORRENT_ASSERT(m_peer_info == nullptr
				|| m_peer_info->optimistically_unchoked == false);
			return false;
		}

		if (m_peer_info && m_peer_info->optimistically_unchoked)
		{
			m_peer_info->optimistically_unchoked = false;
			m_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic, -1);
		}

		m_suggest_pieces.clear();
		m_suggest_pieces.shrink_to_fit();

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "CHOKE");
#endif
		write_choke();
		m_counters.inc_stats_counter(counters::num_peers_up_unchoked_all, -1);
		if (!ignore_unchoke_slots())
			m_counters.inc_stats_counter(counters::num_peers_up_unchoked, -1);
		m_choked = true;

		m_last_choke.set(m_connect, aux::time_now());
		m_num_invalid_requests = 0;

		// reject the requests we have in the queue
		// except the allowed fast pieces
		for (auto i = m_requests.begin(); i != m_requests.end();)
		{
			if (std::find(m_accept_fast.begin(), m_accept_fast.end(), i->piece)
				!= m_accept_fast.end())
			{
				++i;
				continue;
			}
			peer_request const& r = *i;
			m_counters.inc_stats_counter(counters::choked_piece_requests);
			write_reject_request(r);
			i = m_requests.erase(i);

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests, -1);
		}
		return true;
	}

	bool peer_connection::send_unchoke()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (!m_choked) return false;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return false;

		if (m_settings.get_int(settings_pack::suggest_mode)
			== settings_pack::suggest_read_cache)
		{
			// immediately before unchoking this peer, we should send some
			// suggested pieces for it to request
			send_piece_suggestions(2);
		}

		m_last_unchoke.set(m_connect, aux::time_now());
		write_unchoke();
		m_counters.inc_stats_counter(counters::num_peers_up_unchoked_all);
		if (!ignore_unchoke_slots())
			m_counters.inc_stats_counter(counters::num_peers_up_unchoked);
		m_choked = false;

		m_uploaded_at_last_unchoke = m_statistics.total_payload_upload();

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "UNCHOKE");
#endif
		return true;
	}

	void peer_connection::send_interested()
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_interesting) return;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return;
		m_interesting = true;
		m_counters.inc_stats_counter(counters::num_peers_down_interested);
		write_interested();

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "INTERESTED");
#endif
	}

	void peer_connection::send_not_interested()
	{
		TORRENT_ASSERT(is_single_thread());
		// we cannot disconnect in a constructor, and
		// this function may end up doing that
		TORRENT_ASSERT(m_in_constructor == false);

		if (!m_interesting)
		{
			disconnect_if_redundant();
			return;
		}

		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return;
		m_interesting = false;
		m_slow_start = false;
		m_counters.inc_stats_counter(counters::num_peers_down_interested, -1);

		disconnect_if_redundant();
		if (m_disconnecting) return;

		write_not_interested();

		m_became_uninteresting.set(m_connect, aux::time_now());

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing_message))
		{
			peer_log(peer_log_alert::outgoing_message, "NOT_INTERESTED");
		}
#endif
	}

	void peer_connection::send_upload_only(bool const enabled)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_connecting || in_handshake()) return;

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing_message))
		{
			peer_log(peer_log_alert::outgoing_message, "UPLOAD_ONLY", "%d"
				, int(enabled));
		}
#endif

		write_upload_only(enabled);
	}

	void peer_connection::send_piece_suggestions(int const num)
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		int const new_suggestions = t->get_suggest_pieces(m_suggest_pieces
			, m_have_piece, num);

		// higher priority pieces are farther back in the vector, the last
		// suggested piece to be received is the highest priority, so send the
		// highest priority piece last.
		for (auto i = m_suggest_pieces.end() - new_suggestions;
			i != m_suggest_pieces.end(); ++i)
		{
			send_suggest(*i);
		}
		int const max = m_settings.get_int(settings_pack::max_suggest_pieces);
		if (m_suggest_pieces.end_index() > max)
		{
			int const to_erase = m_suggest_pieces.end_index() - max;
			m_suggest_pieces.erase(m_suggest_pieces.begin()
				, m_suggest_pieces.begin() + to_erase);
		}
	}

	void peer_connection::send_suggest(piece_index_t const piece)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_connecting || in_handshake()) return;

		// don't suggest a piece that the peer already has
		if (has_piece(piece)) return;

		// we cannot suggest a piece we don't have!
#if TORRENT_USE_ASSERTS
		{
			std::shared_ptr<torrent> t = m_torrent.lock();
			TORRENT_ASSERT(t);
			TORRENT_ASSERT(t->has_piece_passed(piece));
			TORRENT_ASSERT(piece < t->torrent_file().end_piece());
		}
#endif

		write_suggest(piece);
	}

	void peer_connection::send_block_requests()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (m_disconnecting) return;

		// TODO: 3 once peers are properly put in graceful pause mode, they can
		// cancel all outstanding requests and this test can be removed.
		if (t->graceful_pause()) return;

		// we can't download pieces in these states
		if (t->state() == torrent_status::checking_files
			|| t->state() == torrent_status::checking_resume_data
			|| t->state() == torrent_status::downloading_metadata)
			return;

		if (int(m_download_queue.size()) >= m_desired_queue_size
			|| t->upload_mode()) return;

		bool const empty_download_queue = m_download_queue.empty();

		while (!m_request_queue.empty()
			&& (int(m_download_queue.size()) < m_desired_queue_size
				|| m_queued_time_critical > 0))
		{
			pending_block block = m_request_queue.front();

			m_request_queue.erase(m_request_queue.begin());
			if (m_queued_time_critical) --m_queued_time_critical;

			// if we're a seed, we don't have a piece picker
			// so we don't have to worry about invariants getting
			// out of sync with it
			if (!t->has_picker()) continue;

			// this can happen if a block times out, is re-requested and
			// then arrives "unexpectedly"
			if (t->picker().is_downloaded(block.block))
			{
				t->picker().abort_download(block.block, peer_info_struct());
				continue;
			}

			int block_offset = block.block.block_index * t->block_size();
			int bs = std::min(t->torrent_file().piece_size(
				block.block.piece_index) - block_offset, t->block_size());
			TORRENT_ASSERT(bs > 0);
			TORRENT_ASSERT(bs <= t->block_size());

			peer_request r;
			r.piece = block.block.piece_index;
			r.start = block_offset;
			r.length = bs;

			if (m_download_queue.empty())
				m_counters.inc_stats_counter(counters::num_peers_down_requests);

			TORRENT_ASSERT(verify_piece(t->to_req(block.block)));
			block.send_buffer_offset = aux::numeric_cast<std::uint32_t>(m_send_buffer.size());
			m_download_queue.push_back(block);
			m_outstanding_bytes += bs;
#if TORRENT_USE_INVARIANT_CHECKS
			check_invariant();
#endif

			// if we are requesting large blocks, merge the smaller
			// blocks that are in the same piece into larger requests
			if (m_request_large_blocks)
			{
				int const blocks_per_piece = t->torrent_file().piece_length() / t->block_size();

				while (!m_request_queue.empty())
				{
					// check to see if this block is connected to the previous one
					// if it is, merge them, otherwise, break this merge loop
					pending_block const& front = m_request_queue.front();
					if (static_cast<int>(front.block.piece_index) * blocks_per_piece + front.block.block_index
						!= static_cast<int>(block.block.piece_index) * blocks_per_piece + block.block.block_index + 1)
						break;
					block = m_request_queue.front();
					m_request_queue.erase(m_request_queue.begin());
					TORRENT_ASSERT(verify_piece(t->to_req(block.block)));

					if (m_download_queue.empty())
						m_counters.inc_stats_counter(counters::num_peers_down_requests);

					block.send_buffer_offset = aux::numeric_cast<std::uint32_t>(m_send_buffer.size());
					m_download_queue.push_back(block);
					if (m_queued_time_critical) --m_queued_time_critical;

					block_offset = block.block.block_index * t->block_size();
					bs = std::min(t->torrent_file().piece_size(
						block.block.piece_index) - block_offset, t->block_size());
					TORRENT_ASSERT(bs > 0);
					TORRENT_ASSERT(bs <= t->block_size());

					r.length += bs;
					m_outstanding_bytes += bs;
#if TORRENT_USE_INVARIANT_CHECKS
					check_invariant();
#endif
				}

#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "MERGING_REQUESTS"
					, "piece: %d start: %d length: %d", static_cast<int>(r.piece)
					, r.start, r.length);
#endif

			}

			// the verification will fail for coalesced blocks
			TORRENT_ASSERT(verify_piece(r) || m_request_large_blocks);

#ifndef TORRENT_DISABLE_EXTENSIONS
			bool handled = false;
			for (auto const& e : m_extensions)
			{
				handled = e->write_request(r);
				if (handled) break;
			}
			if (is_disconnecting()) return;
			if (!handled)
#endif
			{
				write_request(r);
				m_last_request.set(m_connect, aux::time_now());
			}

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::outgoing_message))
			{
				peer_log(peer_log_alert::outgoing_message, "REQUEST"
					, "piece: %d s: %x l: %x ds: %dB/s dqs: %d rqs: %d blk: %s"
					, static_cast<int>(r.piece), r.start, r.length, statistics().download_rate()
					, int(m_desired_queue_size), int(m_download_queue.size())
					, m_request_large_blocks?"large":"single");
			}
#endif
		}
		m_last_piece.set(m_connect, aux::time_now());

		if (!m_download_queue.empty()
			&& empty_download_queue)
		{
			// This means we just added a request to this connection that
			// previously did not have a request. That's when we start the
			// request timeout.
			m_requested.set(m_connect, aux::time_now());
		}
	}

	void peer_connection::connect_failed(error_code const& e)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(e);

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "CONNECTION FAILED"
				, "%s %s", print_endpoint(m_remote).c_str(), print_error(e).c_str());
		}
#endif
#ifndef TORRENT_DISABLE_LOGGING
		if (m_ses.should_log())
			m_ses.session_log("CONNECTION FAILED: %s", print_endpoint(m_remote).c_str());
#endif

		m_counters.inc_stats_counter(counters::connect_timeouts);

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(!m_connecting || t);
		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t && m_peer_info) t->dec_num_connecting(m_peer_info);
			m_connecting = false;
		}

		// a connection attempt using uTP just failed
		// mark this peer as not supporting uTP
		// we'll never try it again (unless we're trying holepunch)
		if (is_utp(m_socket)
			&& m_peer_info
			&& m_peer_info->supports_utp
			&& !m_holepunch_mode)
		{
			m_peer_info->supports_utp = false;
			// reconnect immediately using TCP
			fast_reconnect(true);
			disconnect(e, operation_t::connect, normal);
			if (t && m_peer_info)
			{
				std::weak_ptr<torrent> weak_t = t;
				std::weak_ptr<peer_connection> weak_self = shared_from_this();

				// we can't touch m_connections here, since we're likely looping
				// over it. So defer the actual reconnection to after we've handled
				// the existing message queue
				post(m_ses.get_context(), [weak_t, weak_self]()
				{
					std::shared_ptr<torrent> tor = weak_t.lock();
					std::shared_ptr<peer_connection> p = weak_self.lock();
					if (tor && p)
					{
						torrent_peer* pi = p->peer_info_struct();
						tor->connect_to_peer(pi, true);
					}
				});
			}
			return;
		}

		if (m_holepunch_mode)
			fast_reconnect(true);

#ifndef TORRENT_DISABLE_EXTENSIONS
		if ((!is_utp(m_socket)
				|| !m_settings.get_bool(settings_pack::enable_outgoing_tcp))
			&& m_peer_info
			&& m_peer_info->supports_holepunch
			&& !m_holepunch_mode)
		{
			// see if we can try a holepunch
			bt_peer_connection* p = t->find_introducer(remote());
			if (p)
				p->write_holepunch_msg(bt_peer_connection::hp_message::rendezvous, remote());
		}
#endif

		disconnect(e, operation_t::connect, failure);
	}

	// the error argument defaults to 0, which means deliberate disconnect
	// 1 means unexpected disconnect/error
	// 2 protocol error (client sent something invalid)
	void peer_connection::disconnect(error_code const& ec
		, operation_t const op, disconnect_severity_t const error)
	{
		TORRENT_ASSERT(is_single_thread());
#if TORRENT_USE_ASSERTS
		m_disconnect_started = true;
#endif

		if (m_disconnecting) return;

		set_close_reason(m_socket, error_to_close_reason(ec));
		close_reason_t const close_reason = get_close_reason(m_socket);
#ifndef TORRENT_DISABLE_LOGGING
		if (close_reason != close_reason_t::none)
		{
			peer_log(peer_log_alert::info, "CLOSE_REASON", "%d", int(close_reason));
		}
#endif

		// while being disconnected, it's possible that our torrent_peer
		// pointer gets cleared. Make sure we save it to be able to keep
		// proper books in the piece_picker (when debugging is enabled)
		torrent_peer* self_peer = peer_info_struct();

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info)) try
		{
			static aux::array<char const*, 3, disconnect_severity_t> const str{{{
				"CONNECTION_CLOSED", "CONNECTION_FAILED", "PEER_ERROR"}}};
			peer_log(peer_log_alert::info, str[error], "op: %d %s"
				, static_cast<int>(op), print_error(ec).c_str());

			if (ec == boost::asio::error::eof
				&& !in_handshake()
				&& !is_connecting()
				&& aux::time_now() - connected_time() < seconds(15))
			{
				peer_log(peer_log_alert::info, "SHORT_LIVED_DISCONNECT", "");
			}
		}
		catch (std::exception const& err)
		{
			peer_log(peer_log_alert::info, "PEER_ERROR" ,"op: %d ERROR: unknown error (failed with exception) %s"
				, static_cast<int>(op), err.what());
		}
#endif

		if (!(m_channel_state[upload_channel] & peer_info::bw_network))
		{
			// make sure we free up all send buffers that are owned
			// by the disk thread
			m_send_buffer.clear();
		}

		// we cannot do this in a constructor
		TORRENT_ASSERT(m_in_constructor == false);
		if (error > normal)
		{
			m_failed = true;
		}

		if (m_connected)
			m_counters.inc_stats_counter(counters::num_peers_connected, -1);
		m_connected = false;

		// for incoming connections, we get invalid argument errors
		// when asking for the remote endpoint and the socket already
		// closed, which is an edge case, but possible to happen when
		// a peer makes a TCP and uTP connection in parallel.
		// for outgoing connections however, why would we get this?
//		TORRENT_ASSERT(ec != error::invalid_argument || !m_outgoing);

		m_counters.inc_stats_counter(counters::disconnected_peers);
		if (error == peer_error) m_counters.inc_stats_counter(counters::error_peers);

		if (ec == error::connection_reset)
			m_counters.inc_stats_counter(counters::connreset_peers);
		else if (ec == error::eof)
			m_counters.inc_stats_counter(counters::eof_peers);
		else if (ec == error::connection_refused)
			m_counters.inc_stats_counter(counters::connrefused_peers);
		else if (ec == error::connection_aborted)
			m_counters.inc_stats_counter(counters::connaborted_peers);
		else if (ec == error::not_connected)
			m_counters.inc_stats_counter(counters::notconnected_peers);
		else if (ec == error::no_permission)
			m_counters.inc_stats_counter(counters::perm_peers);
		else if (ec == error::no_buffer_space)
			m_counters.inc_stats_counter(counters::buffer_peers);
		else if (ec == error::host_unreachable)
			m_counters.inc_stats_counter(counters::unreachable_peers);
		else if (ec == error::broken_pipe)
			m_counters.inc_stats_counter(counters::broken_pipe_peers);
		else if (ec == error::address_in_use)
			m_counters.inc_stats_counter(counters::addrinuse_peers);
		else if (ec == error::access_denied)
			m_counters.inc_stats_counter(counters::no_access_peers);
		else if (ec == error::invalid_argument)
			m_counters.inc_stats_counter(counters::invalid_arg_peers);
		else if (ec == error::operation_aborted)
			m_counters.inc_stats_counter(counters::aborted_peers);
		else if (ec == errors::upload_upload_connection
			|| ec == errors::uninteresting_upload_peer
			|| ec == errors::torrent_aborted
			|| ec == errors::self_connection
			|| ec == errors::torrent_paused)
			m_counters.inc_stats_counter(counters::uninteresting_peers);

		if (ec == errors::timed_out
			|| ec == error::timed_out)
			m_counters.inc_stats_counter(counters::transport_timeout_peers);

		if (ec == errors::timed_out_inactivity
			|| ec == errors::timed_out_no_request
			|| ec == errors::timed_out_no_interest)
			m_counters.inc_stats_counter(counters::timeout_peers);

		if (ec == errors::no_memory)
			m_counters.inc_stats_counter(counters::no_memory_peers);

		if (ec == errors::too_many_connections)
			m_counters.inc_stats_counter(counters::too_many_peers);

		if (ec == errors::timed_out_no_handshake)
			m_counters.inc_stats_counter(counters::connect_timeouts);

		if (error > normal)
		{
			if (is_utp(m_socket)) m_counters.inc_stats_counter(counters::error_utp_peers);
			else m_counters.inc_stats_counter(counters::error_tcp_peers);

			if (m_outgoing) m_counters.inc_stats_counter(counters::error_outgoing_peers);
			else m_counters.inc_stats_counter(counters::error_incoming_peers);

#if !defined TORRENT_DISABLE_ENCRYPTION
			if (type() == connection_type::bittorrent && op != operation_t::connect)
			{
				auto* bt = static_cast<bt_peer_connection*>(this);
				if (bt->supports_encryption()) m_counters.inc_stats_counter(
					counters::error_encrypted_peers);
				if (bt->rc4_encrypted() && bt->supports_encryption())
					m_counters.inc_stats_counter(counters::error_rc4_peers);
			}
#endif // TORRENT_DISABLE_ENCRYPTION
		}

		std::shared_ptr<peer_connection> me(self());

		INVARIANT_CHECK;

		if (m_channel_state[upload_channel] & peer_info::bw_disk)
		{
			m_counters.inc_stats_counter(counters::num_peers_up_disk, -1);
			m_channel_state[upload_channel] &= ~peer_info::bw_disk;
		}
		if (m_channel_state[download_channel] & peer_info::bw_disk)
		{
			m_counters.inc_stats_counter(counters::num_peers_down_disk, -1);
			m_channel_state[download_channel] &= ~peer_info::bw_disk;
		}

		std::shared_ptr<torrent> t = m_torrent.lock();

		// don't try to connect to ourself again
		if (ec == errors::self_connection && m_peer_info && t)
			t->ban_peer(m_peer_info);

		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t) t->dec_num_connecting(m_peer_info);
			m_connecting = false;
		}

		torrent_handle handle;
		if (t) handle = t->get_handle();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			e->on_disconnect(ec);
		}
#endif

		if (ec == error::address_in_use
			&& m_settings.get_int(settings_pack::outgoing_port) != 0
			&& t)
		{
			if (t->alerts().should_post<performance_alert>())
				t->alerts().emplace_alert<performance_alert>(
					handle, performance_alert::too_few_outgoing_ports);
		}

		m_disconnecting = true;

		if (t)
		{
			if (ec)
			{
				if ((error > failure || ec.category() == socks_category())
					&& t->alerts().should_post<peer_error_alert>())
				{
					t->alerts().emplace_alert<peer_error_alert>(handle, remote()
						, pid(), op, ec);
				}

				if (error <= failure && t->alerts().should_post<peer_disconnected_alert>())
				{
					t->alerts().emplace_alert<peer_disconnected_alert>(handle
						, remote(), pid(), op, socket_type_idx(m_socket), ec, close_reason);
				}
			}

			// make sure we keep all the stats!
			if (!m_ignore_stats)
			{
				// report any partially received payload as redundant
				piece_block_progress pbp = downloading_piece_progress();
				if (pbp.piece_index != piece_block_progress::invalid_index
					&& pbp.bytes_downloaded > 0
					&& pbp.bytes_downloaded < pbp.full_block_bytes)
				{
					t->add_redundant_bytes(pbp.bytes_downloaded, waste_reason::piece_closing);
				}
			}

			if (t->has_picker())
			{
				clear_download_queue();
				piece_picker& picker = t->picker();
				while (!m_request_queue.empty())
				{
					pending_block const& qe = m_request_queue.back();
					if (!qe.timed_out && !qe.not_wanted)
						picker.abort_download(qe.block, self_peer);
					m_request_queue.pop_back();
				}
			}
			else
			{
				m_download_queue.clear();
				m_request_queue.clear();
				m_outstanding_bytes = 0;
			}
			m_queued_time_critical = 0;

#if TORRENT_USE_INVARIANT_CHECKS
			try { check_invariant(); } catch (std::exception const&) {}
#endif
			t->remove_peer(self());

			// we need to do this here to maintain accurate accounting of number of
			// unchoke slots. Ideally the updating of choked state and the
			// accounting should be tighter
			if (!m_choked)
			{
				m_choked = true;
				m_counters.inc_stats_counter(counters::num_peers_up_unchoked_all, -1);
				if (!ignore_unchoke_slots())
					m_counters.inc_stats_counter(counters::num_peers_up_unchoked, -1);
			}
		}
		else
		{
			TORRENT_ASSERT(m_download_queue.empty());
			TORRENT_ASSERT(m_request_queue.empty());
			m_ses.close_connection(this);
		}

		async_shutdown(m_socket, self());
	}

	bool peer_connection::ignore_unchoke_slots() const
	{
		TORRENT_ASSERT(is_single_thread());
		if (num_classes() == 0) return true;

		if (m_ses.ignore_unchoke_slots_set(*this)) return true;
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (t && m_ses.ignore_unchoke_slots_set(*t)) return true;
		return false;
	}

	bool peer_connection::on_local_network() const
	{
		TORRENT_ASSERT(is_single_thread());
		return aux::is_local(m_remote.address())
			|| m_remote.address().is_loopback();
	}

	int peer_connection::request_timeout() const
	{
		const int deviation = m_request_time.avg_deviation();
		const int avg = m_request_time.mean();

		int ret;
		if (m_request_time.num_samples() < 2)
		{
			if (m_request_time.num_samples() == 0)
				return m_settings.get_int(settings_pack::request_timeout);

			ret = avg + avg / 5;
		}
		else
		{
			ret = avg + deviation * 4;
		}

		// ret is milliseconds, the return value is seconds. Convert to
		// seconds and round up
		ret = std::min((ret + 999) / 1000
			, m_settings.get_int(settings_pack::request_timeout));

		// timeouts should never be less than 2 seconds. The granularity is whole
		// seconds, and only checked once per second. 2 is the minimum to avoid
		// being considered timed out instantly
		return std::max(2, ret);
	}

	void peer_connection::get_peer_info(peer_info& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!associated_torrent().expired());

		time_point const now = aux::time_now();

		p.download_rate_peak = m_download_rate_peak;
		p.upload_rate_peak = m_upload_rate_peak;
		p.rtt = m_request_time.mean();
		p.down_speed = statistics().download_rate();
		p.up_speed = statistics().upload_rate();
		p.payload_down_speed = statistics().download_payload_rate();
		p.payload_up_speed = statistics().upload_payload_rate();
		p.pid = pid();
		p.ip = remote();
		p.pending_disk_bytes = m_outstanding_writing_bytes;
		p.pending_disk_read_bytes = m_reading_bytes;
		p.send_quota = m_quota[upload_channel];
		p.receive_quota = m_quota[download_channel];
		p.num_pieces = m_num_pieces;
		if (m_download_queue.empty()) p.request_timeout = -1;
		else p.request_timeout = int(total_seconds(m_requested.get(m_connect) - now)
			+ request_timeout());

		p.download_queue_time = download_queue_time();
		p.queue_bytes = m_outstanding_bytes;

		p.total_download = statistics().total_payload_download();
		p.total_upload = statistics().total_payload_upload();
#if TORRENT_ABI_VERSION == 1
		p.upload_limit = -1;
		p.download_limit = -1;
		p.load_balancing = 0;
#endif

		p.download_queue_length = int(download_queue().size() + m_request_queue.size());
		p.requests_in_buffer = int(std::count_if(m_download_queue.begin()
			, m_download_queue.end()
			, &pending_block_in_buffer));

		p.target_dl_queue_length = desired_queue_size();
		p.upload_queue_length = int(upload_queue().size());
		p.timed_out_requests = 0;
		p.busy_requests = 0;
		for (auto const& pb : m_download_queue)
		{
			if (pb.timed_out) ++p.timed_out_requests;
			if (pb.busy) ++p.busy_requests;
		}

		piece_block_progress const ret = downloading_piece_progress();
		if (ret.piece_index != piece_block_progress::invalid_index)
		{
			p.downloading_piece_index = ret.piece_index;
			p.downloading_block_index = ret.block_index;
			p.downloading_progress = ret.bytes_downloaded;
			p.downloading_total = ret.full_block_bytes;
		}
		else
		{
			p.downloading_piece_index = piece_index_t(-1);
			p.downloading_block_index = -1;
			p.downloading_progress = 0;
			p.downloading_total = 0;
		}

		p.pieces = get_bitfield();
		p.last_request = now - m_last_request.get(m_connect);
		p.last_active = now - std::max(m_last_sent.get(m_connect), m_last_receive.get(m_connect));

		// this will set the flags so that we can update them later
		p.flags = {};
		get_specific_peer_info(p);

		if (is_seed()) p.flags |= peer_info::seed;
		if (m_snubbed) p.flags |= peer_info::snubbed;
		if (m_upload_only) p.flags |= peer_info::upload_only;
		if (m_endgame_mode) p.flags |= peer_info::endgame_mode;
		if (m_holepunch_mode) p.flags |= peer_info::holepunched;
		if (peer_info_struct())
		{
			torrent_peer* pi = peer_info_struct();
			TORRENT_ASSERT(pi->in_use);
			p.source = peer_source_flags_t(pi->source);
			p.failcount = pi->failcount;
			p.num_hashfails = pi->hashfails;
			if (pi->on_parole) p.flags |= peer_info::on_parole;
			if (pi->optimistically_unchoked) p.flags |= peer_info::optimistic_unchoke;
		}
		else
		{
			p.source = {};
			p.failcount = 0;
			p.num_hashfails = 0;
		}

#if TORRENT_ABI_VERSION == 1
		p.remote_dl_rate = 0;
#endif
		p.send_buffer_size = m_send_buffer.capacity();
		p.used_send_buffer = m_send_buffer.size();
		p.receive_buffer_size = m_recv_buffer.capacity();
		p.used_receive_buffer = m_recv_buffer.pos();
		p.receive_buffer_watermark = m_recv_buffer.watermark();
		p.write_state = m_channel_state[upload_channel];
		p.read_state = m_channel_state[download_channel];

		// pieces may be empty if we don't have metadata yet
		if (p.pieces.empty())
		{
			p.progress = 0.f;
			p.progress_ppm = 0;
		}
		else
		{
#if TORRENT_NO_FPU
			p.progress = 0.f;
#else
			p.progress = float(p.pieces.count()) / float(p.pieces.size());
#endif
			p.progress_ppm = int(std::int64_t(p.pieces.count()) * 1000000 / p.pieces.size());
		}

		error_code ec;
		p.local_endpoint = get_socket().local_endpoint(ec);
	}

#ifndef TORRENT_DISABLE_SUPERSEEDING
	// TODO: 3 new_piece should be an optional<piece_index_t>. piece index -1
	// should not be allowed
	void peer_connection::superseed_piece(piece_index_t const replace_piece
		, piece_index_t const new_piece)
	{
		TORRENT_ASSERT(is_single_thread());

		if (is_connecting()) return;
		if (in_handshake()) return;

		if (new_piece == piece_index_t(-1))
		{
			if (m_superseed_piece[0] == piece_index_t(-1)) return;
			m_superseed_piece[0] = piece_index_t(-1);
			m_superseed_piece[1] = piece_index_t(-1);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SUPER_SEEDING", "ending");
#endif
			std::shared_ptr<torrent> t = m_torrent.lock();
			TORRENT_ASSERT(t);

			// this will either send a full bitfield or
			// a have-all message, effectively terminating
			// super-seeding, since the peer may pick any piece
			write_bitfield();

			return;
		}

		TORRENT_ASSERT(!has_piece(new_piece));

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "HAVE", "piece: %d (super seed)"
			, static_cast<int>(new_piece));
#endif
		write_have(new_piece);

		if (replace_piece >= piece_index_t(0))
		{
			// move the piece we're replacing to the tail
			if (m_superseed_piece[0] == replace_piece)
				std::swap(m_superseed_piece[0], m_superseed_piece[1]);
		}

		m_superseed_piece[1] = m_superseed_piece[0];
		m_superseed_piece[0] = new_piece;
	}
#endif // TORRENT_DISABLE_SUPERSEEDING

	void peer_connection::max_out_request_queue(int s)
	{
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "MAX_OUT_QUEUE_SIZE", "%d -> %d"
			, m_max_out_request_queue, s);
#endif
		m_max_out_request_queue = aux::clamp_assign<std::uint16_t>(s);
	}

	int peer_connection::max_out_request_queue() const
	{
		return int(m_max_out_request_queue);
	}

	void peer_connection::update_desired_queue_size()
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_snubbed)
		{
			m_desired_queue_size = 1;
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		int const previous_queue_size = m_desired_queue_size;
#endif

		int const download_rate = statistics().download_payload_rate();

		// the desired download queue size
		int const queue_time = m_settings.get_int(settings_pack::request_queue_time);

		// when we're in slow-start mode we increase the desired queue size every
		// time we receive a piece, no need to adjust it here (other than
		// enforcing the upper limit)
		if (!m_slow_start)
		{
			// (if the latency is more than this, the download will stall)
			// so, the queue size is queue_time * down_rate / 16 kiB
			// (16 kB is the size of each request)
			// the minimum number of requests is 2 and the maximum is 48
			// the block size doesn't have to be 16. So we first query the
			// torrent for it
			std::shared_ptr<torrent> t = m_torrent.lock();
			int const bs = t->block_size();

			TORRENT_ASSERT(bs > 0);

			m_desired_queue_size = std::uint16_t(queue_time * download_rate / bs);
		}

		if (m_desired_queue_size > m_max_out_request_queue)
			m_desired_queue_size = m_max_out_request_queue;
		if (m_desired_queue_size < min_request_queue)
			m_desired_queue_size = min_request_queue;

#ifndef TORRENT_DISABLE_LOGGING
		if (previous_queue_size != m_desired_queue_size)
		{
			peer_log(peer_log_alert::info, "UPDATE_QUEUE_SIZE"
				, "dqs: %d max: %d dl: %d qt: %d snubbed: %d slow-start: %d"
				, int(m_desired_queue_size), int(m_max_out_request_queue)
				, download_rate, queue_time, int(m_snubbed), int(m_slow_start));
		}
#endif
	}

	void peer_connection::second_tick(int const tick_interval_ms)
	{
		TORRENT_ASSERT(is_single_thread());
		time_point const now = aux::time_now();
		std::shared_ptr<peer_connection> me(self());

		// the invariant check must be run before me is destructed
		// in case the peer got disconnected
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();

		int warning = 0;
		// drain the IP overhead from the bandwidth limiters
		if (m_settings.get_bool(settings_pack::rate_limit_ip_overhead) && t)
		{
			warning |= m_ses.use_quota_overhead(*this, m_statistics.download_ip_overhead()
				, m_statistics.upload_ip_overhead());
			warning |= m_ses.use_quota_overhead(*t, m_statistics.download_ip_overhead()
				, m_statistics.upload_ip_overhead());
		}

		if (warning && t->alerts().should_post<performance_alert>())
		{
			for (int channel = 0; channel < 2; ++channel)
			{
				if ((warning & (1 << channel)) == 0) continue;
				t->alerts().emplace_alert<performance_alert>(t->get_handle()
					, channel == peer_connection::download_channel
					? performance_alert::download_limit_too_low
					: performance_alert::upload_limit_too_low);
			}
		}

		if (!t || m_disconnecting)
		{
			TORRENT_ASSERT(t || !m_connecting);
			if (m_connecting)
			{
				m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
				if (t) t->dec_num_connecting(m_peer_info);
				m_connecting = false;
			}
			disconnect(errors::torrent_aborted, operation_t::bittorrent);
			return;
		}

		if (m_endgame_mode
			&& m_interesting
			&& m_download_queue.empty()
			&& m_request_queue.empty()
			&& now - seconds(5) >= m_last_request.get(m_connect))
		{
			// this happens when we're in strict end-game
			// mode and the peer could not request any blocks
			// because they were all taken but there were still
			// unrequested blocks. Now, 5 seconds later, there
			// might not be any unrequested blocks anymore, so
			// we should try to pick another block to see
			// if we can pick a busy one
			m_last_request.set(m_connect, now);
			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::end_game_piece_picks);
			if (m_disconnecting) return;
			send_block_requests();
		}

#ifndef TORRENT_DISABLE_SUPERSEEDING
		if (t->super_seeding()
			&& t->ready_for_connections()
			&& !m_peer_interested
			&& m_became_uninterested.get(m_connect) + seconds(10) < now)
		{
			// maybe we need to try another piece, to see if the peer
			// become interested in us then
			superseed_piece(piece_index_t(-1), t->get_piece_to_super_seed(m_have_piece));
		}
#endif

		on_tick();
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& e : m_extensions)
		{
			e->tick();
		}
		if (is_disconnecting()) return;
#endif

		// if the peer hasn't said a thing for a certain
		// time, it is considered to have timed out
		time_duration d = now - m_last_receive.get(m_connect);

		if (m_connecting)
		{
			int connect_timeout = m_settings.get_int(settings_pack::peer_connect_timeout);
			if (m_peer_info) connect_timeout += 3 * m_peer_info->failcount;

			// SSL and i2p handshakes are slow
			if (is_ssl(m_socket))
				connect_timeout += 10;

#if TORRENT_USE_I2P
			if (is_i2p(m_socket))
				connect_timeout += 20;
#endif

			if (d > seconds(connect_timeout)
				&& can_disconnect(errors::timed_out))
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "CONNECT_FAILED", "waited %d seconds"
					, int(total_seconds(d)));
#endif
				connect_failed(errors::timed_out);
				return;
			}
		}

		// if the bw_network flag isn't set, it means we are not even trying to
		// read from this peer's socket. Most likely because we're applying a
		// rate limit. If the peer is "slow" because we are rate limiting it,
		// don't enforce timeouts. However, as soon as we *do* read from the
		// socket, we expect to receive data, and not have timed out. Then we
		// can enforce the timeouts.
		bool const reading_socket = bool(m_channel_state[download_channel] & peer_info::bw_network);

		// TODO: 2 use a deadline_timer for timeouts. Don't rely on second_tick()!
		// Hook this up to connect timeout as well. This would improve performance
		// because of less work in second_tick(), and might let use remove ticking
		// entirely eventually
		if (reading_socket && d > seconds(timeout()) && !m_connecting && m_reading_bytes == 0
			&& can_disconnect(errors::timed_out_inactivity))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "LAST_ACTIVITY", "%d seconds ago"
				, int(total_seconds(d)));
#endif
			disconnect(errors::timed_out_inactivity, operation_t::bittorrent);
			return;
		}

		// do not stall waiting for a handshake
		int timeout = m_settings.get_int (settings_pack::handshake_timeout);
#if TORRENT_USE_I2P
		timeout *= is_i2p(m_socket) ? 4 : 1;
#endif
		if (reading_socket
			&& !m_connecting
			&& in_handshake()
			&& d > seconds(timeout))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "NO_HANDSHAKE", "waited %d seconds"
				, int(total_seconds(d)));
#endif
			disconnect(errors::timed_out_no_handshake, operation_t::bittorrent);
			return;
		}

		// disconnect peers that we unchoked, but they didn't send a request in
		// the last 60 seconds, and we haven't been working on servicing a request
		// for more than 60 seconds.
		// but only if we're a seed
		d = now - std::max(std::max(m_last_unchoke.get(m_connect)
			, m_last_incoming_request.get(m_connect))
			, m_last_sent_payload.get(m_connect));

		if (reading_socket
			&& !m_connecting
			&& m_requests.empty()
			&& m_reading_bytes == 0
			&& !m_choked
			&& m_peer_interested
			&& t && t->is_upload_only()
			&& d > seconds(60)
			&& can_disconnect(errors::timed_out_no_request))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "NO_REQUEST", "waited %d seconds"
				, int(total_seconds(d)));
#endif
			disconnect(errors::timed_out_no_request, operation_t::bittorrent);
			return;
		}

		// if the peer hasn't become interested and we haven't
		// become interested in the peer for 10 minutes, it
		// has also timed out.
		time_duration const d1 = now - m_became_uninterested.get(m_connect);
		time_duration const d2 = now - m_became_uninteresting.get(m_connect);
		time_duration const time_limit = seconds(
			m_settings.get_int(settings_pack::inactivity_timeout));

		// don't bother disconnect peers we haven't been interested
		// in (and that hasn't been interested in us) for a while
		// unless we have used up all our connection slots
		if (reading_socket
			&& !m_interesting
			&& !m_peer_interested
			&& d1 > time_limit
			&& d2 > time_limit
			&& (m_ses.num_connections() >= m_settings.get_int(settings_pack::connections_limit)
				|| (t && t->num_peers() >= t->max_connections()))
			&& can_disconnect(errors::timed_out_no_interest))
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "MUTUAL_NO_INTEREST", "t1: %d t2: %d"
					, int(total_seconds(d1)), int(total_seconds(d2)));
			}
#endif
			disconnect(errors::timed_out_no_interest, operation_t::bittorrent);
			return;
		}

		if (reading_socket
			&& !m_download_queue.empty()
			&& m_quota[download_channel] > 0
			&& now > m_requested.get(m_connect) + seconds(request_timeout()))
		{
			snub_peer();
		}

		// if we haven't sent something in too long, send a keep-alive
		keep_alive();

		// if our download rate isn't increasing significantly anymore, end slow
		// start. The 10kB is to have some slack here.
		// we can't do this when we're choked, because we aren't sending any
		// requests yet, so there hasn't been an opportunity to ramp up the
		// connection yet.
		if (m_slow_start
			&& !m_peer_choked
			&& m_downloaded_last_second > 0
			&& m_downloaded_last_second + 5000
				>= m_statistics.last_payload_downloaded())
		{
			m_slow_start = false;
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "SLOW_START", "exit slow start: "
					"prev-dl: %d dl: %d"
					, int(m_downloaded_last_second)
					, m_statistics.last_payload_downloaded());
			}
#endif
		}
		m_downloaded_last_second = m_statistics.last_payload_downloaded();
		m_uploaded_last_second = m_statistics.last_payload_uploaded();

		m_statistics.second_tick(tick_interval_ms);

		if (m_statistics.upload_payload_rate() > m_upload_rate_peak)
		{
			m_upload_rate_peak = m_statistics.upload_payload_rate();
		}
		if (m_statistics.download_payload_rate() > m_download_rate_peak)
		{
			m_download_rate_peak = m_statistics.download_payload_rate();
		}
		if (is_disconnecting()) return;

		if (!t->ready_for_connections()) return;

		update_desired_queue_size();

		if (m_desired_queue_size == m_max_out_request_queue
			&& t->alerts().should_post<performance_alert>())
		{
			t->alerts().emplace_alert<performance_alert>(t->get_handle()
				, performance_alert::outstanding_request_limit_reached);
		}

		int const piece_timeout = m_settings.get_int(settings_pack::piece_timeout);

		if (!m_download_queue.empty()
			&& m_quota[download_channel] > 0
			&& now - m_last_piece.get(m_connect) > seconds(piece_timeout))
		{
			// this peer isn't sending the pieces we've
			// requested (this has been observed by BitComet)
			// in this case we'll clear our download queue and
			// re-request the blocks.
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "PIECE_REQUEST_TIMED_OUT"
					, "%d time: %d to: %d"
					, int(m_download_queue.size()), int(total_seconds(now - m_last_piece.get(m_connect)))
					, piece_timeout);
			}
#endif

			snub_peer();
		}

		fill_send_buffer();
	}

	void peer_connection::snub_peer()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (!m_snubbed)
		{
			m_snubbed = true;
			m_slow_start = false;
			if (t->alerts().should_post<peer_snubbed_alert>())
			{
				t->alerts().emplace_alert<peer_snubbed_alert>(t->get_handle()
					, m_remote, m_peer_id);
			}
		}
		m_desired_queue_size = 1;

		if (on_parole()) return;

		if (!t->has_picker()) return;
		piece_picker& picker = t->picker();

		// first, if we have any unsent requests, just
		// wipe those out
		while (!m_request_queue.empty())
		{
			t->picker().abort_download(m_request_queue.back().block, peer_info_struct());
			m_request_queue.pop_back();
		}
		m_queued_time_critical = 0;

		TORRENT_ASSERT(!m_download_queue.empty());

		// time out the last request eligible
		// block in the queue
		int i = int(m_download_queue.size()) - 1;
		for (; i >= 0; --i)
		{
			if (!m_download_queue[i].timed_out
				&& !m_download_queue[i].not_wanted)
				break;
		}

		if (i >= 0)
		{
			pending_block& qe = m_download_queue[i];
			piece_block const r = qe.block;

			// only cancel a request if it blocks the piece from being completed
			// (i.e. no free blocks to request from it)
			piece_picker::downloading_piece p;
			picker.piece_info(qe.block.piece_index, p);
			int const free_blocks = picker.blocks_in_piece(qe.block.piece_index)
				- p.finished - p.writing - p.requested;

			// if there are still blocks available for other peers to pick, we're
			// still not holding up the completion of the piece and there's no
			// need to cancel the requests. For more information, see:
			// http://blog.libtorrent.org/2011/11/block-request-time-outs/
			if (free_blocks > 0)
			{
				send_block_requests();
				return;
			}

			if (t->alerts().should_post<block_timeout_alert>())
			{
				t->alerts().emplace_alert<block_timeout_alert>(t->get_handle()
					, remote(), pid(), qe.block.block_index
					, qe.block.piece_index);
			}

			// request a new block before removing the previous
			// one, in order to prevent it from
			// picking the same block again, stalling the
			// same piece indefinitely.
			m_desired_queue_size = 2;
			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::snubbed_piece_picks);

			// the block we just picked (potentially)
			// hasn't been put in m_download_queue yet.
			// it's in m_request_queue and will be sent
			// once send_block_requests() is called.

			m_desired_queue_size = 1;

			qe.timed_out = true;
			picker.abort_download(r, peer_info_struct());
		}

		send_block_requests();
	}

	void peer_connection::fill_send_buffer()
	{
		TORRENT_ASSERT(is_single_thread());
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

#ifndef TORRENT_DISABLE_SHARE_MODE
		bool sent_a_piece = false;
#endif
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || t->is_aborted() || m_requests.empty()) return;

		// only add new piece-chunks if the send buffer is small enough
		// otherwise there will be no end to how large it will be!

		int buffer_size_watermark = int(std::int64_t(m_uploaded_last_second)
			* m_settings.get_int(settings_pack::send_buffer_watermark_factor) / 100);

		if (buffer_size_watermark < m_settings.get_int(settings_pack::send_buffer_low_watermark))
		{
			buffer_size_watermark = m_settings.get_int(settings_pack::send_buffer_low_watermark);
		}
		else if (buffer_size_watermark > m_settings.get_int(settings_pack::send_buffer_watermark))
		{
			buffer_size_watermark = m_settings.get_int(settings_pack::send_buffer_watermark);
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing))
		{
			peer_log(peer_log_alert::outgoing, "SEND_BUFFER_WATERMARK"
				, "current watermark: %d max: %d min: %d factor: %d uploaded: %d B/s"
				, buffer_size_watermark
				, m_ses.settings().get_int(settings_pack::send_buffer_watermark)
				, m_ses.settings().get_int(settings_pack::send_buffer_low_watermark)
				, m_ses.settings().get_int(settings_pack::send_buffer_watermark_factor)
				, int(m_uploaded_last_second));
		}
#endif

		if (t->is_deleted())
		{
			// can this happen here?
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "TORRENT_ABORTED", "");
#endif
			for (peer_request const& r : m_requests)
				write_reject_request(r);
			m_requests.clear();
			return;
		}

		// don't just pop the front element here, since in seed mode one request may
		// be blocked because we have to verify the hash first, so keep going with the
		// next request. However, only let each peer have one hash verification outstanding
		// at any given time
		for (int i = 0; i < int(m_requests.size())
			&& (send_buffer_size() + m_reading_bytes < buffer_size_watermark); ++i)
		{
			TORRENT_ASSERT(t->ready_for_connections());
			peer_request const& r = m_requests[i];

			TORRENT_ASSERT(r.piece >= piece_index_t(0));
			TORRENT_ASSERT(r.piece < piece_index_t(m_have_piece.size()));
			TORRENT_ASSERT(r.start + r.length <= t->torrent_file().piece_size(r.piece));
			TORRENT_ASSERT(r.length > 0);
			TORRENT_ASSERT(r.start >= 0);

			bool const seed_mode = t->seed_mode();

			if (seed_mode
				&& !t->verified_piece(r.piece)
				&& !m_settings.get_bool(settings_pack::disable_hash_checks))
			{
				// we're still verifying the hash of this piece
				// so we can't return it yet.
				if (t->verifying_piece(r.piece)) continue;

				// only have three outstanding hash check per peer
				if (m_outstanding_piece_verification >= 3) continue;

				++m_outstanding_piece_verification;

#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "SEED_MODE_FILE_ASYNC_HASH"
					, "piece: %d", static_cast<int>(r.piece));
#endif
				// this means we're in seed mode and we haven't yet
				// verified this piece (r.piece)
				disk_job_flags_t flags;
				if (t->info_hash().has_v1())
					flags |= disk_interface::v1_hash;
				aux::vector<sha256_hash> hashes;
				if (t->info_hash().has_v2())
					hashes.resize(t->torrent_file().orig_files().blocks_in_piece2(r.piece));

				span<sha256_hash> v2_hashes(hashes);
				m_disk_thread.async_hash(t->storage(), r.piece, v2_hashes, flags
					, [conn = self(), h2 = std::move(hashes)]
					(piece_index_t p, sha1_hash const& ph, storage_error const& e)
				{ conn->wrap(&peer_connection::on_seed_mode_hashed, p, ph, h2, e); });

				t->verifying(r.piece);
				continue;
			}

			if (!t->has_piece_passed(r.piece) && !seed_mode)
			{
#ifndef TORRENT_DISABLE_PREDICTIVE_PIECES
				// we don't have this piece yet, but we anticipate to have
				// it very soon, so we have told our peers we have it.
				// hold off on sending it. If the piece fails later
				// we will reject this request
				if (t->is_predictive_piece(r.piece)) continue;
#endif
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "PIECE_FAILED"
					, "piece: %d s: %x l: %x piece failed hash check"
					, static_cast<int>(r.piece), r.start , r.length);
#endif
				write_reject_request(r);
			}
			else
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "FILE_ASYNC_READ"
					, "piece: %d s: %x l: %x", static_cast<int>(r.piece), r.start, r.length);
#endif
				m_reading_bytes += r.length;
#ifndef TORRENT_DISABLE_SHARE_MODE
				sent_a_piece = true;
#endif

				// the callback function may be called immediately, instead of being posted

				TORRENT_ASSERT(t->valid_metadata());
				TORRENT_ASSERT(r.piece >= piece_index_t(0));
				TORRENT_ASSERT(r.piece < t->torrent_file().end_piece());

				m_disk_thread.async_read(t->storage(), r
					, [conn = self(), r](disk_buffer_holder buf, storage_error const& ec)
					{ conn->wrap(&peer_connection::on_disk_read_complete, std::move(buf), ec, r, clock_type::now()); });
			}
			m_last_sent_payload.set(m_connect, clock_type::now());
			m_requests.erase(m_requests.begin() + i);

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests, -1);

			--i;
		}
		m_ses.deferred_submit_jobs();

#ifndef TORRENT_DISABLE_SHARE_MODE
		if (t->share_mode() && sent_a_piece)
			t->recalc_share_mode();
#endif
	}

	// this is called when a previously unchecked piece has been
	// checked, while in seed-mode
	void peer_connection::on_seed_mode_hashed(piece_index_t const piece
		, sha1_hash const& piece_hash, aux::vector<sha256_hash> const& block_hashes
		, storage_error const& error)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();

		TORRENT_ASSERT(m_outstanding_piece_verification > 0);
		--m_outstanding_piece_verification;

		if (!t || t->is_aborted()) return;

		if (error)
		{
			t->handle_disk_error("hash", error, this);
			t->leave_seed_mode(torrent::seed_mode_t::check_files);
			return;
		}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif
		aux::array<boost::tribool, num_protocols, protocol_version>
			hash_failed{ { boost::indeterminate, boost::indeterminate } };
#ifdef __clang__
#pragma clang diagnostic pop
#endif

		// we're using the piece hashes here, we need the torrent to be loaded
		if (!m_settings.get_bool(settings_pack::disable_hash_checks)
			&& t->info_hash().has_v1())
		{
			hash_failed[protocol_version::V1] = piece_hash != t->torrent_file().hash_for_piece(piece);
		}

		if (!m_settings.get_bool(settings_pack::disable_hash_checks)
			&& t->info_hash().has_v2())
		{
			hash_failed[protocol_version::V2] = false;

			int const blocks_in_piece = t->torrent_file().files().blocks_in_piece2(piece);

			TORRENT_ASSERT(blocks_in_piece == int(block_hashes.size()));

			t->need_hash_picker();
			auto picker = t->get_hash_picker();
			set_block_hash_result result = set_block_hash_result::unknown();
			for (int i = 0; i < blocks_in_piece; ++i)
			{
				result = picker.set_block_hash(piece, i * default_block_size, block_hashes[i]);
				if (result.status == set_block_hash_result::result::block_hash_failed
					|| result.status == set_block_hash_result::result::piece_hash_failed)
				{
					hash_failed[protocol_version::V2] = true;
				}
			}

			// if the last block still couldn't be verified
			// it means we don't know the piece's root hash
			// we must leave seed mode
			if (result.status == set_block_hash_result::result::unknown)
				hash_failed[protocol_version::V1] = hash_failed[protocol_version::V2] = true;
		}

		if ((hash_failed[protocol_version::V1] && !hash_failed[protocol_version::V2])
			|| (!hash_failed[protocol_version::V1] && hash_failed[protocol_version::V2]))
		{
			t->set_error(errors::torrent_inconsistent_hashes, torrent_status::error_file_none);
			t->pause();
			return;
		}

		if (hash_failed[protocol_version::V1] || hash_failed[protocol_version::V2])
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SEED_MODE_FILE_HASH"
				, "piece: %d failed", static_cast<int>(piece));
#endif

			t->leave_seed_mode(torrent::seed_mode_t::check_files);
		}
		else
		{
			if (t->seed_mode())
			{
				TORRENT_ASSERT(t->verifying_piece(piece));
				t->verified(piece);
			}

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SEED_MODE_FILE_HASH"
				, "piece: %d passed", static_cast<int>(piece));
#endif
			if (t->seed_mode() && t->all_verified())
				t->leave_seed_mode(torrent::seed_mode_t::skip_checking);
		}

		// try to service the requests again, now that the piece
		// has been verified
		fill_send_buffer();
	}

		// this is for a future per-block request feature
#if 0
	void peer_connection::on_hash2_complete(storage_error const& error
		, peer_request const& r, sha256_hash const& hash)
	{
		auto t = associated_torrent().lock();
		if (!t) return;

		t->picker().completed_hash_job(r.piece);

		t->need_hash_picker();
		auto result = t->get_hash_picker().set_block_hash(r.piece, r.start, hash);

		switch (result.status)
		{
		case set_block_hash_result::block_hash_failed:
			// If the hash failed immediately at the leaf layer it means that
			// the chuck hash is known so this peer definately sent bad data.
			t->piece_failed(r.piece, std::vector<int>{r.start / default_block_size});
			TORRENT_ASSERT(m_disconnecting);
			return;
		case set_block_hash_result::piece_hash_failed:
			t->verify_block_hashes(r.piece);
			break;
		case set_block_hash_result::success:
		{
			t->need_picker();
			int const blocks_per_piece = t->torrent_file().files().piece_length() / default_block_size;
			for (piece_index_t verified_piece = int(r.piece) + result.first_verified_block / blocks_per_piece
				, end = int(verified_piece) + (result.num_verified + blocks_per_piece - 1) / blocks_per_piece
				; verified_piece < end; ++verified_piece)
			{
				if (!t->picker().is_piece_finished(verified_piece)
					|| !t->get_hash_picker().piece_verified(verified_piece)
					|| t->picker().is_hashing(verified_piece))
					continue;
				t->piece_passed(verified_piece);
			}
			break;
		}
		case set_block_hash_result::unknown:break;
		default:
			TORRENT_ASSERT_FAIL();
			break;
		}
	}
#endif

	void peer_connection::on_disk_read_complete(disk_buffer_holder buffer
		, storage_error const& error
		, peer_request const& r, time_point const issue_time)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(r.length >= 0);

		// return value:
		// 0: success, piece passed hash check
		// -1: disk failure

		int const disk_rtt = int(total_microseconds(clock_type::now() - issue_time));

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "FILE_ASYNC_READ_COMPLETE"
				, "piece: %d s: %x l: %x b: %p e: %s rtt: %d us"
				, static_cast<int>(r.piece), r.start, r.length
				, static_cast<void*>(buffer.data())
				, error.ec.message().c_str(), disk_rtt);
		}
#endif

		m_reading_bytes -= r.length;

		std::shared_ptr<torrent> t = m_torrent.lock();
		if (error)
		{
			if (!t)
			{
				disconnect(error.ec, operation_t::file_read);
				return;
			}

			write_dont_have(r.piece);
			write_reject_request(r);
			if (t->alerts().should_post<file_error_alert>())
				t->alerts().emplace_alert<file_error_alert>(error.ec
					, t->resolve_filename(error.file())
					, error.operation, t->get_handle());

			++m_disk_read_failures;
			if (m_disk_read_failures > 100) disconnect(error.ec, operation_t::file_read);
			return;
		}

		// we're only interested in failures in a row.
		// if we every now and then successfully send a
		// block, the peer is still useful
		m_disk_read_failures = 0;

		if (t && m_settings.get_int(settings_pack::suggest_mode)
			== settings_pack::suggest_read_cache)
		{
			// tell the torrent that we just read a block from this piece.
			// if this piece is low-availability, it's now a candidate for being
			// suggested to other peers
			t->add_suggest_piece(r.piece);
		}

		if (m_disconnecting) return;

		if (!t)
		{
			disconnect(error.ec, operation_t::file_read);
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message
			, "PIECE", "piece: %d s: %x l: %x"
			, static_cast<int>(r.piece), r.start, r.length);
#endif

		m_counters.blend_stats_counter(counters::request_latency, disk_rtt, 5);

		// we probably just pulled this piece into the cache.
		// if it's rare enough to make it into the suggested piece
		// push another piece out
		if (m_settings.get_int(settings_pack::suggest_mode) == settings_pack::suggest_read_cache)
		{
			t->add_suggest_piece(r.piece);
		}
		write_piece(r, std::move(buffer));
	}

	void peer_connection::assign_bandwidth(int const channel, int const amount)
	{
		TORRENT_ASSERT(is_single_thread());
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(channel == upload_channel
			? peer_log_alert::outgoing : peer_log_alert::incoming
			, "ASSIGN_BANDWIDTH", "bytes: %d", amount);
#endif

		TORRENT_ASSERT(amount > 0 || is_disconnecting());
		m_quota[channel] += amount;
		TORRENT_ASSERT(m_channel_state[channel] & peer_info::bw_limit);
		m_channel_state[channel] &= ~peer_info::bw_limit;

#if TORRENT_USE_INVARIANT_CHECKS
		check_invariant();
#endif

		if (is_disconnecting()) return;
		if (channel == upload_channel)
		{
			setup_send();
		}
		else if (channel == download_channel)
		{
			setup_receive();
		}
	}

	// the number of bytes we expect to receive, or want to send
	// channel either refer to upload or download. This is used
	// by the rate limiter to allocate quota for this peer
	int peer_connection::wanted_transfer(int const channel)
	{
		TORRENT_ASSERT(is_single_thread());

		const int tick_interval = std::max(1, m_settings.get_int(settings_pack::tick_interval));

		if (channel == download_channel)
		{
			std::int64_t const download_rate = std::int64_t(m_statistics.download_rate()) * 3 / 2;
			return std::max({m_outstanding_bytes + 30
				, m_recv_buffer.packet_bytes_remaining() + 30
				, int(download_rate * tick_interval / 1000)});
		}
		else
		{
			std::int64_t const upload_rate = std::int64_t(m_statistics.upload_rate()) * 2;
			return std::max({m_reading_bytes
				, m_send_buffer.size()
				, int(upload_rate * tick_interval / 1000)});
		}
	}

	int peer_connection::request_bandwidth(int const channel, int bytes)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		// we can only have one outstanding bandwidth request at a time
		if (m_channel_state[channel] & peer_info::bw_limit) return 0;

		std::shared_ptr<torrent> t = m_torrent.lock();

		bytes = std::max(wanted_transfer(channel), bytes);

		// we already have enough quota
		if (m_quota[channel] >= bytes) return 0;

		// deduct the bytes we already have quota for
		bytes -= m_quota[channel];

		int const priority = get_priority(channel);

		int const max_channels = num_classes() + (t ? t->num_classes() : 0) + 2;
		TORRENT_ALLOCA(channels, aux::bandwidth_channel*, max_channels);

		// collect the pointers to all bandwidth channels
		// that apply to this torrent
		int c = 0;

		c += m_ses.copy_pertinent_channels(*this, channel
			, channels.subspan(c).data(), max_channels - c);
		if (t)
		{
			c += m_ses.copy_pertinent_channels(*t, channel
				, channels.subspan(c).data(), max_channels - c);
		}

#if TORRENT_USE_ASSERTS
		// make sure we don't have duplicates
		std::set<aux::bandwidth_channel*> unique_classes;
		for (int i = 0; i < c; ++i)
		{
			TORRENT_ASSERT(unique_classes.count(channels[i]) == 0);
			unique_classes.insert(channels[i]);
		}
#endif

		TORRENT_ASSERT(!(m_channel_state[channel] & peer_info::bw_limit));

		aux::bandwidth_manager* manager = m_ses.get_bandwidth_manager(channel);

		int const ret = manager->request_bandwidth(self()
			, bytes, priority, channels.data(), c);

		if (ret == 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			auto const dir = channel == download_channel ? peer_log_alert::incoming
				: peer_log_alert::outgoing;
			if (should_log(dir))
			{
				peer_log(dir,
					"REQUEST_BANDWIDTH", "bytes: %d quota: %d wanted_transfer: %d "
					"prio: %d num_channels: %d", bytes, m_quota[channel]
					, wanted_transfer(channel), priority, c);
			}
#endif
			m_channel_state[channel] |= peer_info::bw_limit;
		}
		else
		{
			m_quota[channel] += ret;
		}

		return ret;
	}

	void peer_connection::setup_send()
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_disconnecting || m_send_buffer.empty()) return;

		// we may want to request more quota at this point
		request_bandwidth(upload_channel);

		// if we already have an outstanding send operation, don't issue another
		// one, instead accrue more send buffer to coalesce for the next write
		if (m_channel_state[upload_channel] & peer_info::bw_network)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing, "CORKED_WRITE", "bytes: %d"
				, m_send_buffer.size());
#endif
			return;
		}

		if (m_send_barrier == 0)
		{
			std::vector<span<char>> vec;
			// limit outgoing crypto messages to 1MB
			int const send_bytes = std::min(m_send_buffer.size(), 1024 * 1024);
			m_send_buffer.build_mutable_iovec(send_bytes, vec);
			int next_barrier;
			span<span<char const>> inject_vec;
			std::tie(next_barrier, inject_vec) = hit_send_barrier(vec);
			for (auto i = inject_vec.rbegin(); i != inject_vec.rend(); ++i)
			{
				// this const_cast is a here because chained_buffer need to be
				// fixed.
				auto* ptr = const_cast<char*>(i->data());
				m_send_buffer.prepend_buffer(span<char>(ptr, i->size())
					, static_cast<int>(i->size()));
			}
			set_send_barrier(next_barrier);
		}

		if ((m_quota[upload_channel] == 0 || m_send_barrier == 0)
			&& !m_send_buffer.empty()
			&& !m_connecting)
		{
			return;
		}

		int const quota_left = m_quota[upload_channel];
		if (m_send_buffer.empty()
			&& m_reading_bytes > 0
			&& quota_left > 0)
		{
			if (!(m_channel_state[upload_channel] & peer_info::bw_disk))
				m_counters.inc_stats_counter(counters::num_peers_up_disk);
			m_channel_state[upload_channel] |= peer_info::bw_disk;
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing, "WAITING_FOR_DISK", "outstanding: %d"
				, m_reading_bytes);
#endif

			if (!m_connecting
				&& !m_requests.empty()
				&& m_reading_bytes > m_settings.get_int(settings_pack::send_buffer_watermark) - 0x4000)
			{
				std::shared_ptr<torrent> t = m_torrent.lock();

				// we're stalled on the disk. We want to write and we can write
				// but our send buffer is empty, waiting to be refilled from the disk
				// this either means the disk is slower than the network connection
				// or that our send buffer watermark is too small, because we can
				// send it all before the disk gets back to us. That's why we only
				// trigger this if we've also filled the allowed send buffer. The
				// first request would not fill it all the way up because of the
				// upload rate being virtually 0. If m_requests is empty, it doesn't
				// matter anyway, because we don't have any more requests from the
				// peer to hang on to the disk
				if (t && t->alerts().should_post<performance_alert>())
				{
					t->alerts().emplace_alert<performance_alert>(t->get_handle()
						, performance_alert::send_buffer_watermark_too_low);
				}
			}
		}
		else
		{
			if (m_channel_state[upload_channel] & peer_info::bw_disk)
				m_counters.inc_stats_counter(counters::num_peers_up_disk, -1);
			m_channel_state[upload_channel] &= ~peer_info::bw_disk;
		}

		if (!can_write())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::outgoing))
			{
				if (m_send_buffer.empty())
				{
					peer_log(peer_log_alert::outgoing, "SEND_BUFFER_DEPLETED"
						, "quota: %d buf: %d connecting: %s disconnecting: %s "
						"pending_disk: %d piece-requests: %d"
						, m_quota[upload_channel]
						, m_send_buffer.size(), m_connecting?"yes":"no"
						, m_disconnecting?"yes":"no", m_reading_bytes
						, int(m_requests.size()));
				}
				else
				{
					peer_log(peer_log_alert::outgoing, "CANNOT_WRITE"
						, "quota: %d buf: %d connecting: %s disconnecting: %s "
						"pending_disk: %d"
						, m_quota[upload_channel]
						, m_send_buffer.size(), m_connecting?"yes":"no"
						, m_disconnecting?"yes":"no", m_reading_bytes);
				}
			}
#endif
			return;
		}

		int const amount_to_send = std::min({
			m_send_buffer.size()
			, quota_left
			, m_send_barrier});

		TORRENT_ASSERT(amount_to_send > 0);

		TORRENT_ASSERT(!(m_channel_state[upload_channel] & peer_info::bw_network));
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "ASYNC_WRITE", "bytes: %d", amount_to_send);
#endif
		auto const vec = m_send_buffer.build_iovec(amount_to_send);
		ADD_OUTSTANDING_ASYNC("peer_connection::on_send_data");

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(!m_socket_is_writing);
		m_socket_is_writing = true;
#endif

		using write_handler_type = aux::handler<
			peer_connection
			, decltype(&peer_connection::on_send_data)
			, &peer_connection::on_send_data
			, &peer_connection::on_error
			, &peer_connection::on_exception
			, decltype(m_write_handler_storage)
			, &peer_connection::m_write_handler_storage
			>;
		static_assert(sizeof(write_handler_type) == sizeof(std::shared_ptr<peer_connection>)
			, "write handler does not have the expected size");
		m_socket.async_write_some(vec, write_handler_type(self()));

		m_channel_state[upload_channel] |= peer_info::bw_network;
		m_last_sent.set(m_connect, aux::time_now());
	}

	void peer_connection::on_disk()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!(m_channel_state[download_channel] & peer_info::bw_disk)) return;
		std::shared_ptr<peer_connection> me(self());

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "DISK", "dropped below disk buffer watermark");
#endif
		m_counters.inc_stats_counter(counters::num_peers_down_disk, -1);
		m_channel_state[download_channel] &= ~peer_info::bw_disk;
		setup_receive();
	}

	void peer_connection::setup_receive()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_disconnecting) return;

		if (m_recv_buffer.capacity() < 100
			&& m_recv_buffer.max_receive() == 0)
		{
			m_recv_buffer.reserve(100);
		}

		// we may want to request more quota at this point
		int const buffer_size = m_recv_buffer.max_receive();
		request_bandwidth(download_channel, buffer_size);

		if (m_channel_state[download_channel] & peer_info::bw_network) return;

		if (m_quota[download_channel] == 0
			&& !m_connecting)
		{
			return;
		}

		if (!can_read())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::incoming))
			{
				peer_log(peer_log_alert::incoming, "CANNOT_READ", "quota: %d  "
					"can-write-to-disk: %s queue-limit: %d disconnecting: %s "
					" connecting: %s"
					, m_quota[download_channel]
					, ((m_channel_state[download_channel] & peer_info::bw_disk)?"no":"yes")
					, m_settings.get_int(settings_pack::max_queued_disk_bytes)
					, (m_disconnecting?"yes":"no")
					, (m_connecting?"yes":"no"));
			}
#endif
			// if we block reading, waiting for the disk, we will wake up
			// by the disk_io_thread posting a message every time it drops
			// from being at or exceeding the limit down to below the limit
			return;
		}
		TORRENT_ASSERT(m_connected);
		if (m_quota[download_channel] == 0) return;

		int const quota_left = m_quota[download_channel];
		int const max_receive = std::min(buffer_size, quota_left);

		if (max_receive == 0) return;

		span<char> const vec = m_recv_buffer.reserve(max_receive);
		TORRENT_ASSERT(!(m_channel_state[download_channel] & peer_info::bw_network));
		m_channel_state[download_channel] |= peer_info::bw_network;
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming, "ASYNC_READ"
			, "max: %d bytes", max_receive);
#endif

		ADD_OUTSTANDING_ASYNC("peer_connection::on_receive_data");

		using read_handler_type = aux::handler<
			peer_connection
			, decltype(&peer_connection::on_receive_data)
			, &peer_connection::on_receive_data
			, &peer_connection::on_error
			, &peer_connection::on_exception
			, decltype(m_read_handler_storage)
			, &peer_connection::m_read_handler_storage
			>;
		static_assert(sizeof(read_handler_type) == sizeof(std::shared_ptr<peer_connection>)
			, "read handler does not have the expected size");
		m_socket.async_read_some(boost::asio::buffer(vec.data(), std::size_t(vec.size())), read_handler_type(self()));
	}

	piece_block_progress peer_connection::downloading_piece_progress() const
	{
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "ERROR"
			, "downloading_piece_progress() dispatched to the base class!");
#endif
		return {};
	}

	void peer_connection::send_buffer(span<char const> buf)
	{
		TORRENT_ASSERT(is_single_thread());

		int const free_space = std::min(
			m_send_buffer.space_in_last_buffer(), int(buf.size()));
		if (free_space > 0)
		{
			char* dst = m_send_buffer.append(buf.first(free_space));

			// this should always succeed, because we checked how much space
			// there was up-front
			TORRENT_UNUSED(dst);
			TORRENT_ASSERT(dst != nullptr);
			buf = buf.subspan(free_space);
		}
		if (buf.empty()) return;

		// allocate a buffer and initialize the beginning of it with 'buf'
		aux::buffer snd_buf(std::max(int(buf.size()), 128), buf);
		m_send_buffer.append_buffer(std::move(snd_buf), int(buf.size()));

		setup_send();
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	void peer_connection::account_received_bytes(int const bytes_transferred)
	{
		// tell the receive buffer we just fed it this many bytes of incoming data
		TORRENT_ASSERT(bytes_transferred > 0);
		m_recv_buffer.received(bytes_transferred);

		// update the dl quota
		TORRENT_ASSERT(bytes_transferred <= m_quota[download_channel]);
		m_quota[download_channel] -= bytes_transferred;

		// account receiver buffer size stats to the session
		m_ses.received_buffer(bytes_transferred);

		// estimate transport protocol overhead
		trancieve_ip_packet(bytes_transferred, aux::is_v6(m_remote));

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming, "READ"
			, "%d bytes", bytes_transferred);
#endif
	}

	void peer_connection::on_receive_data(error_code const& error
		, std::size_t bytes_transferred)
	{
		TORRENT_ASSERT(is_single_thread());
		COMPLETE_ASYNC("peer_connection::on_receive_data");

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::incoming))
		{
			peer_log(peer_log_alert::incoming, "ON_RECEIVE_DATA"
				, "bytes: %d %s"
				, int(bytes_transferred), print_error(error).c_str());
		}
#endif

		// leave this bit set until we're done looping, reading from the socket.
		// that way we don't trigger any async read calls until the end of this
		// function.
		TORRENT_ASSERT(m_channel_state[download_channel] & peer_info::bw_network);

		TORRENT_ASSERT(bytes_transferred > 0 || error);

		m_counters.inc_stats_counter(counters::on_read_counter);

		INVARIANT_CHECK;

		if (error)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "ERROR"
					, "in peer_connection::on_receive_data_impl %s"
					, print_error(error).c_str());
			}
#endif
			on_receive(error, bytes_transferred);
			disconnect(error, operation_t::sock_read);
			return;
		}

		m_last_receive.set(m_connect, aux::time_now());

		// submit all disk jobs later
		m_ses.deferred_submit_jobs();

		// keep ourselves alive in until this function exits in
		// case we disconnect
		// this needs to be created before the invariant check,
		// to keep the object alive through the exit check
		std::shared_ptr<peer_connection> me(self());

		TORRENT_ASSERT(bytes_transferred > 0);

		// flush the send buffer at the end of this function
		cork _c(*this);

		// if we received exactly as many bytes as we provided a receive buffer
		// for. There most likely are more bytes to read, and we should grow our
		// receive buffer.
		TORRENT_ASSERT(int(bytes_transferred) <= m_recv_buffer.max_receive());
		bool const grow_buffer = (int(bytes_transferred) == m_recv_buffer.max_receive());
		account_received_bytes(int(bytes_transferred));

		if (m_extension_outstanding_bytes > 0)
			m_extension_outstanding_bytes -= std::min(m_extension_outstanding_bytes, int(bytes_transferred));

		check_graceful_pause();
		if (m_disconnecting) return;

		// this is the case where we try to grow the receive buffer and try to
		// drain the socket
		if (grow_buffer)
		{
			error_code ec;
			int buffer_size = int(m_socket.available(ec));
			if (ec)
			{
				disconnect(ec, operation_t::available);
				return;
			}

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "AVAILABLE"
				, "%d bytes", buffer_size);
#endif

			request_bandwidth(download_channel, buffer_size);

			int const quota_left = m_quota[download_channel];
			if (buffer_size > quota_left) buffer_size = quota_left;
			if (buffer_size > 0)
			{
				span<char> const vec = m_recv_buffer.reserve(buffer_size);
				std::size_t const bytes = m_socket.read_some(
					boost::asio::mutable_buffer(vec.data(), std::size_t(vec.size())), ec);

				// this is weird. You would imagine read_some() would do this
				if (bytes == 0 && !ec) ec = boost::asio::error::eof;

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log(peer_log_alert::incoming))
				{
					peer_log(peer_log_alert::incoming, "SYNC_READ", "max: %d ret: %d e: %s"
						, buffer_size, int(bytes), ec ? ec.message().c_str() : "");
				}
#endif

				TORRENT_ASSERT(bytes > 0 || ec);
				if (ec)
				{
					if (ec != boost::asio::error::would_block
						&& ec != boost::asio::error::try_again)
					{
						disconnect(ec, operation_t::sock_read);
						return;
					}
				}
				else
				{
					account_received_bytes(int(bytes));
					bytes_transferred += bytes;
				}
			}
		}

		// feed bytes in receive buffer to upper layer by calling on_receive()

		bool const prev_choked = m_peer_choked;
		int bytes = int(bytes_transferred);
		int sub_transferred = 0;
		do {
			sub_transferred = m_recv_buffer.advance_pos(bytes);
			TORRENT_ASSERT(sub_transferred > 0);
			on_receive(error, std::size_t(sub_transferred));
			bytes -= sub_transferred;
			if (m_disconnecting) return;
		} while (bytes > 0 && sub_transferred > 0);

		// if the peer went from unchoked to choked, suggest to the receive
		// buffer that it shrinks to 100 bytes
		int const force_shrink = (m_peer_choked && !prev_choked)
			? 100 : 0;
		m_recv_buffer.normalize(force_shrink);

		if (m_recv_buffer.max_receive() == 0)
		{
			// the message we're receiving is larger than our receive
			// buffer, we must grow.
			int const buffer_size_limit
				= m_settings.get_int(settings_pack::max_peer_recv_buffer_size);
			m_recv_buffer.grow(buffer_size_limit);
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "GROW_BUFFER", "%d bytes"
				, m_recv_buffer.capacity());
#endif
		}

		TORRENT_ASSERT(m_recv_buffer.pos_at_end());
		TORRENT_ASSERT(m_recv_buffer.packet_size() > 0);

		if (is_seed())
		{
			std::shared_ptr<torrent> t = m_torrent.lock();
			if (t) t->seen_complete();
		}

		// allow reading from the socket again
		TORRENT_ASSERT(m_channel_state[download_channel] & peer_info::bw_network);
		m_channel_state[download_channel] &= ~peer_info::bw_network;

		setup_receive();
	}

	bool peer_connection::can_write() const
	{
		TORRENT_ASSERT(is_single_thread());
		// if we have requests or pending data to be sent or announcements to be made
		// we want to send data
		return !m_send_buffer.empty()
			&& m_quota[upload_channel] > 0
			&& (m_send_barrier > 0)
			&& !m_connecting;
	}

	bool peer_connection::can_read()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = m_torrent.lock();

		bool bw_limit = m_quota[download_channel] > 0;

		if (!bw_limit) return false;

		if (m_outstanding_bytes > 0)
		{
			// if we're expecting to download piece data, we might not
			// want to read from the socket in case we're out of disk
			// cache space right now

			if (m_channel_state[download_channel] & peer_info::bw_disk) return false;
		}

		return !m_connecting && !m_disconnecting;
	}

	void peer_connection::on_connection_complete(error_code const& e)
	{
		TORRENT_ASSERT(is_single_thread());
		COMPLETE_ASYNC("peer_connection::on_connection_complete");

		INVARIANT_CHECK;

		// if t is nullptr, we better not be connecting, since
		// we can't decrement the connecting counter
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t || !m_connecting);
		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t) t->dec_num_connecting(m_peer_info);
			m_connecting = false;
		}

		if (m_disconnecting) return;

		if (e)
		{
			connect_failed(e);
			return;
		}

		TORRENT_ASSERT(!m_connected);
		m_connected = true;
		m_counters.inc_stats_counter(counters::num_peers_connected);

		if (m_disconnecting) return;
		m_last_receive.set(m_connect, aux::time_now());

		error_code ec;
		m_local = m_socket.local_endpoint(ec);
		if (ec)
		{
			disconnect(ec, operation_t::getname);
			return;
		}

		// if there are outgoing interfaces specified, verify this
		// peer is correctly bound to one of them
		if (!m_settings.get_str(settings_pack::outgoing_interfaces).empty())
		{
			if (!m_ses.verify_bound_address(m_local.address()
				, is_utp(m_socket), ec))
			{
				if (ec)
				{
					disconnect(ec, operation_t::get_interface);
					return;
				}
				disconnect(error_code(
					boost::system::errc::no_such_device, generic_category())
					, operation_t::connect);
				return;
			}
		}

		if (is_utp(m_socket) && m_peer_info)
		{
			m_peer_info->confirmed_supports_utp = true;
			m_peer_info->supports_utp = false;
		}

		// this means the connection just succeeded

		received_synack(aux::is_v6(m_remote));

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing))
		{
			peer_log(peer_log_alert::outgoing, "COMPLETED"
				, "ep: %s", print_endpoint(m_remote).c_str());
		}
#endif

		// set the socket to non-blocking, so that we can
		// read the entire buffer on each read event we get
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "SET_NON_BLOCKING");
#endif
		m_socket.non_blocking(true, ec);
		if (ec)
		{
			disconnect(ec, operation_t::iocontrol);
			return;
		}

		if (m_remote == m_socket.local_endpoint(ec))
		{
			disconnect(errors::self_connection, operation_t::bittorrent, failure);
			return;
		}

		if (aux::is_v4(m_remote) && m_settings.get_int(settings_pack::peer_tos) != 0)
		{
			error_code err;
			m_socket.set_option(type_of_service(char(m_settings.get_int(settings_pack::peer_tos))), err);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::outgoing))
			{
				peer_log(peer_log_alert::outgoing, "SET_TOS", "tos: %d e: %s"
					, m_settings.get_int(settings_pack::peer_tos), err.message().c_str());
			}
#endif
		}
#if defined IPV6_TCLASS
		else if (aux::is_v6(m_remote) && m_settings.get_int(settings_pack::peer_tos) != 0)
		{
			error_code err;
			m_socket.set_option(traffic_class(char(m_settings.get_int(settings_pack::peer_tos))), err);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::outgoing))
			{
				peer_log(peer_log_alert::outgoing, "SET_TOS", "tos: %d e: %s"
					, m_settings.get_int(settings_pack::peer_tos), err.message().c_str());
			}
#endif
		}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& ext : m_extensions)
		{
			ext->on_connected();
		}
#endif

		on_connected();
		setup_send();
		setup_receive();
	}

	// --------------------------
	// SEND DATA
	// --------------------------

	void peer_connection::on_send_data(error_code const& error
		, std::size_t const bytes_transferred)
	{
		TORRENT_ASSERT(is_single_thread());
		m_counters.inc_stats_counter(counters::on_write_counter);
		m_ses.sent_buffer(int(bytes_transferred));

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_socket_is_writing);
		m_socket_is_writing = false;
#endif

		// submit all disk jobs when we've processed all messages
		// in the current message queue
		m_ses.deferred_submit_jobs();

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "ON_SEND_DATA", "bytes: %d %s"
				, int(bytes_transferred), print_error(error).c_str());
		}
#endif

		INVARIANT_CHECK;

		COMPLETE_ASYNC("peer_connection::on_send_data");
		// keep ourselves alive in until this function exits in
		// case we disconnect
		std::shared_ptr<peer_connection> me(self());

		TORRENT_ASSERT(m_channel_state[upload_channel] & peer_info::bw_network);

		m_send_buffer.pop_front(int(bytes_transferred));

		time_point const now = clock_type::now();

		for (auto& block : m_download_queue)
		{
			if (block.send_buffer_offset == pending_block::not_in_buffer)
				continue;
			if (block.send_buffer_offset < int(bytes_transferred))
				block.send_buffer_offset = pending_block::not_in_buffer;
			else
				block.send_buffer_offset -= int(bytes_transferred);
		}

		m_channel_state[upload_channel] &= ~peer_info::bw_network;

		TORRENT_ASSERT(int(bytes_transferred) <= m_quota[upload_channel]);
		m_quota[upload_channel] -= int(bytes_transferred);

		trancieve_ip_packet(int(bytes_transferred), aux::is_v6(m_remote));

		if (m_send_barrier != INT_MAX)
			m_send_barrier -= int(bytes_transferred);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "WROTE"
			, "%d bytes", int(bytes_transferred));
#endif

		if (error)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "ERROR"
					, "%s in peer_connection::on_send_data", error.message().c_str());
			}
#endif
			disconnect(error, operation_t::sock_write);
			return;
		}
		if (m_disconnecting)
		{
			// make sure we free up all send buffers that are owned
			// by the disk thread
			m_send_buffer.clear();
			return;
		}

		TORRENT_ASSERT(!m_connecting);
		TORRENT_ASSERT(bytes_transferred > 0);

		m_last_sent.set(m_connect, now);

#if TORRENT_USE_ASSERTS
		std::int64_t const cur_payload_ul = m_statistics.last_payload_uploaded();
		std::int64_t const cur_protocol_ul = m_statistics.last_protocol_uploaded();
#endif
		on_sent(error, bytes_transferred);
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_statistics.last_payload_uploaded() - cur_payload_ul >= 0);
		TORRENT_ASSERT(m_statistics.last_protocol_uploaded() - cur_protocol_ul >= 0);
		std::int64_t stats_diff = m_statistics.last_payload_uploaded() - cur_payload_ul
			+ m_statistics.last_protocol_uploaded() - cur_protocol_ul;
		TORRENT_ASSERT(stats_diff == int(bytes_transferred));
#endif

		fill_send_buffer();

		setup_send();
	}

#if TORRENT_USE_INVARIANT_CHECKS
	struct peer_count_t
	{
		peer_count_t(): num_peers(0), num_peers_with_timeouts(0), num_peers_with_nowant(0), num_not_requested(0) {}
		int num_peers;
		int num_peers_with_timeouts;
		int num_peers_with_nowant;
		int num_not_requested;
//		std::vector<peer_connection const*> peers;
	};

	void peer_connection::check_invariant() const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_in_use == 1337);
		TORRENT_ASSERT(m_queued_time_critical <= int(m_request_queue.size()));
		TORRENT_ASSERT(m_accept_fast.size() == m_accept_fast_piece_cnt.size());

		m_recv_buffer.check_invariant();

		for (int i = 0; i < 2; ++i)
		{
			if (m_channel_state[i] & peer_info::bw_limit)
			{
				// if we're waiting for bandwidth, we should be in the
				// bandwidth manager's queue
				TORRENT_ASSERT(m_ses.get_bandwidth_manager(i)->is_queued(this));
			}
		}

		std::shared_ptr<torrent> t = m_torrent.lock();

#if TORRENT_USE_INVARIANT_CHECKS \
	&& !defined TORRENT_NO_EXPENSIVE_INVARIANT_CHECK
		if (t && t->has_picker() && !m_disconnecting)
			t->picker().check_peer_invariant(m_have_piece, peer_info_struct());
#endif

		if (!m_disconnect_started && m_initialized)
		{
			// none of this matters if we're disconnecting anyway
			if (t->is_finished())
				TORRENT_ASSERT(!is_interesting() || m_need_interest_update);
			if (is_seed())
				TORRENT_ASSERT(upload_only());
		}

		if (m_disconnecting)
		{
			TORRENT_ASSERT(m_download_queue.empty());
			TORRENT_ASSERT(m_request_queue.empty());
			TORRENT_ASSERT(m_disconnect_started);
		}

		TORRENT_ASSERT(m_outstanding_bytes >= 0);
		if (t && t->valid_metadata() && !m_disconnecting)
		{
			torrent_info const& ti = t->torrent_file();
			// if the piece is fully downloaded, we might have popped it from the
			// download queue already
			int outstanding_bytes = 0;
//			bool in_download_queue = false;
			int const bs = t->block_size();
			piece_block last_block(ti.last_piece()
				, (ti.piece_size(ti.last_piece()) + bs - 1) / bs);
			for (std::vector<pending_block>::const_iterator i = m_download_queue.begin()
				, end(m_download_queue.end()); i != end; ++i)
			{
				TORRENT_ASSERT(i->block.piece_index <= last_block.piece_index);
				TORRENT_ASSERT(i->block.piece_index < last_block.piece_index
					|| i->block.block_index <= last_block.block_index);
				if (m_received_in_piece && i == m_download_queue.begin())
				{
//					in_download_queue = true;
					// this assert is not correct since block may have different sizes
					// and may not be returned in the order they were requested
//					TORRENT_ASSERT(t->to_req(i->block).length >= m_received_in_piece);
					outstanding_bytes += t->to_req(i->block).length - m_received_in_piece;
				}
				else
				{
					outstanding_bytes += t->to_req(i->block).length;
				}
			}
			//if (p && p->bytes_downloaded < p->full_block_bytes) TORRENT_ASSERT(in_download_queue);

			if (m_outstanding_bytes != outstanding_bytes)
			{
				std::fprintf(stderr, "m_outstanding_bytes = %d\noutstanding_bytes = %d\n"
					, m_outstanding_bytes, outstanding_bytes);
			}

			TORRENT_ASSERT(m_outstanding_bytes == outstanding_bytes);
		}

		for (auto const& r : m_requests)
		{
			TORRENT_ASSERT(r.piece >= piece_index_t(0));
			TORRENT_ASSERT(r.piece < piece_index_t(m_have_piece.size()));
			if (t) TORRENT_ASSERT(r.start + r.length <= t->torrent_file().piece_size(r.piece));
			TORRENT_ASSERT(r.length > 0);
			TORRENT_ASSERT(r.length <= default_block_size);
			TORRENT_ASSERT(r.start >= 0);
		}

		std::set<piece_block> unique;
		std::transform(m_download_queue.begin(), m_download_queue.end()
			, std::inserter(unique, unique.begin()), std::bind(&pending_block::block, _1));
		std::transform(m_request_queue.begin(), m_request_queue.end()
			, std::inserter(unique, unique.begin()), std::bind(&pending_block::block, _1));
		TORRENT_ASSERT(unique.size() == m_download_queue.size() + m_request_queue.size());
		if (m_peer_info)
		{
			TORRENT_ASSERT(m_peer_info->prev_amount_upload == 0);
			TORRENT_ASSERT(m_peer_info->prev_amount_download == 0);
			TORRENT_ASSERT(m_peer_info->connection == this
				|| m_peer_info->connection == nullptr);

			if (m_peer_info->optimistically_unchoked)
				TORRENT_ASSERT(!is_choked());
		}

		TORRENT_ASSERT(m_have_piece.count() == m_num_pieces);

		if (!t)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			// since this connection doesn't have a torrent reference
			// no torrent should have a reference to this connection either
			TORRENT_ASSERT(!m_ses.any_torrent_has_peer(this));
#endif
			return;
		}

		auto const& ih = t->info_hash();
		if (peer_info_struct() && peer_info_struct()->protocol_v2)
			TORRENT_ASSERT(ih.has_v2());

		if (t->ready_for_connections() && m_initialized)
			TORRENT_ASSERT(t->torrent_file().num_pieces() == int(m_have_piece.size()));

		// in share mode we don't close redundant connections
		if (m_settings.get_bool(settings_pack::close_redundant_connections)
#ifndef TORRENT_DISABLE_SHARE_MODE
			&& !t->share_mode()
#endif
			)
		{
			bool const ok_to_disconnect =
				can_disconnect(errors::upload_upload_connection)
					|| can_disconnect(errors::uninteresting_upload_peer)
					|| can_disconnect(errors::too_many_requests_when_choked)
					|| can_disconnect(errors::timed_out_no_interest)
					|| can_disconnect(errors::timed_out_no_request)
					|| can_disconnect(errors::timed_out_inactivity);

			// make sure upload only peers are disconnected
			if (t->is_upload_only()
				&& m_upload_only
				&& !m_need_interest_update
				&& t->valid_metadata()
				&& has_metadata()
				&& ok_to_disconnect)
				TORRENT_ASSERT(m_disconnect_started || t->graceful_pause() || t->has_error());

			if (m_upload_only
				&& !m_interesting
				&& !m_need_interest_update
				&& m_bitfield_received
				&& t->are_files_checked()
				&& t->valid_metadata()
				&& has_metadata()
				&& ok_to_disconnect)
				TORRENT_ASSERT(m_disconnect_started);
		}

		if (!m_disconnect_started && m_initialized
			&& m_settings.get_bool(settings_pack::close_redundant_connections))
		{
			// none of this matters if we're disconnecting anyway
			if (t->is_upload_only() && !m_need_interest_update)
				TORRENT_ASSERT(!m_interesting || t->graceful_pause() || t->has_error());
			if (is_seed())
				TORRENT_ASSERT(m_upload_only);
		}

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		if (t->has_picker())
		{
			std::map<piece_block, peer_count_t> num_requests;
			for (torrent::const_peer_iterator i = t->begin(); i != t->end(); ++i)
			{
				// make sure this peer is not a dangling pointer
				TORRENT_ASSERT(m_ses.has_peer(*i));
				peer_connection const& p = *(*i);
				for (std::vector<pending_block>::const_iterator j = p.request_queue().begin()
					, end(p.request_queue().end()); j != end; ++j)
				{
					++num_requests[j->block].num_peers;
					++num_requests[j->block].num_peers_with_timeouts;
					++num_requests[j->block].num_peers_with_nowant;
					++num_requests[j->block].num_not_requested;
//					num_requests[j->block].peers.push_back(&p);
				}
				for (std::vector<pending_block>::const_iterator j = p.download_queue().begin()
					, end(p.download_queue().end()); j != end; ++j)
				{
					if (!j->not_wanted && !j->timed_out) ++num_requests[j->block].num_peers;
					if (j->timed_out) ++num_requests[j->block].num_peers_with_timeouts;
					if (j->not_wanted) ++num_requests[j->block].num_peers_with_nowant;
//					num_requests[j->block].peers.push_back(&p);
				}
			}
			for (std::map<piece_block, peer_count_t>::iterator j = num_requests.begin()
				, end(num_requests.end()); j != end; ++j)
			{
				piece_block b = j->first;
				peer_count_t const& pc = j->second;
				int count = pc.num_peers;
				int count_with_timeouts = pc.num_peers_with_timeouts;
				int count_with_nowant = pc.num_peers_with_nowant;
				(void)count_with_timeouts;
				(void)count_with_nowant;
				int picker_count = t->picker().num_peers(b);
				if (!t->picker().is_downloaded(b))
					TORRENT_ASSERT(picker_count == count);
			}
		}
#endif
/*
		if (t->has_picker() && !t->is_aborted())
		{
			for (std::vector<pending_block>::const_iterator i = m_download_queue.begin()
				, end(m_download_queue.end()); i != end; ++i)
			{
				pending_block const& pb = *i;
				if (pb.timed_out || pb.not_wanted) continue;
				TORRENT_ASSERT(t->picker().get_block_state(pb.block) != piece_picker::block_info::state_none);
				TORRENT_ASSERT(complete);
			}
		}
*/
// extremely expensive invariant check
/*
		if (!t->is_seed())
		{
			piece_picker& p = t->picker();
			const std::vector<piece_picker::downloading_piece>& dlq = p.get_download_queue();
			const int blocks_per_piece = static_cast<int>(
				t->torrent_file().piece_length() / t->block_size());

			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dlq.begin(); i != dlq.end(); ++i)
			{
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					if (std::find(m_request_queue.begin(), m_request_queue.end()
						, piece_block(i->index, j)) != m_request_queue.end()
						||
						std::find(m_download_queue.begin(), m_download_queue.end()
						, piece_block(i->index, j)) != m_download_queue.end())
					{
						TORRENT_ASSERT(i->info[j].peer == m_remote);
					}
					else
					{
						TORRENT_ASSERT(i->info[j].peer != m_remote || i->info[j].finished);
					}
				}
			}
		}
*/
	}
#endif

	void peer_connection::set_holepunch_mode()
	{
		m_holepunch_mode = true;
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "HOLEPUNCH_MODE", "[ on ]");
#endif
	}

	void peer_connection::keep_alive()
	{
		TORRENT_ASSERT(is_single_thread());
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		time_duration const d = aux::time_now() - m_last_sent.get(m_connect);
		if (total_seconds(d) < timeout() / 2) return;

		if (m_connecting) return;
		if (in_handshake()) return;

		// if the last send has not completed yet, do not send a keep
		// alive
		if (m_channel_state[upload_channel] & peer_info::bw_network) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "KEEPALIVE");
#endif

		write_keepalive();
	}

	bool peer_connection::is_seed() const
	{
		TORRENT_ASSERT(is_single_thread());
		// if m_num_pieces == 0, we probably don't have the
		// metadata yet.
		std::shared_ptr<torrent> t = m_torrent.lock();
		return m_num_pieces == m_have_piece.size()
			&& m_num_pieces > 0 && t && t->valid_metadata();
	}

#ifndef TORRENT_DISABLE_SHARE_MODE
	void peer_connection::set_share_mode(bool u)
	{
		TORRENT_ASSERT(is_single_thread());
		// if the peer is a seed, ignore share mode messages
		if (is_seed()) return;

		m_share_mode = u;
	}
#endif

	void peer_connection::set_upload_only(bool u)
	{
		TORRENT_ASSERT(is_single_thread());
		// if the peer is a seed, don't allow setting
		// upload_only to false
		if (m_upload_only || is_seed()) return;

		m_upload_only = u;
		std::shared_ptr<torrent> t = associated_torrent().lock();
		t->set_seed(m_peer_info, u);
		disconnect_if_redundant();
	}

}
