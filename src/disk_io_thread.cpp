/*

Copyright (c) 2007, Arvid Norberg
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

#include "libtorrent/storage.hpp"
#include <deque>
#include "libtorrent/disk_io_thread.hpp"

#ifdef TORRENT_DISK_STATS

#include "libtorrent/time.hpp"
#include <boost/lexical_cast.hpp>

namespace
{
	std::string log_time()
	{
		using namespace libtorrent;
		static ptime start = time_now();
		return boost::lexical_cast<std::string>(
			total_milliseconds(time_now() - start));
	}
}

#endif

namespace libtorrent
{

	disk_io_thread::disk_io_thread(int block_size)
		: m_abort(false)
		, m_queue_buffer_size(0)
		, m_pool(block_size)
#ifndef NDEBUG
		, m_block_size(block_size)
#endif
		, m_disk_io_thread(boost::ref(*this))
	{
	
#ifdef TORRENT_DISK_STATS
		m_log.open("disk_io_thread.log", std::ios::trunc);
#endif
	}

	disk_io_thread::~disk_io_thread()
	{
		boost::mutex::scoped_lock l(m_mutex);
		m_abort = true;
		m_signal.notify_all();
		l.unlock();

		m_disk_io_thread.join();
	}

	// aborts read operations
	void disk_io_thread::stop(boost::intrusive_ptr<piece_manager> s)
	{
		boost::mutex::scoped_lock l(m_mutex);
		// read jobs are aborted, write and move jobs are syncronized
		for (std::deque<disk_io_job>::iterator i = m_jobs.begin();
			i != m_jobs.end();)
		{
			if (i->storage != s)
			{
				++i;
				continue;
			}
			if (i->action == disk_io_job::read)
			{
				i->callback(-1, *i);
				m_jobs.erase(i++);
				continue;
			}
			++i;
		}
		m_signal.notify_all();
	}

	bool range_overlap(int start1, int length1, int start2, int length2)
	{
		return (start1 <= start2 && start1 + length1 > start2)
			|| (start2 <= start1 && start2 + length2 > start1);
	}
	
	namespace
	{
		// The semantic of this operator is:
		// shouls lhs come before rhs in the job queue
		bool operator<(disk_io_job const& lhs, disk_io_job const& rhs)
		{
			// NOTE: comparison inverted to make higher priority
			// skip _in_front_of_ lower priority
			if (lhs.priority > rhs.priority) return true;
			if (lhs.priority < rhs.priority) return false;

			if (lhs.storage.get() < rhs.storage.get()) return true;
			if (lhs.storage.get() > rhs.storage.get()) return false;
			if (lhs.piece < rhs.piece) return true;
			if (lhs.piece > rhs.piece) return false;
			if (lhs.offset < rhs.offset) return true;
//			if (lhs.offset > rhs.offset) return false;
			return false;
		}
	}
	
	void disk_io_thread::add_job(disk_io_job const& j
		, boost::function<void(int, disk_io_job const&)> const& f)
	{
		assert(!j.callback);
		boost::mutex::scoped_lock l(m_mutex);
		
		std::deque<disk_io_job>::reverse_iterator i = m_jobs.rbegin();
		if (j.action == disk_io_job::read)
		{
			// when we're reading, we may not skip
			// ahead of any write operation that overlaps
			// the region we're reading
			for (; i != m_jobs.rend(); ++i)
			{
				if (i->action == disk_io_job::read && *i < j)
					break;
				if (i->action == disk_io_job::write
					&& i->storage == j.storage
					&& i->piece == j.piece
					&& range_overlap(i->offset, i->buffer_size
						, j.offset, j.buffer_size))
				{
					// we have to stop, and we haven't
					// found a suitable place for this job
					// so just queue it up at the end
					i = m_jobs.rbegin();
					break;
				}
			}
		}
		else if (j.action == disk_io_job::write)
		{
			for (; i != m_jobs.rend(); ++i)
			{
				if (i->action == disk_io_job::write && *i < j)
				{
					if (i != m_jobs.rbegin()
						&& i.base()->storage.get() != j.storage.get())
						i = m_jobs.rbegin();
					break;
				}
			}
		}
		
		if (i == m_jobs.rend()) i = m_jobs.rbegin();

		std::deque<disk_io_job>::iterator k = m_jobs.insert(i.base(), j);
		k->callback.swap(const_cast<boost::function<void(int, disk_io_job const&)>&>(f));
		if (j.action == disk_io_job::write)
			m_queue_buffer_size += j.buffer_size;
		assert(j.storage.get());
		m_signal.notify_all();
	}

	char* disk_io_thread::allocate_buffer()
	{
		boost::mutex::scoped_lock l(m_mutex);
		return (char*)m_pool.ordered_malloc();
	}

	void disk_io_thread::operator()()
	{
		for (;;)
		{
#ifdef TORRENT_DISK_STATS
			m_log << log_time() << " idle" << std::endl;
#endif
			boost::mutex::scoped_lock l(m_mutex);
			while (m_jobs.empty() && !m_abort)
				m_signal.wait(l);
			if (m_abort && m_jobs.empty()) return;

			boost::function<void(int, disk_io_job const&)> handler;
			handler.swap(m_jobs.front().callback);
			disk_io_job j = m_jobs.front();
			m_jobs.pop_front();
			m_queue_buffer_size -= j.buffer_size;
			l.unlock();

			int ret = 0;

			bool free_buffer = true;
			try
			{
#ifdef TORRENT_DISK_STATS
				ptime start = time_now();
#endif
//				std::cerr << "DISK THREAD: executing job: " << j.action << std::endl;
				switch (j.action)
				{
					case disk_io_job::read:
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " read " << j.buffer_size << std::endl;
#endif
						if (j.buffer == 0)
						{
							l.lock();
							j.buffer = (char*)m_pool.ordered_malloc();
							l.unlock();
							assert(j.buffer_size <= m_block_size);
							if (j.buffer == 0)
							{
								ret = -1;
								j.str = "out of memory";
								break;
							}
						}
						else
						{
							free_buffer = false;
						}
						ret = j.storage->read_impl(j.buffer, j.piece, j.offset
							, j.buffer_size);

						// simulates slow drives
						// usleep(300);
						break;
					case disk_io_job::write:
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " write " << j.buffer_size << std::endl;
#endif
						assert(j.buffer);
						assert(j.buffer_size <= m_block_size);
						j.storage->write_impl(j.buffer, j.piece, j.offset
							, j.buffer_size);
						
						// simulates a slow drive
						// usleep(300);
						break;
					case disk_io_job::hash:
						{
#ifdef TORRENT_DISK_STATS
							m_log << log_time() << " hash" << std::endl;
#endif
							sha1_hash h = j.storage->hash_for_piece_impl(j.piece);
							j.str.resize(20);
							std::memcpy(&j.str[0], &h[0], 20);
						}
						break;
					case disk_io_job::move_storage:
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " move" << std::endl;
#endif
						ret = j.storage->move_storage_impl(j.str) ? 1 : 0;
						j.str = j.storage->save_path().string();
						break;
					case disk_io_job::release_files:
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " release" << std::endl;
#endif
						j.storage->release_files_impl();
						break;
				}
			}
			catch (std::exception& e)
			{
//				std::cerr << "DISK THREAD: exception: " << e.what() << std::endl;
				j.str = e.what();
				ret = -1;
			}

//			if (!handler) std::cerr << "DISK THREAD: no callback specified" << std::endl;
//			else std::cerr << "DISK THREAD: invoking callback" << std::endl;
			try { if (handler) handler(ret, j); }
			catch (std::exception&) {}
			
			if (j.buffer && free_buffer)
			{
				l.lock();
				m_pool.ordered_free(j.buffer);
			}
		}
	}
}

