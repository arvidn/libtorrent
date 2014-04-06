/*

Copyright (c) 2013, Arvid Norberg
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

#ifndef FILE_REQUESTS_HPP_
#define FILE_REQUESTS_HPP_

#include <boost/shared_array.hpp>
#include <boost/thread/future.hpp>
#include <set>
#include <boost/functional/hash.hpp>

#include "libtorrent/thread.hpp" // for mutex
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/peer_id.hpp" // for sha1_hash
#include "libtorrent/extensions.hpp" // for plugin
#include "libtorrent/time.hpp" // for ptime

using libtorrent::sha1_hash;
using libtorrent::mutex;

struct piece_entry
{
	boost::shared_array<char> buffer;
	int size;
	int piece;
};

// this is a session plugin which wraps the concept of reading pieces
// from torrents, returning futures for when those pieces are complete
struct file_requests : libtorrent::plugin
{
	file_requests();
	void on_alert(libtorrent::alert const* a);
	void on_tick();
	boost::shared_future<piece_entry> read_piece(libtorrent::torrent_handle const& h
		, int piece, int timeout_ms);

private:

	struct piece_request
	{
		sha1_hash info_hash;
		int piece;
		boost::shared_ptr<boost::promise<piece_entry> > promise;
		libtorrent::ptime timeout;
		bool operator==(piece_request const& rq) const
		{ return rq.info_hash == info_hash && rq.piece == piece; }
		bool operator<(piece_request const& rq) const
		{ return info_hash == rq.info_hash ? piece < rq.piece : info_hash < rq.info_hash; }
	};

	std::size_t hash_value(piece_request const& r) const;

	mutex m_mutex;
	typedef std::multiset<piece_request> requests_t;
	requests_t m_requests;
	requests_t::iterator m_next_timeout;

	// TOOD: figure out a way to clear out info-hashes
	std::map<sha1_hash, std::set<int> > m_have_pieces;
};

#endif // FILE_REQUESTS_HPP_

