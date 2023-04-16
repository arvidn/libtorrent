/*

Copyright (c) 2003-2021, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2009, Daniel Wallin
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016, 2018, Steven Siloti
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

#include <functional>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/peer_list.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/torrent_peer_allocator.hpp"
#include "libtorrent/ip_voter.hpp" // for external_ip
#include "libtorrent/aux_/ip_helpers.hpp" // for is_v6

#if TORRENT_USE_ASSERTS
#include "libtorrent/socket_io.hpp" // for print_endpoint
#endif

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/socket_io.hpp" // for print_endpoint
#endif

using namespace std::placeholders;

namespace {

	using namespace libtorrent;

	struct match_peer_endpoint
	{
		match_peer_endpoint(address const& addr, std::uint16_t port)
			: m_addr(addr), m_port(port)
		{}

		bool operator()(torrent_peer const* p) const
		{
			TORRENT_ASSERT(p->in_use);
			return p->address() == m_addr && p->port == m_port;
		}

		address const& m_addr;
		std::uint16_t m_port;
	};

	// this returns true if lhs is a better erase candidate than rhs
	bool compare_peer_erase(torrent_peer const& lhs, torrent_peer const& rhs)
	{
		TORRENT_ASSERT(lhs.connection == nullptr);
		TORRENT_ASSERT(rhs.connection == nullptr);

		// primarily, prefer getting rid of peers we've already tried and failed
		if (lhs.failcount != rhs.failcount)
			return lhs.failcount > rhs.failcount;

		bool const lhs_resume_data_source = lhs.peer_source() == peer_info::resume_data;
		bool const rhs_resume_data_source = rhs.peer_source() == peer_info::resume_data;

		// prefer to drop peers whose only source is resume data
		if (lhs_resume_data_source != rhs_resume_data_source)
			return int(lhs_resume_data_source) > int(rhs_resume_data_source);

		if (lhs.connectable != rhs.connectable)
			return int(lhs.connectable) < int(rhs.connectable);

		return lhs.trust_points < rhs.trust_points;
	}

	// this returns true if lhs is a better connect candidate than rhs
	bool compare_peer(torrent_peer const* lhs, torrent_peer const* rhs
		, external_ip const& external, int const external_port, bool const finished)
	{
		// prefer peers with lower failcount
		if (lhs->failcount != rhs->failcount)
			return lhs->failcount < rhs->failcount;

		// Local peers should always be tried first
		bool const lhs_local = aux::is_local(lhs->address());
		bool const rhs_local = aux::is_local(rhs->address());
		if (lhs_local != rhs_local) return int(lhs_local) > int(rhs_local);

		if (lhs->last_connected != rhs->last_connected)
			return lhs->last_connected < rhs->last_connected;

		if (finished && lhs->maybe_upload_only != rhs->maybe_upload_only)
		{
			// if we're finished, de-prioritze peers we think may be seeds
			// since being upload-only doesn't necessarily mean it's a good peer
			// to be connected to as a downloader, we don't prioritize the
			// inverse when we're not finished
			return rhs->maybe_upload_only;
		}

		int const lhs_rank = source_rank(lhs->peer_source());
		int const rhs_rank = source_rank(rhs->peer_source());
		if (lhs_rank != rhs_rank) return lhs_rank > rhs_rank;

		std::uint32_t const lhs_peer_rank = lhs->rank(external, external_port);
		std::uint32_t const rhs_peer_rank = rhs->rank(external, external_port);
		return lhs_peer_rank > rhs_peer_rank;
	}

} // anonymous namespace

namespace libtorrent {

	constexpr erase_peer_flags_t peer_list::force_erase;

	peer_list::peer_list(torrent_peer_allocator_interface& alloc)
		: m_locked_peer(nullptr)
		, m_peer_allocator(alloc)
		, m_num_seeds(0)
		, m_finished(0)
	{
		thread_started();
	}

	void peer_list::clear()
	{
		for (auto const p : m_peers)
			m_peer_allocator.free_peer_entry(p);
		m_peers.clear();
		m_num_connect_candidates = 0;
	}

	peer_list::~peer_list()
	{
		for (auto const p : m_peers)
			m_peer_allocator.free_peer_entry(p);
	}

	void peer_list::set_max_failcount(torrent_state* state)
	{
		INVARIANT_CHECK;
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

		for (auto i = m_peers.begin(); i != m_peers.end();)
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

			int const current = int(i - m_peers.begin());
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
					, operation_t::bittorrent);

				// what *i refers to has changed, i.e. cur was deleted
				if (m_peers.size() < count)
				{
					i = m_peers.begin() + current;
					continue;
				}
				TORRENT_ASSERT((*i)->connection == nullptr
					|| (*i)->connection->peer_info_struct() == nullptr);
			}

			erase_peer(i, state);
			i = m_peers.begin() + current;
		}
	}

	void peer_list::clear_peer_prio()
	{
		INVARIANT_CHECK;
		for (auto& p : m_peers)
			p->peer_rank = 0;
	}

	// disconnects and removes all peers that are now filtered
	// fills in 'erased' with torrent_peer pointers that were removed
	// from the peer list. Any references to these peers must be cleared
	// immediately after this call returns. For instance, in the piece picker.
	void peer_list::apply_port_filter(port_filter const& filter
		, torrent_state* state, std::vector<tcp::endpoint>& banned)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		for (auto i = m_peers.begin(); i != m_peers.end();)
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

			int const current = int(i - m_peers.begin());
			TORRENT_ASSERT(current >= 0);
			TORRENT_ASSERT(m_peers.size() > 0);
			TORRENT_ASSERT(i != m_peers.end());

			if ((*i)->connection)
			{
				// disconnecting the peer here may also delete the
				// peer_info_struct. If that is the case, just continue
				int count = int(m_peers.size());
				peer_connection_interface* p = (*i)->connection;

				banned.push_back(p->remote());

				p->disconnect(errors::banned_by_port_filter, operation_t::bittorrent);
				// what *i refers to has changed, i.e. cur was deleted
				if (int(m_peers.size()) < count)
				{
					i = m_peers.begin() + current;
					continue;
				}
				TORRENT_ASSERT((*i)->connection == nullptr
					|| (*i)->connection->peer_info_struct() == nullptr);
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

		auto const range = std::equal_range(m_peers.begin(), m_peers.end(), p, peer_address_compare{});
		auto const iter = std::find_if(range.first, range.second, [&](torrent_peer const* needle) {
			return torrent_peer_equal(needle, p);
		});
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
		auto const ci = std::find(m_candidate_cache.begin(), m_candidate_cache.end(), *i);
		if (ci != m_candidate_cache.end()) m_candidate_cache.erase(ci);

		m_peer_allocator.free_peer_entry(*i);
		m_peers.erase(i);
	}

	bool peer_list::should_erase_immediately(torrent_peer const& p) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(p.in_use);
		if (&p == m_locked_peer) return false;
		return p.peer_source() == peer_info::resume_data;
	}

	bool peer_list::is_erase_candidate(torrent_peer const& pe) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(pe.in_use);
		if (&pe == m_locked_peer) return false;
		if (pe.connection) return false;
		if (is_connect_candidate(pe)) return false;

		return (pe.failcount > 0)
			|| (pe.peer_source() == peer_info::resume_data);
	}

	bool peer_list::is_force_erase_candidate(torrent_peer const& pe) const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(pe.in_use);
		if (&pe == m_locked_peer) return false;
		return pe.connection == nullptr;
	}

	void peer_list::erase_peers(torrent_state* state, erase_peer_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		int max_peerlist_size = state->max_peerlist_size;

		if (max_peerlist_size == 0 || m_peers.empty()) return;

		int erase_candidate = -1;
		int force_erase_candidate = -1;

		if (bool(m_finished) != state->is_finished)
			recalculate_connect_candidates(state);

		int round_robin = aux::numeric_cast<int>(random(std::uint32_t(m_peers.size() - 1)));

		int low_watermark = max_peerlist_size * 95 / 100;
		if (low_watermark == max_peerlist_size) --low_watermark;

		for (int iterations = std::min(int(m_peers.size()), 300);
			iterations > 0; --iterations)
		{
			if (int(m_peers.size()) < low_watermark)
				break;

			if (round_robin == int(m_peers.size())) round_robin = 0;

			torrent_peer& pe = *m_peers[round_robin];
			TORRENT_ASSERT(pe.in_use);
			int const current = round_robin;

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
		// now that we're connected, no need to assume ther peer is a seed
		// anymore. We'll soon know.
		p->maybe_upload_only = false;
		if (was_conn_cand) update_connect_candidates(-1);
	}

	void peer_list::inc_failcount(torrent_peer* p)
	{
		INVARIANT_CHECK;
		// failcount is a 5 bit value
		if (p->failcount == 31) return;

		bool const was_conn_cand = is_connect_candidate(*p);
		++p->failcount;
		if (was_conn_cand && !is_connect_candidate(*p))
			update_connect_candidates(-1);
	}

	void peer_list::set_failcount(torrent_peer* p, int const f)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		TORRENT_ASSERT(p->in_use);
		bool const was_conn_cand = is_connect_candidate(*p);
		p->failcount = aux::numeric_cast<std::uint32_t>(f);
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

		if (bool(m_finished) != state->is_finished)
			recalculate_connect_candidates(state);

		external_ip const& external = state->ip;
		int external_port = state->port;

		if (m_round_robin >= int(m_peers.size())) m_round_robin = 0;

		int max_peerlist_size = state->max_peerlist_size;

		// TODO: 2 it would be nice if there was a way to iterate over these
		// torrent_peer objects in the order they are allocated in the pool
		// instead. It would probably be more efficient
		for (int iterations = std::min(int(m_peers.size()), 300);
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
				&& compare_peer(peers.back(), &pe, external, external_port, m_finished)) continue;

			if (peers.size() >= candidate_count)
				peers.resize(candidate_count - 1);

			// insert this candidate sorted into peers
			auto const i = std::lower_bound(peers.begin(), peers.end()
				, &pe, std::bind(&compare_peer, _1, _2, std::cref(external), external_port, bool(m_finished)));

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

		iterator iter;
		torrent_peer* i = nullptr;

#if TORRENT_USE_I2P
		std::string const i2p_dest = c.destination();
#else
		std::string const i2p_dest;
#endif

		bool found = false;
		// this check doesn't support i2p peers
		if (state->allow_multiple_connections_per_ip && i2p_dest.empty())
		{
			auto const& remote = c.remote();
			auto const addr = remote.address();
			auto const range = find_peers(addr);
			iter = std::find_if(range.first, range.second, match_peer_endpoint(addr, remote.port()));

			if (iter != range.second)
			{
				TORRENT_ASSERT((*iter)->in_use);
				found = true;
			}
		}
		else
		{
#if TORRENT_USE_I2P
			if (!i2p_dest.empty())
			{
				iter = std::lower_bound(
					m_peers.begin(), m_peers.end()
					, i2p_dest, peer_address_compare()
					);

				if (iter != m_peers.end() && (*iter)->is_i2p_addr && (*iter)->dest() == i2p_dest)
				{
					TORRENT_ASSERT((*iter)->in_use);
					found = true;
				}
			}
			else
#endif
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
			if (i->connection != nullptr && c.should_log(peer_log_alert::info))
			{
#if TORRENT_USE_I2P
				if (!i2p_dest.empty())
				{
					c.peer_log(peer_log_alert::info, "DUPLICATE PEER", "destination: \"%s\""
						, i2p_dest.c_str());
				}
				else
#endif
				{
					c.peer_log(peer_log_alert::info, "DUPLICATE PEER", "this: \"%s\" that: \"%s\""
						, print_address(c.remote().address()).c_str()
						, print_address(i->address()).c_str());
				}
			}
#endif
			if (i->banned)
			{
				c.disconnect(errors::peer_banned, operation_t::bittorrent);
				return false;
			}

			if (i->connection != nullptr)
			{
				bool self_connection = false;
#if TORRENT_USE_I2P
				if (!i2p_dest.empty())
				{
					self_connection = i->connection->local_i2p_endpoint() == i2p_dest;
				}
				else
#endif
				{
					self_connection = i->connection->remote() == c.local_endpoint()
					|| i->connection->local_endpoint() == c.remote();
				}

				if (self_connection)
				{
					c.disconnect(errors::self_connection, operation_t::bittorrent, peer_connection_interface::failure);
					TORRENT_ASSERT(i->connection->peer_info_struct() == i);
					i->connection->disconnect(errors::self_connection, operation_t::bittorrent, peer_connection_interface::failure);
					TORRENT_ASSERT(i->connection == nullptr);
					return false;
				}

				TORRENT_ASSERT(i->connection != &c);
				// the new connection is a local (outgoing) connection
				// or the current one is already connected
				if (i->connection->is_outgoing() == c.is_outgoing())
				{
					// if the other end connected to us both times, just drop
					// the second one. Or if we made both connections.
					c.disconnect(errors::duplicate_peer_id, operation_t::bittorrent);
					return false;
				}
#if TORRENT_USE_I2P
				else if (!i2p_dest.empty())
				{
					// duplicate connection resolution for i2p connections is
					// simple. The smaller address takes priority for making the
					// outgoing connection

					std::string const& other_dest = i->connection->destination();

					// decide which peer connection to disconnect
					// if the ports are equal, pick on at random
					bool disconnect1 = c.is_outgoing() && i2p_dest > other_dest;

#ifndef TORRENT_DISABLE_LOGGING
					if (c.should_log(peer_log_alert::info))
					{
						c.peer_log(peer_log_alert::info, "DUPLICATE_PEER_RESOLUTION"
							, "our: %s other: %s disconnecting: %s"
							, i2p_dest.c_str(), other_dest.c_str(), disconnect1 ? "yes" : "no");
						i->connection->peer_log(peer_log_alert::info, "DUPLICATE_PEER_RESOLUTION"
							, "our: %s other: %s disconnecting: %s"
							, other_dest.c_str(), i2p_dest.c_str(), disconnect1 ? "no" : "yes");
					}
#endif

					if (disconnect1)
					{
						c.disconnect(errors::duplicate_peer_id, operation_t::bittorrent);
						return false;
					}
					TORRENT_ASSERT(m_locked_peer == nullptr);
					m_locked_peer = i;
					i->connection->disconnect(errors::duplicate_peer_id, operation_t::bittorrent);
					m_locked_peer = nullptr;
				}
#endif
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
					bool disconnect1 = ((our_port < other_port) && !outgoing1)
						|| ((our_port > other_port) && outgoing1)
						|| ((our_port == other_port) && random(1));
					disconnect1 &= !i->connection->failed();

#ifndef TORRENT_DISABLE_LOGGING
					if (c.should_log(peer_log_alert::info))
					{
						c.peer_log(peer_log_alert::info, "DUPLICATE_PEER_RESOLUTION"
							, "our: %d other: %d disconnecting: %s"
							, our_port, other_port, disconnect1 ? "yes" : "no");
						i->connection->peer_log(peer_log_alert::info, "DUPLICATE_PEER_RESOLUTION"
							, "our: %d other: %d disconnecting: %s"
							, our_port, other_port, disconnect1 ? "no" : "yes");
					}
#endif

					if (disconnect1)
					{
						c.disconnect(errors::duplicate_peer_id, operation_t::bittorrent);
						return false;
					}
					TORRENT_ASSERT(m_locked_peer == nullptr);
					m_locked_peer = i;
					i->connection->disconnect(errors::duplicate_peer_id, operation_t::bittorrent);
					m_locked_peer = nullptr;
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
					c.disconnect(errors::too_many_connections, operation_t::bittorrent);
					return false;
				}
				// restore it
				iter = std::lower_bound(
					m_peers.begin(), m_peers.end()
					, c.remote().address(), peer_address_compare()
				);
			}

#if TORRENT_USE_I2P
			if (!i2p_dest.empty())
			{
				i = add_i2p_peer(i2p_dest, peer_info::incoming, {}, state);
				// we're about to attach the new connection to this torrent_peer
				if (is_connect_candidate(*i))
					update_connect_candidates(-1);
			}
			else
#endif
			{
				bool const is_v6 = lt::aux::is_v6(c.remote());
				torrent_peer* p = m_peer_allocator.allocate_peer_entry(
					is_v6 ? torrent_peer_allocator_interface::ipv6_peer_type
					: torrent_peer_allocator_interface::ipv4_peer_type);
				if (p == nullptr) return false;

				if (is_v6)
					p = new (p) ipv6_peer(c.remote(), false, {});
				else
					p = new (p) ipv4_peer(c.remote(), false, {});

				iter = m_peers.insert(iter, p);

				if (m_round_robin >= iter - m_peers.begin()) ++m_round_robin;

				i = *iter;

				i->source = static_cast<std::uint8_t>(peer_info::incoming);
			}
		}

		TORRENT_ASSERT(i);
		c.set_peer_info(i);
		TORRENT_ASSERT(i->connection == nullptr);
		c.add_stat(std::int64_t(i->prev_amount_download) << 10, std::int64_t(i->prev_amount_upload) << 10);

		i->prev_amount_download = 0;
		i->prev_amount_upload = 0;
		i->connection = &c;
		TORRENT_ASSERT(i->connection);
		if (!c.fast_reconnect())
			i->last_connected = std::uint16_t(session_time);

		// this cannot be a connect candidate anymore, since i->connection is set
		TORRENT_ASSERT(!is_connect_candidate(*i));
		TORRENT_ASSERT(has_connection(&c));
		return true;
	}

	bool peer_list::update_peer_port(int const port, torrent_peer* p
		, peer_source_flags_t const src, torrent_state* state)
	{
		TORRENT_ASSERT(p != nullptr);
		TORRENT_ASSERT(p->connection);
		TORRENT_ASSERT(p->in_use);
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

#if TORRENT_USE_I2P
		if (p->is_i2p_addr) return true;
#endif

		if (p->port == port) return true;

		if (state->allow_multiple_connections_per_ip)
		{
			auto const addr = p->address();
			auto const range = find_peers(addr);
			auto const i = std::find_if(range.first, range.second
				, match_peer_endpoint(addr, std::uint16_t(port)));
			if (i != range.second)
			{
				torrent_peer& pp = **i;
				TORRENT_ASSERT(pp.in_use);
				if (pp.connection)
				{
					bool const was_conn_cand = is_connect_candidate(pp);
					// if we already have an entry with this
					// new endpoint, disconnect this one
					pp.connectable = true;
					pp.source |= static_cast<std::uint8_t>(src);
					if (!was_conn_cand && is_connect_candidate(pp))
						update_connect_candidates(1);
					// calling disconnect() on a peer, may actually end
					// up "garbage collecting" its torrent_peer entry
					// as well, if it's considered useless (which this specific)
					// case will, since it was an incoming peer that just disconnected
					// and we allow multiple connections per IP. Because of that,
					// we need to make sure we don't let it do that, locking i
					TORRENT_ASSERT(m_locked_peer == nullptr);
					m_locked_peer = p;
					p->connection->disconnect(errors::duplicate_peer_id, operation_t::bittorrent);
					m_locked_peer = nullptr;
					erase_peer(p, state);
					return false;
				}
				erase_peer(i, state);
			}
		}
#if TORRENT_USE_ASSERTS
		else
		{
			std::pair<iterator, iterator> range = find_peers(p->address());
			TORRENT_ASSERT(std::distance(range.first, range.second) == 1);
		}
#endif

		bool const was_conn_cand = is_connect_candidate(*p);
		p->port = std::uint16_t(port);
		p->source |= static_cast<std::uint8_t>(src);
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
		return std::find(m_peers.begin(), m_peers.end(), p) != m_peers.end();
	}

	void peer_list::set_seed(torrent_peer* p, bool s)
	{
		TORRENT_ASSERT(is_single_thread());
		if (p == nullptr) return;
		TORRENT_ASSERT(p->in_use);
		if (bool(p->seed) == s) return;
		bool const was_conn_cand = is_connect_candidate(*p);
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
	bool peer_list::insert_peer(torrent_peer* p, iterator iter
		, pex_flags_t const flags
		, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(p);
		TORRENT_ASSERT(p->in_use);

		int const max_peerlist_size = state->max_peerlist_size;

		if (max_peerlist_size
			&& int(m_peers.size()) >= max_peerlist_size)
		{
			if (p->peer_source() == peer_info::resume_data) return false;

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

#if !defined TORRENT_DISABLE_ENCRYPTION
		if (flags & pex_encryption) p->pe_support = true;
#endif
		if (flags & pex_seed)
			p->maybe_upload_only = true;
		if (flags & pex_utp)
			p->supports_utp = true;
		if (flags & pex_holepunch)
			p->supports_holepunch = true;
		if (flags & pex_lt_v2)
			p->protocol_v2 = true;
		if (is_connect_candidate(*p))
			update_connect_candidates(1);

		return true;
	}

	void peer_list::update_peer(torrent_peer* p, peer_source_flags_t const src
		, pex_flags_t const flags, tcp::endpoint const& remote)
	{
		TORRENT_ASSERT(is_single_thread());
		bool const was_conn_cand = is_connect_candidate(*p);

		TORRENT_ASSERT(p->in_use);
		p->connectable = true;

		TORRENT_ASSERT(p->address() == remote.address());
		p->port = remote.port();
		p->source |= static_cast<std::uint8_t>(src);

		// if this peer has failed before, decrease the
		// counter to allow it another try, since somebody
		// else is apparently able to connect to it
		// only trust this if it comes from the tracker
		if (p->failcount > 0 && src == peer_info::tracker)
			--p->failcount;

		// if we're connected to this peer
		// we already know if it's a seed or not
		// so we don't have to trust this source
		if ((flags & pex_seed) && !p->connection)
			p->maybe_upload_only = true;
		if (flags & pex_utp)
			p->supports_utp = true;
		if (flags & pex_holepunch)
			p->supports_holepunch = true;
		if (flags & pex_lt_v2)
			p->protocol_v2 = true;

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
	torrent_peer* peer_list::add_i2p_peer(string_view const destination
		, peer_source_flags_t const src, pex_flags_t const flags
		, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		auto iter = std::lower_bound(m_peers.begin(), m_peers.end()
			, destination, peer_address_compare());

		if (iter != m_peers.end() && (*iter)->dest() == destination)
		{
			update_peer(*iter, src, flags, tcp::endpoint());
			return *iter;
		}

		// we don't have any info about this peer.
		// add a new entry
		torrent_peer* p = m_peer_allocator.allocate_peer_entry(
			torrent_peer_allocator_interface::i2p_peer_type);
		if (p == nullptr) return nullptr;
		p = new (p) i2p_peer(destination, true, src);

		if (!insert_peer(p, iter, flags, state))
		{
			m_peer_allocator.free_peer_entry(p);
			return nullptr;
		}
		return p;
	}
#endif // TORRENT_USE_I2P

	// if this returns non-nullptr, the torrent need to post status update
	torrent_peer* peer_list::add_peer(tcp::endpoint const& remote
		, peer_source_flags_t const src, pex_flags_t const flags
		, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		auto const remote_address = remote.address();

		// just ignore the obviously invalid entries
		if (remote_address == address() || remote.port() == 0 || remote.port() == 1)
			return nullptr;

		// don't allow link-local IPv6 addresses since they
		// can't be used like normal addresses, they require an interface
		// and will just cause connect() to fail with EINVAL
		if (remote_address.is_v6() && remote_address.to_v6().is_link_local())
			return nullptr;

		iterator iter;
		torrent_peer* p = nullptr;

		bool found = false;
		if (state->allow_multiple_connections_per_ip)
		{
			auto const range = find_peers(remote_address);
			iter = std::find_if(range.first, range.second
				, match_peer_endpoint(remote_address, remote.port()));
			if (iter != range.second) found = true;
		}
		else
		{
			iter = std::lower_bound(m_peers.begin(), m_peers.end()
				, remote_address, peer_address_compare());

			if (iter != m_peers.end() && (*iter)->address() == remote_address) found = true;
		}

		if (!found)
		{
			// we don't have any info about this peer.
			// add a new entry

			bool const is_v6 = remote_address.is_v6();
			p = m_peer_allocator.allocate_peer_entry(
				is_v6 ? torrent_peer_allocator_interface::ipv6_peer_type
				: torrent_peer_allocator_interface::ipv4_peer_type);
			if (p == nullptr) return nullptr;

			if (is_v6)
				p = new (p) ipv6_peer(remote, true, src);
			else
				p = new (p) ipv4_peer(remote, true, src);

			try
			{
				if (!insert_peer(p, iter, flags, state))
				{
					m_peer_allocator.free_peer_entry(p);
					return nullptr;
				}
			}
			catch (std::exception const&)
			{
				m_peer_allocator.free_peer_entry(p);
				return nullptr;
			}
			state->first_time_seen = true;
		}
		else
		{
			p = *iter;
			TORRENT_ASSERT(p->in_use);
			update_peer(p, src, flags, remote);
			state->first_time_seen = false;
		}

		return p;
	}

	torrent_peer* peer_list::connect_one_peer(int session_time, torrent_state* state)
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		if (bool(m_finished) != state->is_finished)
			recalculate_connect_candidates(state);

		// clear out any peers from the cache that no longer
		// are connection candidates
		for (auto i = m_candidate_cache.begin(); i != m_candidate_cache.end();)
		{
			if (!is_connect_candidate(**i))
				i = m_candidate_cache.erase(i);
			else
				++i;
		}

		if (m_candidate_cache.empty())
		{
			find_connect_candidates(m_candidate_cache, session_time, state);
			if (m_candidate_cache.empty()) return nullptr;
		}

		torrent_peer* p = m_candidate_cache.front();
		m_candidate_cache.erase(m_candidate_cache.begin());

		TORRENT_ASSERT(p->in_use);

		TORRENT_ASSERT(!p->banned);
		TORRENT_ASSERT(!p->connection);
		TORRENT_ASSERT(p->connectable);

		// this should hold because find_connect_candidates should have done this
		TORRENT_ASSERT(bool(m_finished) == state->is_finished);

		// if we're finished, p->seed must be 0. We shouldn't be connecting to
		// seeds in that case
		TORRENT_ASSERT(m_finished == 0 || p->seed == 0);

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
		if (p == nullptr) return;

		TORRENT_ASSERT(p->in_use);

#if TORRENT_USE_INVARIANT_CHECKS
		// web seeds are special, they're not connected via the peer list
		// so they're not kept in m_peers
		TORRENT_ASSERT(p->web_seed
			|| std::any_of(m_peers.begin(), m_peers.end()
				, [&c](torrent_peer const* tp)
				{
					TORRENT_ASSERT(tp->in_use);
					return tp->connection == &c;
				}));
#endif

		TORRENT_ASSERT(p->connection == &c);
		TORRENT_ASSERT(!is_connect_candidate(*p));

		p->connection = nullptr;
		p->optimistically_unchoked = false;

		// if fast reconnect is true, we won't
		// update the timestamp, and it will remain
		// the time when we initiated the connection.
		if (!c.fast_reconnect())
			p->last_connected = std::uint16_t(session_time);

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

		m_num_connect_candidates += static_cast<int>(std::count_if(m_peers.begin(), m_peers.end()
			, [this](torrent_peer const* p) { return this->is_connect_candidate(*p); } ));

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

		auto const iter = std::lower_bound(m_peers.begin(), m_peers.end()
			, c->remote().address(), peer_address_compare());

		if (iter != m_peers.end() && (*iter)->address() == c->remote().address())
			return true;

		return std::any_of(m_peers.begin(), m_peers.end()
			, [c](torrent_peer const* p)
			{
				TORRENT_ASSERT(p->in_use);
				return p->connection == c
					|| (p->ip() == c->remote() && p->connectable);
			});
	}
#endif

#if TORRENT_USE_INVARIANT_CHECKS
	void peer_list::check_invariant() const
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_num_connect_candidates >= 0);
		TORRENT_ASSERT(m_num_connect_candidates <= int(m_peers.size()));

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		int connect_candidates = 0;

		const_iterator prev = m_peers.end();
		for (const_iterator i = m_peers.begin(); i != m_peers.end(); ++i)
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
			if (!p.connection)
			{
				continue;
			}
			if (p.optimistically_unchoked)
			{
				TORRENT_ASSERT(p.connection);
				TORRENT_ASSERT(!p.connection->is_choked());
			}
			TORRENT_ASSERT(p.connection->peer_info_struct() == nullptr
				|| p.connection->peer_info_struct() == &p);
		}

		TORRENT_ASSERT(m_num_connect_candidates == connect_candidates);
#endif // TORRENT_EXPENSIVE_INVARIANT_CHECKS

	}
#endif

}
