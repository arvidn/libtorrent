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

	hash_thread::hash_thread(disk_io_thread* d)
		: m_num_threads(0)
		, m_disk_thread(d)
	{}

	// returns true if the job was submitted for async. processing
	// and false if it was processed immediately
	bool hash_thread::async_hash(cached_piece_entry* p, int start, int end)
	{
		TORRENT_ASSERT(p->hashing == -1);
		if (p->hashing != -1) return false;

		hash_queue_entry e;
		e.piece = p;
		e.start = start;
		e.end = end;

		if (m_num_threads == 0)
		{
			// if we don't have any worker threads
			// just do the work immediately
			process_piece(e);
			return false;
		}
		else
		{
			// increase refcounts to make sure these blocks are not removed
			p->hashing = start;
			for (int i = start; i < end; ++i)
			{
				if (p->blocks[i].refcount == 0) m_disk_thread->pinned_change(1);
				++p->blocks[i].refcount;
				++p->refcount;
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(!p->blocks[i].hashing);
				p->blocks[i].hashing = true;
#endif
			}

			mutex::scoped_lock l(m_mutex);
			m_queue.push_back(e);
			m_cond.signal_all(l);
			return true;
		}
	}

	void hash_thread::set_num_threads(int i, bool wait)
	{
		if (i == m_num_threads) return;

		if (i > m_num_threads)
		{
			int k = m_num_threads;
			while (k < i)
			{
				m_threads.push_back(boost::shared_ptr<thread>(
					new thread(boost::bind(&hash_thread::thread_fun, this))));
				++k;
			}
		}
		else
		{
			while (m_num_threads > i) { --m_num_threads; }
			mutex::scoped_lock l(m_mutex);
			m_cond.signal_all(l);
			l.unlock();
			// TODO: technically, we can't not wait, since if
			// we destruct immediately following those threads
			// may post messages back to the disk thread after
			// it has been destructed
			if (wait) for (int i = m_num_threads; i < m_threads.size(); ++i) m_threads[i]->join();
			// this will detach the threads
			m_threads.resize(m_num_threads);
		}
	}

	void hash_thread::stop() { set_num_threads(0, true); }

	void hash_thread::thread_fun()
	{
		int thread_id = (++m_num_threads) - 1;
	
		for (;;)
		{
			mutex::scoped_lock l(m_mutex);
			while (m_queue.empty() && thread_id < m_num_threads) m_cond.wait(l);

			// if the number of wanted thread is decreased,
			// we may stop this thread
			// when we're terminating the last hasher thread (id=0), make sure
			// we finish up all queud jobs first
			if ((thread_id != 0 || m_queue.empty()) && thread_id >= m_num_threads) break;

			hash_queue_entry e = m_queue.front();
			m_queue.pop_front();
			l.unlock();

			process_piece(e);

			// post back to the disk thread
			disk_io_job* j = m_disk_thread->aiocbs()->allocate_job(
				disk_io_job::hash_complete);
			j->buffer = (char*)e.piece;
			j->piece = e.start;
			j->offset = e.end;
			m_disk_thread->add_job(j);
		}

#ifdef TORRENT_DEBUG
		if (thread_id == 0)
		{
			// when we're terminating the last hasher thread, make sure
			// there are no more scheduled jobs
			mutex::scoped_lock l(m_mutex);
			TORRENT_ASSERT(m_queue.empty());
		}
#endif
	}

	void hash_thread::process_piece(hash_queue_entry const& e)
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
	}
}

