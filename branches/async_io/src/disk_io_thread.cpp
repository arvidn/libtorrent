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

namespace libtorrent
{

	disk_io_thread::disk_io_thread(int block_size)
		: m_abort(false)
		, m_queue_buffer_size(0)
		, m_pool(block_size)
		, m_disk_io_thread(boost::ref(*this))
	{}

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

	void disk_io_thread::add_job(disk_io_job const& j)
	{
		boost::mutex::scoped_lock l(m_mutex);
		m_jobs.push_back(j);
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
			boost::mutex::scoped_lock l(m_mutex);
			while (m_jobs.empty() && !m_abort)
				m_signal.wait(l);
			if (m_abort && m_jobs.empty()) return;

			disk_io_job j = m_jobs.front();
			m_jobs.pop_front();
			m_queue_buffer_size -= j.buffer_size;
			l.unlock();

			int ret = 0;

			try
			{
//				std::cerr << "DISK THREAD: executing job: " << j.action << std::endl;
				switch (j.action)
				{
					case disk_io_job::read:
						l.lock();
						j.buffer = (char*)m_pool.ordered_malloc();
						l.unlock();
						if (j.buffer == 0)
						{
							ret = -1;
							j.str = "out of memory";
						}
						else
						{
							ret = j.storage->read_impl(j.buffer, j.piece, j.offset
								, j.buffer_size);

							// simulates slow drives
							// usleep(300);
						}
						break;
					case disk_io_job::write:
						assert(j.buffer);
						j.storage->write_impl(j.buffer, j.piece, j.offset
							, j.buffer_size);
						
						// simulates a slow drive
						// usleep(300);
						break;
					case disk_io_job::hash:
						{
							sha1_hash h = j.storage->hash_for_piece_impl(j.piece);
							j.str.resize(20);
							std::copy(h.begin(), h.end(), j.str.begin());
						}
						break;
					case disk_io_job::move_storage:
						ret = j.storage->move_storage_impl(j.str) ? 1 : 0;
						break;
					case disk_io_job::release_files:
						j.storage->release_files_impl();
						break;
				}
			}
			catch (std::exception& e)
			{
				std::cerr << "DISK THREAD: exception: " << e.what() << std::endl;
				j.str = e.what();
				ret = -1;
			}

//			if (!j.callback) std::cerr << "DISK THREAD: no callback specified" << std::endl;
//			else std::cerr << "DISK THREAD: invoking callback" << std::endl;
			try { if (j.callback) j.callback(ret, j); }
			catch (std::exception&) {}
			
			if (j.buffer) m_pool.ordered_free(j.buffer);
		}
	}
}

