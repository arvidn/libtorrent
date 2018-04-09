/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include <boost/bind.hpp>
#include <boost/utility.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/peer_list.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/torrent_peer_allocator.hpp"

#ifdef TORRENT_DEBUG
#include "libtorrent/bt_peer_connection.hpp"
#endif

#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
#include "libtorrent/socket_io.hpp" // for print_endpoint
#endif

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/socket_io.hpp" // for print_endpoint
#include "libtorrent/ip_voter.hpp" // for external_ip
#endif

namespace
{
	using namespace libtorrent;

	struct match_peer_endpoint
	{
		match_peer_endpoint(tcp::endpoint const& ep)
			: m_ep(ep)
		{}

		bool operator()(torrent_peer const* p) const
		{
			TORRENT_ASSERT(p->in_use);
			return p->address() == m_ep.address() && p->port == m_ep.port();
		}

		tcp::endpoint const& m_ep;
	};

#ifndef TORRENT_DISABLE_INVARIANT_CHECKS
	struct match_peer_connection
	{
		match_peer_connection(peer_connection_interface const& c) : m_conn(c) {}

		bool operator()(torrent_peer const* p) const
		{
			TORRENT_ASSERT(p->in_use);
			return p->connection == &m_conn;
		}

		peer_connection_interface const& m_conn;
	};
#endif

#if TORRENT_USE_ASSERTS
	struct match_peer_connection_or_endpoint
	{
		match_peer_connection_or_endpoint(peer_connection_interface const& c) : m_conn(c) {}

		bool operator()(torrent_peer const* p) const
		{
			TORRENT_ASSERT(p->in_use);
			return p->connection == &m_conn
				|| (p->ip() == m_conn.remote()
					&& p->connectable);
		}

		peer_connection_interface const& m_conn;
	};
#endif

}

namespace libtorrent
{
	peer_list::peer_list(torrent_peer_allocator_interface& alloc)
		: m_locked_peer(NULL)
		, m_peer_allocator(alloc)
		, m_num_seeds(0)
		, m_finished(0)
		, m_round_robin(0)
		, m_num_connect_candidates(0)
		, m_max_failcount(3)
	{
		thread_started();
	}

	peer_list::~peer_list()
	{
		for (peers_t::iterator i = m_peers.begin()
			, end(m_peers.end()); i != end; ++i)
		{
			m_peer_allocator.free_peer_entry(*i);
		}
	}

	void peer_list::set_max_failcount(torrent_state* state)
	{
		if (state->max_failcount == m_max_failcount) return;

		recalculate_connect_candidates(state);
	}

	// disconnects and removes all peers that are now filtered fills in 'erased'
	// with torrent_peer pointers that were removed from the peer list. Any
	// references to these peers must be cleared immediately after this call
	// returns. For instance, in the piece picker.
	void peer_list::apply_ip_filter(ip_filter const& filter
		, torrent_state* state, std::vector<address>& banned)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		for (iterator i = m_peers.begin(); i != m_peers.end();)
		{
			if ((filter.access((*i)->address()) & ip_filter::blocked) == 0)
			{
				++i;
				continue;
			}
			if (*i == m_locked_peer)
			{
				++i;
				continue;
			}

			int current = i - m_peers.begin();
			TORRENT_ASSERT(current >= 0);
			TORRENT_ASSERT(m_peers.size() > 0);
			TORRENT_ASSERT(i != m_peers.end());

			if ((*i)->connection)
			{
				// disconnecting the peer here may also delete the
				// peer_info_struct. If that is the case, just continue
				size_t count = m_peers.size();
				peer_connection_interface* p = (*i)->connection;

				banned.push_back(p->remote().address());

				p->disconnect(errors::banned_by_ip_filter
					, op_bittorrent);

				// what *i refers to has changed, i.e. cur was deleted
				if (m_peers.size() < count)
				{
					i = m_peers.begin() + current;
					continue;
				}
				TORRENT_ASSERT((*i)->connection == 0
					|| (*i)->connection->peer_info_struct() == 0);
			}

			erase_peer(i, state);
			i = m_peers.begin() + current;
		}
	}

	void peer_list::clear_peer_prio()
	{
		for (peers_t::iterator i = m_peers.begin()
			, end(m_peers.end()); i != end; ++i)
			(*i)->peer_rank = 0;
	}

	// disconnects and removes all peers that are now filtered
	// fills in 'erased' with torrent_peer pointers that were removed
	// from the peer list. Any references to these peers must be cleared
	// immediately after this call returns. For instance, in the piece picker.
	void peer_list::apply_port_filter(port_filter const& filter
		, torrent_state* state, std::vector<address>& banned)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		for (iterator i = m_peers.begin(); i != m_peers.end();)
		{
			if ((filter.access((*i)->port) & port_filter::blocked) == 0)
			{
				++i;
				continue;
			}
			if (*i == m_locked_peer)
			{
				++i;
				continue;
			}

			int current = i - m_peers.begin();
			TORRENT_ASSERT(current >= 0);
			TORRENT_ASSERT(m_peers.size() > 0);
			TORRENT_ASSERT(i != m_peers.end());

			if ((*i)->connection)
			{
				// disconnecting the peer here may also delete the
				// peer_info_struct. If that is the case, just continue
				int count = m_peers.size();
				peer_connection_interface* p = (*i)->connection;

				banned.push_back(p->remote().address());

				p->disconnect(errors::banned_by_port_filter, op_bittorrent);
				// what *i refers to has changed, i.e. cur was deleted
				if (int(m_peers.size()) < count)
				{
					i = m_peers.begin() + current;
					continue;
				}
				TORRENT_ASSERT((*i)->connection == 0
					|| (*i)->connection->peer_info_struct() == 0);
			}

			erase_peer(i, state);
			i = m_peers.begin() + current;
		}
	}

	void peer_list::erase_peer(torrent_peer* p, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(p->in_use);
		TORRENT_ASSERT(m_locked_peer != p);

		std::pair<iterator, iterator> range = find_peers(p->address());
		iterator iter = std::find_if(range.first, range.second, match_peer_endpoint(p->ip()));
		if (iter == range.second) return;
		erase_peer(iter, state);
	}

	// any peer that is erased from m_peers will be
	// erased through this function. This way we can make
	// sure that any references to the peer are removed
	// as well, such as in the piece picker.
	void peer_list::erase_peer(iterator i, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;
		TORRENT_ASSERT(i != m_peers.end());
		TORRENT_ASSERT(m_locked_peer != *i);

		state->erased.push_back(*i);
		if ((*i)->seed)
		{
			TORRENT_ASSERT(m_num_seeds > 0);
			--m_num_seeds;
		}
		if (is_connect_candidate(**i))
			update_connect_candidates(-1);
		TORRENT_ASSERT(m_num_connect_candidates < int(m_peers.size()));
		if (m_round_robin > i - m_peers.begin()) --m_round_robin;
		if (m_round_robin >= int(m_peers.size())) m_round_robin = 0;

		// if this peer is in the connect candidate
		// cache, erase it from there as well
		std::vector<torrent_peer*>::iterator ci = std::find(m_candidate_cache.begin(), m_candidate_cache.end(), *i);
		if (ci != m_candidate_cache.end()) m_candidate_cache.erase(ci);

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT((*i)->in_use);
		(*i)->in_use = false;
#endif

		m_peer_allocator.free_peer_entry(*i);
		m_peers.erase(i);
	}

	bool peer_list::should_erase_immediately(torrent_peer const& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(p.in_use);
		if (&p == m_locked_peer) return false;
		return p.source == peer_info::resume_data;
	}

	bool peer_list::is_erase_candidate(torrent_peer const& pe) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(pe.in_use);
		if (&pe == m_locked_peer) return false;
		if (pe.connection) return false;
		if (is_connect_candidate(pe)) return false;

		return (pe.failcount > 0)
			|| (pe.source == peer_info::resume_data);
	}

	bool peer_list::is_force_erase_candidate(torrent_peer const& pe) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(pe.in_use);
		if (&pe == m_locked_peer) return false;
		return pe.connection == 0;
	}

	void peer_list::erase_peers(torrent_state* state, int flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		int max_peerlist_size = state->max_peerlist_size;

		if (max_peerlist_size == 0 || m_peers.empty()) return;

		int erase_candidate = -1;
		int force_erase_candidate = -1;

		if (m_finished != state->is_finished)
			recalculate_connect_candidates(state);

		int round_robin = random() % m_peers.size();

		int low_watermark = max_peerlist_size * 95 / 100;
		if (low_watermark == max_peerlist_size) --low_watermark;

		for (int iterations = (std::min)(int(m_peers.size()), 300);
			iterations > 0; --iterations)
		{
			if (int(m_peers.size()) < low_watermark)
				break;

			if (round_robin == int(m_peers.size())) round_robin = 0;

			torrent_peer& pe = *m_peers[round_robin];
			TORRENT_ASSERT(pe.in_use);
			int current = round_robin;

			if (is_erase_candidate(pe)
				&& (erase_candidate == -1
					|| !compare_peer_erase(*m_peers[erase_candidate], pe)))
			{
				if (should_erase_immediately(pe))
				{
					if (erase_candidate > current) --erase_candidate;
					if (force_erase_candidate > current) --force_erase_candidate;
					TORRENT_ASSERT(current >= 0 && current < int(m_peers.size()));
					erase_peer(m_peers.begin() + current, state);
					continue;
				}
				else
				{
					erase_candidate = current;
				}
			}
			if (is_force_erase_candidate(pe)
				&& (force_erase_candidate == -1
					|| !compare_peer_erase(*m_peers[force_erase_candidate], pe)))
			{
				force_erase_candidate = current;
			}

			++round_robin;
		}

		if (erase_candidate > -1)
		{
			TORRENT_ASSERT(erase_candidate >= 0 && erase_candidate < int(m_peers.size()));
			erase_peer(m_peers.begin() + erase_candidate, state);
		}
		else if ((flags & force_erase) && force_erase_candidate > -1)
		{
			TORRENT_ASSERT(force_erase_candidate >= 0 && force_erase_candidate < int(m_peers.size()));
			erase_peer(m_peers.begin() + force_erase_candidate, state);
		}
	}

	// returns true if the peer was actually banned
	bool peer_list::ban_peer(torrent_peer* p)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(p->in_use);

		if (is_connect_candidate(*p))
			update_connect_candidates(-1);

		p->banned = true;
		TORRENT_ASSERT(!is_connect_candidate(*p));
		return true;
	}

	void peer_list::set_connection(torrent_peer* p, peer_connection_interface* c)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(p->in_use);
		TORRENT_ASSERT(c);

		const bool was_conn_cand = is_connect_candidate(*p);
		p->connection = c;
		if (was_conn_cand) update_connect_candidates(-1);
	}

	void peer_list::inc_failcount(torrent_peer* p)
	{
		// failcount is a 5 bit value
		if (p->failcount == 31) return;

		const bool was_conn_cand = is_connect_candidate(*p);
		++p->failcount;
		if (was_conn_cand && !is_connect_candidate(*p))
			update_connect_candidates(-1);
	}

	void peer_list::set_failcount(torrent_peer* p, int f)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(p->in_use);
		const bool was_conn_cand = is_connect_candidate(*p);
		p->failcount = f;
		if (was_conn_cand != is_connect_candidate(*p))
		{
			update_connect_candidates(was_conn_cand ? -1 : 1);
		}
	}

	bool peer_list::is_connect_candidate(torrent_peer const& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(p.in_use);
		if (p.connection
			|| p.banned
			|| p.web_seed
			|| !p.connectable
			|| (p.seed && m_finished)
			|| int(p.failcount) >= m_max_failcount)
			return false;

		return true;
	}

	void peer_list::find_connect_candidates(std::vector<torrent_peer*>& peers
		, int session_time, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		const int candidate_count = 10;
		peers.reserve(candidate_count);

		int erase_candidate = -1;

		if (m_finished != state->is_finished)
			recalculate_connect_candidates(state);

		external_ip const& external = *state->ip;
		int external_port = state->port;

		if (m_round_robin >= int(m_peers.size())) m_round_robin = 0;

		int max_peerlist_size = state->max_peerlist_size;

		// TODO: 2 it would be nice if there was a way to iterate over these
		// torrent_peer objects in the order they are allocated in the pool
		// instead. It would probably be more efficient
		for (int iterations = (std::min)(int(m_peers.size()), 300);
			iterations > 0; --iterations)
		{
			++state->loop_counter;

			if (m_round_robin >= int(m_peers.size())) m_round_robin = 0;

			torrent_peer& pe = *m_peers[m_round_robin];
			TORRENT_ASSERT(pe.in_use);
			int current = m_round_robin;

			// if the number of peers is growing large
			// we need to start weeding.

			if (int(m_peers.size()) >= max_peerlist_size * 0.95
				&& max_peerlist_size > 0)
			{
				if (is_erase_candidate(pe)
					&& (erase_candidate == -1
						|| !compare_peer_erase(*m_peers[erase_candidate], pe)))
				{
					if (should_erase_immediately(pe))
					{
						if (erase_candidate > current) --erase_candidate;
						erase_peer(m_peers.begin() + current, state);
						continue;
					}
					else
					{
						erase_candidate = current;
					}
				}
			}

			++m_round_robin;

			if (!is_connect_candidate(pe)) continue;

			if (pe.last_connected
				&& session_time - pe.last_connected <
				(int(pe.failcount) + 1) * state->min_reconnect_time)
				continue;

			// compare peer returns true if lhs is better than rhs. In this
			// case, it returns true if the current candidate is better than
			// pe, which is the peer m_round_robin points to. If it is, just
			// keep looking.
			if (peers.size() == candidate_count
				&& compare_peer(peers.back(), &pe, external, external_port)) continue;

			if (peers.size() >= candidate_count)
				peers.resize(candidate_count - 1);

			// insert this candidate sorted into peers
			std::vector<torrent_peer*>::iterator i = std::lower_bound(peers.begin(), peers.end()
				, &pe, boost::bind(&peer_list::compare_peer, this, _1, _2, boost::cref(external), external_port));

			peers.insert(i, &pe);
		}

		if (erase_candidate > -1)
		{
			erase_peer(m_peers.begin() + erase_candidate, state);
		}
	}

	bool peer_list::new_connection(peer_connection_interface& c, int session_time
		, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
//		TORRENT_ASSERT(!c.is_outgoing());

		INVARIANT_CHECK;

		TORRENT_ASSERT(!state->is_paused);

		iterator iter;
		torrent_peer* i = 0;

		bool found = false;
		if (state->allow_multiple_connections_per_ip)
		{
			tcp::endpoint remote = c.remote();
			std::pair<iterator, iterator> range = find_peers(remote.address());
			iter = std::find_if(range.first, range.second, match_peer_endpoint(remote));

			if (iter != range.second)
			{
				TORRENT_ASSERT((*iter)->in_use);
				found = true;
			}
		}
		else
		{
			iter = std::lower_bound(
				m_peers.begin(), m_peers.end()
				, c.remote().address(), peer_address_compare()
			);

			if (iter != m_peers.end() && (*iter)->address() == c.remote().address())
			{
				TORRENT_ASSERT((*iter)->in_use);
				found = true;
			}
		}

		// make sure the iterator we got is properly sorted relative
		// to the connection's address
//		TORRENT_ASSERT(m_peers.empty()
//			|| (iter == m_peers.end() && (*(iter-1))->address() < c.remote().address())
//			|| (iter != m_peers.end() && c.remote().address() < (*iter)->address())
//			|| (iter != m_peers.end() && iter != m_peers.begin() && (*(iter-1))->address() < c.remote().address()));

		if (found)
		{
			i = *iter;
			TORRENT_ASSERT(i->in_use);
			TORRENT_ASSERT(i->connection != &c);
			TORRENT_ASSERT(i->address() == c.remote().address());

#ifndef TORRENT_DISABLE_LOGGING
			c.peer_log(peer_log_alert::info, "DUPLICATE PEER", "this: \"%s\" that: \"%s\""
				, print_address(c.remote().address()).c_str()
				, print_address(i->address()).c_str());
#endif
			if (i->banned)
			{
				c.disconnect(errors::peer_banned, op_bittorrent);
				return false;
			}

			if (i->connection != 0)
			{
				bool const self_connection =
					i->connection->remote() == c.local_endpoint()
					|| i->connection->local_endpoint() == c.remote();

				if (self_connection)
				{
					c.disconnect(errors::self_connection, op_bittorrent, 1);
					TORRENT_ASSERT(i->connection->peer_info_struct() == i);
					i->connection->disconnect(errors::self_connection, op_bittorrent, 1);
					TORRENT_ASSERT(i->connection == 0);
					return false;
				}

				TORRENT_ASSERT(i->connection != &c);
				// the new connection is a local (outgoing) connection
				// or the current one is already connected
				if (i->connection->is_outgoing() == c.is_outgoing())
				{
					// if the other end connected to us both times, just drop
					// the second one. Or if we made both connections.
					c.disconnect(errors::duplicate_peer_id, op_bittorrent);
					return false;
				}
				else
				{
					// at this point, we need to disconnect either
					// i->connection or c. In order for both this client
					// and the client on the other end to decide to
					// disconnect the same one, we need a consistent rule to
					// select which one.

					bool const outgoing1 = c.is_outgoing();

					// for this, we compare our ports and whoever has the lower port
					// should be the one keeping its outgoing connection. Since
					// outgoing ports are selected at random by the OS, we need to
					// be careful to only look at the target end of a connection for
					// the endpoint.

					int const our_port = outgoing1 ? i->connection->local_endpoint().port() : c.local_endpoint().port();
					int const other_port = outgoing1 ? c.remote().port() : i->connection->remote().port();

					// decide which peer connection to disconnect
					// if the ports are equal, pick on at random
					bool const disconnect1 = ((our_port < other_port) && !outgoing1)
						|| ((our_port > other_port) && outgoing1)
						|| ((our_port == other_port) && randint(2));

#ifndef TORRENT_DISABLE_LOGGING
					c.peer_log(peer_log_alert::info, "DUPLICATE_PEER_RESOLUTION"
						, "our: %d other: %d disconnecting: %s"
						, our_port, other_port, disconnect1 ? "yes" : "no");
					i->connection->peer_log(peer_log_alert::info, "DUPLICATE_PEER_RESOLUTION"
						, "our: %d other: %d disconnecting: %s"
						, our_port, other_port, disconnect1 ? "no" : "yes");
#endif

					if (disconnect1)
					{
						c.disconnect(errors::duplicate_peer_id, op_bittorrent);
						return false;
					}
					TORRENT_ASSERT(m_locked_peer == NULL);
					m_locked_peer = i;
					i->connection->disconnect(errors::duplicate_peer_id, op_bittorrent);
					m_locked_peer = NULL;
				}
			}

			if (is_connect_candidate(*i))
				update_connect_candidates(-1);
		}
		else
		{
			// we don't have any info about this peer.
			// add a new entry

			if (state->max_peerlist_size
				&& int(m_peers.size()) >= state->max_peerlist_size)
			{
				// this may invalidate our iterator!
				erase_peers(state, force_erase);
				if (int(m_peers.size()) >= state->max_peerlist_size)
				{
					c.disconnect(errors::too_many_connections, op_bittorrent);
					return false;
				}
				// restore it
				iter = std::lower_bound(
					m_peers.begin(), m_peers.end()
					, c.remote().address(), peer_address_compare()
				);
			}

#if TORRENT_USE_IPV6
			bool is_v6 = c.remote().address().is_v6();
#else
			bool is_v6 = false;
#endif
			torrent_peer* p = m_peer_allocator.allocate_peer_entry(
				is_v6 ? torrent_peer_allocator_interface::ipv6_peer_type
				: torrent_peer_allocator_interface::ipv4_peer_type);
			if (p == 0) return false;

#if TORRENT_USE_IPV6
			if (is_v6)
				new (p) ipv6_peer(c.remote(), false, 0);
			else
#endif
				new (p) ipv4_peer(c.remote(), false, 0);

#if TORRENT_USE_ASSERTS
			p->in_use = true;
#endif

			iter = m_peers.insert(iter, p);

			if (m_round_robin >= iter - m_peers.begin()) ++m_round_robin;

			i = *iter;

			i->source = peer_info::incoming;
		}

		TORRENT_ASSERT(i);
		c.set_peer_info(i);
		TORRENT_ASSERT(i->connection == 0);
		c.add_stat(boost::int64_t(i->prev_amount_download) << 10, boost::int64_t(i->prev_amount_upload) << 10);

		i->prev_amount_download = 0;
		i->prev_amount_upload = 0;
		i->connection = &c;
		TORRENT_ASSERT(i->connection);
		if (!c.fast_reconnect())
			i->last_connected = session_time;

		// this cannot be a connect candidate anymore, since i->connection is set
		TORRENT_ASSERT(!is_connect_candidate(*i));
		TORRENT_ASSERT(has_connection(&c));
		return true;
	}

	bool peer_list::update_peer_port(int port, torrent_peer* p, int src, torrent_state* state)
	{
		TORRENT_ASSERT(p != 0);
		TORRENT_ASSERT(p->connection);
		TORRENT_ASSERT(p->in_use);
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		if (p->port == port) return true;

		if (state->allow_multiple_connections_per_ip)
		{
			tcp::endpoint remote(p->address(), port);
			std::pair<iterator, iterator> range = find_peers(remote.address());
			iterator i = std::find_if(range.first, range.second
				, match_peer_endpoint(remote));
			if (i != range.second)
			{
				torrent_peer& pp = **i;
				TORRENT_ASSERT(pp.in_use);
				if (pp.connection)
				{
					bool was_conn_cand = is_connect_candidate(pp);
					// if we already have an entry with this
					// new endpoint, disconnect this one
					pp.connectable = true;
					pp.source |= src;
					if (!was_conn_cand && is_connect_candidate(pp))
						update_connect_candidates(1);
					// calling disconnect() on a peer, may actually end
					// up "garbage collecting" its torrent_peer entry
					// as well, if it's considered useless (which this specific)
					// case will, since it was an incoming peer that just disconnected
					// and we allow multiple connections per IP. Because of that,
					// we need to make sure we don't let it do that, locking i
					TORRENT_ASSERT(m_locked_peer == NULL);
					m_locked_peer = p;
					p->connection->disconnect(errors::duplicate_peer_id, op_bittorrent);
					m_locked_peer = NULL;
					erase_peer(p, state);
					return false;
				}
				erase_peer(i, state);
			}
		}
#if TORRENT_USE_ASSERTS
		else
		{
#if TORRENT_USE_I2P
			if (!p->is_i2p_addr)
#endif
			{
				std::pair<iterator, iterator> range = find_peers(p->address());
				TORRENT_ASSERT(std::distance(range.first, range.second) == 1);
			}
		}
#endif

		bool was_conn_cand = is_connect_candidate(*p);
		p->port = port;
		p->source |= src;
		p->connectable = true;

		if (was_conn_cand != is_connect_candidate(*p))
			update_connect_candidates(was_conn_cand ? -1 : 1);
		return true;
	}

	// it's important that we don't dereference
	// p here, since it is allowed to be a dangling
	// pointer. see smart_ban.cpp
	bool peer_list::has_peer(torrent_peer const* p) const
	{
		TORRENT_ASSERT(is_single_thread());
		// find p in m_peers
		for (const_iterator i = m_peers.begin()
			, end(m_peers.end()); i != end; ++i)
		{
			if (*i == p) return true;
		}
		return false;
	}

	void peer_list::set_seed(torrent_peer* p, bool s)
	{
		TORRENT_ASSERT(is_single_thread());
		if (p == 0) return;
		TORRENT_ASSERT(p->in_use);
		if (p->seed == s) return;
		bool was_conn_cand = is_connect_candidate(*p);
		p->seed = s;
		if (was_conn_cand && !is_connect_candidate(*p))
			update_connect_candidates(-1);

		if (p->web_seed) return;
		if (s)
		{
			TORRENT_ASSERT(m_num_seeds < int(m_peers.size()));
			++m_num_seeds;
		}
		else
		{
			TORRENT_ASSERT(m_num_seeds > 0);
			--m_num_seeds;
		}
	}

	// this is an internal function
	bool peer_list::insert_peer(torrent_peer* p, iterator iter, int flags
		, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(p);
		TORRENT_ASSERT(p->in_use);

		int max_peerlist_size = state->max_peerlist_size;

		if (max_peerlist_size
			&& int(m_peers.size()) >= max_peerlist_size)
		{
			if (p->source == peer_info::resume_data) return false;

			erase_peers(state);
			if (int(m_peers.size()) >= max_peerlist_size)
				return false;

			// since some peers were removed, we need to
			// update the iterator to make it valid again
#if TORRENT_USE_I2P
			if (p->is_i2p_addr)
			{
				iter = std::lower_bound(
					m_peers.begin(), m_peers.end()
					, p->dest(), peer_address_compare());
			}
			else
#endif
			iter = std::lower_bound(
				m_peers.begin(), m_peers.end()
				, p->address(), peer_address_compare());
		}

		iter = m_peers.insert(iter, p);

		if (m_round_robin >= iter - m_peers.begin()) ++m_round_robin;

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		if (flags & flag_encryption) p->pe_support = true;
#endif
		if (flags & flag_seed)
		{
			p->seed = true;
			TORRENT_ASSERT(m_num_seeds < int(m_peers.size()));
			++m_num_seeds;
		}
		if (flags & flag_utp)
			p->supports_utp = true;
		if (flags & flag_holepunch)
			p->supports_holepunch = true;
		if (is_connect_candidate(*p))
			update_connect_candidates(1);

		return true;
	}

	void peer_list::update_peer(torrent_peer* p, int src, int flags
		, tcp::endpoint const& remote, char const* /* destination*/)
	{
		TORRENT_ASSERT(is_single_thread());
		bool was_conn_cand = is_connect_candidate(*p);

		TORRENT_ASSERT(p->in_use);
		p->connectable = true;

		TORRENT_ASSERT(p->address() == remote.address());
		p->port = remote.port();
		p->source |= src;

		// if this peer has failed before, decrease the
		// counter to allow it another try, since somebody
		// else is appearantly able to connect to it
		// only trust this if it comes from the tracker
		if (p->failcount > 0 && src == peer_info::tracker)
			--p->failcount;

		// if we're connected to this peer
		// we already know if it's a seed or not
		// so we don't have to trust this source
		if ((flags & flag_seed) && !p->connection)
		{
			if (!p->seed)
			{
				TORRENT_ASSERT(m_num_seeds < int(m_peers.size()));
				++m_num_seeds;
			}
			p->seed = true;
		}
		if (flags & flag_utp)
			p->supports_utp = true;
		if (flags & flag_holepunch)
			p->supports_holepunch = true;

		if (was_conn_cand != is_connect_candidate(*p))
		{
			update_connect_candidates(was_conn_cand ? -1 : 1);
		}
	}

	void peer_list::update_connect_candidates(int delta)
	{
		TORRENT_ASSERT(is_single_thread());
		if (delta == 0) return;
		m_num_connect_candidates += delta;
		if (delta < 0)
		{
			TORRENT_ASSERT(m_num_connect_candidates >= 0);
			if (m_num_connect_candidates < 0) m_num_connect_candidates = 0;
		}
	}

#if TORRENT_USE_I2P
	torrent_peer* peer_list::add_i2p_peer(char const* destination, int src, char flags, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		bool found = false;
		iterator iter = std::lower_bound(
			m_peers.begin(), m_peers.end()
			, destination, peer_address_compare()
		);

		if (iter != m_peers.end() && strcmp((*iter)->dest(), destination) == 0)
			found = true;

		torrent_peer* p = 0;

		if (!found)
		{
			// we don't have any info about this peer.
			// add a new entry
			p = m_peer_allocator.allocate_peer_entry(torrent_peer_allocator_interface::i2p_peer_type);
			if (p == NULL) return NULL;
			new (p) i2p_peer(destination, true, src);

#if TORRENT_USE_ASSERTS
			p->in_use = true;
#endif

			if (!insert_peer(p, iter, flags, state))
			{
#if TORRENT_USE_ASSERTS
				p->in_use = false;
#endif

				m_peer_allocator.free_peer_entry(p);
				return 0;
			}
		}
		else
		{
			p = *iter;
			update_peer(p, src, flags, tcp::endpoint(), destination);
		}
		return p;
	}
#endif // TORRENT_USE_I2P

	// if this returns non-NULL, the torrent need to post status update
	torrent_peer* peer_list::add_peer(tcp::endpoint const& remote, int src, char flags
		, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		// just ignore the obviously invalid entries
		if (remote.address() == address() || remote.port() == 0)
			return 0;

#if TORRENT_USE_IPV6
		// don't allow link-local IPv6 addresses since they
		// can't be used like normal addresses, they require an interface
		// and will just cause connect() to fail with EINVAL
		if (remote.address().is_v6() && remote.address().to_v6().is_link_local())
			return 0;
#endif

		iterator iter;
		torrent_peer* p = 0;

		bool found = false;
		if (state->allow_multiple_connections_per_ip)
		{
			std::pair<iterator, iterator> range = find_peers(remote.address());
			iter = std::find_if(range.first, range.second, match_peer_endpoint(remote));
			if (iter != range.second) found = true;
		}
		else
		{
			iter = std::lower_bound(
				m_peers.begin(), m_peers.end()
				, remote.address(), peer_address_compare()
			);

			if (iter != m_peers.end() && (*iter)->address() == remote.address()) found = true;
		}

		if (!found)
		{
			// we don't have any info about this peer.
			// add a new entry

#if TORRENT_USE_IPV6
			bool is_v6 = remote.address().is_v6();
#else
			bool is_v6 = false;
#endif
			p = m_peer_allocator.allocate_peer_entry(
				is_v6 ? torrent_peer_allocator_interface::ipv6_peer_type
				: torrent_peer_allocator_interface::ipv4_peer_type);
			if (p == NULL) return NULL;

#if TORRENT_USE_IPV6
			if (is_v6)
				new (p) ipv6_peer(remote, true, src);
			else
#endif
				new (p) ipv4_peer(remote, true, src);

#if TORRENT_USE_ASSERTS
			p->in_use = true;
#endif

			if (!insert_peer(p, iter, flags, state))
			{
#if TORRENT_USE_ASSERTS
				p->in_use = false;
#endif
				// TODO: 3 this is not exception safe!
				m_peer_allocator.free_peer_entry(p);
				return 0;
			}
			state->first_time_seen = true;
		}
		else
		{
			p = *iter;
			TORRENT_ASSERT(p->in_use);
			update_peer(p, src, flags, remote, 0);
			state->first_time_seen = false;
		}

		return p;
	}

	torrent_peer* peer_list::connect_one_peer(int session_time, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (m_finished != state->is_finished)
			recalculate_connect_candidates(state);

		// clear out any peers from the cache that no longer
		// are connection candidates
		for (std::vector<torrent_peer*>::iterator i = m_candidate_cache.begin();
			i != m_candidate_cache.end();)
		{
			if (!is_connect_candidate(**i))
				i = m_candidate_cache.erase(i);
			else
				++i;
		}

		if (m_candidate_cache.empty())
		{
			find_connect_candidates(m_candidate_cache, session_time, state);
			if (m_candidate_cache.empty()) return NULL;
		}

		torrent_peer* p = m_candidate_cache.front();
		m_candidate_cache.erase(m_candidate_cache.begin());

		TORRENT_ASSERT(p->in_use);

		TORRENT_ASSERT(!p->banned);
		TORRENT_ASSERT(!p->connection);
		TORRENT_ASSERT(p->connectable);

		// this should hold because find_connect_candidates should have done this
		TORRENT_ASSERT(m_finished == state->is_finished);

		TORRENT_ASSERT(is_connect_candidate(*p));
		return p;
	}

	// this is called whenever a peer connection is closed
	void peer_list::connection_closed(const peer_connection_interface& c
		, int session_time, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		torrent_peer* p = c.peer_info_struct();

		// if we couldn't find the connection in our list, just ignore it.
		if (p == 0) return;

		TORRENT_ASSERT(p->in_use);

#ifndef TORRENT_DISABLE_INVARIANT_CHECKS
		// web seeds are special, they're not connected via the peer list
		// so they're not kept in m_peers
		TORRENT_ASSERT(p->web_seed
			|| std::find_if(
				m_peers.begin()
				, m_peers.end()
				, match_peer_connection(c))
				!= m_peers.end());
#endif

		TORRENT_ASSERT(p->connection == &c);
		TORRENT_ASSERT(!is_connect_candidate(*p));

		p->connection = 0;
		p->optimistically_unchoked = false;

		// if fast reconnect is true, we won't
		// update the timestamp, and it will remain
		// the time when we initiated the connection.
		if (!c.fast_reconnect())
			p->last_connected = session_time;

		if (c.failed())
		{
			// failcount is a 5 bit value
			if (p->failcount < 31) ++p->failcount;
		}

		if (is_connect_candidate(*p))
			update_connect_candidates(1);

		// if we're already a seed, it's not as important
		// to keep all the possibly stale peers
		// if we're not a seed, but we have too many peers
		// start weeding the ones we only know from resume
		// data first
		// at this point it may be tempting to erase peers
		// from the peer list, but keep in mind that we might
		// have gotten to this point through new_connection, just
		// disconnecting an old peer, relying on this torrent_peer
		// to still exist when we get back there, to assign the new
		// peer connection pointer to it. The peer list must
		// be left intact.

		// if we allow multiple connections per IP, and this peer
		// was incoming and it never advertised its listen
		// port, we don't really know which peer it was. In order
		// to avoid adding one entry for every single connection
		// the peer makes to us, don't save this entry
		if (state->allow_multiple_connections_per_ip
			&& !p->connectable
			&& p != m_locked_peer)
		{
			erase_peer(p, state);
		}
	}

	void peer_list::recalculate_connect_candidates(torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());

		m_num_connect_candidates = 0;
		m_finished = state->is_finished;
		m_max_failcount = state->max_failcount;

		for (const_iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			m_num_connect_candidates += is_connect_candidate(**i);
		}

#if TORRENT_USE_INVARIANT_CHECKS
		// the invariant is not likely to be upheld at the entry of this function
		// but it is likely to have been restored by the end of it
		check_invariant();
#endif
	}

#if TORRENT_USE_ASSERTS
	bool peer_list::has_connection(const peer_connection_interface* c)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(c);

		iterator iter = std::lower_bound(
			m_peers.begin(), m_peers.end()
			, c->remote().address(), peer_address_compare());

		if (iter != m_peers.end() && (*iter)->address() == c->remote().address())
			return true;

		return std::find_if(
			m_peers.begin()
			, m_peers.end()
			, match_peer_connection_or_endpoint(*c)) != m_peers.end();
	}
#endif

#if TORRENT_USE_INVARIANT_CHECKS
	void peer_list::check_invariant() const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_num_connect_candidates >= 0);
		TORRENT_ASSERT(m_num_connect_candidates <= int(m_peers.size()));

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		int total_connections = 0;
		int nonempty_connections = 0;
		int connect_candidates = 0;

		const_iterator prev = m_peers.end();
		for (const_iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			if (prev != m_peers.end()) ++prev;
			if (i == m_peers.begin() + 1) prev = m_peers.begin();
			if (prev != m_peers.end())
			{
				TORRENT_ASSERT(!((*i)->address() < (*prev)->address()));
			}
			torrent_peer const& p = **i;
			TORRENT_ASSERT(p.in_use);
			if (is_connect_candidate(p)) ++connect_candidates;
			++total_connections;
			if (!p.connection)
			{
				continue;
			}
			if (p.optimistically_unchoked)
			{
				TORRENT_ASSERT(p.connection);
				TORRENT_ASSERT(!p.connection->is_choked());
			}
			TORRENT_ASSERT(p.connection->peer_info_struct() == 0
				|| p.connection->peer_info_struct() == &p);
			++nonempty_connections;
		}

		TORRENT_ASSERT(m_num_connect_candidates == connect_candidates);
#endif // TORRENT_EXPENSIVE_INVARIANT_CHECKS

	}
#endif // TORRENT_DEBUG

	// this returns true if lhs is a better erase candidate than rhs
	bool peer_list::compare_peer_erase(torrent_peer const& lhs, torrent_peer const& rhs) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(lhs.connection == 0);
		TORRENT_ASSERT(rhs.connection == 0);

		// primarily, prefer getting rid of peers we've already tried and failed
		if (lhs.failcount != rhs.failcount)
			return lhs.failcount > rhs.failcount;

		bool lhs_resume_data_source = lhs.source == peer_info::resume_data;
		bool rhs_resume_data_source = rhs.source == peer_info::resume_data;

		// prefer to drop peers whose only source is resume data
		if (lhs_resume_data_source != rhs_resume_data_source)
			return lhs_resume_data_source > rhs_resume_data_source;

		if (lhs.connectable != rhs.connectable)
			return lhs.connectable < rhs.connectable;

		return lhs.trust_points < rhs.trust_points;
	}

	// this returns true if lhs is a better connect candidate than rhs
	bool peer_list::compare_peer(torrent_peer const* lhs, torrent_peer const* rhs
		, external_ip const& external, int external_port) const
	{
		TORRENT_ASSERT(is_single_thread());
		// prefer peers with lower failcount
		if (lhs->failcount != rhs->failcount)
			return lhs->failcount < rhs->failcount;

		// Local peers should always be tried first
		bool lhs_local = is_local(lhs->address());
		bool rhs_local = is_local(rhs->address());
		if (lhs_local != rhs_local) return lhs_local > rhs_local;

		if (lhs->last_connected != rhs->last_connected)
			return lhs->last_connected < rhs->last_connected;

		int lhs_rank = source_rank(lhs->source);
		int rhs_rank = source_rank(rhs->source);
		if (lhs_rank != rhs_rank) return lhs_rank > rhs_rank;

		boost::uint32_t lhs_peer_rank = lhs->rank(external, external_port);
		boost::uint32_t rhs_peer_rank = rhs->rank(external, external_port);
		if (lhs_peer_rank > rhs_peer_rank) return true;
		return false;
	}
}

