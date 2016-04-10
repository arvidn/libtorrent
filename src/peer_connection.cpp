/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <boost/limits.hpp>
#include <boost/bind.hpp>
#include <boost/cstdint.hpp>

#ifdef TORRENT_DEBUG
#include <set>
#endif

#ifdef TORRENT_USE_OPENSSL
#include <openssl/rand.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifndef TORRENT_DISABLE_LOGGING
#include <stdarg.h> // for va_start, va_end
#include <stdio.h> // for vsnprintf
#include "libtorrent/socket_io.hpp"
#endif

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/peer_list.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/network_thread_pool.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/request_blocks.hpp" // for request_a_block
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/alert_manager.hpp" // for alert_manageralert_manager
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/close_reason.hpp"
#include "libtorrent/aux_/time.hpp"

//#define TORRENT_CORRUPT_DATA

using boost::shared_ptr;

namespace libtorrent
{

	enum
	{
		// the limits of the download queue size
		min_request_queue = 2
	};

	namespace {

	bool pending_block_in_buffer(pending_block const& pb)
	{
		return pb.send_buffer_offset != pending_block::not_in_buffer;
	}

	}

#if TORRENT_USE_ASSERTS
	bool peer_connection::is_single_thread() const
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return true;
		return t->is_single_thread();
	}
#endif

	// outbound connection
	peer_connection::peer_connection(peer_connection_args const& pack)
		: peer_connection_hot_members(pack.tor, *pack.ses, *pack.sett)
		, m_socket(pack.s)
		, m_peer_info(pack.peerinfo)
		, m_counters(*pack.stats_counters)
		, m_num_pieces(0)
		, m_recv_buffer(*pack.allocator)
		, m_max_out_request_queue(m_settings.get_int(settings_pack::max_out_request_queue))
		, m_remote(pack.endp)
		, m_disk_thread(*pack.disk_thread)
		, m_allocator(*pack.allocator)
		, m_ios(*pack.ios)
		, m_work(m_ios)
		, m_last_piece(aux::time_now())
		, m_last_request(aux::time_now())
		, m_last_incoming_request(min_time())
		, m_last_unchoke(aux::time_now())
		, m_last_unchoked(aux::time_now())
		, m_last_choke(min_time())
		, m_last_receive(aux::time_now())
		, m_last_sent(aux::time_now())
		, m_last_sent_payload(aux::time_now())
		, m_requested(min_time())
		, m_remote_dl_update(aux::time_now())
		, m_connect(aux::time_now())
		, m_became_uninterested(aux::time_now())
		, m_became_uninteresting(aux::time_now())
		, m_downloaded_at_last_round(0)
		, m_uploaded_at_last_round(0)
		, m_uploaded_at_last_unchoke(0)
		, m_downloaded_last_second(0)
		, m_uploaded_last_second(0)
		, m_outstanding_bytes(0)
		, m_last_seen_complete(0)
		, m_receiving_block(piece_block::invalid)
		, m_extension_outstanding_bytes(0)
		, m_queued_time_critical(0)
		, m_reading_bytes(0)
		, m_picker_options(0)
		, m_num_invalid_requests(0)
		, m_remote_pieces_dled(0)
		, m_remote_dl_rate(0)
		, m_outstanding_writing_bytes(0)
		, m_download_rate_peak(0)
		, m_upload_rate_peak(0)
		, m_send_barrier(INT_MAX)
		, m_desired_queue_size(4)
		, m_prefer_contiguous_blocks(0)
		, m_disk_read_failures(0)
		, m_outstanding_piece_verification(0)
		, m_outgoing(!pack.tor.expired())
		, m_received_listen_port(false)
		, m_fast_reconnect(false)
		, m_failed(false)
		, m_connected(pack.tor.expired())
		, m_request_large_blocks(false)
		, m_share_mode(false)
		, m_upload_only(false)
		, m_bitfield_received(false)
		, m_no_download(false)
		, m_sent_suggests(false)
		, m_holepunch_mode(false)
		, m_peer_choked(true)
		, m_have_all(false)
		, m_peer_interested(false)
		, m_need_interest_update(false)
		, m_has_metadata(true)
		, m_exceeded_limit(false)
		, m_slow_start(true)
#if TORRENT_USE_ASSERTS
		, m_in_constructor(true)
		, m_disconnect_started(false)
		, m_initialized(false)
		, m_in_use(1337)
		, m_received_in_piece(0)
		, m_destructed(false)
		, m_socket_is_writing(false)
#endif
	{
		m_counters.inc_stats_counter(counters::num_tcp_peers + m_socket->type() - 1);

		if (m_connected)
			m_counters.inc_stats_counter(counters::num_peers_connected);
		else if (m_connecting)
			m_counters.inc_stats_counter(counters::num_peers_half_open);

		m_superseed_piece[0] = -1;
		m_superseed_piece[1] = -1;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		// if t is NULL, we better not be connecting, since
		// we can't decrement the connecting counter
		TORRENT_ASSERT(t || !m_connecting);
		if (m_connecting && t) t->inc_num_connecting();
		m_est_reciprocation_rate = m_settings.get_int(settings_pack::default_est_reciprocation_rate);

		m_channel_state[upload_channel] = peer_info::bw_idle;
		m_channel_state[download_channel] = peer_info::bw_idle;

		m_quota[0] = 0;
		m_quota[1] = 0;

		TORRENT_ASSERT(pack.peerinfo == 0 || pack.peerinfo->banned == false);
#ifndef TORRENT_NO_DEPRECATE
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		std::fill(m_country, m_country + 2, 0);
#endif
#endif // TORRENT_NO_DEPRECATE
#ifndef TORRENT_DISABLE_LOGGING
		error_code ec;
		TORRENT_ASSERT(m_socket->remote_endpoint(ec) == m_remote || ec);
		tcp::endpoint local_ep = m_socket->local_endpoint(ec);

		peer_log(m_outgoing ? peer_log_alert::outgoing : peer_log_alert::incoming
			, m_outgoing ? "OUTGOING_CONNECTION" : "INCOMING_CONNECTION"
			, "ep: %s type: %s seed: %d p: %p local: %s"
			, print_endpoint(m_remote).c_str()
			, m_socket->type_name()
			, m_peer_info ? m_peer_info->seed : 0
			, static_cast<void*>(m_peer_info)
			, print_endpoint(local_ep).c_str());
#endif
#ifdef TORRENT_DEBUG
		piece_failed = false;
#endif
		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);
	}

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

	void peer_connection::increase_est_reciprocation_rate()
	{
		TORRENT_ASSERT(is_single_thread());
		m_est_reciprocation_rate += m_est_reciprocation_rate
			* m_settings.get_int(settings_pack::increase_est_reciprocation_rate) / 100;
	}

	void peer_connection::decrease_est_reciprocation_rate()
	{
		TORRENT_ASSERT(is_single_thread());
		m_est_reciprocation_rate -= m_est_reciprocation_rate
			* m_settings.get_int(settings_pack::decrease_est_reciprocation_rate) / 100;
	}

	int peer_connection::get_priority(int channel) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(channel >= 0 && channel < 2);
		int prio = 1;
		for (int i = 0; i < num_classes(); ++i)
		{
			int class_prio = m_ses.peer_classes().at(class_at(i))->priority[channel];
			if (prio < class_prio) prio = class_prio;
		}

		boost::shared_ptr<torrent> t = associated_torrent().lock();

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
		TORRENT_ASSERT(m_peer_info == 0 || m_peer_info->connection == this);
		boost::shared_ptr<torrent> t = m_torrent.lock();

		if (!m_outgoing)
		{
			tcp::socket::non_blocking_io ioc(true);
			error_code ec;
			m_socket->io_control(ioc, ec);
			if (ec)
			{
				disconnect(ec, op_iocontrol);
				return;
			}
			m_remote = m_socket->remote_endpoint(ec);
			if (ec)
			{
				disconnect(ec, op_getpeername);
				return;
			}
			m_local = m_socket->local_endpoint(ec);
			if (ec)
			{
				disconnect(ec, op_getname);
				return;
			}
			if (m_remote.address().is_v4() && m_settings.get_int(settings_pack::peer_tos) != 0)
			{
				m_socket->set_option(type_of_service(m_settings.get_int(settings_pack::peer_tos)), ec);
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::outgoing, "SET_TOS", "tos: %d e: %s"
					, m_settings.get_int(settings_pack::peer_tos), ec.message().c_str());
#endif
			}
#if TORRENT_USE_IPV6 && defined IPV6_TCLASS
			else if (m_remote.address().is_v6() && m_settings.get_int(settings_pack::peer_tos) != 0)
			{
				m_socket->set_option(traffic_class(m_settings.get_int(settings_pack::peer_tos)), ec);
			}
#endif
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "SET_PEER_CLASS", "a: %s"
			, print_address(m_remote.address()).c_str());
#endif

		m_ses.set_peer_classes(this, m_remote.address(), m_socket->type());

#ifndef TORRENT_DISABLE_LOGGING
		for (int i = 0; i < num_classes(); ++i)
		{
			peer_log(peer_log_alert::info, "CLASS", "%s"
				, m_ses.peer_classes().at(class_at(i))->label.c_str());
		}
#endif

		if (t && t->ready_for_connections())
		{
			init();
		}

		// if this is an incoming connection, we're done here
		if (!m_connecting) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "OPEN", "protocol: %s"
			, (m_remote.address().is_v4()?"IPv4":"IPv6"));
#endif
		error_code ec;
		m_socket->open(m_remote.protocol(), ec);
		if (ec)
		{
			disconnect(ec, op_sock_open);
			return;
		}

		tcp::endpoint bound_ip = m_ses.bind_outgoing_socket(*m_socket
			, m_remote.address(), ec);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "BIND", "dst: %s ec: %s"
			, print_endpoint(bound_ip).c_str()
			, ec.message().c_str());
#else
		TORRENT_UNUSED(bound_ip);
#endif
		if (ec)
		{
			disconnect(ec, op_sock_bind);
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "ASYNC_CONNECT", "dst: %s"
			, print_endpoint(m_remote).c_str());
#endif
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("peer_connection::on_connection_complete");
#endif

#ifndef TORRENT_DISABLE_LOGGING
		if (t)
			t->debug_log("START connect [%p] (%d)", static_cast<void*>(this)
				, int(t->num_peers()));
#endif

		m_socket->async_connect(m_remote
			, boost::bind(&peer_connection::on_connection_complete, self(), _1));
		m_connect = clock_type::now();

		sent_syn(m_remote.address().is_v6());

		if (t && t->alerts().should_post<peer_connect_alert>())
		{
			t->alerts().emplace_alert<peer_connect_alert>(
				t->get_handle(), remote(), pid(), m_socket->type());
		}
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "LOCAL ENDPOINT", "e: %s"
			, print_endpoint(m_socket->local_endpoint(ec)).c_str());
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
			m_ios.post(boost::bind(&peer_connection::do_update_interest, self()));
		}
		m_need_interest_update = true;
	}

	void peer_connection::do_update_interest()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_need_interest_update);
		m_need_interest_update = false;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;

		// if m_have_piece is 0, it means the connections
		// have not been initialized yet. The interested
		// flag will be updated once they are.
		if (m_have_piece.size() == 0)
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
			int num_pieces = p.num_pieces();
			for (int j = 0; j != num_pieces; ++j)
			{
				if (m_have_piece[j]
					&& t->piece_priority(j) > 0
					&& !p.has_piece_passed(j))
				{
					interested = true;
#ifndef TORRENT_DISABLE_LOGGING
					peer_log(peer_log_alert::info, "UPDATE_INTEREST", "interesting, piece: %d", j);
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
	void peer_connection::peer_log(peer_log_alert::direction_t direction
		, char const* event) const
	{
		peer_log(direction, event, "");
	}

	TORRENT_FORMAT(4,5)
	void peer_connection::peer_log(peer_log_alert::direction_t direction
		, char const* event, char const* fmt, ...) const
	{
		TORRENT_ASSERT(is_single_thread());

		if (!m_ses.alerts().should_post<peer_log_alert>()) return;

		va_list v;
		va_start(v, fmt);

		// TODO: it would be neat to be able to print this straight into the
		// alert's stack allocator
		char buf[512];
		vsnprintf(buf, sizeof(buf), fmt, v);
		va_end(v);

		torrent_handle h;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (t) h = t->get_handle();

		m_ses.alerts().emplace_alert<peer_log_alert>(
			h, m_remote, m_peer_id, direction, event, buf);
	}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
	void peer_connection::add_extension(boost::shared_ptr<peer_plugin> ext)
	{
		TORRENT_ASSERT(is_single_thread());
		m_extensions.push_back(ext);
	}

	peer_plugin const* peer_connection::find_plugin(char const* type)
	{
		TORRENT_ASSERT(is_single_thread());
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if (strcmp((*i)->type(), type) == 0) return (*i).get();
		}
		return 0;
	}
#endif

	void peer_connection::send_allowed_set()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (t->super_seeding())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ALLOWED", "skipping allowed set because of super seeding");
#endif
			return;
		}

		if (upload_only())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ALLOWED", "skipping allowed set because peer is upload only");
#endif
			return;
		}

		int num_allowed_pieces = m_settings.get_int(settings_pack::allowed_fast_set_size);
		if (num_allowed_pieces == 0) return;

		int num_pieces = t->torrent_file().num_pieces();

		if (num_allowed_pieces >= num_pieces)
		{
			// this is a special case where we have more allowed
			// fast pieces than pieces in the torrent. Just send
			// an allowed fast message for every single piece
			for (int i = 0; i < num_pieces; ++i)
			{
				// there's no point in offering fast pieces
				// that the peer already has
				if (has_piece(i)) continue;

#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::outgoing_message, "ALLOWED_FAST", "%d", i);
#endif
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
#if TORRENT_USE_IPV6
		else
		{
			address_v6::bytes_type bytes = addr.to_v6().to_bytes();
			x.assign(reinterpret_cast<char*>(bytes.data()), bytes.size());
		}
#endif
		x.append(t->torrent_file().info_hash().data(), 20);

		sha1_hash hash = hasher(x.c_str(), x.size()).final();
		for (;;)
		{
			char* p = hash.data();
			for (int i = 0; i < 5; ++i)
			{
				int piece = detail::read_uint32(p) % num_pieces;
				if (std::find(m_accept_fast.begin(), m_accept_fast.end(), piece)
					== m_accept_fast.end())
				{
#ifndef TORRENT_DISABLE_LOGGING
					peer_log(peer_log_alert::outgoing_message, "ALLOWED_FAST", "%d", piece);
#endif
					write_allow_fast(piece);
					if (m_accept_fast.empty())
					{
						m_accept_fast.reserve(10);
						m_accept_fast_piece_cnt.reserve(10);
					}
					m_accept_fast.push_back(piece);
					m_accept_fast_piece_cnt.push_back(0);
					if (int(m_accept_fast.size()) >= num_allowed_pieces
						|| int(m_accept_fast.size()) == num_pieces) return;
				}
			}
			hash = hasher(hash.data(), 20).final();
		}
	}

	void peer_connection::on_metadata_impl()
	{
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		m_have_piece.resize(t->torrent_file().num_pieces(), m_have_all);
		m_num_pieces = m_have_piece.count();

		// now that we know how many pieces there are
		// remove any invalid allowed_fast and suggest pieces
		// now that we know what the number of pieces are
		for (std::vector<int>::iterator i = m_allowed_fast.begin();
			i != m_allowed_fast.end();)
		{
			if (*i < m_num_pieces)
			{
				++i;
				continue;
			}
			i = m_allowed_fast.erase(i);
		}

		for (std::vector<int>::iterator i = m_suggested_pieces.begin();
			i != m_suggested_pieces.end();)
		{
			if (*i < m_num_pieces)
			{
				++i;
				continue;
			}
			i = m_suggested_pieces.erase(i);
		}
		on_metadata();
		if (m_disconnecting) return;
	}

	void peer_connection::init()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->valid_metadata());
		TORRENT_ASSERT(t->ready_for_connections());

		m_have_piece.resize(t->torrent_file().num_pieces(), m_have_all);

		if (m_have_all) m_num_pieces = t->torrent_file().num_pieces();
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(!m_initialized);
		m_initialized = true;
#endif
		// now that we have a piece_picker,
		// update it with this peer's pieces

		TORRENT_ASSERT(m_num_pieces == m_have_piece.count());

		if (m_num_pieces == int(m_have_piece.size()))
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
			return;
		}

		// if we're a seed, we don't keep track of piece availability
		if (t->has_picker())
		{
			TORRENT_ASSERT(m_have_piece.size() == t->torrent_file().num_pieces());
			t->peer_has(m_have_piece, this);
			bool interesting = false;
			for (int i = 0; i < int(m_have_piece.size()); ++i)
			{
				if (m_have_piece[i])
				{
					// if the peer has a piece and we don't, the peer is interesting
					if (!t->have_piece(i)
						&& t->picker().piece_priority(i) != 0)
						interesting = true;
				}
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
		m_counters.inc_stats_counter(counters::num_tcp_peers + m_socket->type() - 1, -1);

//		INVARIANT_CHECK;
		TORRENT_ASSERT(!m_in_constructor);
		TORRENT_ASSERT(m_disconnecting);
		TORRENT_ASSERT(m_disconnect_started);
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
		boost::shared_ptr<torrent> t = m_torrent.lock();
		// if t is NULL, we better not be connecting, since
		// we can't decrement the connecting counter
		TORRENT_ASSERT(t || !m_connecting);

		// we should really have dealt with this already
		TORRENT_ASSERT(!m_connecting);
		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t) t->dec_num_connecting();
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

#if TORRENT_USE_ASSERTS
		if (m_peer_info)
			TORRENT_ASSERT(m_peer_info->connection == 0);
#endif
	}

	bool peer_connection::on_parole() const
	{ return peer_info_struct() && peer_info_struct()->on_parole; }

	int peer_connection::picker_options() const
	{
		TORRENT_ASSERT(is_single_thread());
		int ret = m_picker_options;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		if (!t) return 0;

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
		}

		if (m_snubbed)
		{
			// snubbed peers should request
			// the common pieces first, just to make
			// it more likely for all snubbed peers to
			// request blocks from the same piece
			ret |= piece_picker::reverse;
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
		peer_info_struct()->last_connected = boost::uint16_t(m_ses.session_time());
		int rewind = m_settings.get_int(settings_pack::min_reconnect_time)
			* m_settings.get_int(settings_pack::max_failcount);
		if (peer_info_struct()->last_connected < rewind) peer_info_struct()->last_connected = 0;
		else peer_info_struct()->last_connected -= rewind;

		if (peer_info_struct()->fast_reconnects < 15)
			++peer_info_struct()->fast_reconnects;
	}

	void peer_connection::received_piece(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		// dont announce during handshake
		if (in_handshake()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming, "RECEIVED", "piece: %d", index);
#endif

		// remove suggested pieces once we have them
		std::vector<int>::iterator i = std::find(
			m_suggested_pieces.begin(), m_suggested_pieces.end(), index);
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
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
#endif
	}

	void peer_connection::announce_piece(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		// dont announce during handshake
		if (in_handshake()) return;

		if (has_piece(index))
		{
			// optimization, don't send have messages
			// to peers that already have the piece
			if (!m_settings.get_bool(settings_pack::send_redundant_have))
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::outgoing_message, "HAVE", "piece: %d SUPRESSED", index);
#endif
				return;
			}
		}

		if (disconnect_if_redundant()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "HAVE", "piece: %d", index);
#endif
		write_have(index);
#if TORRENT_USE_ASSERTS
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
#endif
	}

	bool peer_connection::has_piece(int i) const
	{
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->valid_metadata());
		TORRENT_ASSERT(i >= 0);
		TORRENT_ASSERT(i < t->torrent_file().num_pieces());
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

	time_duration peer_connection::download_queue_time(int extra_bytes) const
	{
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		int rate = 0;

		// if we haven't received any data recently, the current download rate
		// is not representative
		if (aux::time_now() - m_last_piece > seconds(30) && m_download_rate_peak > 0)
		{
			rate = m_download_rate_peak;
		}
		else if (aux::time_now() - m_last_unchoked < seconds(5)
			&& m_statistics.total_payload_upload() < 2 * 0x4000)
		{
			// if we're have only been unchoked for a short period of time,
			// we don't know what rate we can get from this peer. Instead of assuming
			// the lowest possible rate, assume the average.

			int peers_with_requests = stats_counters()[counters::num_peers_down_requests];
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

	void peer_connection::add_stat(boost::int64_t downloaded, boost::int64_t uploaded)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.add_stat(downloaded, uploaded);
	}

	void peer_connection::received_bytes(int bytes_payload, int bytes_protocol)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.received_bytes(bytes_payload, bytes_protocol);
		if (m_ignore_stats) return;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->received_bytes(bytes_payload, bytes_protocol);
	}

	void peer_connection::sent_bytes(int bytes_payload, int bytes_protocol)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.sent_bytes(bytes_payload, bytes_protocol);
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (bytes_payload)
		{
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				(*i)->sent_payload(bytes_payload);
			}
		}
#endif
		if (m_ignore_stats) return;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->sent_bytes(bytes_payload, bytes_protocol);
	}

	void peer_connection::trancieve_ip_packet(int bytes, bool ipv6)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.trancieve_ip_packet(bytes, ipv6);
		if (m_ignore_stats) return;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->trancieve_ip_packet(bytes, ipv6);
	}

	void peer_connection::sent_syn(bool ipv6)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.sent_syn(ipv6);
		if (m_ignore_stats) return;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->sent_syn(ipv6);
	}

	void peer_connection::received_synack(bool ipv6)
	{
		TORRENT_ASSERT(is_single_thread());
		m_statistics.received_synack(ipv6);
		if (m_ignore_stats) return;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		t->received_synack(ipv6);
	}

	bitfield const& peer_connection::get_bitfield() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_have_piece;
	}

	void peer_connection::received_valid_data(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		// this fails because we haven't had time to disconnect
		// seeds yet, and we might have just become one
//		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_piece_pass(index);
			} TORRENT_CATCH(std::exception&) {}
		}
#else
		TORRENT_UNUSED(index);
#endif
	}

	// single_peer is true if the entire piece was received by a single
	// peer
	bool peer_connection::received_invalid_data(int index, bool single_peer)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;
		TORRENT_UNUSED(single_peer);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_piece_failed(index);
			} TORRENT_CATCH(std::exception&) {}
		}
#else
		TORRENT_UNUSED(index);
#endif
		return true;
	}

	// verifies a piece to see if it is valid (is within a valid range)
	// and if it can correspond to a request generated by libtorrent.
	bool peer_connection::verify_piece(const peer_request& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(t->valid_metadata());
		torrent_info const& ti = t->torrent_file();

		return p.piece >= 0
			&& p.piece < ti.num_pieces()
			&& p.start >= 0
			&& p.start < ti.piece_length()
			&& t->to_req(piece_block(p.piece, p.start / t->block_size())) == p;
	}

	void peer_connection::attach_to_torrent(sha1_hash const& ih)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		m_connect_time = clock_type::now();
		peer_log(peer_log_alert::info, "ATTACH", "attached to torrent");
#endif

		TORRENT_ASSERT(!m_disconnecting);
		TORRENT_ASSERT(m_torrent.expired());
		boost::weak_ptr<torrent> wpt = m_ses.find_torrent(ih);
		boost::shared_ptr<torrent> t = wpt.lock();

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
			if (t)
				peer_log(peer_log_alert::info, "ATTACH"
					, "Delay loaded torrent: %s:", to_hex(ih.to_string()).c_str());
#endif
		}

		if (!t)
		{
			// we couldn't find the torrent!
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ATTACH"
				, "couldn't find a torrent with the given info_hash: %s torrents:"
				, to_hex(ih.to_string()).c_str());
#endif

#ifndef TORRENT_DISABLE_DHT
			if (dht::verify_secret_id(ih))
			{
				// this means the hash was generated from our generate_secret_id()
				// as part of DHT traffic. The fact that we got an incoming
				// connection on this info-hash, means the other end, making this
				// connection fished it out of the DHT chatter. That's suspicious.
				m_ses.ban_ip(m_remote.address());
			}
#endif
			disconnect(errors::invalid_info_hash, op_bittorrent, 1);
			return;
		}

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
			// and inconing_starts_queued_torrents is true
			// torrents that have errors should always reject
			// incoming peers
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ATTACH", "rejected connection to paused torrent");
#endif
			disconnect(errors::torrent_paused, op_bittorrent, 2);
			return;
		}

#if TORRENT_USE_I2P
		i2p_stream* i2ps = m_socket->get<i2p_stream>();
		if (!i2ps && t->torrent_file().is_i2p()
			&& !m_settings.get_bool(settings_pack::allow_i2p_mixed))
		{
			// the torrent is an i2p torrent, the peer is a regular peer
			// and we don't allow mixed mode. Disconnect the peer.
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ATTACH", "rejected regular connection to i2p torrent");
#endif
			disconnect(errors::peer_banned, op_bittorrent, 2);
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

		if (m_exceeded_limit)
		{
			// find a peer in some torrent (presumably the one with most peers)
			// and disconnect the lowest ranking peer
			boost::weak_ptr<torrent> torr = m_ses.find_disconnect_candidate_torrent();
			boost::shared_ptr<torrent> other_t = torr.lock();

			if (other_t)
			{
				if (other_t->num_peers() <= t->num_peers())
				{
					disconnect(errors::too_many_connections, op_bittorrent);
					return;
				}
				// find the lowest ranking peer and disconnect that
				peer_connection* p = other_t->find_lowest_ranking_peer();
				p->disconnect(errors::too_many_connections, op_bittorrent);
				peer_disconnected_other();
			}
			else
			{
				disconnect(errors::too_many_connections, op_bittorrent);
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

	boost::uint32_t peer_connection::peer_rank() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_peer_info == NULL ? 0
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
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_choke()) return;
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
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		if (!t->has_picker())
		{
			m_request_queue.clear();
			return;
		}

		// clear the requests that haven't been sent yet
		if (peer_info_struct() == 0 || !peer_info_struct()->on_parole)
		{
			// if the peer is not in parole mode, clear the queued
			// up block requests
			piece_picker& p = t->picker();
			for (std::vector<pending_block>::const_iterator i = m_request_queue.begin()
				, end(m_request_queue.end()); i != end; ++i)
			{
				p.abort_download(i->block, peer_info_struct());
			}
			m_request_queue.clear();
			m_queued_time_critical = 0;
		}
	}

	namespace {

	bool match_request(peer_request const& r, piece_block const& b, int block_size)
	{
		if (int(b.piece_index) != r.piece) return false;
		if (int(b.block_index) != r.start / block_size) return false;
		if (r.start % block_size != 0) return false;
		return true;
	}
	}

	// -----------------------------
	// -------- REJECT PIECE -------
	// -----------------------------

	void peer_connection::incoming_reject_request(peer_request const& r)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "REJECT_PIECE", "piece: %d s: %x l: %x"
			, r.piece, r.start, r.length);
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_reject(r)) return;
		}
#endif

		if (is_disconnecting()) return;

		std::vector<pending_block>::iterator dlq_iter = std::find_if(
			m_download_queue.begin(), m_download_queue.end()
			, boost::bind(match_request, boost::cref(r), boost::bind(&pending_block::block, _1)
			, t->block_size()));

		if (dlq_iter != m_download_queue.end())
		{
			pending_block b = *dlq_iter;
			bool remove_from_picker = !dlq_iter->timed_out && !dlq_iter->not_wanted;
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
			peer_log(peer_log_alert::info, "REJECT_PIECE", "piece not in request queue");
		}
#endif
		if (has_peer_choked())
		{
			// if we're choked and we got a rejection of
			// a piece in the allowed fast set, remove it
			// from the allow fast set.
			std::vector<int>::iterator i = std::find(
				m_allowed_fast.begin(), m_allowed_fast.end(), r.piece);
			if (i != m_allowed_fast.end()) m_allowed_fast.erase(i);
		}
		else
		{
			std::vector<int>::iterator i = std::find(m_suggested_pieces.begin()
				, m_suggested_pieces.end(), r.piece);
			if (i != m_suggested_pieces.end())
				m_suggested_pieces.erase(i);
		}

		check_graceful_pause();
		if (is_disconnecting()) return;

		if (m_request_queue.empty() && m_download_queue.size() < 2)
		{
			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::reject_piece_picks);
			send_block_requests();
		}
	}

	// -----------------------------
	// ------- SUGGEST PIECE -------
	// -----------------------------

	void peer_connection::incoming_suggest(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "SUGGEST_PIECE"
			, "piece: %d", index);
#endif
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_suggest(index)) return;
		}
#endif

		if (is_disconnecting()) return;
		if (index < 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "INVALID_SUGGEST_PIECE"
				, "%d", index);
#endif
			return;
		}

		if (t->valid_metadata())
		{
			if (index >= int(m_have_piece.size()))
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::incoming_message, "INVALID_SUGGEST"
					, "%d s: %d", index, int(m_have_piece.size()));
#endif
				return;
			}

			// if we already have the piece, we can
			// ignore this message
			if (t->have_piece(index))
				return;
		}

		if (int(m_suggested_pieces.size()) > m_settings.get_int(settings_pack::max_suggest_pieces))
			m_suggested_pieces.erase(m_suggested_pieces.begin());

		m_suggested_pieces.push_back(index);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "SUGGEST_PIECE", "piece: %d added to set: %d"
			, index, int(m_suggested_pieces.size()));
#endif
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void peer_connection::incoming_unchoke()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
		m_unchoke_time = clock_type::now();
		t->debug_log("UNCHOKE [%p] (%d ms)", static_cast<void*>(this)
			, int(total_milliseconds(m_unchoke_time - m_bitfield_time)));
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_unchoke()) return;
		}
#endif

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "UNCHOKE");
#endif
		if (m_peer_choked)
			m_counters.inc_stats_counter(counters::num_peers_down_unchoked);

		m_peer_choked = false;
		m_last_unchoked = aux::time_now();
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

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_interested()) return;
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
			// if this peer is expempted from the choker
			// just unchoke it immediately
			send_unchoke();
		}
		else if (m_ses.preemptive_unchoke())
		{
			// if the peer is choked and we have upload slots left,
			// then unchoke it.

			boost::shared_ptr<torrent> t = m_torrent.lock();
			TORRENT_ASSERT(t);

			t->unchoke_peer(*this);
		}
#ifndef TORRENT_DISABLE_LOGGING
		else
		{
			peer_log(peer_log_alert::info, "UNCHOKE", "did not unchoke, the number of uploads (%d) "
				"is more than or equal to the limit (%d)"
				, m_ses.num_uploads(), m_settings.get_int(settings_pack::unchoke_slots_limit));
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
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_not_interested()) return;
		}
#endif

		m_became_uninterested = aux::time_now();

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "NOT_INTERESTED");
#endif
		if (m_peer_interested)
			m_counters.inc_stats_counter(counters::num_peers_up_interested, -1);

		m_peer_interested = false;
		if (is_disconnecting()) return;

		boost::shared_ptr<torrent> t = m_torrent.lock();
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

		boost::shared_ptr<torrent> t = m_torrent.lock();
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

	void peer_connection::incoming_have(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_have(index)) return;
		}
#endif

		if (is_disconnecting()) return;

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "HAVE", "piece: %d", index);
#endif

		if (is_disconnecting()) return;

		if (!t->valid_metadata() && index >= int(m_have_piece.size()))
		{
			if (index < 131072)
			{
				// if we don't have metadata
				// and we might not have received a bitfield
				// extend the bitmask to fit the new
				// have message
				m_have_piece.resize(index + 1, false);
			}
			else
			{
				// unless the index > 64k, in which case
				// we just ignore it
				return;
			}
		}

		// if we got an invalid message, abort
		if (index >= int(m_have_piece.size()) || index < 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ERROR", "have-metadata have_piece: %d size: %d"
				, index, int(m_have_piece.size()));
#endif
			disconnect(errors::invalid_have, op_bittorrent, 2);
			return;
		}

		if (t->super_seeding() && !m_settings.get_bool(settings_pack::strict_super_seeding))
		{
			// if we're superseeding and the peer just told
			// us that it completed the piece we're superseeding
			// to it, change the superseeding piece for this peer
			// if the peer optimizes out redundant have messages
			// this will be handled when the peer sends not-interested
			// instead.
			if (super_seeded_piece(index))
			{
				superseed_piece(index, t->get_piece_to_super_seed(m_have_piece));
			}
		}

		if (m_have_piece[index])
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "HAVE"
				, "got redundant HAVE message for index: %d", index);
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

		// this will disregard all have messages we get within
		// the first two seconds. Since some clients implements
		// lazy bitfields, these will not be reliable to use
		// for an estimated peer download rate.
		if (!peer_info_struct()
			|| m_ses.session_time() - peer_info_struct()->last_connected > 2)
		{
			// update bytes downloaded since last timer
			++m_remote_pieces_dled;
		}

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
		}

		// it's important to update whether we're intersted in this peer before
		// calling disconnect_if_redundant, otherwise we may disconnect even if
		// we are interested
		if (!t->has_piece_passed(index)
			&& !t->is_seed()
			&& !is_interesting()
			&& (!t->has_picker() || t->picker().piece_priority(index) != 0))
			t->peer_is_interesting(*this);

		disconnect_if_redundant();
		if (is_disconnecting()) return;

		// if we're super seeding, this might mean that somebody
		// forwarded this piece. In which case we need to give
		// a new piece to that peer
		if (t->super_seeding()
			&& m_settings.get_bool(settings_pack::strict_super_seeding)
			&& (!super_seeded_piece(index) || t->num_peers() == 1))
		{
			for (torrent::peer_iterator i = t->begin()
				, end(t->end()); i != end; ++i)
			{
				peer_connection* p = *i;
				if (!p->super_seeded_piece(index)) continue;
				if (!p->has_piece(index)) continue;
				p->superseed_piece(index, t->get_piece_to_super_seed(p->get_bitfield()));
			}
		}
	}

	// -----------------------------
	// -------- DONT HAVE ----------
	// -----------------------------

	void peer_connection::incoming_dont_have(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_dont_have(index)) return;
		}
#endif

		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "DONT_HAVE", "piece: %d", index);
#endif

		// if we got an invalid message, abort
		if (index >= int(m_have_piece.size()) || index < 0)
		{
			disconnect(errors::invalid_dont_have, op_bittorrent, 2);
			return;
		}

		if (!m_have_piece[index])
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "DONT_HAVE"
				, "got redundant DONT_HAVE message for index: %d", index);
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

	void peer_connection::incoming_bitfield(bitfield const& bits)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_bitfield(bits)) return;
		}
#endif

		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		std::string bitfield_str;
		bitfield_str.resize(bits.size());
		for (int i = 0; i < int(bits.size()); ++i)
			bitfield_str[i] = bits[i] ? '1' : '0';
		peer_log(peer_log_alert::incoming_message, "BITFIELD"
			, "%s", bitfield_str.c_str());
#endif

		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& bits.size() != int(m_have_piece.size()))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "BITFIELD"
				, "invalid size: %d expected %d", bits.size()
				, int(m_have_piece.size()));
#endif
			disconnect(errors::invalid_bitfield_size, op_bittorrent, 2);
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

#ifndef TORRENT_DISABLE_LOGGING
		m_bitfield_time = clock_type::now();
		t->debug_log("HANDSHAKE [%p] (%d ms)"
			, static_cast<void*>(this)
			, int(total_milliseconds(m_bitfield_time - m_connect_time)));
#endif
		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!t->ready_for_connections())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_num_pieces == int(bits.size()))
				peer_log(peer_log_alert::info, "SEED", "this is a seed. p: %p"
					, static_cast<void*>(m_peer_info));
#endif
			m_have_piece = bits;
			m_num_pieces = bits.count();
			t->set_seed(m_peer_info, m_num_pieces == int(bits.size()));

#if TORRENT_USE_INVARIANT_CHECKS
			if (t && t->has_picker())
				t->picker().check_peer_invariant(m_have_piece, peer_info_struct());
#endif
			return;
		}

		TORRENT_ASSERT(t->valid_metadata());

		int num_pieces = bits.count();
		if (num_pieces == int(m_have_piece.size()))
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

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return false;

		// if we don't have the metadata yet, don't disconnect
		// also, if the peer doesn't have metadata we shouldn't
		// disconnect it, since it may want to request the
		// metadata from us
		if (!t->valid_metadata() || !has_metadata()) return false;

		// don't close connections in share mode, we don't know if we need them
		if (t->share_mode()) return false;

		if (m_upload_only && t->is_upload_only()
			&& can_disconnect(error_code(errors::upload_upload_connection, get_libtorrent_category())))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UPLOAD_ONLY", "the peer is upload-only and our torrent is also upload-only");
#endif
			disconnect(errors::upload_upload_connection, op_bittorrent);
			return true;
		}

		if (m_upload_only
			&& !m_interesting
			&& m_bitfield_received
			&& t->are_files_checked()
			&& can_disconnect(error_code(errors::uninteresting_upload_peer, get_libtorrent_category())))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "UPLOAD_ONLY", "the peer is upload-only and we're not interested in it");
#endif
			disconnect(errors::uninteresting_upload_peer, op_bittorrent);
			return true;
		}

		return false;
	}

	bool peer_connection::can_disconnect(error_code const& ec) const
	{
		TORRENT_ASSERT(is_single_thread());
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::const_iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if (!(*i)->can_disconnect(ec)) return false;
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

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		m_counters.inc_stats_counter(counters::piece_requests);

#ifndef TORRENT_DISABLE_LOGGING
		const bool valid_piece_index
			= r.piece >= 0 && r.piece < int(t->torrent_file().num_pieces());

		peer_log(peer_log_alert::incoming_message, "REQUEST"
			, "piece: %d s: %x l: %x", r.piece, r.start, r.length);
#endif

		if (t->super_seeding()
			&& !super_seeded_piece(r.piece))
		{
			m_counters.inc_stats_counter(counters::invalid_piece_requests);
			++m_num_invalid_requests;
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST", "piece not superseeded "
				"i: %d t: %d n: %d h: %d ss1: %d ss2: %d"
				, m_peer_interested
				, valid_piece_index
					? int(t->torrent_file().piece_size(r.piece))
					: -1
				, t->torrent_file().num_pieces()
				, valid_piece_index ? t->has_piece_passed(r.piece) : 0
				, m_superseed_piece[0]
				, m_superseed_piece[1]);
#endif

			write_reject_request(r);

			if (t->alerts().should_post<invalid_request_alert>())
			{
				// msvc 12 appears to deduce the rvalue reference template
				// incorrectly for bool temporaries. So, create a dummy instance
				bool peer_interested = bool(m_peer_interested);
				t->alerts().emplace_alert<invalid_request_alert>(
					t->get_handle(), m_remote, m_peer_id, r
					, t->has_piece_passed(r.piece), peer_interested, true);
			}
			return;
		}

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_request(r)) return;
		}
#endif
		if (is_disconnecting()) return;

		if (!t->valid_metadata())
		{
			m_counters.inc_stats_counter(counters::invalid_piece_requests);
			// if we don't have valid metadata yet,
			// we shouldn't get a request
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST", "we don't have metadata yet");
			peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE", "piece: %d s: %x l: %x no metadata"
				, r.piece, r.start, r.length);
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
			peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE", "piece: %d s: %x l: %x too many requests"
				, r.piece, r.start, r.length);
#endif
			write_reject_request(r);
			return;
		}

		int fast_idx = -1;
		std::vector<int>::iterator fast_iter = std::find(m_accept_fast.begin()
			, m_accept_fast.end(), r.piece);
		if (fast_iter != m_accept_fast.end()) fast_idx = fast_iter - m_accept_fast.begin();

		if (!m_peer_interested)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST", "peer is not interested "
				" t: %d n: %d block_limit: %d"
				, valid_piece_index
					? int(t->torrent_file().piece_size(r.piece)) : -1
				, t->torrent_file().num_pieces()
				, t->block_size());
			peer_log(peer_log_alert::info, "INTERESTED", "artificial incoming INTERESTED message");
#endif
			if (t->alerts().should_post<invalid_request_alert>())
			{
				// msvc 12 appears to deduce the rvalue reference template
				// incorrectly for bool temporaries. So, create a dummy instance
				bool peer_interested = bool(m_peer_interested);
				t->alerts().emplace_alert<invalid_request_alert>(
					t->get_handle(), m_remote, m_peer_id, r
					, t->has_piece_passed(r.piece)
					, peer_interested, false);
			}

			// be lenient and pretend that the peer said it was interested
			incoming_interested();
		}

		// make sure this request
		// is legal and that the peer
		// is not choked
		if (r.piece < 0
			|| r.piece >= t->torrent_file().num_pieces()
			|| (!t->has_piece_passed(r.piece)
				&& !t->is_predictive_piece(r.piece)
				&& !t->seed_mode())
			|| r.start < 0
			|| r.start >= t->torrent_file().piece_size(r.piece)
			|| r.length <= 0
			|| r.length + r.start > t->torrent_file().piece_size(r.piece)
			|| r.length > t->block_size())
		{
			m_counters.inc_stats_counter(counters::invalid_piece_requests);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST"
				, "i: %d t: %d n: %d h: %d block_limit: %d"
				, m_peer_interested
				, valid_piece_index
					? int(t->torrent_file().piece_size(r.piece)) : -1
				, t->torrent_file().num_pieces()
				, t->has_piece_passed(r.piece)
				, t->block_size());

			peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE"
				, "piece: %d s: %d l: %d invalid request"
				, r.piece , r.start , r.length);
#endif

			write_reject_request(r);
			++m_num_invalid_requests;

			if (t->alerts().should_post<invalid_request_alert>())
			{
				// msvc 12 appears to deduce the rvalue reference template
				// incorrectly for bool temporaries. So, create a dummy instance
				bool peer_interested = bool(m_peer_interested);
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
					&& can_disconnect(error_code(errors::too_many_requests_when_choked
						, get_libtorrent_category())))
				{
					disconnect(errors::too_many_requests_when_choked, op_bittorrent, 2);
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
		const int blocks_per_piece = static_cast<int>(
			(t->torrent_file().piece_length() + t->block_size() - 1) / t->block_size());

		// disconnect peers that downloads more than foo times an allowed
		// fast piece
		if (m_choked && fast_idx != -1 && m_accept_fast_piece_cnt[fast_idx] >= 3 * blocks_per_piece
			&& can_disconnect(error_code(errors::too_many_requests_when_choked, get_libtorrent_category())))
		{
			disconnect(errors::too_many_requests_when_choked, op_bittorrent, 2);
			return;
		}

		if (m_choked && fast_idx == -1)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "REJECTING REQUEST", "peer choked and piece not in allowed fast set");
			peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE", "piece: %d s: %d l: %d peer choked"
				, r.piece, r.start, r.length);
#endif
			m_counters.inc_stats_counter(counters::choked_piece_requests);
			write_reject_request(r);

			// allow peers to send request up to 2 seconds after getting choked,
			// then disconnect them
			if (aux::time_now() - seconds(2) > m_last_choke
				&& can_disconnect(error_code(errors::too_many_requests_when_choked, get_libtorrent_category())))
			{
				disconnect(errors::too_many_requests_when_choked, op_bittorrent, 2);
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
			TORRENT_ASSERT(r.piece >= 0);
			TORRENT_ASSERT(r.piece < t->torrent_file().num_pieces());

			m_requests.push_back(r);

			if (t->alerts().should_post<incoming_request_alert>())
			{
				t->alerts().emplace_alert<incoming_request_alert>(r, t->get_handle()
					, m_remote, m_peer_id);
			}

			m_last_incoming_request = aux::time_now();
			fill_send_buffer();
		}
	}

	// reject all requests to this piece
	void peer_connection::reject_piece(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		for (std::vector<peer_request>::iterator i = m_requests.begin()
			, end(m_requests.end()); i != end; ++i)
		{
			peer_request const& r = *i;
			if (r.piece != index) continue;
			write_reject_request(r);
			i = m_requests.erase(i);

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests, -1);
		}
	}

	void peer_connection::incoming_piece_fragment(int bytes)
	{
		TORRENT_ASSERT(is_single_thread());
		m_last_piece = aux::time_now();
		TORRENT_ASSERT(m_outstanding_bytes >= bytes);
		m_outstanding_bytes -= bytes;
		if (m_outstanding_bytes < 0) m_outstanding_bytes = 0;
		boost::shared_ptr<torrent> t = associated_torrent().lock();
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
		buffer::const_interval recv_buffer = m_recv_buffer.get();
		int recv_pos = recv_buffer.end - recv_buffer.begin;
		TORRENT_ASSERT(recv_pos >= 9);
#endif

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		if (!verify_piece(r))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_PIECE", "piece: %d s: %d l: %d"
				, r.piece, r.start, r.length);
#endif
			disconnect(errors::invalid_piece, op_bittorrent, 2);
			return;
		}

		piece_block b(r.piece, r.start / t->block_size());
		m_receiving_block = b;

		bool in_req_queue = false;
		for (std::vector<pending_block>::iterator i = m_download_queue.begin()
			, end(m_download_queue.end()); i != end; ++i)
		{
			if (i->block != b) continue;
			in_req_queue = true;
			break;
		}

		// if this is not in the request queue, we have to
		// assume our outstanding bytes includes this piece too
		// if we're disconnecting, we shouldn't add pieces
		if (!in_req_queue && !m_disconnecting)
		{
			for (std::vector<pending_block>::iterator i = m_request_queue.begin()
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
						, m_remote, m_peer_id, int(b.block_index), int(b.piece_index));
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
		check_postcondition(boost::shared_ptr<torrent> const& t_
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

		shared_ptr<torrent> t;
	};
#endif


	// -----------------------------
	// ----------- PIECE -----------
	// -----------------------------

	void peer_connection::incoming_piece(peer_request const& p, char const* data)
	{
		TORRENT_ASSERT(is_single_thread());
		bool exceeded = false;
		char* buffer = m_allocator.allocate_disk_buffer(exceeded, self(), "receive buffer");

		if (buffer == 0)
		{
			disconnect(errors::no_memory, op_alloc_recvbuf);
			return;
		}

		// every peer is entitled to have two disk blocks allocated at any given
		// time, regardless of whether the cache size is exceeded or not. If this
		// was not the case, when the cache size setting is very small, most peers
		// would be blocked most of the time, because the disk cache would
		// continously be in exceeded state. Only rarely would it actually drop
		// down to 0 and unblock all peers.
		if (exceeded && m_outstanding_writing_bytes > 0)
		{
			if ((m_channel_state[download_channel] & peer_info::bw_disk) == 0)
				m_counters.inc_stats_counter(counters::num_peers_down_disk);
			m_channel_state[download_channel] |= peer_info::bw_disk;
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "DISK", "exceeded disk buffer watermark");
#endif
		}

		disk_buffer_holder holder(m_allocator, buffer);
		std::memcpy(buffer, data, p.length);
		incoming_piece(p, holder);
	}

	void peer_connection::incoming_piece(peer_request const& p, disk_buffer_holder& data)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		m_recv_buffer.assert_no_disk_buffer();

		// we're not receiving any block right now
		m_receiving_block = piece_block::invalid;

#ifdef TORRENT_CORRUPT_DATA
		// corrupt all pieces from certain peers
		if (m_remote.address().is_v4()
			&& (m_remote.address().to_v4().to_ulong() & 0xf) == 0)
		{
			data.get()[0] = ~data.get()[0];
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
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_piece(p, data))
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
		hasher h;
		h.update(data.get(), p.length);
		peer_log(peer_log_alert::incoming_message, "PIECE", "piece: %d s: %x l: %x ds: %d qs: %d q: %d hash: %s"
			, p.piece, p.start, p.length, statistics().download_rate()
			, int(m_desired_queue_size), int(m_download_queue.size())
			, to_hex(h.final().to_string()).c_str());
#endif

		if (p.length == 0)
		{
			if (t->alerts().should_post<peer_error_alert>())
			{
				t->alerts().emplace_alert<peer_error_alert>(t->get_handle(), m_remote
					, m_peer_id, op_bittorrent, errors::peer_sent_empty_piece);
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
			t->add_redundant_bytes(p.length, torrent::piece_seed);
			return;
		}

		time_point now = clock_type::now();

		t->need_picker();

		piece_picker& picker = t->picker();

		piece_block block_finished(p.piece, p.start / t->block_size());
		TORRENT_ASSERT(verify_piece(p));

		std::vector<pending_block>::iterator b
			= std::find_if(
				m_download_queue.begin()
				, m_download_queue.end()
				, has_block(block_finished));

		if (b == m_download_queue.end())
		{
			if (t->alerts().should_post<unwanted_block_alert>())
			{
				t->alerts().emplace_alert<unwanted_block_alert>(t->get_handle()
					, m_remote, m_peer_id, int(block_finished.block_index)
					, int(block_finished.piece_index));
			}
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "INVALID_REQUEST", "The block we just got was not in the request queue");
#endif
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT_VAL(m_received_in_piece == p.length, m_received_in_piece);
			m_received_in_piece = 0;
#endif
			t->add_redundant_bytes(p.length, torrent::piece_unknown);

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
			torrent::wasted_reason_t reason;
			if (b->timed_out) reason = torrent::piece_timed_out;
			else if (b->not_wanted) reason = torrent::piece_cancelled;
			else if (b->busy) reason = torrent::piece_end_game;
			else reason = torrent::piece_unknown;

			t->add_redundant_bytes(p.length, reason);

			m_download_queue.erase(b);
			if (m_download_queue.empty())
				m_counters.inc_stats_counter(counters::num_peers_down_requests, -1);

			if (m_disconnecting) return;

			m_request_time.add_sample(total_milliseconds(now - m_requested));
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "REQUEST_TIME", "%d +- %d ms"
				, m_request_time.mean(), m_request_time.avg_deviation());
#endif

			// we completed an incoming block, and there are still outstanding
			// requests. The next block we expect to receive now has another
			// timeout period until we time out. So, reset the timer.
			if (!m_download_queue.empty())
				m_requested = now;

			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::incoming_redundant_piece_picks);
			send_block_requests();
			return;
		}

		// we received a request within the timeout, make sure this peer is
		// not snubbed anymore
		if (total_seconds(now - m_requested)
			< request_timeout()
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
		t->debug_log("PIECE [%p] (%d ms) (%d)", static_cast<void*>(this)
			, int(total_milliseconds(clock_type::now() - m_unchoke_time)), t->num_have());

		peer_log(peer_log_alert::info, "FILE_ASYNC_WRITE", "piece: %d s: %x l: %x"
			, p.piece, p.start, p.length);
#endif
		m_download_queue.erase(b);
		if (m_download_queue.empty())
			m_counters.inc_stats_counter(counters::num_peers_down_requests, -1);

		if (t->is_deleted()) return;

		if (!t->need_loaded())
		{
			t->add_redundant_bytes(p.length, torrent::piece_unknown);
			return;
		}
		t->inc_refcount("async_write");
		m_disk_thread.async_write(&t->storage(), p, data
			, boost::bind(&peer_connection::on_disk_write_complete
			, self(), _1, p, t));

		boost::uint64_t write_queue_size = m_counters.inc_stats_counter(
			counters::queued_write_bytes, p.length);
		m_outstanding_writing_bytes += p.length;

		boost::uint64_t max_queue_size = m_settings.get_int(
			settings_pack::max_queued_disk_bytes);
		if (write_queue_size > max_queue_size
			&& write_queue_size - p.length < max_queue_size
			&& m_settings.get_int(settings_pack::cache_size) > 5
			&& t->alerts().should_post<performance_alert>())
		{
			t->alerts().emplace_alert<performance_alert>(t->get_handle()
				, performance_alert::too_high_disk_queue_limit);
		}

		m_request_time.add_sample(total_milliseconds(now - m_requested));
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "REQUEST_TIME", "%d +- %d ms"
			, m_request_time.mean(), m_request_time.avg_deviation());
#endif

		// we completed an incoming block, and there are still outstanding
		// requests. The next block we expect to receive now has another
		// timeout period until we time out. So, reset the timer.
		if (!m_download_queue.empty())
			m_requested = now;

		bool was_finished = picker.is_piece_finished(p.piece);
		// did we request this block from any other peers?
		bool multi = picker.num_peers(block_finished) > 1;
//		fprintf(stderr, "peer_connection mark_as_writing peer: %p piece: %d block: %d\n"
//			, peer_info_struct(), block_finished.piece_index, block_finished.block_index);
		picker.mark_as_writing(block_finished, peer_info_struct());

		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);
		// if we requested this block from other peers, cancel it now
		if (multi) t->cancel_block(block_finished);

		if (m_settings.get_int(settings_pack::predictive_piece_announce))
		{
			int piece = block_finished.piece_index;
			piece_picker::downloading_piece st;
			t->picker().piece_info(piece, st);

			int num_blocks = t->picker().blocks_in_piece(piece);
			if (st.requested > 0 && st.writing + st.finished + st.requested == num_blocks)
			{
				std::vector<torrent_peer*> d;
				t->picker().get_downloaders(d, piece);
				if (d.size() == 1)
				{
					// only make predictions if all remaining
					// blocks are requested from the same peer
					torrent_peer* peer = d[0];
					if (peer->connection)
					{
						// we have a connection. now, what is the current
						// download rate from this peer, and how many blocks
						// do we have left to download?
						boost::int64_t rate = peer->connection->statistics().download_payload_rate();
						boost::int64_t bytes_left = boost::int64_t(st.requested) * t->block_size();
						// the settings unit is milliseconds, so calculate the
						// number of milliseconds worth of bytes left in the piece
						if (rate > 1000
							&& (bytes_left * 1000) / rate < m_settings.get_int(settings_pack::predictive_piece_announce))
						{
							// we predict we will complete this piece very soon.
							t->predicted_have_piece(piece, (bytes_left * 1000) / rate);
						}
					}
				}
			}
		}

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
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || !t->graceful_pause()) return;

		if (m_outstanding_bytes > 0) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "GRACEFUL_PAUSE", "NO MORE DOWNLOAD");
#endif
		disconnect(errors::torrent_paused, op_bittorrent);
	}

	void peer_connection::on_disk_write_complete(disk_io_job const* j
		, peer_request p, boost::shared_ptr<torrent> t)
	{
		TORRENT_ASSERT(is_single_thread());
		torrent_ref_holder h(t.get(), "async_write");
		if (t) t->dec_refcount("async_write");

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "FILE_ASYNC_WRITE_COMPLETE", "ret: %d piece: %d s: %x l: %x e: %s"
			, j->ret, p.piece, p.start, p.length, j->error.ec.message().c_str());
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

		// flush send buffer at the end of
		// this burst of disk events
//		m_ses.cork_burst(this);

		INVARIANT_CHECK;

		if (!t)
		{
			disconnect(j->error.ec, op_file_write);
			return;
		}

		t->schedule_storage_tick();

		// in case the outstanding bytes just dropped down
		// to allow to receive more data
		setup_receive();

		piece_block block_finished(p.piece, p.start / t->block_size());

		if (j->ret < 0)
		{
			// handle_disk_error may disconnect us
			t->handle_disk_error(j, this);
			return;
		}

		TORRENT_ASSERT(j->ret == p.length);

		if (!t->has_picker()) return;

		piece_picker& picker = t->picker();

		TORRENT_ASSERT(p.piece == j->piece);
		TORRENT_ASSERT(p.start == j->d.io.offset);
		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);

		if (j->ret == -1 && j->error.ec == boost::system::errc::operation_canceled)
		{
			TORRENT_ASSERT(false); // how do we get here?
			picker.mark_as_canceled(block_finished, peer_info_struct());
			return;
		}
//		fprintf(stderr, "peer_connection mark_as_finished peer: %p piece: %d block: %d\n"
//			, peer_info_struct(), block_finished.piece_index, block_finished.block_index);
		picker.mark_as_finished(block_finished, peer_info_struct());

		t->maybe_done_flushing();

		if (t->alerts().should_post<block_finished_alert>())
		{
			t->alerts().emplace_alert<block_finished_alert>(t->get_handle(),
				remote(), pid(), int(block_finished.block_index)
				, int(block_finished.piece_index));
		}

		disconnect_if_redundant();

		if (m_disconnecting) return;

#if TORRENT_USE_ASSERTS
		if (t->has_picker())
		{
			const std::vector<piece_picker::downloading_piece>& q
				= picker.get_download_queue();

			for (std::vector<piece_picker::downloading_piece>::const_iterator
				i = q.begin(), end(q.end()); i != end; ++i)
			{
				if (i->index != block_finished.piece_index) continue;
				piece_picker::block_info* info = picker.blocks_for_piece(*i);
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
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_cancel(r)) return;
		}
#endif
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "CANCEL"
			, "piece: %d s: %x l: %x", r.piece, r.start, r.length);
#endif

		std::vector<peer_request>::iterator i
			= std::find(m_requests.begin(), m_requests.end(), r);

		if (i != m_requests.end())
		{
			m_counters.inc_stats_counter(counters::cancelled_piece_requests);
			m_requests.erase(i);

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests, -1);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE", "piece: %d s: %x l: %x cancelled"
				, r.piece , r.start , r.length);
#endif
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

	void peer_connection::incoming_dht_port(int listen_port)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "DHT_PORT", "p: %d", listen_port);
#endif
#ifndef TORRENT_DISABLE_DHT
		m_ses.add_dht_node(udp::endpoint(
			m_remote.address(), listen_port));
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

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		// we cannot disconnect in a constructor, and
		// this function may end up doing that
		TORRENT_ASSERT(m_in_constructor == false);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming_message, "HAVE_ALL");
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_have_all()) return;
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

#ifndef TORRENT_DISABLE_LOGGING
		m_bitfield_time = clock_type::now();
		t->debug_log("HANDSHAKE [%p] (%d ms)"
			, static_cast<void*>(this)
			, int(total_milliseconds(m_bitfield_time - m_connect_time)));
#endif

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

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_have_none()) return;
		}
#endif
		if (is_disconnecting()) return;

		if (m_bitfield_received)
			t->peer_lost(m_have_piece, this);

		t->set_seed(m_peer_info, false);
		m_bitfield_received = true;

#ifndef TORRENT_DISABLE_LOGGING
		m_bitfield_time = clock_type::now();
		t->debug_log("HANDSHAKE [%p] (%d ms)"
			, static_cast<void*>(this)
			, int(total_milliseconds(m_bitfield_time - m_connect_time)));
#endif
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

	void peer_connection::incoming_allowed_fast(int index)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
		{
			time_point now = clock_type::now();
			t->debug_log("ALLOW FAST [%p] (%d ms)"
				, static_cast<void*>(this)
				, int(total_milliseconds(now - m_connect_time)));
			if (m_peer_choked) m_unchoke_time = now;
		}
		peer_log(peer_log_alert::incoming_message, "ALLOWED_FAST", "%d", index);
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_allowed_fast(index)) return;
		}
#endif
		if (is_disconnecting()) return;

		if (index < 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "INVALID_ALLOWED_FAST"
				, "%d", index);
#endif
			return;
		}

		if (t->valid_metadata())
		{
			if (index >= int(m_have_piece.size()))
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::incoming_message, "INVALID_ALLOWED_FAST"
					, "%d s: %d", index, int(m_have_piece.size()));
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
		if (int(m_have_piece.size()) > index
			&& m_have_piece[index]
			&& !t->has_piece_passed(index)
			&& t->valid_metadata()
			&& t->has_picker()
			&& t->picker().piece_priority(index) > 0)
		{
			t->peer_is_interesting(*this);
		}
	}

	std::vector<int> const& peer_connection::allowed_fast()
	{
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<torrent> t = m_torrent.lock();
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
		boost::shared_ptr<torrent> t = m_torrent.lock();
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
		std::vector<pending_block>::iterator rit = std::find_if(m_request_queue.begin()
			, m_request_queue.end(), has_block(block));
		if (rit == m_request_queue.end()) return false;
#if TORRENT_USE_ASSERTS
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->has_picker());
		TORRENT_ASSERT(t->picker().is_requested(block));
#endif
		// ignore it if it's already time critical
		if (rit - m_request_queue.begin() < m_queued_time_critical) return false;
		pending_block b = *rit;
		m_request_queue.erase(rit);
		m_request_queue.insert(m_request_queue.begin() + m_queued_time_critical, b);
		++m_queued_time_critical;
		return true;
	}

	bool peer_connection::add_request(piece_block const& block, int flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(!m_disconnecting);
		TORRENT_ASSERT(t->valid_metadata());

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(int(block.piece_index) < t->torrent_file().num_pieces());
		TORRENT_ASSERT(int(block.block_index) < t->torrent_file().piece_size(block.piece_index));
		TORRENT_ASSERT(!t->picker().is_requested(block) || (t->picker().num_peers(block) > 0));
		TORRENT_ASSERT(!t->have_piece(block.piece_index));
		TORRENT_ASSERT(std::find_if(m_download_queue.begin(), m_download_queue.end()
			, has_block(block)) == m_download_queue.end());
		TORRENT_ASSERT(std::find(m_request_queue.begin(), m_request_queue.end()
			, block) == m_request_queue.end());

		if (t->upload_mode())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "PIECE_PICKER"
				, "not_picking: %d,%d upload_mode"
				, block.piece_index, block.block_index);
#endif
			return false;
		}
		if (m_disconnecting)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "PIECE_PICKER"
				, "not_picking: %d,%d disconnecting"
				, block.piece_index, block.block_index);
#endif
			return false;
		}

		if ((flags & req_busy) && !(flags & req_time_critical))
		{
			// this block is busy (i.e. it has been requested
			// from another peer already). Only allow one busy
			// request in the pipeline at the time
			// this rule does not apply to time critical pieces,
			// in which case we are allowed to pick more than one
			// busy blocks
			for (std::vector<pending_block>::const_iterator i = m_download_queue.begin()
				, end(m_download_queue.end()); i != end; ++i)
			{
				if (i->busy)
				{
#ifndef TORRENT_DISABLE_LOGGING
					peer_log(peer_log_alert::info, "PIECE_PICKER"
						, "not_picking: %d,%d already in download queue & busy"
						, block.piece_index, block.block_index);
#endif
					return false;
				}
			}

			for (std::vector<pending_block>::const_iterator i = m_request_queue.begin()
				, end(m_request_queue.end()); i != end; ++i)
			{
				if (i->busy)
				{
#ifndef TORRENT_DISABLE_LOGGING
					peer_log(peer_log_alert::info, "PIECE_PICKER"
						, "not_picking: %d,%d already in request queue & busy"
						, block.piece_index, block.block_index);
#endif
					return false;
				}
			}
		}

		if (!t->picker().mark_as_downloading(block, peer_info_struct()
			, picker_options()))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "PIECE_PICKER"
				, "not_picking: %d,%d failed to mark_as_downloading"
				, block.piece_index, block.block_index);
#endif
			return false;
		}

		if (t->alerts().should_post<block_downloading_alert>())
		{
			t->alerts().emplace_alert<block_downloading_alert>(t->get_handle()
				, remote(), pid(), block.block_index, block.piece_index);
		}

		pending_block pb(block);
		pb.busy = (flags & req_busy) ? true : false;
		if (flags & req_time_critical)
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

		boost::shared_ptr<torrent> t = m_torrent.lock();
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

		for (std::vector<pending_block>::iterator i = temp_copy.begin()
			, end(temp_copy.end()); i != end; ++i)
		{
			piece_block b = i->block;

			int block_offset = b.block_index * t->block_size();
			int block_size
				= (std::min)(t->torrent_file().piece_size(b.piece_index)-block_offset,
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
				, b.piece_index, block_offset, block_size, b.block_index);
#endif
			write_cancel(r);
		}
	}

	void peer_connection::cancel_request(piece_block const& block, bool force)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		// this peer might be disconnecting
		if (!t) return;

		TORRENT_ASSERT(t->valid_metadata());

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(int(block.piece_index) < t->torrent_file().num_pieces());
		TORRENT_ASSERT(int(block.block_index) < t->torrent_file().piece_size(block.piece_index));

		// if all the peers that requested this block has been
		// cancelled, then just ignore the cancel.
		if (!t->picker().is_requested(block)) return;

		std::vector<pending_block>::iterator it
			= std::find_if(m_download_queue.begin(), m_download_queue.end(), has_block(block));
		if (it == m_download_queue.end())
		{
			std::vector<pending_block>::iterator rit = std::find_if(m_request_queue.begin()
				, m_request_queue.end(), has_block(block));

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

		int block_offset = block.block_index * t->block_size();
		int block_size
			= (std::min)(t->torrent_file().piece_size(block.piece_index)-block_offset,
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
				, block.piece_index, block_offset, block_size, block.block_index);
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
			TORRENT_ASSERT(m_peer_info == NULL
				|| m_peer_info->optimistically_unchoked == false);
			return false;
		}

		if (m_peer_info && m_peer_info->optimistically_unchoked)
		{
			m_peer_info->optimistically_unchoked = false;
			m_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic, -1);
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "CHOKE");
#endif
		write_choke();
		m_counters.inc_stats_counter(counters::num_peers_up_unchoked_all, -1);
		if (!ignore_unchoke_slots())
			m_counters.inc_stats_counter(counters::num_peers_up_unchoked, -1);
		m_choked = true;

		m_last_choke = aux::time_now();
		m_num_invalid_requests = 0;

		// reject the requests we have in the queue
		// except the allowed fast pieces
		for (std::vector<peer_request>::iterator i = m_requests.begin();
			i != m_requests.end();)
		{
			if (std::find(m_accept_fast.begin(), m_accept_fast.end(), i->piece)
				!= m_accept_fast.end())
			{
				++i;
				continue;
			}
			peer_request const& r = *i;
			m_counters.inc_stats_counter(counters::choked_piece_requests);
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE"
				, "piece: %d s: %d l: %d choking"
				, r.piece , r.start , r.length);
#endif
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
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return false;

		if (!m_sent_suggests)
		{
			std::vector<torrent::suggest_piece_t> const& ret
				= t->get_suggested_pieces();

			for (std::vector<torrent::suggest_piece_t>::const_iterator i = ret.begin()
				, end(ret.end()); i != end; ++i)
			{
				TORRENT_ASSERT(i->piece_index >= 0);
				// this can happen if a piece fail to be
				// flushed to disk for whatever reason
				if (!t->has_piece_passed(i->piece_index)) continue;
				send_suggest(i->piece_index);
			}

			m_sent_suggests = true;
		}

		m_last_unchoke = aux::time_now();
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
		boost::shared_ptr<torrent> t = m_torrent.lock();
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

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return;
		m_interesting = false;
		m_slow_start = false;
		m_counters.inc_stats_counter(counters::num_peers_down_interested, -1);

		disconnect_if_redundant();
		if (m_disconnecting) return;

		write_not_interested();

		m_became_uninteresting = aux::time_now();

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "NOT_INTERESTED");
#endif
	}

	void peer_connection::send_suggest(int piece)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_connecting) return;
		if (in_handshake()) return;

		// don't suggest a piece that the peer already has
		// don't suggest anything to a peer that isn't interested
		if (has_piece(piece) || !m_peer_interested)
			return;

		// we cannot suggest a piece we don't have!
#if TORRENT_USE_ASSERTS
		{
			boost::shared_ptr<torrent> t = m_torrent.lock();
			TORRENT_ASSERT(t);
			TORRENT_ASSERT(t->has_piece_passed(piece));
			TORRENT_ASSERT(piece >= 0 && piece < t->torrent_file().num_pieces());
		}
#endif


		if (m_sent_suggested_pieces.empty())
		{
			boost::shared_ptr<torrent> t = m_torrent.lock();
			m_sent_suggested_pieces.resize(t->torrent_file().num_pieces(), false);
		}

		TORRENT_ASSERT(piece >= 0 && piece < m_sent_suggested_pieces.size());

		if (m_sent_suggested_pieces[piece]) return;
		m_sent_suggested_pieces.set_bit(piece);

		write_suggest(piece);
	}

	void peer_connection::send_block_requests()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (m_disconnecting) return;

		// TODO: 3 once peers are properly put in graceful pause mode, they can
		// cancel all outstanding requests and this test can be removed.
		if (t->graceful_pause()) return;

		// we can't download pieces in these states
		if (t->state() == torrent_status::checking_files
			|| t->state() == torrent_status::checking_resume_data
			|| t->state() == torrent_status::downloading_metadata
			|| t->state() == torrent_status::allocating)
			return;

		if (int(m_download_queue.size()) >= m_desired_queue_size
			|| t->upload_mode()) return;

		bool empty_download_queue = m_download_queue.empty();

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
			if (t->picker().is_finished(block.block)
				|| t->picker().is_downloaded(block.block))
			{
				t->picker().abort_download(block.block, peer_info_struct());
				continue;
			}

			int block_offset = block.block.block_index * t->block_size();
			int block_size = (std::min)(t->torrent_file().piece_size(
				block.block.piece_index) - block_offset, t->block_size());
			TORRENT_ASSERT(block_size > 0);
			TORRENT_ASSERT(block_size <= t->block_size());

			peer_request r;
			r.piece = block.block.piece_index;
			r.start = block_offset;
			r.length = block_size;

			if (m_download_queue.empty())
				m_counters.inc_stats_counter(counters::num_peers_down_requests);

			TORRENT_ASSERT(verify_piece(t->to_req(block.block)));
			block.send_buffer_offset = m_send_buffer.size();
			m_download_queue.push_back(block);
			m_outstanding_bytes += block_size;
#if TORRENT_USE_INVARIANT_CHECKS
			check_invariant();
#endif

			// if we are requesting large blocks, merge the smaller
			// blocks that are in the same piece into larger requests
			if (m_request_large_blocks)
			{
				int blocks_per_piece = t->torrent_file().piece_length() / t->block_size();

				while (!m_request_queue.empty())
				{
					// check to see if this block is connected to the previous one
					// if it is, merge them, otherwise, break this merge loop
					pending_block const& front = m_request_queue.front();
					if (front.block.piece_index * blocks_per_piece + front.block.block_index
						!= block.block.piece_index * blocks_per_piece + block.block.block_index + 1)
						break;
					block = m_request_queue.front();
					m_request_queue.erase(m_request_queue.begin());
					TORRENT_ASSERT(verify_piece(t->to_req(block.block)));

					if (m_download_queue.empty())
						m_counters.inc_stats_counter(counters::num_peers_down_requests);

					block.send_buffer_offset = m_send_buffer.size();
					m_download_queue.push_back(block);
					if (m_queued_time_critical) --m_queued_time_critical;

#ifndef TORRENT_DISABLE_LOGGING
					peer_log(peer_log_alert::info, "MERGING_REQUEST"
						, "piece: %d block: %d"
						, block.block.piece_index, block.block.block_index);
#endif

					block_offset = block.block.block_index * t->block_size();
					block_size = (std::min)(t->torrent_file().piece_size(
						block.block.piece_index) - block_offset, t->block_size());
					TORRENT_ASSERT(block_size > 0);
					TORRENT_ASSERT(block_size <= t->block_size());

					r.length += block_size;
					m_outstanding_bytes += block_size;
#if TORRENT_USE_INVARIANT_CHECKS
					check_invariant();
#endif
				}
			}

			// the verification will fail for coalesced blocks
			TORRENT_ASSERT(verify_piece(r) || m_request_large_blocks);

#ifndef TORRENT_DISABLE_EXTENSIONS
			bool handled = false;
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				if ((handled = (*i)->write_request(r))) break;
			}
			if (is_disconnecting()) return;
			if (!handled)
#endif
			{
				write_request(r);
				m_last_request = aux::time_now();
			}

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing_message, "REQUEST"
				, "piece: %d s: %x l: %x ds: %dB/s dqs: %d rqs: %d blk: %s"
				, r.piece, r.start, r.length, statistics().download_rate()
				, int(m_desired_queue_size), int(m_download_queue.size())
				, m_request_large_blocks?"large":"single");
#endif
		}
		m_last_piece = aux::time_now();

		if (!m_download_queue.empty()
			&& empty_download_queue)
		{
			// This means we just added a request to this connection that
			// previously did not have a request. That's when we start the
			// request timeout.
			m_requested = aux::time_now();
#ifndef TORRENT_DISABLE_LOGGING
			t->debug_log("REQUEST [%p] (%d ms)", static_cast<void*>(this)
				, int(total_milliseconds(clock_type::now() - m_unchoke_time)));
#endif
		}
	}

	void peer_connection::connect_failed(error_code const& e)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(e);

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "CONNECTION FAILED"
			, "%s", print_endpoint(m_remote).c_str());
#endif
#ifndef TORRENT_DISABLE_LOGGING
		m_ses.session_log("CONNECTION FAILED: %s", print_endpoint(m_remote).c_str());
#endif

		m_counters.inc_stats_counter(counters::connect_timeouts);

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(!m_connecting || t);
		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t) t->dec_num_connecting();
			m_connecting = false;
		}

		// a connection attempt using uTP just failed
		// mark this peer as not supporting uTP
		// we'll never try it again (unless we're trying holepunch)
		if (is_utp(*m_socket)
			&& m_peer_info
			&& m_peer_info->supports_utp
			&& !m_holepunch_mode)
		{
			m_peer_info->supports_utp = false;
			// reconnect immediately using TCP
			torrent_peer* pi = peer_info_struct();
			fast_reconnect(true);
			disconnect(e, op_connect, 0);
			if (t && pi) t->connect_to_peer(pi, true);
			return;
		}

		if (m_holepunch_mode)
			fast_reconnect(true);

#ifndef TORRENT_DISABLE_EXTENSIONS
		if ((!is_utp(*m_socket)
				|| !m_settings.get_bool(settings_pack::enable_outgoing_tcp))
			&& m_peer_info
			&& m_peer_info->supports_holepunch
			&& !m_holepunch_mode)
		{
			// see if we can try a holepunch
			bt_peer_connection* p = t->find_introducer(remote());
			if (p)
				p->write_holepunch_msg(bt_peer_connection::hp_rendezvous, remote(), 0);
		}
#endif

		disconnect(e, op_connect, 1);
		return;
	}

	// the error argument defaults to 0, which means deliberate disconnect
	// 1 means unexpected disconnect/error
	// 2 protocol error (client sent something invalid)
	void peer_connection::disconnect(error_code const& ec
		, operation_t op, int error)
	{
		TORRENT_ASSERT(is_single_thread());
#if TORRENT_USE_ASSERTS
		m_disconnect_started = true;
#endif

		if (m_disconnecting) return;

		m_socket->set_close_reason(error_to_close_reason(ec));
		close_reason_t close_reason = close_reason_t(m_socket->get_close_reason());
#ifndef TORRENT_DISABLE_LOGGING
		if (close_reason != 0)
		{
			peer_log(peer_log_alert::info, "CLOSE_REASON", "%d", int(close_reason));
		}
#endif

		// while being disconnected, it's possible that our torrent_peer
		// pointer gets cleared. Make sure we save it to be able to keep
		// proper books in the piece_picker (when debugging is enabled)
		torrent_peer* self_peer = peer_info_struct();

#ifndef TORRENT_DISABLE_LOGGING
		switch (error)
		{
		case 0:
			peer_log(peer_log_alert::info, "CONNECTION_CLOSED", "op: %d error: %s"
				, op, ec.message().c_str());
			break;
		case 1:
			peer_log(peer_log_alert::info, "CONNECTION_FAILED", "op: %d error: %s"
				, op, ec.message().c_str());
			break;
		case 2:
			peer_log(peer_log_alert::info, "PEER_ERROR" ,"op: %d error: %s"
				, op, ec.message().c_str());
			break;
		}

		if (ec == error_code(boost::asio::error::eof
			, boost::asio::error::get_misc_category())
			&& !in_handshake()
			&& !is_connecting()
			&& clock_type::now() - connected_time() < seconds(15))
		{
			peer_log(peer_log_alert::info, "SHORT_LIVED_DISCONNECT", "");
		}
#endif

		if ((m_channel_state[upload_channel] & peer_info::bw_network) == 0)
		{
			// make sure we free up all send buffers that are owned
			// by the disk thread
			m_send_buffer.clear();
			m_recv_buffer.free_disk_buffer();
		}

		// we cannot do this in a constructor
		TORRENT_ASSERT(m_in_constructor == false);
		if (error > 0)
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
		if (error == 2) m_counters.inc_stats_counter(counters::error_peers);

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
		else if (ec == error_code(errors::upload_upload_connection)
			|| ec == error_code(errors::uninteresting_upload_peer)
			|| ec == error_code(errors::torrent_aborted)
			|| ec == error_code(errors::self_connection)
			|| ec == error_code(errors::torrent_paused))
			m_counters.inc_stats_counter(counters::uninteresting_peers);

		if (ec == error_code(errors::timed_out)
			|| ec == error::timed_out)
			m_counters.inc_stats_counter(counters::transport_timeout_peers);

		if (ec == error_code(errors::timed_out_inactivity)
			|| ec == error_code(errors::timed_out_no_request)
			|| ec == error_code(errors::timed_out_no_interest))
			m_counters.inc_stats_counter(counters::timeout_peers);

		if (ec == error_code(errors::no_memory))
			m_counters.inc_stats_counter(counters::no_memory_peers);

		if (ec == error_code(errors::too_many_connections))
			m_counters.inc_stats_counter(counters::too_many_peers);

		if (ec == error_code(errors::timed_out_no_handshake))
			m_counters.inc_stats_counter(counters::connect_timeouts);

		if (error > 0)
		{
			if (is_utp(*m_socket)) m_counters.inc_stats_counter(counters::error_utp_peers);
			else m_counters.inc_stats_counter(counters::error_tcp_peers);

			if (m_outgoing) m_counters.inc_stats_counter(counters::error_outgoing_peers);
			else m_counters.inc_stats_counter(counters::error_incoming_peers);

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			if (type() == bittorrent_connection && op != op_connect)
			{
				bt_peer_connection* bt = static_cast<bt_peer_connection*>(this);
				if (bt->supports_encryption()) m_counters.inc_stats_counter(
					counters::error_encrypted_peers);
				if (bt->rc4_encrypted() && bt->supports_encryption())
					m_counters.inc_stats_counter(counters::error_rc4_peers);
			}
#endif // TORRENT_DISABLE_ENCRYPTION
		}

		boost::shared_ptr<peer_connection> me(self());

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

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t) t->dec_num_connecting();
			m_connecting = false;
		}

		torrent_handle handle;
		if (t) handle = t->get_handle();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			(*i)->on_disconnect(ec);
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

		if (t)
		{
			if (ec)
			{
				if ((error > 1 || ec.category() == get_socks_category())
					&& t->alerts().should_post<peer_error_alert>())
				{
					t->alerts().emplace_alert<peer_error_alert>(handle, remote()
						, pid(), op, ec);
				}

				if (error <= 1 && t->alerts().should_post<peer_disconnected_alert>())
				{
					t->alerts().emplace_alert<peer_disconnected_alert>(handle
						, remote(), pid(), op, m_socket->type(), ec, close_reason);
				}
			}

			// make sure we keep all the stats!
			if (!m_ignore_stats)
			{
				// report any partially received payload as redundant
				boost::optional<piece_block_progress> pbp = downloading_piece_progress();
				if (pbp
					&& pbp->bytes_downloaded > 0
					&& pbp->bytes_downloaded < pbp->full_block_bytes)
				{
					t->add_redundant_bytes(pbp->bytes_downloaded, torrent::piece_closing);
				}
			}

			if (t->has_picker())
			{
				piece_picker& picker = t->picker();

				while (!m_download_queue.empty())
				{
					pending_block& qe = m_download_queue.back();
					if (!qe.timed_out && !qe.not_wanted)
						picker.abort_download(qe.block, self_peer);
					m_outstanding_bytes -= t->to_req(qe.block).length;
					if (m_outstanding_bytes < 0) m_outstanding_bytes = 0;
					m_download_queue.pop_back();
				}
				while (!m_request_queue.empty())
				{
					pending_block& qe = m_request_queue.back();
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
			check_invariant();
#endif
			t->remove_peer(this);
		}
		else
		{
			TORRENT_ASSERT(m_download_queue.empty());
			TORRENT_ASSERT(m_request_queue.empty());
		}

#if defined TORRENT_DEBUG && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		// since this connection doesn't have a torrent reference
		// no torrent should have a reference to this connection either
		TORRENT_ASSERT(!m_ses.any_torrent_has_peer(this));
#endif

		m_disconnecting = true;
		error_code e;

		async_shutdown(*m_socket, m_socket);

		m_ses.close_connection(this, ec);
	}

	bool peer_connection::ignore_unchoke_slots() const
	{
		TORRENT_ASSERT(is_single_thread());
		if (num_classes() == 0) return true;

		if (m_ses.ignore_unchoke_slots_set(*this)) return true;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (t && m_ses.ignore_unchoke_slots_set(*t)) return true;
		return false;
	}

	// defined in upnp.cpp
	bool is_local(address const& a);

	bool peer_connection::on_local_network() const
	{
		TORRENT_ASSERT(is_single_thread());
		if (libtorrent::is_local(m_remote.address())
			|| is_loopback(m_remote.address())) return true;
		return false;
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
		ret = (std::min)((ret + 999) / 1000
			, m_settings.get_int(settings_pack::request_timeout));

		// timeouts should never be less than 2 seconds. The granularity is whole
		// seconds, and only checked once per second. 2 is the minimum to avoid
		// being considered timed out instantly
		return (std::max)(2, ret);
	}

	void peer_connection::get_peer_info(peer_info& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!associated_torrent().expired());

		time_point now = aux::time_now();

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
		else p.request_timeout = int(total_seconds(m_requested - now)
			+ request_timeout());

		p.download_queue_time = download_queue_time();
		p.queue_bytes = m_outstanding_bytes;

#ifndef TORRENT_NO_DEPRECATE
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		p.country[0] = m_country[0];
		p.country[1] = m_country[1];
#else
		std::fill(p.country, p.country + 2, 0);
#endif
#endif // TORRENT_NO_DEPRECATE

		p.total_download = statistics().total_payload_download();
		p.total_upload = statistics().total_payload_upload();
#ifndef TORRENT_NO_DEPRECATE
		p.upload_limit = -1;
		p.download_limit = -1;
		p.load_balancing = 0;
#endif

		p.download_queue_length = int(download_queue().size() + m_request_queue.size());
		p.requests_in_buffer = int(std::count_if(m_download_queue.begin()
			, m_download_queue.end()
			, &pending_block_in_buffer));

		p.target_dl_queue_length = int(desired_queue_size());
		p.upload_queue_length = int(upload_queue().size());
		p.timed_out_requests = 0;
		p.busy_requests = 0;
		for (std::vector<pending_block>::const_iterator i = m_download_queue.begin()
			, end(m_download_queue.end()); i != end; ++i)
		{
			if (i->timed_out) ++p.timed_out_requests;
			if (i->busy) ++p.busy_requests;
		}

		if (boost::optional<piece_block_progress> ret = downloading_piece_progress())
		{
			p.downloading_piece_index = ret->piece_index;
			p.downloading_block_index = ret->block_index;
			p.downloading_progress = ret->bytes_downloaded;
			p.downloading_total = ret->full_block_bytes;
		}
		else
		{
			p.downloading_piece_index = -1;
			p.downloading_block_index = -1;
			p.downloading_progress = 0;
			p.downloading_total = 0;
		}

		p.pieces = get_bitfield();
		p.last_request = now - m_last_request;
		p.last_active = now - (std::max)(m_last_sent, m_last_receive);

		// this will set the flags so that we can update them later
		p.flags = 0;
		get_specific_peer_info(p);

		p.flags |= is_seed() ? peer_info::seed : 0;
		p.flags |= m_snubbed ? peer_info::snubbed : 0;
		p.flags |= m_upload_only ? peer_info::upload_only : 0;
		p.flags |= m_endgame_mode ? peer_info::endgame_mode : 0;
		p.flags |= m_holepunch_mode ? peer_info::holepunched : 0;
		if (peer_info_struct())
		{
			torrent_peer* pi = peer_info_struct();
			TORRENT_ASSERT(pi->in_use);
			p.source = pi->source;
			p.failcount = pi->failcount;
			p.num_hashfails = pi->hashfails;
			p.flags |= pi->on_parole ? peer_info::on_parole : 0;
			p.flags |= pi->optimistically_unchoked ? peer_info::optimistic_unchoke : 0;
		}
		else
		{
			p.source = 0;
			p.failcount = 0;
			p.num_hashfails = 0;
		}

		p.remote_dl_rate = m_remote_dl_rate;
		p.send_buffer_size = m_send_buffer.capacity();
		p.used_send_buffer = m_send_buffer.size();
		p.receive_buffer_size = m_recv_buffer.capacity();
		p.used_receive_buffer = m_recv_buffer.pos();
		p.write_state = m_channel_state[upload_channel];
		p.read_state = m_channel_state[download_channel];

		// pieces may be empty if we don't have metadata yet
		if (p.pieces.size() == 0)
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
			p.progress_ppm = boost::uint64_t(p.pieces.count()) * 1000000 / p.pieces.size();
		}

		p.estimated_reciprocation_rate = m_est_reciprocation_rate;

		error_code ec;
		p.local_endpoint = get_socket()->local_endpoint(ec);
	}

	// allocates a disk buffer of size 'disk_buffer_size' and replaces the
	// end of the current receive buffer with it. i.e. the receive pos
	// must be <= packet_size - disk_buffer_size
	// the disk buffer can be accessed through release_disk_receive_buffer()
	// when it is queried, the responsibility to free it is transferred
	// to the caller
	bool peer_connection::allocate_disk_receive_buffer(int disk_buffer_size)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		m_recv_buffer.assert_no_disk_buffer();
		TORRENT_ASSERT(m_recv_buffer.pos() <= m_recv_buffer.packet_size() - disk_buffer_size);
		TORRENT_ASSERT(disk_buffer_size <= 16 * 1024);

		if (disk_buffer_size == 0) return true;

		if (disk_buffer_size > 16 * 1024)
		{
			disconnect(errors::invalid_piece_size, op_bittorrent, 2);
			return false;
		}

		// first free the old buffer
		m_recv_buffer.free_disk_buffer();
		// then allocate a new one

		bool exceeded = false;
		m_recv_buffer.assign_disk_buffer(
			m_allocator.allocate_disk_buffer(exceeded, self(), "receive buffer")
			, disk_buffer_size);

		if (!m_recv_buffer.has_disk_buffer())
		{
			disconnect(errors::no_memory, op_alloc_recvbuf);
			return false;
		}

		// to understand why m_outstanding_writing_bytes is here, see comment by
		// the other call to allocate_disk_buffer()
		if (exceeded && m_outstanding_writing_bytes > 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "DISK", "exceeded disk buffer watermark");
#endif
			if ((m_channel_state[download_channel] & peer_info::bw_disk) == 0)
				m_counters.inc_stats_counter(counters::num_peers_down_disk);
			m_channel_state[download_channel] |= peer_info::bw_disk;
		}

		return true;
	}

	void peer_connection::superseed_piece(int replace_piece, int new_piece)
	{
		TORRENT_ASSERT(is_single_thread());

		if (is_connecting()) return;
		if (in_handshake()) return;

		if (new_piece == -1)
		{
			if (m_superseed_piece[0] == -1) return;
			m_superseed_piece[0] = -1;
			m_superseed_piece[1] = -1;

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SUPER_SEEDING", "ending");
#endif
			boost::shared_ptr<torrent> t = m_torrent.lock();
			assert(t);

			// this will either send a full bitfield or
			// a have-all message, effectively terminating
			// super-seeding, since the peer may pick any piece
			write_bitfield();

			return;
		}

		assert(!has_piece(new_piece));

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "HAVE", "piece: %d (super seed)"
			, new_piece);
#endif
		write_have(new_piece);

		if (replace_piece >= 0)
		{
			// move the piece we're replacing to the tail
			if (m_superseed_piece[0] == replace_piece)
				std::swap(m_superseed_piece[0], m_superseed_piece[1]);
		}

		m_superseed_piece[1] = m_superseed_piece[0];
		m_superseed_piece[0] = new_piece;
	}

	void peer_connection::max_out_request_queue(int s)
	{
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "MAX_OUT_QUEUE_SIZE", "%d -> %d"
			, m_max_out_request_queue, s);
#endif
		m_max_out_request_queue = s;
	}

	int peer_connection::max_out_request_queue() const
	{
		return m_max_out_request_queue;
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
			boost::shared_ptr<torrent> t = m_torrent.lock();
			int const block_size = t->block_size();

			TORRENT_ASSERT(block_size > 0);

			m_desired_queue_size = queue_time * download_rate / block_size;
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
				, m_desired_queue_size, m_max_out_request_queue
				, download_rate, queue_time, int(m_snubbed), int(m_slow_start));
		}
#endif
	}

	void peer_connection::second_tick(int tick_interval_ms)
	{
		TORRENT_ASSERT(is_single_thread());
		time_point now = aux::time_now();
		boost::shared_ptr<peer_connection> me(self());

		// the invariant check must be run before me is destructed
		// in case the peer got disconnected
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();

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
				if (t) t->dec_num_connecting();
				m_connecting = false;
			}
			disconnect(errors::torrent_aborted, op_bittorrent);
			return;
		}

		if (m_endgame_mode
			&& m_interesting
			&& m_download_queue.empty()
			&& m_request_queue.empty()
			&& now - seconds(5) >= m_last_request)
		{
			// this happens when we're in strict end-game
			// mode and the peer could not request any blocks
			// because they were all taken but there were still
			// unrequested blocks. Now, 5 seconds later, there
			// might not be any unrequested blocks anymore, so
			// we should try to pick another block to see
			// if we can pick a busy one
			m_last_request = now;
			if (request_a_block(*t, *this))
				m_counters.inc_stats_counter(counters::end_game_piece_picks);
			if (m_disconnecting) return;
			send_block_requests();
		}

		if (t->super_seeding()
			&& t->ready_for_connections()
			&& !m_peer_interested
			&& m_became_uninterested + seconds(10) < now)
		{
			// maybe we need to try another piece, to see if the peer
			// become interested in us then
			superseed_piece(-1, t->get_piece_to_super_seed(m_have_piece));
		}

		on_tick();
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			(*i)->tick();
		}
		if (is_disconnecting()) return;
#endif

		// if the peer hasn't said a thing for a certain
		// time, it is considered to have timed out
		time_duration d;
		d = (std::min)(now - m_last_receive, now - m_last_sent);

		if (m_connecting)
		{
			int connect_timeout = m_settings.get_int(settings_pack::peer_connect_timeout);
			if (m_peer_info) connect_timeout += 3 * m_peer_info->failcount;

			// SSL and i2p handshakes are slow
			if (is_ssl(*m_socket))
				connect_timeout += 10;

#if TORRENT_USE_I2P
			if (is_i2p(*m_socket))
				connect_timeout += 20;
#endif

			if (d > seconds(connect_timeout)
				&& can_disconnect(error_code(errors::timed_out, get_libtorrent_category())))
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "CONNECT_FAILED", "waited %d seconds"
					, int(total_seconds(d)));
#endif
				connect_failed(errors::timed_out);
				return;
			}
		}

		// if we can't read, it means we're blocked on the rate-limiter
		// or the disk, not the peer itself. In this case, don't blame
		// the peer and disconnect it
		bool may_timeout = (m_channel_state[download_channel] & peer_info::bw_network) != 0;

		// TODO: 2 use a deadline_timer for timeouts. Don't rely on second_tick()!
		// Hook this up to connect timeout as well. This would improve performance
		// because of less work in second_tick(), and might let use remove ticking
		// entirely eventually
		if (may_timeout && d > seconds(timeout()) && !m_connecting && m_reading_bytes == 0
			&& can_disconnect(error_code(errors::timed_out_inactivity, get_libtorrent_category())))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "LAST_ACTIVITY", "%d seconds ago"
				, int(total_seconds(d)));
#endif
			disconnect(errors::timed_out_inactivity, op_bittorrent);
			return;
		}

		// do not stall waiting for a handshake
		int timeout = m_settings.get_int (settings_pack::handshake_timeout);
#if TORRENT_USE_I2P
		timeout *= is_i2p(*m_socket) ? 4 : 1;
#endif
		if (may_timeout
			&& !m_connecting
			&& in_handshake()
			&& d > seconds(timeout))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "NO_HANDSHAKE", "waited %d seconds"
				, int(total_seconds(d)));
#endif
			disconnect(errors::timed_out_no_handshake, op_bittorrent);
			return;
		}

		// disconnect peers that we unchoked, but they didn't send a request in
		// the last 60 seconds, and we haven't been working on servicing a request
		// for more than 60 seconds.
		// but only if we're a seed
		d = now - (std::max)((std::max)(m_last_unchoke, m_last_incoming_request)
			, m_last_sent_payload);

		if (may_timeout
			&& !m_connecting
			&& m_requests.empty()
			&& m_reading_bytes == 0
			&& !m_choked
			&& m_peer_interested
			&& t && t->is_upload_only()
			&& d > seconds(60)
			&& can_disconnect(error_code(errors::timed_out_no_request, get_libtorrent_category())))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "NO_REQUEST", "waited %d seconds"
				, int(total_seconds(d)));
#endif
			disconnect(errors::timed_out_no_request, op_bittorrent);
			return;
		}

		// if the peer hasn't become interested and we haven't
		// become interested in the peer for 10 minutes, it
		// has also timed out.
		time_duration d1;
		time_duration d2;
		d1 = now - m_became_uninterested;
		d2 = now - m_became_uninteresting;
		time_duration time_limit = seconds(
			m_settings.get_int(settings_pack::inactivity_timeout));

		// don't bother disconnect peers we haven't been interested
		// in (and that hasn't been interested in us) for a while
		// unless we have used up all our connection slots
		if (may_timeout
			&& !m_interesting
			&& !m_peer_interested
			&& d1 > time_limit
			&& d2 > time_limit
			&& (m_ses.num_connections() >= m_settings.get_int(settings_pack::connections_limit)
				|| (t && t->num_peers() >= t->max_connections()))
			&& can_disconnect(error_code(errors::timed_out_no_interest)))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "MUTUAL_NO_INTEREST", "t1: %d t2: %d"
				, int(total_seconds(d1)), int(total_seconds(d2)));
#endif
			disconnect(errors::timed_out_no_interest, op_bittorrent);
			return;
		}

		if (may_timeout
			&& !m_download_queue.empty()
			&& m_quota[download_channel] > 0
			&& now > m_requested + seconds(request_timeout()))
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
			peer_log(peer_log_alert::info, "SLOW_START", "exit slow start: "
				"prev-dl: %d dl: %d"
				, int(m_downloaded_last_second)
				, int(m_statistics.last_payload_downloaded()));
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

		int piece_timeout = m_settings.get_int(settings_pack::piece_timeout);

		if (!m_download_queue.empty()
			&& m_quota[download_channel] > 0
			&& now - m_last_piece > seconds(piece_timeout))
		{
			// this peer isn't sending the pieces we've
			// requested (this has been observed by BitComet)
			// in this case we'll clear our download queue and
			// re-request the blocks.
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "PIECE_REQUEST_TIMED_OUT"
				, "%d time: %d to: %d"
				, int(m_download_queue.size()), int(total_seconds(now - m_last_piece))
				, piece_timeout);
#endif

			snub_peer();
		}

		// update once every minute
		if (now - m_remote_dl_update >= seconds(60))
		{
			boost::int64_t piece_size = t->torrent_file().piece_length();

			if (m_remote_dl_rate > 0)
				m_remote_dl_rate = int((m_remote_dl_rate * 2 / 3)
					+ ((boost::int64_t(m_remote_pieces_dled) * piece_size / 3) / 60));
			else
				m_remote_dl_rate = int(boost::int64_t(m_remote_pieces_dled)
					* piece_size / 60);

			m_remote_pieces_dled = 0;
			m_remote_dl_update = now;
		}

		fill_send_buffer();
	}

	void peer_connection::snub_peer()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
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
		int i = m_download_queue.size() - 1;
		for (; i >= 0; --i)
		{
			if (!m_download_queue[i].timed_out
				&& !m_download_queue[i].not_wanted)
				break;
		}

		if (i >= 0)
		{
			pending_block& qe = m_download_queue[i];
			piece_block r = qe.block;

			// only cancel a request if it blocks the piece from being completed
			// (i.e. no free blocks to request from it)
			piece_picker::downloading_piece p;
			picker.piece_info(qe.block.piece_index, p);
			int free_blocks = picker.blocks_in_piece(qe.block.piece_index)
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
					, remote(), pid(), int(qe.block.block_index)
					, int(qe.block.piece_index));
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

	int peer_connection::preferred_caching() const
	{
		TORRENT_ASSERT(is_single_thread());
		int line_size = 0;
		if (m_settings.get_bool(settings_pack::guided_read_cache))
		{
			boost::shared_ptr<torrent> t = m_torrent.lock();
			int upload_rate = m_statistics.upload_payload_rate();
			if (upload_rate == 0) upload_rate = 1;

			int num_uploads = m_ses.num_uploads();
			if (num_uploads == 0) num_uploads = 1;

			// assume half of the cache is write cache if we're downloading
			// this torrent as well
			int cache_size = m_settings.get_int(settings_pack::cache_size) / num_uploads;
			if (!t->is_upload_only()) cache_size /= 2;
			// cache_size is the amount of cache we have per peer. The
			// cache line should not be greater than this

			line_size = cache_size;
		}
		return line_size;
	}

	void peer_connection::fill_send_buffer()
	{
		TORRENT_ASSERT(is_single_thread());
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		bool sent_a_piece = false;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || t->is_aborted() || m_requests.empty()) return;

		// only add new piece-chunks if the send buffer is small enough
		// otherwise there will be no end to how large it will be!

		int buffer_size_watermark = int(m_uploaded_last_second
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
		peer_log(peer_log_alert::outgoing, "SEND_BUFFER_WATERMARK"
			, "current watermark: %d max: %d min: %d factor: %d uploaded: %d B/s"
			, buffer_size_watermark
			, m_ses.settings().get_int(settings_pack::send_buffer_watermark)
			, m_ses.settings().get_int(settings_pack::send_buffer_low_watermark)
			, m_ses.settings().get_int(settings_pack::send_buffer_watermark_factor)
			, int(m_uploaded_last_second));
#endif

		// don't just pop the front element here, since in seed mode one request may
		// be blocked because we have to verify the hash first, so keep going with the
		// next request. However, only let each peer have one hash verification outstanding
		// at any given time
		for (int i = 0; i < m_requests.size()
			&& (send_buffer_size() + m_reading_bytes < buffer_size_watermark); ++i)
		{
			TORRENT_ASSERT(t->ready_for_connections());
			peer_request& r = m_requests[i];

			TORRENT_ASSERT(r.piece >= 0);
			TORRENT_ASSERT(r.piece < int(m_have_piece.size()));
//			TORRENT_ASSERT(t->have_piece(r.piece));
			TORRENT_ASSERT(r.start + r.length <= t->torrent_file().piece_size(r.piece));
			TORRENT_ASSERT(r.length > 0 && r.start >= 0);

			if (t->is_deleted())
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE"
					, "piece: %d s: %x l: %x torrent deleted"
					, r.piece , r.start , r.length);
#endif
				write_reject_request(r);
				continue;
			}

			if (t->seed_mode() && !t->verified_piece(r.piece))
			{
				// we're still verifying the hash of this piece
				// so we can't return it yet.
				if (t->verifying_piece(r.piece)) continue;

				// only have three outstanding hash check per peer
				if (m_outstanding_piece_verification >= 3) continue;

				++m_outstanding_piece_verification;

#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "SEED_MODE_FILE_ASYNC_HASH"
					, "piece: %d", r.piece);
#endif
				// this means we're in seed mode and we haven't yet
				// verified this piece (r.piece)
				if (!t->need_loaded()) return;
				t->inc_refcount("async_seed_hash");
				m_disk_thread.async_hash(&t->storage(), r.piece, 0
					, boost::bind(&peer_connection::on_seed_mode_hashed, self(), _1)
					, this);
				t->verifying(r.piece);
				continue;
			}

			// in seed mode, we might end up accepting a request
			// which it later turns out we cannot serve, if we ended
			// up not having that piece
			if (!t->has_piece_passed(r.piece))
			{
				// we don't have this piece yet, but we anticipate to have
				// it very soon, so we have told our peers we have it.
				// hold off on sending it. If the piece fails later
				// we will reject this request
				if (t->is_predictive_piece(r.piece)) continue;
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::outgoing_message, "REJECT_PIECE"
					, "piece: %d s: %x l: %x piece not passed hash check"
					, r.piece , r.start , r.length);
#endif
				write_reject_request(r);
			}
			else
			{
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "FILE_ASYNC_READ"
					, "piece: %d s: %x l: %x", r.piece, r.start, r.length);
#endif
				m_reading_bytes += r.length;
				sent_a_piece = true;

				// the callback function may be called immediately, instead of being posted
				if (!t->need_loaded()) return;

				TORRENT_ASSERT(t->valid_metadata());
				TORRENT_ASSERT(r.piece >= 0);
				TORRENT_ASSERT(r.piece < t->torrent_file().num_pieces());

				t->inc_refcount("async_read");
				m_disk_thread.async_read(&t->storage(), r
					, boost::bind(&peer_connection::on_disk_read_complete
					, self(), _1, r, clock_type::now()), this);
			}
			m_last_sent_payload = clock_type::now();
			m_requests.erase(m_requests.begin() + i);

			if (m_requests.empty())
				m_counters.inc_stats_counter(counters::num_peers_up_requests, -1);

			--i;
		}

		if (t->share_mode() && sent_a_piece)
			t->recalc_share_mode();
	}

	// this is called when a previously unchecked piece has been
	// checked, while in seed-mode
	void peer_connection::on_seed_mode_hashed(disk_io_job const* j)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		torrent_ref_holder h(t.get(), "async_seed_hash");
		if (t) t->dec_refcount("async_seed_hash");

		TORRENT_ASSERT(m_outstanding_piece_verification > 0);
		--m_outstanding_piece_verification;

		if (!t || t->is_aborted()) return;

		if (j->error)
		{
			t->handle_disk_error(j, this);
			t->leave_seed_mode(false);
			return;
		}

		// we're using the piece hashes here, we need the torrent to be loaded
		if (!t->need_loaded()) return;

		if (!m_settings.get_bool(settings_pack::disable_hash_checks)
			&& sha1_hash(j->d.piece_hash) != t->torrent_file().hash_for_piece(j->piece))
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SEED_MODE_FILE_HASH"
				, "piece: %d failed", j->piece);
#endif

			t->leave_seed_mode(false);
		}
		else
		{
			TORRENT_ASSERT(t->verifying_piece(j->piece));
			if (t->seed_mode()) t->verified(j->piece);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "SEED_MODE_FILE_HASH"
				, "piece: %d passed", j->piece);
#endif
			if (t)
			{
				if (t->seed_mode() && t->all_verified())
					t->leave_seed_mode(true);
			}
		}

		// try to service the requests again, now that the piece
		// has been verified
		fill_send_buffer();
	}

	void peer_connection::on_disk_read_complete(disk_io_job const* j
		, peer_request r, time_point issue_time)
	{
		TORRENT_ASSERT(is_single_thread());
		// return value:
		// 0: success, piece passed hash check
		// -1: disk failure

		int disk_rtt = int(total_microseconds(clock_type::now() - issue_time));

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "FILE_ASYNC_READ_COMPLETE"
			, "ret: %d piece: %d s: %x l: %x b: %p c: %s e: %s rtt: %d us"
			, j->ret, r.piece, r.start, r.length
			, static_cast<void*>(j->buffer.disk_block)
			, (j->flags & disk_io_job::cache_hit ? "cache hit" : "cache miss")
			, j->error.ec.message().c_str(), disk_rtt);
#endif

		m_reading_bytes -= r.length;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		torrent_ref_holder h(t.get(), "async_read");
		if (t) t->dec_refcount("async_read");

		if (j->ret < 0)
		{
			if (!t)
			{
				disconnect(j->error.ec, op_file_read);
				return;
			}

			TORRENT_ASSERT(j->buffer.disk_block == 0);
			write_dont_have(r.piece);
			write_reject_request(r);
			if (t->alerts().should_post<file_error_alert>())
				t->alerts().emplace_alert<file_error_alert>(j->error.ec
					, t->resolve_filename(j->error.file)
					, j->error.operation_str(), t->get_handle());

			++m_disk_read_failures;
			if (m_disk_read_failures > 100) disconnect(j->error.ec, op_file_read);
			return;
		}

		// we're only interested in failures in a row.
		// if we every now and then successfully send a
		// block, the peer is still useful
		m_disk_read_failures = 0;

		TORRENT_ASSERT(j->ret == r.length);

		// even if we're disconnecting, we need to free this block
		// otherwise the disk thread will hang, waiting for the network
		// thread to be done with it
		disk_buffer_holder buffer(m_allocator, *j);

		if (m_disconnecting) return;

		// flush send buffer at the end of
		// this burst of disk events
//		m_ses.cork_burst(this);

		if (!t)
		{
			disconnect(j->error.ec, op_file_read);
			return;
		}

		if (j->ret != r.length)
		{
			// handle_disk_error may disconnect us
			t->handle_disk_error(j, this);
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message
			, "PIECE", "piece: %d s: %x l: %x", r.piece, r.start, r.length);
#endif

		m_counters.blend_stats_counter(counters::request_latency, disk_rtt, 5);

		// we probably just pulled this piece into the cache.
		// if it's rare enough to make it into the suggested piece
		// push another piece out
		if (m_settings.get_int(settings_pack::suggest_mode) == settings_pack::suggest_read_cache
			&& (j->flags & disk_io_job::cache_hit) == 0)
		{
			t->add_suggest_piece(r.piece);
		}
		write_piece(r, buffer);
	}

	void peer_connection::assign_bandwidth(int channel, int amount)
	{
		TORRENT_ASSERT(is_single_thread());
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(channel == upload_channel
			? peer_log_alert::outgoing : peer_log_alert::incoming
			, "ASSIGN_BANDWIDHT", "bytes: %d", amount);
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
	int peer_connection::wanted_transfer(int channel)
	{
		TORRENT_ASSERT(is_single_thread());
		shared_ptr<torrent> t = m_torrent.lock();

		const int tick_interval = (std::max)(1, m_settings.get_int(settings_pack::tick_interval));

		if (channel == download_channel)
		{
			return (std::max)((std::max)(m_outstanding_bytes
				, m_recv_buffer.packet_bytes_remaining()) + 30
				, int(boost::int64_t(m_statistics.download_rate()) * 2
					/ (1000 / tick_interval)));
		}
		else
		{
			return (std::max)((std::max)(m_reading_bytes
				, m_send_buffer.size())
				, int((boost::int64_t(m_statistics.upload_rate()) * 2
					* tick_interval) / 1000));
		}
	}

	int peer_connection::request_bandwidth(int channel, int bytes)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;
		// we can only have one outstanding bandwidth request at a time
		if (m_channel_state[channel] & peer_info::bw_limit) return 0;

		shared_ptr<torrent> t = m_torrent.lock();

		bytes = (std::max)(wanted_transfer(channel), bytes);

		// we already have enough quota
		if (m_quota[channel] >= bytes) return 0;

		// deduct the bytes we already have quota for
		bytes -= m_quota[channel];

		int priority = get_priority(channel);

		int max_channels = num_classes() + (t ? t->num_classes() : 0) + 2;
		bandwidth_channel** channels = TORRENT_ALLOCA(bandwidth_channel*, max_channels);

		// collect the pointers to all bandwidth channels
		// that apply to this torrent
		int c = 0;

		c += m_ses.copy_pertinent_channels(*this, channel
			, channels + c, max_channels - c);
		if (t)
		{
			c += m_ses.copy_pertinent_channels(*t, channel
				, channels + c, max_channels - c);
		}

#ifdef TORRENT_DEBUG
		// make sure we don't have duplicates
		std::set<bandwidth_channel*> unique_classes;
		for (int i = 0; i < c; ++i)
		{
			TORRENT_ASSERT(unique_classes.count(channels[i]) == 0);
			unique_classes.insert(channels[i]);
		}
#endif

		TORRENT_ASSERT((m_channel_state[channel] & peer_info::bw_limit) == 0);

		bandwidth_manager* manager = m_ses.get_bandwidth_manager(channel);

		int ret = manager->request_bandwidth(self()
			, bytes, priority, channels, c);

		if (ret == 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(
				channel == download_channel ? peer_log_alert::incoming
					: peer_log_alert::outgoing,
				"REQUEST_BANDWIDTH", "bytes: %d quota: %d wanted_transfer: %d "
				"prio: %d num_channels: %d", bytes, m_quota[channel]
				, wanted_transfer(channel), priority, c);
#endif
			m_channel_state[channel] |= peer_info::bw_limit;
		}
		else
		{
			m_quota[channel] += ret;
		}

		return ret;
	}

	void peer_connection::uncork_socket()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_corked) return;
		m_corked = false;
		setup_send();
	}

	void peer_connection::setup_send()
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_disconnecting) return;

		// we may want to request more quota at this point
		request_bandwidth(upload_channel);

		if (m_channel_state[upload_channel] & peer_info::bw_network) return;

		if (m_send_barrier == 0)
		{
			std::vector<boost::asio::mutable_buffer> vec;
			m_send_buffer.build_mutable_iovec(m_send_buffer.size(), vec);
			int next_barrier = hit_send_barrier(vec);
			for (std::vector<boost::asio::mutable_buffer>::reverse_iterator i = vec.rbegin();
				i != vec.rend(); ++i)
			{
				m_send_buffer.prepend_buffer(boost::asio::buffer_cast<char*>(*i)
					, boost::asio::buffer_size(*i)
					, boost::asio::buffer_size(*i)
					, &nop
					, NULL);
			}
			set_send_barrier(next_barrier);
		}

		if ((m_quota[upload_channel] == 0 || m_send_barrier == 0)
			&& !m_send_buffer.empty()
			&& !m_connecting)
		{
			return;
		}

		int quota_left = m_quota[upload_channel];

		if (m_send_buffer.empty()
			&& m_reading_bytes > 0
			&& quota_left > 0)
		{
			if ((m_channel_state[upload_channel] & peer_info::bw_disk) == 0)
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
				boost::shared_ptr<torrent> t = m_torrent.lock();

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
			if (m_send_buffer.empty())
			{
				peer_log(peer_log_alert::outgoing, "SEND_BUFFER_DEPLETED"
					, "quota: %d buf: %d connecting: %s disconnecting: %s "
					"pending_disk: %d piece-requests: %d"
					, m_quota[upload_channel]
					, int(m_send_buffer.size()), m_connecting?"yes":"no"
					, m_disconnecting?"yes":"no", m_reading_bytes
					, int(m_requests.size()));
			}
			else
			{
				peer_log(peer_log_alert::outgoing, "CANNOT_WRITE"
					, "quota: %d buf: %d connecting: %s disconnecting: %s "
					"pending_disk: %d"
					, m_quota[upload_channel]
					, int(m_send_buffer.size()), m_connecting?"yes":"no"
					, m_disconnecting?"yes":"no", m_reading_bytes);
			}
#endif
			return;
		}

		// send the actual buffer
		int amount_to_send = m_send_buffer.size();
		if (amount_to_send > quota_left)
			amount_to_send = quota_left;
		if (amount_to_send > m_send_barrier)
			amount_to_send = m_send_barrier;

		TORRENT_ASSERT(amount_to_send > 0);

		if (m_corked)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing, "CORKED_WRITE", "bytes: %d"
				, amount_to_send);
#endif
			return;
		}

		TORRENT_ASSERT((m_channel_state[upload_channel] & peer_info::bw_network) == 0);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "ASYNC_WRITE", "bytes: %d", amount_to_send);
#endif
		std::vector<boost::asio::const_buffer> const& vec = m_send_buffer.build_iovec(amount_to_send);
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("peer_connection::on_send_data");
#endif

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(!m_socket_is_writing);
		m_socket_is_writing = true;
#endif

		// uTP sockets aren't thread safe...
		if (is_utp(*m_socket))
		{
			m_socket->async_write_some(vec, make_write_handler(boost::bind(
				&peer_connection::on_send_data, self(), _1, _2)));
		}
		else
		{
			socket_job j;
			j.type = socket_job::write_job;
			j.vec = &vec;
			j.peer = self();
			m_ses.post_socket_job(j);
		}

		m_channel_state[upload_channel] |= peer_info::bw_network;
	}

	void peer_connection::on_disk()
	{
		TORRENT_ASSERT(is_single_thread());
		if ((m_channel_state[download_channel] & peer_info::bw_disk) == 0) return;
		boost::shared_ptr<peer_connection> me(self());

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

		// we may want to request more quota at this point
		request_bandwidth(download_channel);

		if (m_channel_state[download_channel] & peer_info::bw_network) return;

		if (m_quota[download_channel] == 0
			&& !m_connecting)
		{
			return;
		}

		if (!can_read())
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "CANNOT_READ", "quota: %d  "
				"can-write-to-disk: %s queue-limit: %d disconnecting: %s "
				" connecting: %s"
				, m_quota[download_channel]
				, ((m_channel_state[download_channel] & peer_info::bw_disk)?"no":"yes")
				, m_settings.get_int(settings_pack::max_queued_disk_bytes)
				, (m_disconnecting?"yes":"no")
				, (m_connecting?"yes":"no"));
#endif
			// if we block reading, waiting for the disk, we will wake up
			// by the disk_io_thread posting a message every time it drops
			// from being at or exceeding the limit down to below the limit
			return;
		}
		error_code ec;

		try_read(read_async, ec);
	}

	size_t peer_connection::try_read(sync_t s, error_code& ec)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_connected);

		if (m_quota[download_channel] == 0)
		{
			ec = boost::asio::error::would_block;
			return 0;
		}

		if (!can_read())
		{
			ec = boost::asio::error::would_block;
			return 0;
		}

		int max_receive = m_recv_buffer.max_receive();

		boost::array<boost::asio::mutable_buffer, 2> vec;
		int num_bufs = 0;
		// only apply the contiguous receive buffer when we don't have any
		// outstanding requests. When we're likely to receive pieces, we'll
		// save more time from avoiding copying data from the socket
		if ((m_settings.get_bool(settings_pack::contiguous_recv_buffer)
			|| m_download_queue.empty()) && !m_recv_buffer.has_disk_buffer())
		{
			if (s == read_sync)
			{
				ec = boost::asio::error::would_block;
				return 0;
			}

			TORRENT_ASSERT((m_channel_state[download_channel] & peer_info::bw_network) == 0);
			m_channel_state[download_channel] |= peer_info::bw_network;
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "ASYNC_READ");
#endif

#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("peer_connection::on_receive_data_nb");
#endif
			m_socket->async_read_some(null_buffers(), make_read_handler(
				boost::bind(&peer_connection::on_receive_data_nb, self(), _1, _2)));
			return 0;
		}

		TORRENT_ASSERT(max_receive >= 0);

		int quota_left = m_quota[download_channel];
		if (max_receive > quota_left)
			max_receive = quota_left;

		if (max_receive == 0)
		{
			ec = boost::asio::error::would_block;
			return 0;
		}

		num_bufs = m_recv_buffer.reserve(vec, max_receive);

		if (s == read_async)
		{
			TORRENT_ASSERT((m_channel_state[download_channel] & peer_info::bw_network) == 0);
			m_channel_state[download_channel] |= peer_info::bw_network;
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "ASYNC_READ"
				, "max: %d bytes", max_receive);
#endif

			// utp sockets aren't thread safe...
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("peer_connection::on_receive_data");
#endif
			if (is_utp(*m_socket))
			{
				if (num_bufs == 1)
				{
					TORRENT_ASSERT(boost::asio::buffer_size(vec[0]) > 0);
					m_socket->async_read_some(
						boost::asio::mutable_buffers_1(vec[0]), make_read_handler(
							boost::bind(&peer_connection::on_receive_data, self(), _1, _2)));
				}
				else
				{
					TORRENT_ASSERT(boost::asio::buffer_size(vec[0])
						+ boost::asio::buffer_size(vec[1])> 0);
					m_socket->async_read_some(
						vec, make_read_handler(
							boost::bind(&peer_connection::on_receive_data, self(), _1, _2)));
				}
			}
			else
			{
				socket_job j;
				j.type = socket_job::read_job;
				j.peer = self();
				if (num_bufs == 1)
				{
					TORRENT_ASSERT(boost::asio::buffer_size(vec[0]) > 0);
					j.recv_buf = boost::asio::buffer_cast<char*>(vec[0]);
					j.buf_size = boost::asio::buffer_size(vec[0]);
				}
				else
				{
					TORRENT_ASSERT(boost::asio::buffer_size(vec[0])
						+ boost::asio::buffer_size(vec[1])> 0);
					j.read_vec = vec;
				}
				m_ses.post_socket_job(j);
			}
			return 0;
		}

		size_t ret = 0;
		if (num_bufs == 1)
		{
			ret = m_socket->read_some(boost::asio::mutable_buffers_1(vec[0]), ec);
		}
		else
		{
			ret = m_socket->read_some(vec, ec);
		}
		// this is weird. You would imagine read_some() would do this
		if (ret == 0 && !ec) ec = boost::asio::error::eof;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming, "SYNC_READ", "max: %d ret: %d e: %s"
			, max_receive, int(ret), ec ? ec.message().c_str() : "");
#endif
		return ret;
	}

	void peer_connection::append_send_buffer(char* buffer, int size
		, chained_buffer::free_buffer_fun destructor, void* userdata
		, block_cache_reference ref)
	{
		TORRENT_ASSERT(is_single_thread());
		m_send_buffer.append_buffer(buffer, size, size, destructor
			, userdata, ref);
	}

	void peer_connection::append_const_send_buffer(char const* buffer, int size
		, chained_buffer::free_buffer_fun destructor, void* userdata
		, block_cache_reference ref)
	{
		TORRENT_ASSERT(is_single_thread());
		m_send_buffer.append_buffer(const_cast<char*>(buffer), size, size, destructor
			, userdata, ref);
	}

	boost::optional<piece_block_progress>
	peer_connection::downloading_piece_progress() const
	{
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "ERROR"
			, "downloading_piece_progress() dispatched to the base class!");
#endif
		return boost::optional<piece_block_progress>();
	}

	namespace {
		void session_free_buffer(char* buffer, void* userdata, block_cache_reference)
		{
			aux::session_interface* ses = static_cast<aux::session_interface*>(userdata);
			ses->free_buffer(buffer);
		}
	}

	void peer_connection::send_buffer(char const* buf, int size, int flags)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_UNUSED(flags);

		int free_space = m_send_buffer.space_in_last_buffer();
		if (free_space > size) free_space = size;
		if (free_space > 0)
		{
			char* dst = m_send_buffer.append(buf, free_space);

			// this should always succeed, because we checked how much space
			// there was up-front
			TORRENT_UNUSED(dst);
			TORRENT_ASSERT(dst != 0);
			size -= free_space;
			buf += free_space;
		}
		if (size <= 0) return;

		int i = 0;
		while (size > 0)
		{
			char* chain_buf = m_ses.allocate_buffer();
			if (chain_buf == 0)
			{
				disconnect(errors::no_memory, op_alloc_sndbuf);
				return;
			}

			const int alloc_buf_size = m_ses.send_buffer_size();
			int buf_size = (std::min)(alloc_buf_size, size);
			memcpy(chain_buf, buf, buf_size);
			buf += buf_size;
			size -= buf_size;
			m_send_buffer.append_buffer(chain_buf, alloc_buf_size, buf_size
				, &session_free_buffer, &m_ses);
			++i;
		}
		setup_send();
	}

	template<class T>
	struct set_to_zero
	{
		set_to_zero(T& v, bool cond): m_val(v), m_cond(cond) {}
		void fire() { if (!m_cond) return; m_cond = false; m_val = 0; }
		~set_to_zero() { if (m_cond) m_val = 0; }
	private:
		T& m_val;
		bool m_cond;
	};

	void peer_connection::on_receive_data_nb(const error_code& error
		, std::size_t bytes_transferred)
	{
		TORRENT_ASSERT(is_single_thread());
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("peer_connection::on_receive_data_nb");
#endif

		// leave this bit set until we're done looping, reading from the socket.
		// that way we don't trigger any async read calls until the end of this
		// function.
		TORRENT_ASSERT(m_channel_state[download_channel] & peer_info::bw_network);

		// nb is short for null_buffers. In this mode we don't actually
		// allocate a receive buffer up-front, but get notified when
		// we can read from the socket, and then determine how much there
		// is to read.

		if (error)
		{
			TORRENT_ASSERT_VAL(error.value() != 0, error.value());
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ERROR"
				, "in peer_connection::on_receive_data_nb error: (%s:%d) %s"
				, error.category().name(), error.value()
				, error.message().c_str());
#endif
			on_receive(error, bytes_transferred);
			disconnect(error, op_sock_read);
			return;
		}

		error_code ec;
		std::size_t buffer_size = m_socket->available(ec);
		if (ec)
		{
			disconnect(ec, op_available);
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming, "READ_AVAILABLE"
			, "bytes: %d", int(buffer_size));
#endif

		// at this point the ioctl told us the socket doesn't have any
		// pending bytes. This probably means some error happened.
		// in order to find out though, we need to initiate a read
		// operation
		if (buffer_size == 0)
		{
			// try to read one byte. The socket is non-blocking anyway
			// so worst case, we'll fail with EWOULDBLOCK
			buffer_size = 1;
		}
		else
		{
			if (buffer_size > m_quota[download_channel])
			{
				request_bandwidth(download_channel, buffer_size);
				buffer_size = m_quota[download_channel];
			}
			// we're already waiting to get some more
			// quota from the bandwidth manager
			if (buffer_size == 0)
			{
				// allow reading from the socket again
				TORRENT_ASSERT(m_channel_state[download_channel] & peer_info::bw_network);
				m_channel_state[download_channel] &= ~peer_info::bw_network;
				return;
			}
		}

		if (buffer_size > 2097152) buffer_size = 2097152;

		boost::asio::mutable_buffer buffer = m_recv_buffer.reserve(buffer_size);
		TORRENT_ASSERT(m_recv_buffer.normalized());

		// utp sockets aren't thread safe...
		if (is_utp(*m_socket))
		{
			bytes_transferred = m_socket->read_some(boost::asio::mutable_buffers_1(buffer), ec);

			if (ec)
			{
				if (ec == boost::asio::error::try_again
					|| ec == boost::asio::error::would_block)
				{
					// allow reading from the socket again
					TORRENT_ASSERT(m_channel_state[download_channel] & peer_info::bw_network);
					m_channel_state[download_channel] &= ~peer_info::bw_network;
					setup_receive();
					return;
				}
				disconnect(ec, op_sock_read);
				return;
			}
		}
		else
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("peer_connection::on_receive_data");
#endif
			socket_job j;
			j.type = socket_job::read_job;
			j.recv_buf = boost::asio::buffer_cast<char*>(buffer);
			j.buf_size = boost::asio::buffer_size(buffer);
			j.peer = self();
			m_ses.post_socket_job(j);
			return;
		}

		receive_data_impl(error, bytes_transferred, 0);
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	// nb is true if this callback is due to a null_buffers()
	// invocation of async_read_some(). In that case, we need
	// to disregard bytes_transferred.
	// at all exit points of this function, one of the following MUST hold:
	//  1. the socket is disconnecting
	//  2. m_channel_state[download_channel] & peer_info::bw_network == 0

	void peer_connection::on_receive_data(const error_code& error
		, std::size_t bytes_transferred)
	{
		TORRENT_ASSERT(is_single_thread());
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("peer_connection::on_receive_data");
#endif

		// leave this bit set until we're done looping, reading from the socket.
		// that way we don't trigger any async read calls until the end of this
		// function.
		TORRENT_ASSERT(m_channel_state[download_channel] & peer_info::bw_network);

		TORRENT_ASSERT(bytes_transferred > 0 || error);

		receive_data_impl(error, bytes_transferred, 10);
	}

	void peer_connection::receive_data_impl(const error_code& error
		, std::size_t bytes_transferred, int read_loops)
	{
		TORRENT_ASSERT(is_single_thread());
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::incoming, "ON_RECEIVE_DATA"
			, "bytes: %d error: (%s:%d) %s"
			, int(bytes_transferred), error.category().name(), error.value()
			, error.message().c_str());
#endif

		// submit all disk jobs later
		m_ses.deferred_submit_jobs();

		// keep ourselves alive in until this function exits in
		// case we disconnect
		// this needs to be created before the invariant check,
		// to keep the object alive through the exit check
		boost::shared_ptr<peer_connection> me(self());

		// flush the send buffer at the end of this function
		cork _c(*this);

		INVARIANT_CHECK;

		int bytes_in_loop = bytes_transferred;

		if (error)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ERROR"
				, "in peer_connection::on_receive_data_impl error: %s"
				, error.message().c_str());
#endif
			trancieve_ip_packet(bytes_in_loop, m_remote.address().is_v6());
			on_receive(error, bytes_transferred);
			disconnect(error, op_sock_read);
			return;
		}

		TORRENT_ASSERT(bytes_transferred > 0);

		m_counters.inc_stats_counter(counters::on_read_counter);
		m_ses.received_buffer(bytes_transferred);

		if (m_extension_outstanding_bytes > 0)
			m_extension_outstanding_bytes -= (std::min)(m_extension_outstanding_bytes, int(bytes_transferred));

		check_graceful_pause();
		if (m_disconnecting) return;

		int num_loops = 0;
		do
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "READ"
				, "%d bytes", int(bytes_transferred));
#endif
			// correct the dl quota usage, if not all of the buffer was actually read
			TORRENT_ASSERT(int(bytes_transferred) <= m_quota[download_channel]);
			m_quota[download_channel] -= bytes_transferred;

			if (m_disconnecting)
			{
				trancieve_ip_packet(bytes_in_loop, m_remote.address().is_v6());
				return;
			}

			TORRENT_ASSERT(bytes_transferred > 0);
			m_recv_buffer.received(bytes_transferred);

			int bytes = bytes_transferred;
			int sub_transferred = 0;
			do {
// TODO: The stats checks can not be honored when authenticated encryption is in use
// because we may have encrypted data which we cannot authenticate yet
#if 0
				boost::int64_t cur_payload_dl = m_statistics.last_payload_downloaded();
				boost::int64_t cur_protocol_dl = m_statistics.last_protocol_downloaded();
#endif
				sub_transferred = m_recv_buffer.advance_pos(bytes);
				on_receive(error, sub_transferred);
				bytes -= sub_transferred;
				TORRENT_ASSERT(sub_transferred > 0);

#if 0
				TORRENT_ASSERT(m_statistics.last_payload_downloaded() - cur_payload_dl >= 0);
				TORRENT_ASSERT(m_statistics.last_protocol_downloaded() - cur_protocol_dl >= 0);
				boost::int64_t stats_diff = m_statistics.last_payload_downloaded() - cur_payload_dl +
					m_statistics.last_protocol_downloaded() - cur_protocol_dl;
				TORRENT_ASSERT(stats_diff == int(sub_transferred));
#endif
				if (m_disconnecting) return;

			} while (bytes > 0 && sub_transferred > 0);

			m_recv_buffer.normalize();

			TORRENT_ASSERT(m_recv_buffer.pos_at_end());
			TORRENT_ASSERT(m_recv_buffer.packet_size() > 0);

			if (m_peer_choked)
			{
				m_recv_buffer.clamp_size();
			}

			if (num_loops > read_loops) break;

			error_code ec;
			bytes_transferred = try_read(read_sync, ec);
			TORRENT_ASSERT(bytes_transferred > 0 || ec);
			if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) break;
			if (ec)
			{
				trancieve_ip_packet(bytes_in_loop, m_remote.address().is_v6());
				disconnect(ec, op_sock_read);
				return;
			}
			bytes_in_loop += bytes_transferred;
			++num_loops;
		}
		while (bytes_transferred > 0);

		m_last_receive = aux::time_now();

		if (is_seed())
		{
			boost::shared_ptr<torrent> t = m_torrent.lock();
			if (t) t->seen_complete();
		}

		trancieve_ip_packet(bytes_in_loop, m_remote.address().is_v6());

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

		boost::shared_ptr<torrent> t = m_torrent.lock();

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
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("peer_connection::on_connection_complete");
#endif

#if !defined TORRENT_DISABLE_LOGGING || defined TORRENT_USE_OPENSSL
		time_point completed = clock_type::now();
#endif

		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_LOGGING
		{
			boost::shared_ptr<torrent> t = m_torrent.lock();
			if (t) t->debug_log("END connect [%p]", static_cast<void*>(this));
			m_connect_time = completed;
		}
#endif

#ifdef TORRENT_USE_OPENSSL
		// add this RTT to the PRNG seed, to add more unpredictability
		boost::uint64_t now = total_microseconds(completed - m_connect);
		// assume 12 bits of entropy (i.e. about 8 milliseconds)
		RAND_add(&now, 8, 1.5);
#endif

		// if t is NULL, we better not be connecting, since
		// we can't decrement the connecting counter
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t || !m_connecting);
		if (m_connecting)
		{
			m_counters.inc_stats_counter(counters::num_peers_half_open, -1);
			if (t) t->dec_num_connecting();
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
		m_last_receive = aux::time_now();

		error_code ec;
		m_local = m_socket->local_endpoint(ec);
		if (ec)
		{
			disconnect(ec, op_getname);
			return;
		}

		// if there are outgoing interfaces specified, verify this
		// peer is correctly bound to on of them
		if (!m_settings.get_str(settings_pack::outgoing_interfaces).empty())
		{
			if (!m_ses.verify_bound_address(m_local.address()
				, is_utp(*m_socket), ec))
			{
				if (ec)
				{
					disconnect(ec, op_get_interface);
					return;
				}
				disconnect(error_code(
					boost::system::errc::no_such_device, generic_category())
					, op_connect);
				return;
			}
		}

		if (is_utp(*m_socket) && m_peer_info)
		{
			m_peer_info->confirmed_supports_utp = true;
			m_peer_info->supports_utp = false;
		}

		// this means the connection just succeeded

		received_synack(m_remote.address().is_v6());

		TORRENT_ASSERT(m_socket);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "COMPLETED"
			, "ep: %s", print_endpoint(m_remote).c_str());
#endif

		// set the socket to non-blocking, so that we can
		// read the entire buffer on each read event we get
		tcp::socket::non_blocking_io ioc(true);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "SET_NON_BLOCKING");
#endif
		m_socket->io_control(ioc, ec);
		if (ec)
		{
			disconnect(ec, op_iocontrol);
			return;
		}

		if (m_remote == m_socket->local_endpoint(ec))
		{
			// if the remote endpoint is the same as the local endpoint, we're connected
			// to ourselves
			if (m_peer_info && t) t->ban_peer(m_peer_info);
			disconnect(errors::self_connection, op_bittorrent, 1);
			return;
		}

		if (m_remote.address().is_v4() && m_settings.get_int(settings_pack::peer_tos) != 0)
		{
			error_code err;
			m_socket->set_option(type_of_service(m_settings.get_int(settings_pack::peer_tos)), err);
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing, "SET_TOS", "tos: %d e: %s"
				, m_settings.get_int(settings_pack::peer_tos), err.message().c_str());
#endif
		}
#if TORRENT_USE_IPV6 && defined IPV6_TCLASS
		else if (m_remote.address().is_v6() && m_settings.get_int(settings_pack::peer_tos) != 0)
		{
			error_code err;
			m_socket->set_option(traffic_class(m_settings.get_int(settings_pack::peer_tos)), err);
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::outgoing, "SET_TOS", "tos: %d e: %s"
				, m_settings.get_int(settings_pack::peer_tos), err.message().c_str());
#endif
		}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			(*i)->on_connected();
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
		, std::size_t bytes_transferred)
	{
		TORRENT_ASSERT(is_single_thread());
		m_counters.inc_stats_counter(counters::on_write_counter);
		m_ses.sent_buffer(bytes_transferred);

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_socket_is_writing);
		m_socket_is_writing = false;
#endif

		// submit all disk jobs when we've processed all messages
		// in the current message queue
		m_ses.deferred_submit_jobs();

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "ON_SEND_DATA", "bytes: %d error: %s"
			, int(bytes_transferred), error.message().c_str());
#endif

		INVARIANT_CHECK;

#if defined TORRENT_ASIO_DEBUGGING
		complete_async("peer_connection::on_send_data");
#endif
		// keep ourselves alive in until this function exits in
		// case we disconnect
		boost::shared_ptr<peer_connection> me(self());

		TORRENT_ASSERT(m_channel_state[upload_channel] & peer_info::bw_network);

		m_send_buffer.pop_front(bytes_transferred);

		time_point now = clock_type::now();

		for (std::vector<pending_block>::iterator i = m_download_queue.begin()
			, end(m_download_queue.end()); i != end; ++i)
		{
			if (i->send_buffer_offset == pending_block::not_in_buffer) continue;
			boost::int32_t offset = i->send_buffer_offset;
			offset -= bytes_transferred;
			if (offset < 0)
				i->send_buffer_offset = pending_block::not_in_buffer;
			else
				i->send_buffer_offset = offset;
		}

		m_channel_state[upload_channel] &= ~peer_info::bw_network;

		TORRENT_ASSERT(int(bytes_transferred) <= m_quota[upload_channel]);
		m_quota[upload_channel] -= bytes_transferred;

		trancieve_ip_packet(bytes_transferred, m_remote.address().is_v6());

		if (m_send_barrier != INT_MAX)
			m_send_barrier -= bytes_transferred;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing, "WROTE"
			, "%d bytes", int(bytes_transferred));
#endif

		if (error)
		{
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "ERROR"
				, "%s in peer_connection::on_send_data", error.message().c_str());
#endif
			disconnect(error, op_sock_write);
			return;
		}
		if (m_disconnecting)
		{
			// make sure we free up all send buffers that are owned
			// by the disk thread
			m_send_buffer.clear();
			m_recv_buffer.free_disk_buffer();
			return;
		}

		TORRENT_ASSERT(!m_connecting);
		TORRENT_ASSERT(bytes_transferred > 0);

		m_last_sent = now;

#if TORRENT_USE_ASSERTS
		boost::int64_t cur_payload_ul = m_statistics.last_payload_uploaded();
		boost::int64_t cur_protocol_ul = m_statistics.last_protocol_uploaded();
#endif
		on_sent(error, bytes_transferred);
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_statistics.last_payload_uploaded() - cur_payload_ul >= 0);
		TORRENT_ASSERT(m_statistics.last_protocol_uploaded() - cur_protocol_ul >= 0);
		boost::int64_t stats_diff = m_statistics.last_payload_uploaded() - cur_payload_ul
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

		boost::shared_ptr<torrent> t = m_torrent.lock();

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
		else if (!m_in_constructor)
		{
			TORRENT_ASSERT(m_ses.has_peer(this));
		}

		TORRENT_ASSERT(m_outstanding_bytes >= 0);
		if (t && t->valid_metadata() && !m_disconnecting)
		{
			torrent_info const& ti = t->torrent_file();
			// if the piece is fully downloaded, we might have popped it from the
			// download queue already
			int outstanding_bytes = 0;
//			bool in_download_queue = false;
			int block_size = t->block_size();
			piece_block last_block(ti.num_pieces()-1
				, (ti.piece_size(ti.num_pieces()-1) + block_size - 1) / block_size);
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
				fprintf(stderr, "m_outstanding_bytes = %d\noutstanding_bytes = %d\n"
					, m_outstanding_bytes, outstanding_bytes);
			}

			TORRENT_ASSERT(m_outstanding_bytes == outstanding_bytes);
		}

		std::set<piece_block> unique;
		std::transform(m_download_queue.begin(), m_download_queue.end()
			, std::inserter(unique, unique.begin()), boost::bind(&pending_block::block, _1));
		std::transform(m_request_queue.begin(), m_request_queue.end()
			, std::inserter(unique, unique.begin()), boost::bind(&pending_block::block, _1));
		TORRENT_ASSERT(unique.size() == m_download_queue.size() + m_request_queue.size());
		if (m_peer_info)
		{
			TORRENT_ASSERT(m_peer_info->prev_amount_upload == 0);
			TORRENT_ASSERT(m_peer_info->prev_amount_download == 0);
			TORRENT_ASSERT(m_peer_info->connection == this
				|| m_peer_info->connection == 0);

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

		if (t->ready_for_connections() && m_initialized)
			TORRENT_ASSERT(t->torrent_file().num_pieces() == int(m_have_piece.size()));

		// in share mode we don't close redundant connections
		if (m_settings.get_bool(settings_pack::close_redundant_connections)
			&& !t->share_mode())
		{
			bool const ok_to_disconnect =
				can_disconnect(error_code(errors::upload_upload_connection))
					|| can_disconnect(error_code(errors::uninteresting_upload_peer))
					|| can_disconnect(error_code(errors::too_many_requests_when_choked))
					|| can_disconnect(error_code(errors::timed_out_no_interest))
					|| can_disconnect(error_code(errors::timed_out_no_request))
					|| can_disconnect(error_code(errors::timed_out_inactivity));

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

		time_duration d;
		d = aux::time_now() - m_last_sent;
		if (total_seconds(d) < timeout() / 2) return;

		if (m_connecting) return;
		if (in_handshake()) return;

		// if the last send has not completed yet, do not send a keep
		// alive
		if (m_channel_state[upload_channel] & peer_info::bw_network) return;

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "KEEPALIVE");
#endif

		m_last_sent = aux::time_now();
		write_keepalive();
	}

	bool peer_connection::is_seed() const
	{
		TORRENT_ASSERT(is_single_thread());
		// if m_num_pieces == 0, we probably don't have the
		// metadata yet.
		boost::shared_ptr<torrent> t = m_torrent.lock();
		return m_num_pieces == int(m_have_piece.size())
			&& m_num_pieces > 0 && t && t->valid_metadata();
	}

	void peer_connection::set_share_mode(bool u)
	{
		TORRENT_ASSERT(is_single_thread());
		// if the peer is a seed, ignore share mode messages
		if (is_seed()) return;

		m_share_mode = u;
	}

	void peer_connection::set_upload_only(bool u)
	{
		TORRENT_ASSERT(is_single_thread());
		// if the peer is a seed, don't allow setting
		// upload_only to false
		if (m_upload_only || is_seed()) return;

		m_upload_only = u;
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		t->set_seed(m_peer_info, u);
		disconnect_if_redundant();
	}

}
