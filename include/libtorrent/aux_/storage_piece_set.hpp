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

#ifndef TORRENT_STORAGE_PIECE_SET_HPP_INCLUDE
#define TORRENT_STORAGE_PIECE_SET_HPP_INCLUDE

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/intrusive/list.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/aux_/export.hpp"
#include "libtorrent/block_cache.hpp" // for cached_piece_entry

namespace libtorrent {

struct cached_piece_entry;

namespace aux {

	// this class keeps track of which pieces, belonging to
	// a specific storage, are in the cache right now. It's
	// used for quickly being able to evict all pieces for a
	// specific torrent
	struct TORRENT_EXPORT storage_piece_set
	{
		using list_t = boost::intrusive::list<cached_piece_entry, boost::intrusive::constant_time_size<false>>;
		void add_piece(cached_piece_entry* p);
		void remove_piece(cached_piece_entry* p);
		int num_pieces() const { return m_num_pieces; }
		list_t const& cached_pieces() const
		{ return m_cached_pieces; }
	private:
		// these are cached pieces belonging to this storage
		list_t m_cached_pieces;
		int m_num_pieces = 0;
	};
}}

#endif
