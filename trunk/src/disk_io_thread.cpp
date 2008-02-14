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

#ifdef _WIN32
#include <malloc.h>
#define alloca(s) _alloca(s)
#endif

#ifdef TORRENT_DISK_STATS
#include "libtorrent/time.hpp"
#endif

namespace libtorrent
{

	disk_io_thread::disk_io_thread(asio::io_service& ios, int block_size)
		: m_abort(false)
		, m_queue_buffer_size(0)
		, m_num_cached_blocks(0)
		, m_cache_size(512) // 512 * 16kB = 8MB
		, m_cache_expiry(60) // 1 minute
		, m_pool(block_size)
#ifndef NDEBUG
		, m_block_size(block_size)
#endif
		, m_writes(0)
		, m_blocks_written(0)
		, m_ios(ios)
		, m_disk_io_thread(boost::ref(*this))
	{
#ifdef TORRENT_STATS
		m_allocations = 0;
#endif
#ifdef TORRENT_DISK_STATS
		m_log.open("disk_io_thread.log", std::ios::trunc);
#endif
	}

	disk_io_thread::~disk_io_thread()
	{
		TORRENT_ASSERT(m_abort == true);
	}

#ifndef NDEBUG
	disk_io_job disk_io_thread::find_job(boost::intrusive_ptr<piece_manager> s
		, int action, int piece) const
	{
		mutex_t::scoped_lock l(m_mutex);
		for (std::deque<disk_io_job>::const_iterator i = m_jobs.begin();
			i != m_jobs.end(); ++i)
		{
			if (i->storage != s)
				continue;
			if ((i->action == action || action == -1) && i->piece == piece)
				return *i;
		}
		if ((m_current.action == action || action == -1)
			&& m_current.piece == piece)
			return m_current;

		disk_io_job ret;
		ret.action = (disk_io_job::action_t)-1;
		ret.piece = -1;
		return ret;
	}

#endif

	void disk_io_thread::join()
	{
		mutex_t::scoped_lock l(m_mutex);
		m_abort = true;
		m_signal.notify_all();
		l.unlock();

		m_disk_io_thread.join();
	}

	void disk_io_thread::get_cache_info(sha1_hash const& ih, std::vector<cached_piece_info>& ret) const
	{
		mutex_t::scoped_lock l(m_mutex);
		ret.clear();
		ret.reserve(m_pieces.size());
		for (std::vector<cached_piece_entry>::const_iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i)
		{
			torrent_info const& ti = *i->storage->info();
			if (ti.info_hash() != ih) continue;
			cached_piece_info info;
			info.piece = i->piece;
			info.last_write = i->last_write;
			int blocks_in_piece = (ti.piece_size(i->piece) + (16 * 1024) - 1) / (16 * 1024);
			info.blocks.resize(blocks_in_piece);
			for (int b = 0; b < blocks_in_piece; ++b)
				if (i->blocks[b]) info.blocks[b] = true;
			ret.push_back(info);
		}
	}
	
	cache_status disk_io_thread::status() const
	{
		mutex_t::scoped_lock l(m_mutex);
		cache_status st;
		st.blocks_written = m_blocks_written;
		st.writes = m_writes;
		st.write_size = m_num_cached_blocks;
		return st;
	}

	void disk_io_thread::set_cache_size(int s)
	{
		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(s >= 0);
		m_cache_size = s;
	}

	void disk_io_thread::set_cache_expiry(int ex)
	{
		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(ex > 0);
		m_cache_expiry = ex;
	}

	// aborts read operations
	void disk_io_thread::stop(boost::intrusive_ptr<piece_manager> s)
	{
		mutex_t::scoped_lock l(m_mutex);
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
				if (i->callback) m_ios.post(bind(i->callback, -1, *i));
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

	std::vector<disk_io_thread::cached_piece_entry>::iterator disk_io_thread::find_cached_piece(
		disk_io_job const& j, mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		for (std::vector<cached_piece_entry>::iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i)
		{
			if (i->storage != j.storage || i->piece != j.piece) continue;
			return i;
		}
		return m_pieces.end();
	}
	
	void disk_io_thread::flush_expired_pieces(mutex_t::scoped_lock& l)
	{
		ptime now = time_now();

		TORRENT_ASSERT(l.locked());
		for (;;)
		{
			std::vector<cached_piece_entry>::iterator i = std::min_element(
				m_pieces.begin(), m_pieces.end()
				, bind(&cached_piece_entry::last_write, _1)
				< bind(&cached_piece_entry::last_write, _2));
			if (i == m_pieces.end()) return;
			int age = total_seconds(now - i->last_write);
			if (age < m_cache_expiry) return;
			flush_and_remove(i, l);
		}
	}

	void disk_io_thread::flush_oldest_piece(mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		std::vector<cached_piece_entry>::iterator i = std::min_element(
			m_pieces.begin(), m_pieces.end()
			, bind(&cached_piece_entry::last_write, _1)
			< bind(&cached_piece_entry::last_write, _2));
		if (i == m_pieces.end()) return;
		flush_and_remove(i, l);
	}

	void disk_io_thread::flush_and_remove(std::vector<disk_io_thread::cached_piece_entry>::iterator e
		, mutex_t::scoped_lock& l)
	{
		flush(e, l);
		m_pieces.erase(e);
	}

	void disk_io_thread::flush(std::vector<disk_io_thread::cached_piece_entry>::iterator e
		, mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		cached_piece_entry& p = *e;
		int piece_size = p.storage->info()->piece_size(p.piece);
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " flushing " << piece_size << std::endl;
#endif
		TORRENT_ASSERT(piece_size > 0);
//		char* buf = (char*)alloca(piece_size);
		std::vector<char> temp(piece_size);
		char* buf = &temp[0];
		TORRENT_ASSERT(buf != 0);
		
		int blocks_in_piece = (piece_size + (16 * 1024) - 1) / (16 * 1024);
		int buffer_size = 0;
		int offset = 0;
		for (int i = 0; i <= blocks_in_piece; ++i)
		{
			if (i == blocks_in_piece || p.blocks[i] == 0)
			{
				if (buffer_size == 0) continue;
			
				TORRENT_ASSERT(buffer_size <= i * 16 * 1024);
				l.unlock();
				p.storage->write_impl(buf, p.piece, (std::min)(i * 16 * 1024, piece_size) - buffer_size, buffer_size);
				l.lock();
				++m_writes;
//				std::cerr << " flushing p: " << p.piece << " bytes: " << buffer_size << std::endl;
				buffer_size = 0;
				offset = 0;
				continue;
			}
			int block_size = (std::min)(piece_size - offset, 16 * 1024);
			TORRENT_ASSERT(offset + block_size <= piece_size);
			TORRENT_ASSERT(offset + block_size > 0);
			std::memcpy(buf + offset, p.blocks[i], block_size);
			offset += 16 * 1024;
			free_buffer(p.blocks[i], l);
			p.blocks[i] = 0;
			buffer_size += block_size;
			++m_blocks_written;
			--m_num_cached_blocks;
		}
		TORRENT_ASSERT(buffer_size == 0);
//		std::cerr << " flushing p: " << p.piece << " cached_blocks: " << m_num_cached_blocks << std::endl;
#ifndef NDEBUG
		for (int i = 0; i < blocks_in_piece; ++i)
			TORRENT_ASSERT(p.blocks[i] == 0);
#endif
	}

	void disk_io_thread::cache_block(disk_io_job& j, mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		TORRENT_ASSERT(find_cached_piece(j, l) == m_pieces.end());
		cached_piece_entry p;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + (16 * 1024) - 1) / (16 * 1024);

		p.piece = j.piece;
		p.storage = j.storage;
		p.last_write = time_now();
		p.num_blocks = 1;
		p.blocks.reset(new char*[blocks_in_piece]);
		std::memset(&p.blocks[0], 0, blocks_in_piece * sizeof(char*));
		int block = j.offset / (16 * 1024);
//		std::cerr << " adding cache entry for p: " << j.piece << " block: " << block << " cached_blocks: " << m_num_cached_blocks << std::endl;
		p.blocks[block] = j.buffer;
		++m_num_cached_blocks;
		m_pieces.push_back(p);
	}

	void disk_io_thread::add_job(disk_io_job const& j
		, boost::function<void(int, disk_io_job const&)> const& f)
	{
		TORRENT_ASSERT(!j.callback);
		TORRENT_ASSERT(j.storage);
		TORRENT_ASSERT(j.buffer_size <= 16 * 1024);
		mutex_t::scoped_lock l(m_mutex);
#ifndef NDEBUG
		if (j.action == disk_io_job::write)
		{
			std::vector<cached_piece_entry>::iterator p = find_cached_piece(j, l);
			if (p != m_pieces.end())
			{
				int block = j.offset / (16 * 1024);
				char const* buffer = p->blocks[block];
				TORRENT_ASSERT(buffer == 0);
			}
		}
#endif

		std::deque<disk_io_job>::reverse_iterator i = m_jobs.rbegin();
		if (j.action == disk_io_job::read)
		{
			// when we're reading, we may not skip
			// ahead of any write operation that overlaps
			// the region we're reading
			for (; i != m_jobs.rend(); i++)
			{
				// if *i should come before j, stop
				// and insert j before i
				if (*i < j) break;
				// if we come across a write operation that
				// overlaps the region we're reading, we need
				// to stop
				if (i->action == disk_io_job::write
					&& i->storage == j.storage
					&& i->piece == j.piece
					&& range_overlap(i->offset, i->buffer_size
						, j.offset, j.buffer_size))
					break;
			}
		}
		else if (j.action == disk_io_job::write)
		{
			for (; i != m_jobs.rend(); ++i)
			{
				if (*i < j)
				{
					if (i != m_jobs.rbegin()
						&& i.base()->storage.get() != j.storage.get())
						i = m_jobs.rbegin();
					break;
				}
			}
		}
		
		// if we are placed in front of all other jobs, put it on the back of
		// the queue, to sweep the disk in the same direction, and to avoid
		// starvation. The exception is if the priority is higher than the
		// job at the front of the queue
		if (i == m_jobs.rend() && (m_jobs.empty() || j.priority <= m_jobs.back().priority))
			i = m_jobs.rbegin();

		std::deque<disk_io_job>::iterator k = m_jobs.insert(i.base(), j);
		k->callback.swap(const_cast<boost::function<void(int, disk_io_job const&)>&>(f));
		if (j.action == disk_io_job::write)
			m_queue_buffer_size += j.buffer_size;
		TORRENT_ASSERT(j.storage.get());
		m_signal.notify_all();
	}

	char* disk_io_thread::allocate_buffer()
	{
		mutex_t::scoped_lock l(m_mutex);
		return allocate_buffer(l);
	}

	void disk_io_thread::free_buffer(char* buf)
	{
		mutex_t::scoped_lock l(m_mutex);
		free_buffer(buf, l);
	}

	char* disk_io_thread::allocate_buffer(mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
#ifdef TORRENT_STATS
		++m_allocations;
#endif
		return (char*)m_pool.ordered_malloc();
	}

	void disk_io_thread::free_buffer(char* buf, mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
#ifdef TORRENT_STATS
		--m_allocations;
#endif
		m_pool.ordered_free(buf);
	}

	void disk_io_thread::operator()()
	{
		for (;;)
		{
#ifdef TORRENT_DISK_STATS
			m_log << log_time() << " idle" << std::endl;
#endif
			mutex_t::scoped_lock l(m_mutex);
#ifndef NDEBUG
			m_current.action = (disk_io_job::action_t)-1;
			m_current.piece = -1;
#endif
			while (m_jobs.empty() && !m_abort)
				m_signal.wait(l);
			if (m_abort && m_jobs.empty()) return;

			boost::function<void(int, disk_io_job const&)> handler;
			handler.swap(m_jobs.front().callback);
#ifndef NDEBUG
			m_current = m_jobs.front();
#endif
			disk_io_job j = m_jobs.front();
			m_jobs.pop_front();
			m_queue_buffer_size -= j.buffer_size;

			flush_expired_pieces(l);
			l.unlock();

			int ret = 0;

			bool free_current_buffer = true;
			TORRENT_ASSERT(j.storage);
#ifdef TORRENT_DISK_STATS
			ptime start = time_now();
#endif
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
			std::string const& error_string = j.storage->error();
			if (!error_string.empty())
			{
				std::cout << "ERROR: " << error_string << std::endl;
				j.str = error_string;
				j.storage->clear_error();
				ret = -1;
			}
			else
			{
				switch (j.action)
				{
					case disk_io_job::read:
					{
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " read " << j.buffer_size << std::endl;
#endif
						free_current_buffer = false;
						if (j.buffer == 0)
						{
							j.buffer = allocate_buffer();
							TORRENT_ASSERT(j.buffer_size <= m_block_size);
							if (j.buffer == 0)
							{
								ret = -1;
								j.str = "out of memory";
								break;
							}
						}
						ret = j.storage->read_impl(j.buffer, j.piece, j.offset
							, j.buffer_size);
						if (ret < 0)
						{
							j.str = j.storage->error();
							j.storage->clear_error();
						}
						break;
					}
					case disk_io_job::write:
					{
						mutex_t::scoped_lock l(m_mutex);
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " write " << j.buffer_size << std::endl;
#endif
						std::vector<cached_piece_entry>::iterator p = find_cached_piece(j, l);
						int block = j.offset / (16 * 1024);
						TORRENT_ASSERT(j.buffer);
						TORRENT_ASSERT(j.buffer_size <= m_block_size);
						if (p != m_pieces.end())
						{
							TORRENT_ASSERT(p->blocks[block] == 0);
							if (p->blocks[block]) free_buffer(p->blocks[block]);
							p->blocks[block] = j.buffer;
							++m_num_cached_blocks;
							++p->num_blocks;
							p->last_write = time_now();
						}
						else
						{
							cache_block(j, l);
						}
						free_current_buffer = false;
						if (m_num_cached_blocks >= m_cache_size)
							flush_oldest_piece(l);
						break;
					}
					case disk_io_job::hash:
					{
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " hash" << std::endl;
#endif
						mutex_t::scoped_lock l(m_mutex);
						std::vector<cached_piece_entry>::iterator i = find_cached_piece(j, l);
						if (i != m_pieces.end()) flush_and_remove(i, l);
						l.unlock();
						sha1_hash h = j.storage->hash_for_piece_impl(j.piece);
						std::string const& e = j.storage->error();
						if (!e.empty())
						{
							j.str = e;
							ret = -1;
							j.storage->clear_error();
							break;
						}
						j.str.resize(20);
						std::memcpy(&j.str[0], &h[0], 20);
						break;
					}
					case disk_io_job::move_storage:
					{
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " move" << std::endl;
#endif
						ret = j.storage->move_storage_impl(j.str) ? 1 : 0;
						if (ret != 0)
						{
							j.str = j.storage->error();
							j.storage->clear_error();
							break;
						}
						j.str = j.storage->save_path().string();
						break;
					}
					case disk_io_job::release_files:
					{
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " release" << std::endl;
#endif
						mutex_t::scoped_lock l(m_mutex);

						std::vector<cached_piece_entry>::iterator i = std::remove_if(
							m_pieces.begin(), m_pieces.end(), bind(&cached_piece_entry::storage, _1) == j.storage);

						for (std::vector<cached_piece_entry>::iterator k = i; k != m_pieces.end(); ++k)
							flush(k, l);
						m_pieces.erase(i, m_pieces.end());
						m_pool.release_memory();
						l.unlock();
						ret = j.storage->release_files_impl();
						if (ret != 0)
						{
							j.str = j.storage->error();
							j.storage->clear_error();
						}
						break;
					}
					case disk_io_job::delete_files:
					{
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " delete" << std::endl;
#endif
						mutex_t::scoped_lock l(m_mutex);

						std::vector<cached_piece_entry>::iterator i = std::remove_if(
							m_pieces.begin(), m_pieces.end(), bind(&cached_piece_entry::storage, _1) == j.storage);

						for (std::vector<cached_piece_entry>::iterator k = i; k != m_pieces.end(); ++k)
						{
							torrent_info const& ti = *k->storage->info();
							int blocks_in_piece = (ti.piece_size(k->piece) + (16 * 1024) - 1) / (16 * 1024);
							for (int j = 0; j < blocks_in_piece; ++j)
							{
								if (k->blocks[j] == 0) continue;
								free_buffer(k->blocks[j], l);
								k->blocks[j] = 0;
							}
						}
						m_pieces.erase(i, m_pieces.end());
						m_pool.release_memory();
						l.unlock();
						ret = j.storage->delete_files_impl();
						if (ret != 0)
						{
							j.str = j.storage->error();
							j.storage->clear_error();
						}
						break;
					}
				}
			}
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception& e)
			{
				ret = -1;
				try
				{
					j.str = e.what();
				}
				catch (std::exception&) {}
			}
#endif

//			if (!handler) std::cerr << "DISK THREAD: no callback specified" << std::endl;
//			else std::cerr << "DISK THREAD: invoking callback" << std::endl;
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				if (handler) m_ios.post(bind(handler, ret, j));
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif


#ifndef NDEBUG
			m_current.storage = 0;
			m_current.callback.clear();
#endif
			
			if (j.buffer && free_current_buffer) free_buffer(j.buffer);
		}
		TORRENT_ASSERT(false);
	}
}

