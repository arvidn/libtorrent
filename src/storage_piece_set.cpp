/*

Copyright (c) 2003-2016, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/aux_/storage_piece_set.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/block_cache.hpp"
#include "libtorrent/storage.hpp" // for storage_interface

namespace libtorrent { namespace aux {

	void storage_piece_set::add_piece(cached_piece_entry* p)
	{
		TORRENT_ASSERT(p->in_storage == false);
		TORRENT_ASSERT(p->storage.get() == this);
		m_cached_pieces.push_back(*p);
		++m_num_pieces;
#if TORRENT_USE_ASSERTS
		p->in_storage = true;
#endif
	}

	void storage_piece_set::remove_piece(cached_piece_entry* p)
	{
		TORRENT_ASSERT(p->in_storage == true);
		p->unlink();
		--m_num_pieces;
#if TORRENT_USE_ASSERTS
		p->in_storage = false;
#endif
	}

}}
