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

#include "libtorrent/policy.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_connection.hpp"

namespace
{
	using namespace libtorrent;
	void request_a_block(torrent& t, peer_connection& c)
	{
		piece_picker& p = t.picker();
		std::vector<piece_block> interesting_pieces;
		interesting_pieces.reserve(100);
		p.pick_pieces(c.get_bitfield(), interesting_pieces);

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
			return;
		}

		// TODO: compare this peer's bandwidth against the
		// ones downloading these pieces (busy_pieces)
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
*/


	policy::policy(torrent* t)
		: m_num_peers(0)
		, m_torrent(t)
	{}


	// this is called when a connection is made, before any
	// handshake (it's possible to ban certain ip:s).
	bool policy::accept_connection(const address& remote)
	{
		m_num_peers++;
		return true;
	}

	void policy::peer_from_tracker(const address& remote, const peer_id& id)
	{
		m_torrent->connect_to_peer(remote, id);
		m_num_peers++;
	}

	// this is called when we are choked by a peer
	// i.e. a peer lets us know that we will not receive
	// anything for a while
	void policy::choked(peer_connection& c)
	{
		c.choke();
	}

	void policy::piece_finished(peer_connection& c, int index, bool successfully_verified)
	{
		// TODO: if verification failed, mark the peers that were involved
		// in some way
	}

	void policy::block_finished(peer_connection& c, piece_block b)
	{
		if (c.has_peer_choked()) return;
		request_a_block(*m_torrent, c);
	}

	// this is called when we are unchoked by a peer
	// i.e. a peer lets us know that we will receive
	// data from now on
	void policy::unchoked(peer_connection& c)
	{
		c.unchoke();
		if (c.is_interesting()) request_a_block(*m_torrent, c);
	}

	void policy::interested(peer_connection& c)
	{
		c.unchoke();
	}

	void policy::not_interested(peer_connection& c)
	{
	}

	void policy::connection_closed(const peer_connection& c)
	{
	}

	void policy::peer_is_interesting(peer_connection& c)
	{
		c.interested();
		if (c.has_peer_choked()) return;
		request_a_block(*m_torrent, c);
	}
}
