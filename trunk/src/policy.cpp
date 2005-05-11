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

#include <iostream>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/date_time/posix_time/posix_time.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/policy.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
#	define for if (false) {} else for
#endif

namespace libtorrent
{
	class peer_connection;
}

using namespace boost::posix_time;

namespace
{
	enum
	{
		// the limits of the download queue size
		max_request_queue = 100,
		min_request_queue = 2,

		// the amount of free upload allowed before
		// the peer is choked
		free_upload_amount = 4 * 16 * 1024
	};

	using namespace libtorrent;

	// the case where ignore_peer is motivated is if two peers
	// have only one piece that we don't have, and it's the
	// same piece for both peers. Then they might get into an
	// infinite recursion, fighting to request the same blocks.
	void request_a_block(
		torrent& t
		, peer_connection& c
		, std::vector<peer_connection*> ignore = std::vector<peer_connection*>())
	{
		// this will make the number of requests linearly dependent
		// on the rate in which we download from the peer.
		// we want the queue to represent:
		const int queue_time = 5; // seconds
		// (if the latency is more than this, the download will stall)
		// so, the queue size is 5 * down_rate / 16 kiB (16 kB is the size of each request)
		// the minimum request size is 2 and the maximum is 100

		int desired_queue_size = static_cast<int>(queue_time * c.statistics().download_rate() / (16 * 1024));
		if (desired_queue_size > max_request_queue) desired_queue_size = max_request_queue;
		if (desired_queue_size < min_request_queue) desired_queue_size = min_request_queue;

		assert(desired_queue_size >= min_request_queue);

		int num_requests = desired_queue_size - (int)c.download_queue().size();

		// if our request queue is already full, we
		// don't have to make any new requests yet
		if (num_requests <= 0) return;

		piece_picker& p = t.picker();
		std::vector<piece_block> interesting_pieces;
		interesting_pieces.reserve(100);

		// picks the interesting pieces from this peer
		// the integer is the number of pieces that
		// should be guaranteed to be available for download
		// (if num_requests is too big, too many pieces are
		// picked and cpu-time is wasted)
		p.pick_pieces(c.get_bitfield(), interesting_pieces, num_requests);

		// this vector is filled with the interesting pieces
		// that some other peer is currently downloading
		// we should then compare this peer's download speed
		// with the other's, to see if we should abort another
		// peer_connection in favour of this one
		std::vector<piece_block> busy_pieces;
		busy_pieces.reserve(10);

		for (std::vector<piece_block>::iterator i = interesting_pieces.begin();
			i != interesting_pieces.end();
			++i)
		{
			if (p.is_downloading(*i))
			{
				busy_pieces.push_back(*i);
				continue;
			}

			// ok, we found a piece that's not being downloaded
			// by somebody else. request it from this peer
			// and return
			c.send_request(*i);
			num_requests--;
			if (num_requests <= 0) return;
		}

		// in this case, we could not find any blocks
		// that was free. If we couldn't find any busy
		// blocks as well, we cannot download anything
		// more from this peer.

		if (busy_pieces.empty()) return;

		// first look for blocks that are just queued
		// and not actually sent to us yet
		// (then we can cancel those and request them
		// from this peer instead)

		while (num_requests > 0)
		{
			peer_connection* peer = 0;
			float min_weight = std::numeric_limits<float>::max();
			// find the peer with the lowest download
			// speed that also has a piece that this
			// peer could send us
			for (torrent::peer_iterator i = t.begin();
				i != t.end();
				++i)
			{
				// don't try to take over blocks from ourself
				if (i->second == &c)
					continue;

				// ignore all peers in the ignore list
				if (std::find(ignore.begin(), ignore.end(), i->second) != ignore.end())
					continue;

				const std::deque<piece_block>& queue = i->second->download_queue();
				const int queue_size = (int)i->second->download_queue().size();
				const float weight = queue_size == 0
					? std::numeric_limits<float>::max()
					: i->second->statistics().download_payload_rate() / queue_size;

				if (weight < min_weight
					&& std::find_first_of(
						busy_pieces.begin()
						, busy_pieces.end()
						, queue.begin()
						, queue.end()) != busy_pieces.end())
				{
					peer = i->second;
					min_weight = weight;
				}
			}

			if (peer == 0)
			{
				// we probably couldn't request the block because
				// we are ignoring some peers
				break;
			}

			// find a suitable block to take over from this peer

			std::deque<piece_block>::const_reverse_iterator common_block =
				std::find_first_of(
					peer->download_queue().rbegin()
					, peer->download_queue().rend()
					, busy_pieces.begin()
					, busy_pieces.end());

			assert(common_block != peer->download_queue().rend());
			piece_block block = *common_block;
			peer->send_cancel(block);
			c.send_request(block);

			// the one we interrupted may need to request a new piece
			// make sure it doesn't take over a block from the peer
			// that just took over its block
			ignore.push_back(&c);
			request_a_block(t, *peer, ignore);
			num_requests--;

			// this peer doesn't have a faster connection than the
			// slowest peer. Don't take over any blocks
			const int queue_size = (int)c.download_queue().size();
			const float weight = queue_size == 0
				? std::numeric_limits<float>::max()
				: c.statistics().download_payload_rate() / queue_size;

			if (weight <= min_weight) break;
		}
	}


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
			size_type diff = i->second->share_diff();
			assert(diff < std::numeric_limits<size_type>::max());
			if (i->second->is_peer_interested() || diff <= 0)
				continue;

			assert(diff > 0);
			i->second->add_free_upload(-diff);
			accumulator += diff;
			assert(accumulator > 0);
		}
		assert(accumulator >= 0);
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
			size_type d = i->second->share_diff();
			assert(d < std::numeric_limits<size_type>::max());
			total_diff += d;
			if (!i->second->is_peer_interested() || i->second->share_diff() >= 0) continue;
			++num_peers;
		}

		if (num_peers == 0) return free_upload;
		size_type upload_share;
		if (total_diff >= 0)
		{
			upload_share = std::min(free_upload, total_diff) / num_peers;
		}
		else
		{
			upload_share = (free_upload + total_diff) / num_peers;
		}
		if (upload_share < 0) return free_upload;

		for (torrent::peer_iterator i = start; i != end; ++i)
		{
			peer_connection* p = i->second;
			if (!p->is_peer_interested() || p->share_diff() >= 0) continue;
			p->add_free_upload(upload_share);
			free_upload -= upload_share;
		}
		return free_upload;
	}

	struct match_peer_ip
	{
		match_peer_ip(const address& id)
			: m_id(id)
		{}

		bool operator()(const policy::peer& p) const
		{ return p.id.ip() == m_id.ip(); }

		address m_id;
	};

	struct match_peer_connection
	{
		match_peer_connection(const peer_connection& c)
			: m_conn(c)
		{}

		bool operator()(const policy::peer& p) const
		{ return p.connection == &m_conn; }

		const peer_connection& m_conn;
	};


}

namespace libtorrent
{
	policy::policy(torrent* t)
		: m_num_peers(0)
		, m_torrent(t)
//		, m_max_uploads(std::numeric_limits<int>::max())
//		, m_max_connections(std::numeric_limits<int>::max())
		, m_num_unchoked(0)
		, m_available_free_upload(0)
		, m_last_optimistic_disconnect(boost::gregorian::date(1970,boost::gregorian::Jan,1))
	{ assert(t); }
	// finds the peer that has the worst download rate
	// and returns it. May return 0 if all peers are
	// choked.
	policy::peer* policy::find_choke_candidate()
	{
		INVARIANT_CHECK;

		peer* worst_peer = 0;
		size_type min_weight = std::numeric_limits<int>::min();

#ifndef NDEBUG
		int unchoked_counter = m_num_unchoked;
#endif
		
		// TODO: make this selection better

		for (std::vector<peer>::iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			peer_connection* c = i->connection;

			if (c == 0) continue;
			if (c->is_choked()) continue;
#ifndef NDEBUG
			unchoked_counter--;
#endif
			// if the peer isn't interested, just choke it
			if (!c->is_peer_interested())
				return &(*i);

			size_type diff = i->total_download()
				- i->total_upload();

			size_type weight = static_cast<int>(c->statistics().download_rate() * 10.f)
				+ diff
				+ ((c->is_interesting() && c->has_peer_choked())?-10:10)*1024;

			if (weight >= min_weight && worst_peer) continue;

			min_weight = weight;
			worst_peer = &(*i);
			continue;
		}
		assert(unchoked_counter == 0);
		return worst_peer;
	}

	policy::peer* policy::find_unchoke_candidate()
	{
		INVARIANT_CHECK;

		// if all of our peers are unchoked, there's
		// no left to unchoke
		if (m_num_unchoked == m_torrent->num_peers())
			return 0;

		using namespace boost::posix_time;
		using namespace boost::gregorian;

		peer* unchoke_peer = 0;
		ptime min_time(date(9999,Jan,1));
		float max_down_speed = 0.f;

		// TODO: make this selection better

		for (std::vector<peer>::iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			peer_connection* c = i->connection;
			if (c == 0) continue;
			if (c->is_disconnecting()) continue;
			if (!c->is_choked()) continue;
			if (!c->is_peer_interested()) continue;
			if (c->share_diff() < -free_upload_amount
				&& m_torrent->ratio() != 0) continue;
			if (c->statistics().download_rate() < max_down_speed) continue;
//			if (i->last_optimistically_unchoked > min_time) continue;

			min_time = i->last_optimistically_unchoked;
			max_down_speed = c->statistics().download_rate();
			unchoke_peer = &(*i);
		}
		return unchoke_peer;
	}

	policy::peer* policy::find_disconnect_candidate()
	{
		peer *disconnect_peer = 0;
		double slowest_transfer_rate = std::numeric_limits<double>::max();

		boost::posix_time::ptime local_time
			= second_clock::universal_time();

		for (std::vector<peer>::iterator i = m_peers.begin();
			i != m_peers.end();
			++i)
		{
			peer_connection* c = i->connection;
			if(c == 0)
				continue;
			if(c->is_disconnecting())
				continue;

			double transferred_amount
				= (double)c->statistics().total_payload_download();

			boost::posix_time::time_duration connected_time
				= local_time - i->connected;

			double connected_time_in_seconds
				= connected_time.seconds()
				+ connected_time.minutes()*60.0
				+ connected_time.hours()*60.0*60.0;

			double transfer_rate
				= transferred_amount / (connected_time_in_seconds+1);

			if (transfer_rate <= slowest_transfer_rate)
			{
				slowest_transfer_rate = transfer_rate;
				disconnect_peer = &(*i);
			}
		}
		return disconnect_peer;
	}

	policy::peer *policy::find_connect_candidate()
	{
		boost::posix_time::ptime local_time=second_clock::universal_time();
		boost::posix_time::ptime ptime(local_time);
		policy::peer* candidate  =0;

		for (std::vector<peer>::iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			if(i->connection) continue;
			if(i->banned) continue;
			if(i->type == peer::not_connectable) continue;

			assert(i->connected <= local_time);

			boost::posix_time::ptime next_connect = i->connected;

			if (next_connect <= ptime)
			{
				ptime = next_connect;
				candidate = &(*i);
			}
		}
		
		assert(ptime <= local_time);

		return candidate;
	}

	policy::peer* policy::find_seed_choke_candidate()
	{
		INVARIANT_CHECK;

		// first choice candidate.
		// it is a candidate we owe nothing to and which has been unchoked
		// the longest.
		using namespace boost::posix_time;
		using namespace boost::gregorian;

		peer* candidate = 0;

		// not valid when candidate == 0
		ptime last_unchoke = ptime(date(1970, Jan, 1));

		// second choice candidate.
		// if there is no first choice candidate, this candidate will be chosen.
		// it is the candidate that we owe the least to.
		peer* second_candidate = 0;
		size_type lowest_share_diff = 0; // not valid when secondCandidate==0

		for (std::vector<peer>::iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			peer_connection* c = i->connection;
            // ignore peers that are choked or
            // whose connection is closed
			if (c == 0) continue;

			if (c->is_choked()) continue;

			size_type share_diff = c->share_diff();

			// select as second candidate the one that we owe the least
			// to
			if (!second_candidate || share_diff <= lowest_share_diff)
			{
				lowest_share_diff = share_diff;
				second_candidate = &(*i);
			}
			
			// select as first candidate the one that we don't owe anything to
			// and has been waiting for an unchoke the longest
			if (share_diff > 0) continue;
			if (!candidate || last_unchoke > i->last_optimistically_unchoked)
			{
				last_unchoke = i->last_optimistically_unchoked;
				candidate = &(*i);
			}
		}
		if (candidate) return candidate;
		if (second_candidate) return second_candidate;
		assert(false);
		return 0;
	}

	policy::peer* policy::find_seed_unchoke_candidate()
	{
		INVARIANT_CHECK;

		peer* candidate = 0;
		boost::posix_time::ptime last_unchoke
			= second_clock::universal_time();

		for (std::vector<peer>::iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			peer_connection* c = i->connection;
			if (c == 0) continue;
			if (!c->is_choked()) continue;
			if (!c->is_peer_interested()) continue;
			if (c->is_disconnecting()) continue;
			if (last_unchoke < i->last_optimistically_unchoked) continue;
			last_unchoke = i->last_optimistically_unchoked;
			candidate = &(*i);
		}
		return candidate;
	}

	bool policy::seed_unchoke_one_peer()
	{
		peer* p = find_seed_unchoke_candidate();
		if (p != 0)
		{
			assert(p->connection->is_choked());
			p->connection->send_unchoke();
			p->last_optimistically_unchoked
				= second_clock::universal_time();
			++m_num_unchoked;
		}
		return p != 0;
	}

	void policy::pulse()
	{
		INVARIANT_CHECK;

		if (m_torrent->is_paused()) return;

		using namespace boost::posix_time;

		// TODO: we must also remove peers that
		// we failed to connect to from this list
		// to avoid being part of a DDOS-attack

		// remove old disconnected peers from the list
		m_peers.erase(
			std::remove_if(m_peers.begin()
				, m_peers.end()
				, old_disconnected_peer())
			, m_peers.end());

		// -------------------------------------
		// maintain the number of connections
		// -------------------------------------

		// count the number of connected peers except for peers
		// that are currently in the process of disconnecting
		int num_connected_peers = 0;

		for (std::vector<peer>::iterator i = m_peers.begin();
					i != m_peers.end();
					++i)
		{
			if (i->connection && !i->connection->is_disconnecting())
				++num_connected_peers;
		}

		if (m_torrent->m_connections_quota.given != std::numeric_limits<int>::max())
		{

			int max_connections = m_torrent->m_connections_quota.given;

			if (num_connected_peers >= max_connections)
			{
				// every minute, disconnect the worst peer in hope of finding a better peer

				boost::posix_time::ptime local_time = second_clock::universal_time();
				if (m_last_optimistic_disconnect + boost::posix_time::seconds(120) <= local_time)
				{
					m_last_optimistic_disconnect = local_time;
					--max_connections; // this will have the effect of disconnecting the worst peer
				}
			}
			else
			{
				// don't do a disconnect earlier than 1 minute after some peer was connected
				m_last_optimistic_disconnect = second_clock::universal_time();
			}

			while (num_connected_peers > max_connections)
			{
				bool ret = disconnect_one_peer();
				assert(ret);
				--num_connected_peers;
			}
		}

		while (m_torrent->num_peers() < m_torrent->m_connections_quota.given)
		{
			if (!connect_one_peer())
				break;
		}


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

		// ------------------------
		// seed choking policy
		// ------------------------
		if (m_torrent->is_seed())
		{
			if (num_connected_peers > m_torrent->m_uploads_quota.given)
			{
				// this means there are some peers that
				// are choked. To have the choked peers
				// rotate, unchoke one peer here
				// and let the next condiional block
				// make sure another peer is choked.
				seed_unchoke_one_peer();
			}

			while (m_num_unchoked > m_torrent->m_uploads_quota.given)
			{
				peer* p = find_seed_choke_candidate();
				assert(p != 0);

				assert(!p->connection->is_choked());
				p->connection->send_choke();
				--m_num_unchoked;
			}

			// make sure we have enough
			// unchoked peers
			while (m_num_unchoked < m_torrent->m_uploads_quota.given)
			{
				if (!seed_unchoke_one_peer()) break;
			}
#ifndef NDEBUG
			check_invariant();
#endif
		}

		// ----------------------------
		// downloading choking policy
		// ----------------------------
		else
		{
			if (m_torrent->ratio() != 0)
			{
				// choke peers that have leeched too much without giving anything back
				for (std::vector<peer>::iterator i = m_peers.begin();
					i != m_peers.end(); ++i)
				{
					peer_connection* c = i->connection;
					if (c == 0) continue;

					size_type diff = i->connection->share_diff();
					if (diff < -free_upload_amount
						&& !c->is_choked())
					{
						// if we have uploaded more than a piece for free, choke peer and
						// wait until we catch up with our download.
						c->send_choke();
						--m_num_unchoked;
					}
				}
			}
			
			if (m_torrent->m_uploads_quota.given < m_torrent->num_peers())
			{
				assert(m_torrent->m_uploads_quota.given >= 0);

				// make sure we don't have too many
				// unchoked peers
				while (m_num_unchoked > m_torrent->m_uploads_quota.given)
				{
					peer* p = find_choke_candidate();
					if (!p) break;
					assert(p);
					assert(!p->connection->is_choked());
					p->connection->send_choke();
					--m_num_unchoked;
				}

				// optimistic unchoke. trade the 'worst'
				// unchoked peer with one of the choked
				assert(m_num_unchoked <= m_torrent->num_peers());
				peer* p = find_choke_candidate();
				if (p)
				{
					assert(!p->connection->is_choked());
					p->connection->send_choke();
					--m_num_unchoked;
					unchoke_one_peer();
				}
			}

			// make sure we have enough
			// unchoked peers
			while (m_num_unchoked < m_torrent->m_uploads_quota.given
				&& unchoke_one_peer());
		}
	}

	void policy::ban_peer(const peer_connection& c)
	{
		std::vector<peer>::iterator i = std::find_if(
			m_peers.begin()
			, m_peers.end()
			, match_peer_connection(c));

		assert(i != m_peers.end());

		i->type = peer::not_connectable;
		i->id.port = address::any_port;
		i->banned = true;
	}

	void policy::new_connection(peer_connection& c)
	{
		assert(!c.is_local());

		// if the connection comes from the tracker,
		// it's probably just a NAT-check. Ignore the
		// num connections constraint then.

		// TODO: only allow _one_ connection to use this
		// override at a time
		if (m_torrent->num_peers() >= m_torrent->m_connections_quota.given
			&& c.get_socket()->sender().ip() != m_torrent->current_tracker().ip())
		{
			throw protocol_error("too many connections, refusing incoming connection"); // cause a disconnect
		}

#ifdef TORRENT_VERBOSE_LOGGING
		if (c.get_socket()->sender().ip() == m_torrent->current_tracker().ip())
		{
			m_torrent->debug_log("overriding connection limit for tracker NAT-check");
		}
#endif

		std::vector<peer>::iterator i = std::find_if(
			m_peers.begin()
			, m_peers.end()
			, match_peer_ip(c.get_socket()->sender()));

		if (i == m_peers.end())
		{
			using namespace boost::posix_time;
			using namespace boost::gregorian;

			// we don't have ny info about this peer.
			// add a new entry
			
			peer p(c.get_socket()->sender(), peer::not_connectable);
			m_peers.push_back(p);
			i = m_peers.end()-1;
		}
		else
		{
			if (i->connection != 0)
				throw protocol_error("duplicate connection, closing");
			if (i->banned)
				throw protocol_error("ip address banned, closing");
		}
		
		assert(i->connection == 0);
		c.add_stat(i->prev_amount_download, i->prev_amount_upload);
		i->prev_amount_download = 0;
		i->prev_amount_upload = 0;
		i->connection = &c;
		i->connected = second_clock::universal_time();
		m_last_optimistic_disconnect = second_clock::universal_time();
	}

	void policy::peer_from_tracker(const address& remote, const peer_id& id)
	{
		// just ignore the obviously invalid entries from the tracker
		if(remote.ip() == 0 || remote.port == 0)
			return;

		try
		{
			std::vector<peer>::iterator i = std::find_if(
				m_peers.begin()
				, m_peers.end()
				, match_peer_ip(remote));
			
			if (i == m_peers.end())
			{
				using namespace boost::posix_time;
				using namespace boost::gregorian;

				// we don't have any info about this peer.
				// add a new entry
				peer p(remote, peer::connectable);
				m_peers.push_back(p);
				// the iterator is invalid
				// because of the push_back()
				i = m_peers.end()-1;
			}
			else
			{
				i->type = peer::connectable;

				// in case we got the ip from a remote connection, port is
				// not known, so save it. Client may also have changed port
				// for some reason.
				i->id = remote;

				if (i->connection)
				{
					// this means we're already connected
					// to this peer. don't connect to
					// it again.
					assert(i->connection->associated_torrent() == m_torrent);
					return;
				}
			}

			if (i->banned) return;

			if (m_torrent->num_peers() < m_torrent->m_connections_quota.given
				&& !m_torrent->is_paused())
			{
				connect_peer(&*i);
			}
			return;
		}
		catch(network_error& e)
		{
			if (m_torrent->alerts().should_post(alert::debug))
			{
				m_torrent->alerts().post_alert(
					peer_error_alert(remote, id, e.what()));
			}
		}
		catch(protocol_error& e)
		{
			if (m_torrent->alerts().should_post(alert::debug))
			{
				m_torrent->alerts().post_alert(
					peer_error_alert(remote, id, e.what()));
			}
		}
	}

	// this is called when we are choked by a peer
	// i.e. a peer lets us know that we will not receive
	// anything for a while
	void policy::choked(peer_connection&)
	{
	}

	void policy::piece_finished(int index, bool successfully_verified)
	{
		assert(index >= 0 && index < m_torrent->torrent_file().num_pieces());

		if (successfully_verified)
		{
			// have all peers update their interested-flag
			for (std::vector<peer>::iterator i = m_peers.begin();
				i != m_peers.end();
				++i)
			{
				if (i->connection == 0) continue;
				// if we're not interested, we will not become interested
				if (!i->connection->is_interesting()) continue;
				if (!i->connection->has_piece(index)) continue;

				bool interested = false;
				const std::vector<bool>& peer_has = i->connection->get_bitfield();
				const std::vector<bool>& we_have = m_torrent->pieces();
				assert(we_have.size() == peer_has.size());
				for (int j = 0; j != (int)we_have.size(); ++j)
				{
					if (!we_have[j] && peer_has[j])
					{
						interested = true;
						break;
					}
				}
				if (!interested)
					i->connection->send_not_interested();
				assert(i->connection->is_interesting() == interested);
			}
		}
	}

	// TODO: we must be able to get interested
	// in a peer again, if a piece fails that
	// this peer has.
	void policy::block_finished(peer_connection& c, piece_block)
	{
		// if the peer hasn't choked us, ask for another piece
		if (!c.has_peer_choked())
			request_a_block(*m_torrent, c);
	}

	// this is called when we are unchoked by a peer
	// i.e. a peer lets us know that we will receive
	// data from now on
	void policy::unchoked(peer_connection& c)
	{
		if (c.is_interesting())
		{
			request_a_block(*m_torrent, c);
		}
	}

	// called when a peer is interested in us
	void policy::interested(peer_connection&)
	{
	}

	// called when a peer is no longer interested in us
	void policy::not_interested(peer_connection& c)
	{
		if (m_torrent->ratio() != 0.f)
		{
			assert(c.share_diff() < std::numeric_limits<size_type>::max());
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
		if (!c.is_choked())
		{
			c.send_choke();
			--m_num_unchoked;
		}
	}

	bool policy::unchoke_one_peer()
	{
		peer* p = find_unchoke_candidate();
		if (p == 0) return false;
		assert(p->connection);
		assert(!p->connection->is_disconnecting());

		assert(p->connection->is_choked());
		p->connection->send_unchoke();
		p->last_optimistically_unchoked = second_clock::universal_time();
		++m_num_unchoked;
		return true;
	}

	bool policy::connect_one_peer()
	{
		if(m_torrent->num_peers() >= m_torrent->m_connections_quota.given)
			return false;
		peer* p = find_connect_candidate();
		if (p == 0) return false;
		assert(!p->banned);
		assert(!p->connection);
		assert(p->type == peer::connectable);

		return connect_peer(p);
	}

	bool policy::connect_peer(peer *p)
	{
		try
		{
			p->connection = &m_torrent->connect_to_peer(p->id);
			p->connection->add_stat(p->prev_amount_download, p->prev_amount_upload);
			p->prev_amount_download = 0;
			p->prev_amount_upload = 0;
			p->connected =
				m_last_optimistic_disconnect = 
					second_clock::universal_time();
			return true;
		}
		catch (network_error&)
		{
			// TODO: remove the peer
//			m_peers.erase(std::find(m_peers.begin(), m_peers.end(), p));
		}
		return false;
	}

	bool policy::disconnect_one_peer()
	{
		peer *p = find_disconnect_candidate();
		if(!p)
			return false;
		p->connection->disconnect();
		return true;
	}

	// this is called whenever a peer connection is closed
	void policy::connection_closed(const peer_connection& c)
	{
		INVARIANT_CHECK;

		bool unchoked = false;

		std::vector<peer>::iterator i = std::find_if(
			m_peers.begin()
			, m_peers.end()
			, match_peer_connection(c));

		// if we couldn't find the connection in our list, just ignore it.
		if (i == m_peers.end()) return;
		assert(i->connection == &c);

		i->connected = second_clock::universal_time();
		if (!i->connection->is_choked() && !m_torrent->is_aborted())
		{
			unchoked = true;
		}

		if (c.failed())
		{
			i->type = peer::not_connectable;
			i->id.port = address::any_port;
		}

		// if the share ratio is 0 (infinite), the
		// m_available_free_upload isn't used,
		// because it isn't necessary.
		if (m_torrent->ratio() != 0.f)
		{
			assert(i->connection->associated_torrent() == m_torrent);
			assert(i->connection->share_diff() < std::numeric_limits<size_type>::max());
			m_available_free_upload += i->connection->share_diff();
		}
		i->prev_amount_download += c.statistics().total_payload_download();
		i->prev_amount_upload += c.statistics().total_payload_upload();
		i->connection = 0;

		if (unchoked)
		{
			// if the peer that is diconnecting is unchoked
			// then unchoke another peer in order to maintain
			// the total number of unchoked peers
			--m_num_unchoked;
			unchoke_one_peer();
		}
	}

	void policy::peer_is_interesting(peer_connection& c)
	{
		c.send_interested();
		if (c.has_peer_choked()) return;
		request_a_block(*m_torrent, c);
	}

#ifndef NDEBUG
	bool policy::has_connection(const peer_connection* c)
	{
		assert(c);
		return std::find_if(
			m_peers.begin()
			, m_peers.end()
			, match_peer_ip(c->get_socket()->sender())) != m_peers.end();
	}

	void policy::check_invariant() const
	{
		assert(m_torrent->m_uploads_quota.given >= 2);
		int actual_unchoked = 0;
		for (std::vector<peer>::const_iterator i = m_peers.begin();
			i != m_peers.end(); ++i)
		{
			if (!i->connection) continue;
			if (!i->connection->is_choked()) actual_unchoked++;
		}
//		assert(actual_unchoked <= m_torrent->m_uploads_quota.given);
		assert(actual_unchoked == m_num_unchoked);
	}
#endif

	policy::peer::peer(const address& pid, peer::connection_type t)
		: id(pid)
		, type(t)
		, last_optimistically_unchoked(
			boost::gregorian::date(1970,boost::gregorian::Jan,1))
		, connected(boost::gregorian::date(1970,boost::gregorian::Jan,1))
		, prev_amount_upload(0)
		, prev_amount_download(0)
		, banned(false)
		, connection(0)
	{
		assert(connected < second_clock::universal_time());
	}

	size_type policy::peer::total_download() const
	{
		if (connection != 0)
		{
			assert(prev_amount_download == 0);
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
			assert(prev_amount_upload == 0);
			return connection->statistics().total_payload_upload();
		}
		else
		{
			return prev_amount_upload;
		}
	}
}
