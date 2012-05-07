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

#ifndef TORRENT_HASH_THREAD
#define TORRENT_HASH_THREAD

#include "libtorrent/config.hpp"
#include "libtorrent/thread_pool.hpp"

namespace libtorrent
{
	struct cached_piece_entry;
	struct disk_io_thread;
	struct block_cache;

	struct hash_queue_entry
	{
		cached_piece_entry* piece;
		int start;
		int end;
	};

	struct hash_thread_interface
	{
		virtual bool async_hash(cached_piece_entry* p, int start, int end) = 0;
		virtual ~hash_thread_interface() {}
	};

	struct hash_thread : thread_pool<hash_queue_entry>, hash_thread_interface
	{
		hash_thread(disk_io_thread* d) : m_outstanding_jobs(0), m_disk_thread(d) {}
		virtual bool async_hash(cached_piece_entry* p, int start, int end);

		int num_pending_jobs() const { return m_outstanding_jobs; }
		void hash_job_done() { TORRENT_ASSERT(m_outstanding_jobs > 0); --m_outstanding_jobs; }

	protected:

		void retain_job(hash_queue_entry& e);

	private:

		void process_job(hash_queue_entry const& e, bool post);

		// the number of async. hash jobs that have been issued
		// and not completed yet
		int m_outstanding_jobs;

		// used for posting completion notifications back
		// to the disk thread
		disk_io_thread* m_disk_thread;
	};

}

#endif // TORRENT_HASH_THREAD

