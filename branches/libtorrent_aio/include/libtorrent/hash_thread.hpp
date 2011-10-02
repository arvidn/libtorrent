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
#include "libtorrent/thread.hpp"
#include <deque>
#include <vector>
#include <boost/detail/atomic_count.hpp>

namespace libtorrent
{
	struct cached_piece_entry;
	struct disk_io_thread;

	struct hash_thread
	{
		hash_thread(disk_io_thread* d);
		void stop();
		bool async_hash(cached_piece_entry* p, int start, int end);
		void set_num_threads(int i, bool wait = true);

		int num_pending_jobs() const { return m_outstanding_jobs; }
		void hash_job_done() { TORRENT_ASSERT(m_outstanding_jobs > 0); --m_outstanding_jobs; }

	private:

		void thread_fun(int thread_id);

		struct hash_queue_entry
		{
			cached_piece_entry const* piece;
			int start;
			int end;
		};

		void process_piece(hash_queue_entry const& e);

		// the mutex only protects m_cond and m_queue
		// all other members are only used from a single
		// thread (the user of this class, i.e. the disk
		// thread).
		mutex m_mutex;
		condition m_cond;
		std::deque<hash_queue_entry> m_queue;

		// the number of async. hash jobs that have been issued
		// and not completed yet
		int m_outstanding_jobs;

		std::vector<boost::shared_ptr<thread> > m_threads;
		// this is a counter which is atomically incremented
		// by each thread as it's started up, in order to
		// assign a unique id to each thread
		boost::detail::atomic_count m_num_threads;

		// used for posting completion notifications back
		// to the disk thread
		disk_io_thread* m_disk_thread;
	};

}

#endif // TORRENT_HASH_THREAD

