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

#include <boost/date_time/posix_time/posix_time.hpp>

#include "libtorrent/policy.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_connection.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
#	define for if (false) {} else for
#endif

namespace
{
	enum
	{
		// we try to maintain 4 requested blocks in the download
		// queue
		request_queue = 16
	};


	using namespace libtorrent;


	// TODO: replace these two functions with std::find_first_of
	template<class It1, class It2>
	bool has_intersection(It1 start1, It1 end1, It2 start2, It2 end2)
	{
		for (;start1 != end1; ++start1)
			for (;start2 != end2; ++start2)
				if (*start1 == *start2) return true;
		return false;
	}

	piece_block find_first_common(const std::vector<piece_block>& queue,
		const std::vector<piece_block>& busy)
	{
		for (std::vector<piece_block>::const_reverse_iterator i
			= queue.rbegin();
			i != queue.rend();
			++i)
		{
			for (std::vector<piece_block>::const_iterator j
				= busy.begin();
				j != busy.end();
				++j)
			{
				if ((*j) == (*i)) return *i;
			}
		}
		assert(false);
	}

	void request_a_block(torrent& t, peer_connection& c)
	{
		int num_requests = request_queue - c.download_queue().size();

		// if our request queue is already full, we
		// don't have to make any new requests yet
		if (num_requests <= 0) return;

		piece_picker& p = t.picker();
		std::vector<piece_block> interesting_pieces;
		interesting_pieces.reserve(100);

		// picks the interesting pieces from this peer
		// the integer is the number of pieces that
		// should be guaranteed to be available for download
		// (if this number is too big, too many pieces are
		// picked and cpu-time is wasted)
		p.pick_pieces(c.get_bitfield(), interesting_pieces, num_requests);

		// this vector is filled with the interestin pieces
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
			c.request_block(*i);
			num_requests--;
			if (num_requests <= 0) return;
		}

		if (busy_pieces.empty()) return;

		// first look for blocks that are just queued
		// and not actually sent to us yet
		// (then we can cancel those and request them
		// from this peer instead)

		peer_connection* peer = 0;
		float down_speed = -1.f;
		// find the peer with the lowest download
		// speed that also has a piece that this
		// peer could send us
		for (torrent::peer_iterator i = t.begin();
			i != t.end();
			++i)
		{
			const std::vector<piece_block>& queue = (*i)->download_queue();
			if ((*i)->statistics().down_peak() > down_speed
				&& has_intersection(busy_pieces.begin(),
					busy_pieces.end(),
					queue.begin(),
					queue.end()))
			{
				peer = *i;
				down_speed = (*i)->statistics().down_peak();
			}
		}

		assert(peer != 0);

		// this peer doesn't have a faster connection than the
		// slowest peer. Don't take over any blocks
		if (c.statistics().down_peak() <= down_speed) return;

		// find a suitable block to take over from this peer
		piece_block block = find_first_common(peer->download_queue(), busy_pieces);
		peer->cancel_block(block);
		c.request_block(block);

		// the one we interrupted may need to request a new piece
		request_a_block(t, *peer);

		num_requests--;
	}

}

namespace libtorrent
{
/*
	TODO: make two proxy classes that filter out
	all unneccesary members from torrent and peer_connection
	to make it easier to use them in the policy

	useful member functions:

	void torrent::connect_to_peer(address, peer_id);
	piece_picker& torrent::picker();
	std::vector<peer_connection*>::const_iterator torrent::begin() const
	std::vector<peer_connection*>::const_iterator torrent::end() const

	void peer_connection::interested();
	void peer_connection::not_interested();
	void peer_connection::choke();
	void peer_connection::unchoke();
	void peer_connection::request_piece(int index);
	const std::vector<int>& peer_connection::download_queue();

	TODO: to implement choking/unchoking we need a list with all
	connected peers. Something like this:

*/


	policy::policy(torrent* t)
		: m_num_peers(0)
		, m_torrent(t)
	{}

	void policy::pulse()
	{
		using namespace boost::posix_time;

		// remove old disconnected peers from the list
		m_peers.erase(
			std::remove_if(m_peers.begin()
			, m_peers.end()
			, old_disconnected_peer())
			, m_peers.end());

		// choke peers that have leeched too much without giving anything back
		for (std::vector<peer>::iterator i = m_peers.begin(); i != m_peers.end(); ++i)
		{
			boost::shared_ptr<peer_connection> c = i->connection.lock();
			if (c.get() == 0) continue;

			int downloaded = i->prev_amount_download + c->statistics().total_download();
			int uploaded = i->prev_amount_upload + c->statistics().total_upload();

			if (uploaded - downloaded > m_torrent->torrent_file().piece_length()
				&& !c->is_choked())
			{
				// if we have uploaded more than a piece for free, choke peer and
				// wait until we catch up with our download.
				c->choke();
			}
			else if (uploaded - downloaded <= m_torrent->block_size()
				&& c->is_choked() && c->is_peer_interested())
			{
				// we have catched up. We have now shared the same amount
				// to eachother. Unchoke this peer.
				c->unchoke();
			}
		}
	}

	void policy::new_connection(const boost::weak_ptr<peer_connection>& c)
	{
		boost::shared_ptr<peer_connection> con = c.lock();
		assert(con.get() != 0);
		if (con.get() == 0) return;

		std::vector<peer>::iterator i
			= std::find(m_peers.begin(), m_peers.end(), con->get_peer_id());
		if (i == m_peers.end())
		{
			// we don't have ny info about this peer.
			// add a new entry
			peer p(con->get_peer_id());
			m_peers.push_back(p);
			i = m_peers.end()-1;
		}
		else
		{
			assert(i->connection.expired());
		}
		
		i->connected = boost::posix_time::second_clock::local_time();
		i->connection = c;
	}

	void policy::peer_from_tracker(const address& remote, const peer_id& id)
	{
		try
		{
			std::vector<peer>::iterator i = std::find(m_peers.begin(), m_peers.end(), id);
			if (i == m_peers.end())
			{
				// we don't have ny info about this peer.
				// add a new entry
				peer p(id);
				m_peers.push_back(p);
				i = m_peers.end()-1;
			}
			else if (!i->connection.expired())
			{
				// this means we're already connected
				// to this peer. don't connect to
				// it again.
				return;
			}

			i->connected = boost::posix_time::second_clock::local_time();
			i->connection = m_torrent->connect_to_peer(remote, id);

		}
		catch(network_error&) {}
	}

	// this is called when we are choked by a peer
	// i.e. a peer lets us know that we will not receive
	// anything for a while
	void policy::choked(peer_connection& c)
	{
	}

	// TODO: the peer_connection argument here should be removed.
	void policy::piece_finished(peer_connection& c, int index, bool successfully_verified)
	{
		// TODO: if verification failed, mark the peers that were involved
		// in some way
	}

	void policy::block_finished(peer_connection& c, piece_block b)
	{
		// if the peer hasn't choked us, ask for another piece
		if (!c.has_peer_choked()) request_a_block(*m_torrent, c);
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

	void policy::interested(peer_connection& c)
	{
		// if we're interested in the peer, we unchoke it
		// and hopes it will unchoke us too
	}

	void policy::not_interested(peer_connection& c)
	{
	}

	// this is called whenever a peer connection is closed
	void policy::connection_closed(const peer_connection& c)
	{
		std::vector<peer>::iterator i
			= std::find(m_peers.begin(), m_peers.end(), c.get_peer_id());

		assert(i != m_peers.end());

		i->connected = boost::posix_time::second_clock::local_time();
		i->prev_amount_download += c.statistics().total_download();
		i->prev_amount_upload += c.statistics().total_upload();
	}

	void policy::peer_is_interesting(peer_connection& c)
	{
		c.interested();
		if (c.has_peer_choked()) return;
		request_a_block(*m_torrent, c);
	}

#ifndef NDEBUG
	bool policy::has_connection(const peer_connection* p)
	{
		return std::find(m_peers.begin(), m_peers.end(), p->get_peer_id()) != m_peers.end();
	}
#endif
}
