/*

Copyright (c) 2003, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include <iostream>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>
#include <boost/utility.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/broadcast_socket.hpp"

#ifdef TORRENT_DEBUG
#include "libtorrent/bt_peer_connection.hpp"
#endif

namespace libtorrent
{
	class peer_connection;
}

using boost::bind;

namespace
{
	using namespace libtorrent;

	size_type collect_free_download(
		torrent::peer_iterator start
		, torrent::peer_iterator end)
	{
		size_type accumulator = 0;
		for (torrent::peer_iterator i = start; i != end; ++i)
		{
			// if the peer is interested in us, it means it may
			// want to trade it's surplus uploads for downloads itself
			// (and we should not consider it free). If the share diff is
			// negative, there's no free download to get from this peer.
			size_type diff = (*i)->share_diff();
			TORRENT_ASSERT(diff < (std::numeric_limits<size_type>::max)());
			if ((*i)->is_peer_interested() || diff <= 0)
				continue;

			TORRENT_ASSERT(diff > 0);
			(*i)->add_free_upload(-diff);
			accumulator += diff;
			TORRENT_ASSERT(accumulator > 0);
		}
		TORRENT_ASSERT(accumulator >= 0);
		return accumulator;
	}


	// returns the amount of free upload left after
	// it has been distributed to the peers
	size_type distribute_free_upload(
		torrent::peer_iterator start
		, torrent::peer_iterator end
		, size_type free_upload)
	{
		if (free_upload <= 0) return free_upload;
		int num_peers = 0;
		size_type total_diff = 0;
		for (torrent::peer_iterator i = start; i != end; ++i)
		{
			size_type d = (*i)->share_diff();
			TORRENT_ASSERT(d < (std::numeric_limits<size_type>::max)());
			total_diff += d;
			if (!(*i)->is_peer_interested() || (*i)->share_diff() >= 0) continue;
			++num_peers;
		}

		if (num_peers == 0) return free_upload;
		size_type upload_share;
		if (total_diff >= 0)
		{
			upload_share = (std::min)(free_upload, total_diff) / num_peers;
		}
		else
		{
			upload_share = (free_upload + total_diff) / num_peers;
		}
		if (upload_share < 0) return free_upload;

		for (torrent::peer_iterator i = start; i != end; ++i)
		{
			peer_connection* p = *i;
			if (!p->is_peer_interested() || p->share_diff() >= 0) continue;
			p->add_free_upload(upload_share);
			free_upload -= upload_share;
		}
		return free_upload;
	}

	struct match_peer_endpoint
	{
		match_peer_endpoint(tcp::endpoint const& ep)
			: m_ep(ep)
		{}

		bool operator()(std::pair<const address, policy::peer> const& p) const
		{ return p.second.addr == m_ep.address() && p.second.port == m_ep.port(); }

		tcp::endpoint const& m_ep;
	};

#ifdef TORRENT_DEBUG
	struct match_peer_connection
	{
		match_peer_connection(peer_connection const& c)
			: m_conn(c)
		{}

		bool operator()(std::pair<const address, policy::peer> const& p) const
		{
			return p.second.connection == &m_conn
				|| (p.second.ip() == m_conn.remote()
					&& p.second.type == policy::peer::connectable);
		}

		peer_connection const& m_conn;
	};
#endif

}

namespace libtorrent
{
	// the case where ignore_peer is motivated is if two peers
	// have only one piece that we don't have, and it's the
	// same piece for both peers. Then they might get into an
	// infinite loop, fighting to request the same blocks.
	void request_a_block(torrent& t, peer_connection& c)
	{
		if (t.is_seed()) return;

		// don't request pieces before we have the metadata
		if (!t.valid_metadata()) return;

		// don't request pieces before the peer is properly
		// initialized after we have the metadata
		if (!t.are_files_checked()) return;

		TORRENT_ASSERT(t.valid_metadata());
		TORRENT_ASSERT(c.peer_info_struct() != 0 || !dynamic_cast<bt_peer_connection*>(&c));
		int num_requests = c.desired_queue_size()
			- (int)c.download_queue().size()
			- (int)c.request_queue().size();

#ifdef TORRENT_VERBOSE_LOGGING
		(*c.m_logger) << time_now_string() << " PIECE_PICKER [ req: " << num_requests << " ]\n";
#endif
		TORRENT_ASSERT(c.desired_queue_size() > 0);
		// if our request queue is already full, we
		// don't have to make any new requests yet
		if (num_requests <= 0) return;

		piece_picker& p = t.picker();
		std::vector<piece_block> interesting_pieces;
		interesting_pieces.reserve(100);

		int prefer_whole_pieces = c.prefer_whole_pieces();

		if (prefer_whole_pieces == 0)
		{
			prefer_whole_pieces = c.statistics().download_payload_rate()
				* t.settings().whole_pieces_threshold
				> t.torrent_file().piece_length() ? 1 : 0;
		}
	
		// if we prefer whole pieces, the piece picker will pick at least
		// the number of blocks we want, but it will try to make the picked
		// blocks be from whole pieces, possibly by returning more blocks
		// than we requested.
#ifdef TORRENT_DEBUG
		error_code ec;
		TORRENT_ASSERT(c.remote() == c.get_socket()->remote_endpoint(ec) || ec);
#endif

		piece_picker::piece_state_t state;
		peer_connection::peer_speed_t speed = c.peer_speed();
		if (speed == peer_connection::fast) state = piece_picker::fast;
		else if (speed == peer_connection::medium) state = piece_picker::medium;
		else state = piece_picker::slow;

		// this vector is filled with the interesting pieces
		// that some other peer is currently downloading
		// we should then compare this peer's download speed
		// with the other's, to see if we should abort another
		// peer_connection in favour of this one
		std::vector<piece_block> busy_pieces;
		busy_pieces.reserve(num_requests);

		std::vector<int> const& suggested = c.suggested_pieces();
		bitfield const& bits = c.get_bitfield();

		if (c.has_peer_choked())
		{
			// if we are choked we can only pick pieces from the
			// allowed fast set. The allowed fast set is sorted
			// in ascending priority order
			std::vector<int> const& allowed_fast = c.allowed_fast();

			// build a bitmask with only the allowed pieces in it
			bitfield mask(c.get_bitfield().size(), false);
			for (std::vector<int>::const_iterator i = allowed_fast.begin()
				, end(allowed_fast.end()); i != end; ++i)
				if (bits[*i]) mask.set_bit(*i);

			p.pick_pieces(mask, interesting_pieces
				, num_requests, prefer_whole_pieces, c.peer_info_struct()
				, state, c.picker_options(), suggested);
		}
		else
		{
			// picks the interesting pieces from this peer
			// the integer is the number of pieces that
			// should be guaranteed to be available for download
			// (if num_requests is too big, too many pieces are
			// picked and cpu-time is wasted)
			// the last argument is if we should prefer whole pieces
			// for this peer. If we're downloading one piece in 20 seconds
			// then use this mode.
			p.pick_pieces(bits, interesting_pieces
				, num_requests, prefer_whole_pieces, c.peer_info_struct()
				, state, c.picker_options(), suggested);
		}

#ifdef TORRENT_VERBOSE_LOGGING
		(*c.m_logger) << time_now_string() << " PIECE_PICKER [ php: " << prefer_whole_pieces
			<< " picked: " << interesting_pieces.size() << " ]\n";
#endif
		std::deque<pending_block> const& dq = c.download_queue();
		std::deque<piece_block> const& rq = c.request_queue();
		for (std::vector<piece_block>::iterator i = interesting_pieces.begin();
			i != interesting_pieces.end(); ++i)
		{
			if (prefer_whole_pieces == 0 && num_requests <= 0) break;

			if (p.is_requested(*i))
			{
				if (num_requests <= 0) break;
				// don't request pieces we already have in our request queue
				if (std::find_if(dq.begin(), dq.end(), has_block(*i)) != dq.end()
					|| std::find(rq.begin(), rq.end(), *i) != rq.end())
					continue;
	
				TORRENT_ASSERT(p.num_peers(*i) > 0);
				busy_pieces.push_back(*i);
				continue;
			}

			TORRENT_ASSERT(p.num_peers(*i) == 0);
			// ok, we found a piece that's not being downloaded
			// by somebody else. request it from this peer
			// and return
			c.add_request(*i);
			TORRENT_ASSERT(p.num_peers(*i) == 1);
			TORRENT_ASSERT(p.is_requested(*i));
			num_requests--;
		}

		if (busy_pieces.empty() || num_requests <= 0)
		{
			return;
		}

		// if all blocks has the same number of peers on them
		// we want to pick a random block
		std::random_shuffle(busy_pieces.begin(), busy_pieces.end());
		
		// find the block with the fewest requests to it
		std::vector<piece_block>::iterator i = std::min_element(
			busy_pieces.begin(), busy_pieces.end()
			, bind(&piece_picker::num_peers, boost::cref(p), _1) <
			bind(&piece_picker::num_peers, boost::cref(p), _2));
#ifdef TORRENT_DEBUG
		piece_picker::downloading_piece st;
		p.piece_info(i->piece_index, st);
		TORRENT_ASSERT(st.requested + st.finished + st.writing == p.blocks_in_piece(i->piece_index));
#endif
		TORRENT_ASSERT(p.is_requested(*i));
		TORRENT_ASSERT(p.num_peers(*i) > 0);
		c.add_request(*i);
	}

	policy::policy(torrent* t)
		: m_round_robin(m_peers.end())
		, m_torrent(t)
		, m_available_free_upload(0)
		, m_num_connect_candidates(0)
		, m_num_seeds(0)
		, m_finished(false)
	{ TORRENT_ASSERT(t); }

	// disconnects and removes all peers that are now filtered
	void policy::ip_filter_updated()
	{
		aux::session_impl& ses = m_torrent->session();
		piece_picker* p = 0;
		if (m_torrent->has_picker())
			p = &m_torrent->picker();
		for (iterator i = m_peers.begin(); i != m_peers.end();)
		{
			if ((ses.m_ip_filter.access(i->second.addr) & ip_filter::blocked) == 0)
			{
				++i;
				continue;
			}
		
			if (i->second.connection)
			{
				i->second.connection->disconnect("peer banned by IP filter");
				if (ses.m_alerts.should_post<peer_blocked_alert>())
					ses.m_alerts.post_alert(peer_blocked_alert(i->second.addr));
				TORRENT_ASSERT(i->second.connection == 0
					|| i->second.connection->peer_info_struct() == 0);
			}
			else
			{
				if (ses.m_alerts.should_post<peer_blocked_alert>())
					ses.m_alerts.post_alert(peer_blocked_alert(i->second.addr));
			}
			erase_peer(i++);
		}
	}

	// any peer that is erased from m_peers will be
	// erased through this function. This way we can make
	// sure that any references to the peer are removed
	// as well, such as in the piece picker.
	void policy::erase_peer(iterator i)
	{
		INVARIANT_CHECK;

		if (m_torrent->has_picker())
			m_torrent->picker().clear_peer(&i->second);
		if (i->second.seed) --m_num_seeds;
		if (is_connect_candidate(i->second, m_finished))
		{
			TORRENT_ASSERT(m_num_connect_candidates > 0);
			--m_num_connect_candidates;
		}
		if (m_round_robin == i) ++m_round_robin;

		m_peers.erase(i);
	}

	void policy::ban_peer(policy::peer* p)
	{
		INVARIANT_CHECK;

		if (is_connect_candidate(*p, m_finished))
			--m_num_connect_candidates;

		p->banned = true;
	}

	bool policy::is_connect_candidate(peer const& p, bool finished) const
	{
		if (p.connection
			|| p.banned
			|| p.type == peer::not_connectable
			|| (p.seed && finished)
			|| p.failcount >= m_torrent->settings().max_failcount)
			return false;
		
		aux::session_impl& ses = m_torrent->session();
		if (ses.m_port_filter.access(p.port) & port_filter::blocked)
			return false;
		return true;
	}

	policy::iterator policy::find_connect_candidate()
	{
		INVARIANT_CHECK;

		ptime now = time_now();
		iterator candidate = m_peers.end();

		int min_reconnect_time = m_torrent->settings().min_reconnect_time;
		address external_ip = m_torrent->session().external_address();

		// don't bias any particular peers when seeding
		if (m_finished || external_ip == address())
		{
			// set external_ip to a random value, to
			// radomize which peers we prefer
			address_v4::bytes_type bytes;
			std::generate(bytes.begin(), bytes.end(), &std::rand);
			external_ip = address_v4(bytes);
		}

		if (m_round_robin == m_peers.end()) m_round_robin = m_peers.begin();

#ifndef TORRENT_DISABLE_DHT
		bool pinged = false;
#endif

		for (int iterations = (std::min)(int(m_peers.size()), 300);
			iterations > 0; --iterations)
		{
			if (m_round_robin == m_peers.end()) m_round_robin = m_peers.begin();

			peer& pe = m_round_robin->second;
			iterator current = m_round_robin;

#ifndef TORRENT_DISABLE_DHT
			// try to send a DHT ping to this peer
			// as well, to figure out if it supports
			// DHT (uTorrent and BitComet doesn't
			// advertise support)
			if (!pinged && !pe.added_to_dht)
			{
				udp::endpoint node(pe.addr, pe.port);
				m_torrent->session().add_dht_node(node);
				pe.added_to_dht = true;
				pinged = true;
			}
#endif
			// if the number of peers is growing large
			// we need to start weeding.
			// don't remove peers we're connected to
			// don't remove peers we've never even tried
			// don't remove banned peers unless they're 2
			// hours old. They should remain banned for
			// at least that long
			// don't remove peers that we still can try again
			if (pe.connection == 0
				&& pe.connected != min_time()
				&& (!pe.banned || now - pe.connected > hours(2))
				&& !is_connect_candidate(pe, m_finished)
				&& m_peers.size() >= m_torrent->settings().max_peerlist_size * 0.9)
			{
				erase_peer(m_round_robin++);
				continue;
			}

			++m_round_robin;

			if (!is_connect_candidate(pe, m_finished)) continue;

			if (candidate != m_peers.end()
				&& compare_peer(candidate->second, pe, external_ip)) continue;

			if (now - pe.connected <
				seconds((pe.failcount + 1) * min_reconnect_time))
				continue;

			candidate = current;
		}
		
#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
		if (candidate != m_peers.end())
		{
			(*m_torrent->session().m_logger) << time_now_string()
				<< " *** FOUND CONNECTION CANDIDATE ["
				" ip: " << candidate->second.ip() <<
				" d: " << cidr_distance(external_ip, candidate->second.addr) <<
				" external: " << external_ip <<
				" t: " << total_seconds(time_now() - candidate->second.connected) <<
				" ]\n";
		}
#endif

		return candidate;
	}

	void policy::pulse()
	{
		INVARIANT_CHECK;

		// ------------------------
		// upload shift
		// ------------------------

		// this part will shift downloads
		// from peers that are seeds and peers
		// that don't want to download from us
		// to peers that cannot upload anything
		// to us. The shifting will make sure
		// that the torrent's share ratio
		// will be maintained

		// if the share ratio is 0 (infinite)
		// m_available_free_upload isn't used
		// because it isn't necessary
		if (m_torrent->ratio() != 0.f)
		{
			// accumulate all the free download we get
			// and add it to the available free upload
			m_available_free_upload
				+= collect_free_download(
					m_torrent->begin()
					, m_torrent->end());

			// distribute the free upload among the peers
			m_available_free_upload = distribute_free_upload(
				m_torrent->begin()
				, m_torrent->end()
				, m_available_free_upload);
		}
	}

	bool policy::new_connection(peer_connection& c)
	{
		TORRENT_ASSERT(!c.is_local());

		INVARIANT_CHECK;

		// if the connection comes from the tracker,
		// it's probably just a NAT-check. Ignore the
		// num connections constraint then.

		// TODO: only allow _one_ connection to use this
		// override at a time
		error_code ec;
		TORRENT_ASSERT(c.remote() == c.get_socket()->remote_endpoint(ec) || ec);

		aux::session_impl& ses = m_torrent->session();
		
		if (m_torrent->num_peers() >= m_torrent->max_connections()
			&& ses.num_connections() >= ses.max_connections()
			&& c.remote().address() != m_torrent->current_tracker().address())
		{
			c.disconnect("too many connections, refusing incoming connection");
			return false;
		}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (c.remote().address() == m_torrent->current_tracker().address())
		{
			m_torrent->debug_log("overriding connection limit for tracker NAT-check");
		}
#endif

		iterator i;

		if (m_torrent->settings().allow_multiple_connections_per_ip)
		{
			tcp::endpoint remote = c.remote();
			std::pair<iterator, iterator> range = m_peers.equal_range(remote.address());
			i = std::find_if(range.first, range.second, match_peer_endpoint(remote));
	
			if (i == range.second) i = m_peers.end();
		}
		else
		{
			i = m_peers.find(c.remote().address());
		}

		if (i != m_peers.end())
		{
			if (i->second.banned)
			{
				c.disconnect("ip address banned, closing");
				return false;
			}

			if (i->second.connection != 0)
			{
				boost::shared_ptr<socket_type> other_socket
					= i->second.connection->get_socket();
				boost::shared_ptr<socket_type> this_socket
					= c.get_socket();

				error_code ec1;
				error_code ec2;
				bool self_connection =
					other_socket->remote_endpoint(ec2) == this_socket->local_endpoint(ec1)
					|| other_socket->local_endpoint(ec2) == this_socket->remote_endpoint(ec1);

				if (ec1)
				{
					c.disconnect(ec1.message().c_str());
					return false;
				}

				if (self_connection)
				{
					c.disconnect("connected to ourselves", 1);
					i->second.connection->disconnect("connected to ourselves", 1);
					return false;
				}

				TORRENT_ASSERT(i->second.connection != &c);
				// the new connection is a local (outgoing) connection
				// or the current one is already connected
				if (ec2)
				{
					i->second.connection->disconnect(ec2.message().c_str());
				}
				else if (!i->second.connection->is_connecting() || c.is_local())
				{
					c.disconnect("duplicate connection, closing");
					return false;
				}
				else
				{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
					m_torrent->debug_log("duplicate connection. existing connection"
					" is connecting and this connection is incoming. closing existing "
					"connection in favour of this one");
#endif
					i->second.connection->disconnect("incoming duplicate connection "
						"with higher priority, closing");
				}
			}

			if (is_connect_candidate(i->second, m_finished)
				&& m_num_connect_candidates > 0)
				--m_num_connect_candidates;
		}
		else
		{
			// we don't have any info about this peer.
			// add a new entry
			error_code ec;
			TORRENT_ASSERT(c.remote() == c.get_socket()->remote_endpoint(ec) || ec);

			if (int(m_peers.size()) >= m_torrent->settings().max_peerlist_size)
			{
				c.disconnect("peer list size exceeded, refusing incoming connection");
				return false;
			}

			peer p(c.remote(), peer::not_connectable, 0);
			i = m_peers.insert(std::make_pair(c.remote().address(), p));
#ifndef TORRENT_DISABLE_GEO_IP
			int as = ses.as_for_ip(c.remote().address());
#ifdef TORRENT_DEBUG
			i->second.inet_as_num = as;
#endif
			i->second.inet_as = ses.lookup_as(as);
#endif
			i->second.source = peer_info::incoming;
		}
	
		c.set_peer_info(&i->second);
		TORRENT_ASSERT(i->second.connection == 0);
		c.add_stat(i->second.prev_amount_download, i->second.prev_amount_upload);
		i->second.prev_amount_download = 0;
		i->second.prev_amount_upload = 0;
		i->second.connection = &c;
		TORRENT_ASSERT(i->second.connection);
		if (!c.fast_reconnect())
			i->second.connected = time_now();
		return true;
	}

	bool policy::update_peer_port(int port, policy::peer* p, int src)
	{
		TORRENT_ASSERT(p != 0);
		TORRENT_ASSERT(p->connection);

		INVARIANT_CHECK;

		if (p->port == port) return true;

		if (m_torrent->settings().allow_multiple_connections_per_ip)
		{
			tcp::endpoint remote(p->addr, port);
			std::pair<iterator, iterator> range = m_peers.equal_range(remote.address());
			iterator i = std::find_if(range.first, range.second
				, match_peer_endpoint(remote));
			if (i != m_peers.end())
			{
				policy::peer& pp = i->second;
				if (pp.connection)
				{
					p->connection->disconnect("duplicate connection");
					return false;
				}
				erase_peer(i);
			}
		}
		else
		{
			TORRENT_ASSERT(m_peers.count(p->addr) == 1);
		}
		bool was_conn_cand = is_connect_candidate(*p, m_finished);
		p->port = port;
		p->source |= src;
		p->type = policy::peer::connectable;

		if (was_conn_cand != is_connect_candidate(*p, m_finished))
		{
			m_num_connect_candidates += was_conn_cand ? -1 : 1;
			TORRENT_ASSERT(m_num_connect_candidates >= 0);
			if (m_num_connect_candidates < 0) m_num_connect_candidates = 0;
		}
		return true;
	}

	bool policy::has_peer(policy::peer const* p) const
	{
		// find p in m_peers
		for (std::multimap<address, peer>::const_iterator i = m_peers.begin()
			, end(m_peers.end()); i != end; ++i)
		{
			if (&i->second == p) return true;
		}
		return false;
	}

	void policy::set_seed(policy::peer* p, bool s)
	{
		if (p == 0) return;
		if (p->seed == s) return;
		bool was_conn_cand = is_connect_candidate(*p, m_finished);
		p->seed = s;
		if (was_conn_cand && !is_connect_candidate(*p, m_finished))
		{
			--m_num_connect_candidates;
			if (m_num_connect_candidates < 0) m_num_connect_candidates = 0;
		}

		if (s) ++m_num_seeds;
		else --m_num_seeds;
	}

	policy::peer* policy::peer_from_tracker(tcp::endpoint const& remote, peer_id const& pid
		, int src, char flags)
	{
		INVARIANT_CHECK;

		// just ignore the obviously invalid entries
		if (remote.address() == address() || remote.port() == 0)
			return 0;

		aux::session_impl& ses = m_torrent->session();

		port_filter const& pf = ses.m_port_filter;
		if (pf.access(remote.port()) & port_filter::blocked)
		{
			if (ses.m_alerts.should_post<peer_blocked_alert>())
				ses.m_alerts.post_alert(peer_blocked_alert(remote.address()));
			return 0;
		}

		iterator i;

		if (m_torrent->settings().allow_multiple_connections_per_ip)
		{
			std::pair<iterator, iterator> range = m_peers.equal_range(remote.address());
			i = std::find_if(range.first, range.second, match_peer_endpoint(remote));
			if (i == range.second) i = m_peers.end();
		}
		else
		{
			i = m_peers.find(remote.address());
		}

		if (i == m_peers.end())
		{
			// if the IP is blocked, don't add it
			if (ses.m_ip_filter.access(remote.address()) & ip_filter::blocked)
			{
				if (ses.m_alerts.should_post<peer_blocked_alert>())
				{
					ses.m_alerts.post_alert(peer_blocked_alert(remote.address()));
				}
				return 0;
			}

			if (int(m_peers.size()) >= m_torrent->settings().max_peerlist_size)
				return 0;

			// we don't have any info about this peer.
			// add a new entry
			i = m_peers.insert(std::make_pair(remote.address()
					, peer(remote, peer::connectable, src)));
#ifndef TORRENT_DISABLE_ENCRYPTION
			if (flags & 0x01) i->second.pe_support = true;
#endif
			if (flags & 0x02)
			{
				i->second.seed = true;
				++m_num_seeds;
			}

#ifndef TORRENT_DISABLE_GEO_IP
			int as = ses.as_for_ip(remote.address());
#ifdef TORRENT_DEBUG
			i->second.inet_as_num = as;
#endif
			i->second.inet_as = ses.lookup_as(as);
#endif
			if (is_connect_candidate(i->second, m_finished))
				++m_num_connect_candidates;
		}
		else
		{
			bool was_conn_cand = is_connect_candidate(i->second, m_finished);

			i->second.type = peer::connectable;

			i->second.set_ip(remote);
			i->second.source |= src;
				
			// if this peer has failed before, decrease the
			// counter to allow it another try, since somebody
			// else is appearantly able to connect to it
			// only trust this if it comes from the tracker
			if (i->second.failcount > 0 && src == peer_info::tracker)
				--i->second.failcount;

			// if we're connected to this peer
			// we already know if it's a seed or not
			// so we don't have to trust this source
			if ((flags & 0x02) && !i->second.connection)
			{
				if (!i->second.seed) ++m_num_seeds;
				i->second.seed = true;
			}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
			if (i->second.connection)
			{
				// this means we're already connected
				// to this peer. don't connect to
				// it again.

				error_code ec;
				m_torrent->debug_log("already connected to peer: " + remote.address().to_string(ec) + ":"
					+ boost::lexical_cast<std::string>(remote.port()) + " "
					+ boost::lexical_cast<std::string>(i->second.connection->pid()));

				TORRENT_ASSERT(i->second.connection->associated_torrent().lock().get() == m_torrent);
			}
#endif

			if (was_conn_cand != is_connect_candidate(i->second, m_torrent->is_finished()))
			{
				m_num_connect_candidates += was_conn_cand ? -1 : 1;
				if (m_num_connect_candidates < 0) m_num_connect_candidates = 0;
			}
		}

		return &i->second;
	}

	// this is called when we are unchoked by a peer
	// i.e. a peer lets us know that we will receive
	// data from now on
	void policy::unchoked(peer_connection& c)
	{
		INVARIANT_CHECK;
		if (c.is_interesting())
		{
			request_a_block(*m_torrent, c);
			c.send_block_requests();
		}
	}

	// called when a peer is interested in us
	void policy::interested(peer_connection& c)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(std::find_if(m_peers.begin(), m_peers.end()
			, boost::bind<bool>(std::equal_to<peer_connection*>(), bind(&peer::connection
			, bind(&iterator::value_type::second, _1)), &c)) != m_peers.end());
		
		aux::session_impl& ses = m_torrent->session();

		// if the peer is choked and we have upload slots left,
		// then unchoke it. Another condition that has to be met
		// is that the torrent doesn't keep track of the individual
		// up/down ratio for each peer (ratio == 0) or (if it does
		// keep track) this particular connection isn't a leecher.
		// If the peer was choked because it was leeching, don't
		// unchoke it again.
		// The exception to this last condition is if we're a seed.
		// In that case we don't care if people are leeching, they
		// can't pay for their downloads anyway.
		if (c.is_choked()
			&& ses.num_uploads() < ses.max_uploads()
			&& (m_torrent->ratio() == 0
				|| c.share_diff() >= -free_upload_amount
				|| m_torrent->is_finished()))
		{
			ses.unchoke_peer(c);
		}
#if defined TORRENT_VERBOSE_LOGGING
		else if (c.is_choked())
		{
			std::string reason;
			if (ses.num_uploads() >= ses.max_uploads())
			{
				reason = "the number of uploads ("
					+ boost::lexical_cast<std::string>(ses.num_uploads())
					+ ") is more than or equal to the limit ("
					+ boost::lexical_cast<std::string>(ses.max_uploads())
					+ ")";
			}
			else
			{
				reason = "the share ratio ("
					+ boost::lexical_cast<std::string>(c.share_diff())
					+ ") is <= free_upload_amount ("
					+ boost::lexical_cast<std::string>(int(free_upload_amount))
					+ ") and we are not seeding and the ratio ("
					+ boost::lexical_cast<std::string>(m_torrent->ratio())
					+ ")is non-zero";
			}
			(*c.m_logger) << time_now_string() << " DID NOT UNCHOKE [ " << reason << " ]\n";
		}
#endif
	}

	// called when a peer is no longer interested in us
	void policy::not_interested(peer_connection& c)
	{
		INVARIANT_CHECK;

		if (m_torrent->ratio() != 0.f)
		{
			TORRENT_ASSERT(c.share_diff() < (std::numeric_limits<size_type>::max)());
			size_type diff = c.share_diff();
			if (diff > 0 && c.is_seed())
			{
				// the peer is a seed and has sent
				// us more than we have sent it back.
				// consider the download as free download
				m_available_free_upload += diff;
				c.add_free_upload(-diff);
			}
		}
	}
/*
	bool policy::unchoke_one_peer()
	{
		INVARIANT_CHECK;

		iterator p = find_unchoke_candidate();
		if (p == m_peers.end()) return false;
		TORRENT_ASSERT(p->connection);
		TORRENT_ASSERT(!p->connection->is_disconnecting());

		TORRENT_ASSERT(p->connection->is_choked());
		p->connection->send_unchoke();
		p->last_optimistically_unchoked = time_now();
		++m_num_unchoked;
		return true;
	}

	void policy::choke_one_peer()
	{
		INVARIANT_CHECK;

		iterator p = find_choke_candidate();
		if (p == m_peers.end()) return;
		TORRENT_ASSERT(p->connection);
		TORRENT_ASSERT(!p->connection->is_disconnecting());
		TORRENT_ASSERT(!p->connection->is_choked());
		p->connection->send_choke();
		--m_num_unchoked;
	}
*/
	bool policy::connect_one_peer()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_torrent->want_more_peers());
		
		iterator p = find_connect_candidate();
		if (p == m_peers.end()) return false;

		TORRENT_ASSERT(!p->second.banned);
		TORRENT_ASSERT(!p->second.connection);
		TORRENT_ASSERT(p->second.type == peer::connectable);

		TORRENT_ASSERT(m_finished == m_torrent->is_finished());
		TORRENT_ASSERT(is_connect_candidate(p->second, m_finished));
		if (!m_torrent->connect_to_peer(&p->second))
		{
			++p->second.failcount;
			if (!is_connect_candidate(p->second, m_finished))
				--m_num_connect_candidates;
			return false;
		}
		TORRENT_ASSERT(p->second.connection);
		TORRENT_ASSERT(!is_connect_candidate(p->second, m_finished));
		--m_num_connect_candidates;
		return true;
	}

	// this is called whenever a peer connection is closed
	void policy::connection_closed(const peer_connection& c)
	{
		INVARIANT_CHECK;

		peer* p = c.peer_info_struct();

		TORRENT_ASSERT((std::find_if(
			m_peers.begin()
			, m_peers.end()
			, match_peer_connection(c))
			!= m_peers.end()) == (p != 0));
		
		// if we couldn't find the connection in our list, just ignore it.
		if (p == 0) return;

		TORRENT_ASSERT(p->connection == &c);

		p->connection = 0;
		p->optimistically_unchoked = false;

		// if fast reconnect is true, we won't
		// update the timestamp, and it will remain
		// the time when we initiated the connection.
		if (!c.fast_reconnect())
			p->connected = time_now();

		if (c.failed())
		{
			++p->failcount;
//			p->connected = time_now();
		}

		if (is_connect_candidate(*p, m_finished))
			++m_num_connect_candidates;

		// if the share ratio is 0 (infinite), the
		// m_available_free_upload isn't used,
		// because it isn't necessary.
		if (m_torrent->ratio() != 0.f)
		{
			TORRENT_ASSERT(c.associated_torrent().lock().get() == m_torrent);
			TORRENT_ASSERT(c.share_diff() < (std::numeric_limits<size_type>::max)());
			m_available_free_upload += c.share_diff();
		}
		TORRENT_ASSERT(p->prev_amount_upload == 0);
		TORRENT_ASSERT(p->prev_amount_download == 0);
		p->prev_amount_download += c.statistics().total_payload_download();
		p->prev_amount_upload += c.statistics().total_payload_upload();
	}

	void policy::peer_is_interesting(peer_connection& c)
	{
		INVARIANT_CHECK;

		if (c.in_handshake()) return;
		c.send_interested();
		if (c.has_peer_choked()
			&& c.allowed_fast().empty())
			return;
		request_a_block(*m_torrent, c);
		c.send_block_requests();
	}

	void policy::recalculate_connect_candidates()
	{
		INVARIANT_CHECK;

		const bool is_finished = m_torrent->is_finished();
		if (is_finished == m_finished) return;

		m_num_connect_candidates = 0;
		m_finished = is_finished;
		for (const_iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			m_num_connect_candidates += is_connect_candidate(i->second, m_finished);
		}
	}

#ifdef TORRENT_DEBUG
	bool policy::has_connection(const peer_connection* c)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(c);
		error_code ec;
		TORRENT_ASSERT(c->remote() == c->get_socket()->remote_endpoint(ec) || ec);

		return std::find_if(
			m_peers.begin()
			, m_peers.end()
			, match_peer_connection(*c)) != m_peers.end();
	}

	void policy::check_invariant() const
	{
		TORRENT_ASSERT(m_num_connect_candidates >= 0);
		TORRENT_ASSERT(m_num_connect_candidates <= m_peers.size());
		if (m_torrent->is_aborted()) return;

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		int connected_peers = 0;

		int total_connections = 0;
		int nonempty_connections = 0;
		int connect_candidates = 0;

		std::set<tcp::endpoint> unique_test;
		for (const_iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			peer const& p = i->second;
 			if (is_connect_candidate(p, m_finished)) ++connect_candidates;
#ifndef TORRENT_DISABLE_GEO_IP
			TORRENT_ASSERT(p.inet_as == 0 || p.inet_as->first == p.inet_as_num);
#endif
			if (!m_torrent->settings().allow_multiple_connections_per_ip)
			{
				TORRENT_ASSERT(m_peers.count(p.addr) == 1);
			}
			else
			{
				TORRENT_ASSERT(unique_test.count(p.ip()) == 0);
				unique_test.insert(p.ip());
				TORRENT_ASSERT(i->first == p.addr);
//				TORRENT_ASSERT(p.connection == 0 || p.ip() == p.connection->remote());
			}
			++total_connections;
			if (!p.connection)
			{
				continue;
			}
			TORRENT_ASSERT(p.prev_amount_upload == 0);
			TORRENT_ASSERT(p.prev_amount_download == 0);
			if (p.optimistically_unchoked)
			{
				TORRENT_ASSERT(p.connection);
				TORRENT_ASSERT(!p.connection->is_choked());
			}
			TORRENT_ASSERT(p.connection->peer_info_struct() == 0
				|| p.connection->peer_info_struct() == &p);
			++nonempty_connections;
			if (!p.connection->is_disconnecting())
				++connected_peers;
		}

		TORRENT_ASSERT(m_num_connect_candidates == connect_candidates);

		int num_torrent_peers = 0;
		for (torrent::const_peer_iterator i = m_torrent->begin();
			i != m_torrent->end(); ++i)
		{
			if ((*i)->is_disconnecting()) continue;
			// ignore web_peer_connections since they are not managed
			// by the policy class
			if (dynamic_cast<web_peer_connection*>(*i)) continue;
			++num_torrent_peers;
		}

		if (m_torrent->has_picker())
		{
			piece_picker& p = m_torrent->picker();
			std::vector<piece_picker::downloading_piece> downloaders = p.get_download_queue();

			std::set<void*> peer_set;
			std::vector<void*> peers;
			for (std::vector<piece_picker::downloading_piece>::iterator i = downloaders.begin()
				, end(downloaders.end()); i != end; ++i)
			{
				p.get_downloaders(peers, i->index);
				std::copy(peers.begin(), peers.end()
					, std::insert_iterator<std::set<void*> >(peer_set, peer_set.begin()));
			}
			
			for (std::set<void*>::iterator i = peer_set.begin()
				, end(peer_set.end()); i != end; ++i)
			{
				policy::peer* p = static_cast<policy::peer*>(*i);
				if (p == 0) continue;
				if (p->connection == 0) continue;
				TORRENT_ASSERT(std::find_if(m_peers.begin(), m_peers.end()
					, match_peer_connection(*p->connection)) != m_peers.end());
			}
		}
#endif

		// this invariant is a bit complicated.
		// the usual case should be that connected_peers
		// == num_torrent_peers. But when there's an incoming
		// connection, it will first be added to the policy
		// and then be added to the torrent.
		// When there's an outgoing connection, it will first
		// be added to the torrent and then to the policy.
		// that's why the two second cases are in there.
/*
		TORRENT_ASSERT(connected_peers == num_torrent_peers
			|| (connected_peers == num_torrent_peers + 1
				&& connected_peers > 0)
			|| (connected_peers + 1 == num_torrent_peers
				&& num_torrent_peers > 0));
*/
	}
#endif

	policy::peer::peer(const tcp::endpoint& ip_, peer::connection_type t, int src)
		: prev_amount_upload(0)
		, prev_amount_download(0)
		, addr(ip_.address())
		, last_optimistically_unchoked(min_time())
		, connected(min_time())
		, connection(0)
#ifndef TORRENT_DISABLE_GEO_IP
		, inet_as(0)
#endif
		, port(ip_.port())
		, failcount(0)
		, trust_points(0)
		, source(src)
		, hashfails(0)
		, type(t)
		, fast_reconnects(0)
#ifndef TORRENT_DISABLE_ENCRYPTION
		, pe_support(true)
#endif
		, optimistically_unchoked(false)
		, seed(false)
		, on_parole(false)
		, banned(false)
#ifndef TORRENT_DISABLE_DHT
		, added_to_dht(false)
#endif
	{
		TORRENT_ASSERT((src & 0xff) == src);
		TORRENT_ASSERT(connected < time_now());
	}

	size_type policy::peer::total_download() const
	{
		if (connection != 0)
		{
			TORRENT_ASSERT(prev_amount_download == 0);
			return connection->statistics().total_payload_download();
		}
		else
		{
			return prev_amount_download;
		}
	}

	size_type policy::peer::total_upload() const
	{
		if (connection != 0)
		{
			TORRENT_ASSERT(prev_amount_upload == 0);
			return connection->statistics().total_payload_upload();
		}
		else
		{
			return prev_amount_upload;
		}
	}

	// this returns true if lhs is a better connect candidate than rhs
	bool policy::compare_peer(policy::peer const& lhs, policy::peer const& rhs
		, address const& external_ip) const
	{
		// prefer peers with lower failcount
		if (lhs.failcount != rhs.failcount)
			return lhs.failcount < rhs.failcount;

		// Local peers should always be tried first
		bool lhs_local = is_local(lhs.addr);
		bool rhs_local = is_local(rhs.addr);
		if (lhs_local != rhs_local) return lhs_local > rhs_local;

		if (lhs.connected != rhs.connected)
			return lhs.connected < rhs.connected;

#ifndef TORRENT_DISABLE_GEO_IP
		// don't bias fast peers when seeding
		if (!m_finished && m_torrent->session().has_asnum_db())
		{
			int lhs_as = lhs.inet_as ? lhs.inet_as->second : 0;
			int rhs_as = rhs.inet_as ? rhs.inet_as->second : 0;
			if (lhs_as != rhs_as) return lhs_as > rhs_as;
		}
#endif
		int lhs_distance = cidr_distance(external_ip, lhs.addr);
		int rhs_distance = cidr_distance(external_ip, rhs.addr);
		if (lhs_distance < rhs_distance) return true;
		return false;
	}
}

