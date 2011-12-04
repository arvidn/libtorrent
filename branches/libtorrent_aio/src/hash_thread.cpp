/*

Copyright (c) 2011, Arvid Norberg
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

#include "libtorrent/config.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#include "libtorrent/hash_thread.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/disk_io_job.hpp"

namespace libtorrent
{

	const int block_size = 0x4000;

	// returns true if the job was submitted for async. processing
	// and false if it was processed immediately
	bool hash_thread::async_hash(cached_piece_entry* p, int start, int end)
	{
		TORRENT_ASSERT(p->hashing == -1);
		if (p->hashing != -1) return false;
		TORRENT_ASSERT(p->hash != 0);

		hash_queue_entry e;
		e.piece = p;
		e.start = start;
		e.end = end;
		return post_job(e);
	}

	void hash_thread::retain_job(hash_queue_entry& e)
	{
		cached_piece_entry* p = e.piece;
		// increase refcounts to make sure these blocks are not removed
		p->hashing = e.start;
		for (int i = e.start; i < e.end; ++i)
		{
			if (p->blocks[i].refcount == 0) m_disk_thread->pinned_change(1);
			++p->blocks[i].refcount;
			++p->refcount;
			// make sure the counters didn't wrap
			TORRENT_ASSERT(p->blocks[i].refcount > 0);
			TORRENT_ASSERT(p->refcount > 0);
#ifdef TORRENT_DEBUG
			TORRENT_ASSERT(!p->blocks[i].hashing);
			p->blocks[i].hashing = true;
#endif
		}
		++m_outstanding_jobs;
	}

	void hash_thread::process_job(hash_queue_entry const& e, bool post)
	{
		int piece_size = e.piece->storage.get()->files()->piece_size(e.piece->piece);
		partial_hash& ph = *e.piece->hash;
		for (int i = e.start; i < e.end; ++i)
		{
			cached_block_entry& bl = e.piece->blocks[i];
			// make sure we're not waiting for data to be read into
			// this buffer. i.e. pending = true and dirty = false
			TORRENT_ASSERT(!bl.pending || bl.dirty);
			TORRENT_ASSERT(bl.buf);

			int size = (std::min)(block_size, piece_size - ph.offset);
			ph.h.update(bl.buf, size);
			ph.offset += size;
		}

		if (post)
		{
			// post back to the disk thread
			disk_io_job* j = m_disk_thread->aiocbs()->allocate_job(
				disk_io_job::hash_complete);
			j->buffer = (char*)e.piece;
			j->piece = e.start;
			j->d.io.offset = e.end;
			m_disk_thread->add_job(j, true);
		}
	}
}

