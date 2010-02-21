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
#include "libtorrent/disk_buffer_holder.hpp"
#include <boost/scoped_array.hpp>

#ifdef _WIN32
#include <malloc.h>
#ifndef alloca
#define alloca(s) _alloca(s)
#endif
#endif

#ifdef TORRENT_DISK_STATS
#include "libtorrent/time.hpp"
#endif

namespace libtorrent
{

	disk_io_thread::disk_io_thread(asio::io_service& ios, int block_size)
		: m_abort(false)
		, m_queue_buffer_size(0)
		, m_cache_size(512) // 512 * 16kB = 8MB
		, m_cache_expiry(60) // 1 minute
		, m_coalesce_writes(true)
		, m_coalesce_reads(true)
		, m_use_read_cache(true)
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		, m_pool(block_size, 16)
#endif
		, m_block_size(block_size)
		, m_ios(ios)
		, m_work(io_service::work(m_ios))
		, m_disk_io_thread(boost::ref(*this))
	{
#ifdef TORRENT_STATS
		m_allocations = 0;
#endif
#ifdef TORRENT_DISK_STATS
		m_log.open("disk_io_thread.log", std::ios::trunc);
#endif
#ifdef TORRENT_DEBUG
	m_magic = 0x1337;
#endif
	}

	disk_io_thread::~disk_io_thread()
	{
		TORRENT_ASSERT(m_abort == true);
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_DEBUG
		m_magic = 0;
#endif
	}

	void disk_io_thread::join()
	{
		mutex_t::scoped_lock l(m_queue_mutex);
		disk_io_job j;
		j.action = disk_io_job::abort_thread;
		m_jobs.insert(m_jobs.begin(), j);
		m_signal.notify_all();
		l.unlock();

		m_disk_io_thread.join();
		l.lock();
		TORRENT_ASSERT(m_abort == true);
		m_jobs.clear();
	}

	void disk_io_thread::get_cache_info(sha1_hash const& ih, std::vector<cached_piece_info>& ret) const
	{
		mutex_t::scoped_lock l(m_piece_mutex);
		ret.clear();
		ret.reserve(m_pieces.size());
		for (cache_t::const_iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i)
		{
			torrent_info const& ti = *i->storage->info();
			if (ti.info_hash() != ih) continue;
			cached_piece_info info;
			info.piece = i->piece;
			info.last_use = i->last_use;
			info.kind = cached_piece_info::write_cache;
			int blocks_in_piece = (ti.piece_size(i->piece) + (m_block_size) - 1) / m_block_size;
			info.blocks.resize(blocks_in_piece);
			for (int b = 0; b < blocks_in_piece; ++b)
				if (i->blocks[b]) info.blocks[b] = true;
			ret.push_back(info);
		}
		for (cache_t::const_iterator i = m_read_pieces.begin()
			, end(m_read_pieces.end()); i != end; ++i)
		{
			torrent_info const& ti = *i->storage->info();
			if (ti.info_hash() != ih) continue;
			cached_piece_info info;
			info.piece = i->piece;
			info.last_use = i->last_use;
			info.kind = cached_piece_info::read_cache;
			int blocks_in_piece = (ti.piece_size(i->piece) + (m_block_size) - 1) / m_block_size;
			info.blocks.resize(blocks_in_piece);
			for (int b = 0; b < blocks_in_piece; ++b)
				if (i->blocks[b]) info.blocks[b] = true;
			ret.push_back(info);
		}
	}
	
	cache_status disk_io_thread::status() const
	{
		mutex_t::scoped_lock l(m_piece_mutex);
		return m_cache_stats;
	}

	void disk_io_thread::set_cache_size(int s)
	{
		mutex_t::scoped_lock l(m_piece_mutex);
		TORRENT_ASSERT(s >= 0);
		m_cache_size = s;
	}

	void disk_io_thread::set_cache_expiry(int ex)
	{
		mutex_t::scoped_lock l(m_piece_mutex);
		TORRENT_ASSERT(ex > 0);
		m_cache_expiry = ex;
	}

	// aborts read operations
	void disk_io_thread::stop(boost::intrusive_ptr<piece_manager> s)
	{
		mutex_t::scoped_lock l(m_queue_mutex);
		// read jobs are aborted, write and move jobs are syncronized
		for (std::list<disk_io_job>::iterator i = m_jobs.begin();
			i != m_jobs.end();)
		{
			if (i->storage != s)
			{
				++i;
				continue;
			}
			if (i->action == disk_io_job::read)
			{
				if (i->callback) m_ios.post(boost::bind(i->callback, -1, *i));
				m_jobs.erase(i++);
				continue;
			}
			if (i->action == disk_io_job::check_files)
			{
				if (i->callback) m_ios.post(boost::bind(i->callback
					, piece_manager::disk_check_aborted, *i));
				m_jobs.erase(i++);
				continue;
			}
			++i;
		}
		disk_io_job j;
		j.action = disk_io_job::abort_torrent;
		j.storage = s;
		add_job(j);
	}

	bool range_overlap(int start1, int length1, int start2, int length2)
	{
		return (start1 <= start2 && start1 + length1 > start2)
			|| (start2 <= start1 && start2 + length2 > start1);
	}
	
	namespace
	{
		// The semantic of this operator is:
		// should lhs come before rhs in the job queue
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

	disk_io_thread::cache_t::iterator disk_io_thread::find_cached_piece(
		disk_io_thread::cache_t& cache
		, disk_io_job const& j, mutex_t::scoped_lock& l)
	{
		for (cache_t::iterator i = cache.begin()
			, end(cache.end()); i != end; ++i)
		{
			if (i->storage != j.storage || i->piece != j.piece) continue;
			return i;
		}
		return cache.end();
	}
	
	void disk_io_thread::flush_expired_pieces()
	{
		ptime now = time_now();

		mutex_t::scoped_lock l(m_piece_mutex);

		INVARIANT_CHECK;
		for (;;)
		{
			cache_t::iterator i = std::min_element(
				m_pieces.begin(), m_pieces.end()
				, bind(&cached_piece_entry::last_use, _1)
				< bind(&cached_piece_entry::last_use, _2));
			if (i == m_pieces.end()) return;
			int age = total_seconds(now - i->last_use);
			if (age < m_cache_expiry) return;
			flush_and_remove(i, l);
		}
	}

	void disk_io_thread::free_piece(cached_piece_entry& p, mutex_t::scoped_lock& l)
	{
		int piece_size = p.storage->info()->piece_size(p.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		for (int i = 0; i < blocks_in_piece; ++i)
		{
			if (p.blocks[i] == 0) continue;
			free_buffer(p.blocks[i]);
			p.blocks[i] = 0;
			--p.num_blocks;
			--m_cache_stats.cache_size;
			--m_cache_stats.read_cache_size;
		}
	}

	bool disk_io_thread::clear_oldest_read_piece(
		cache_t::iterator ignore
		, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;

		cache_t::iterator i = std::min_element(
			m_read_pieces.begin(), m_read_pieces.end()
			, bind(&cached_piece_entry::last_use, _1)
			< bind(&cached_piece_entry::last_use, _2));
		if (i != m_read_pieces.end() && i != ignore)
		{
			// don't replace an entry that is less than one second old
			if (time_now() - i->last_use < seconds(1)) return false;
			free_piece(*i, l);
			m_read_pieces.erase(i);
			return true;
		}
		return false;
	}

	void disk_io_thread::flush_oldest_piece(mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;
		// first look if there are any read cache entries that can
		// be cleared
		if (clear_oldest_read_piece(m_read_pieces.end(), l)) return;

		cache_t::iterator i = std::min_element(
			m_pieces.begin(), m_pieces.end()
			, bind(&cached_piece_entry::last_use, _1)
			< bind(&cached_piece_entry::last_use, _2));
		if (i == m_pieces.end()) return;
		flush_and_remove(i, l);
	}

	void disk_io_thread::flush_and_remove(disk_io_thread::cache_t::iterator e
		, mutex_t::scoped_lock& l)
	{
		flush(e, l);
		m_pieces.erase(e);
	}

	void disk_io_thread::flush(disk_io_thread::cache_t::iterator e
		, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;
		cached_piece_entry& p = *e;
		int piece_size = p.storage->info()->piece_size(p.piece);
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " flushing " << piece_size << std::endl;
#endif
		TORRENT_ASSERT(piece_size > 0);
		boost::scoped_array<char> buf;
		if (m_coalesce_writes) buf.reset(new (std::nothrow) char[piece_size]);
		
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		int buffer_size = 0;
		int offset = 0;
		for (int i = 0; i <= blocks_in_piece; ++i)
		{
			if (i == blocks_in_piece || p.blocks[i] == 0)
			{
				if (buffer_size == 0) continue;
				TORRENT_ASSERT(buf);
			
				TORRENT_ASSERT(buffer_size <= i * m_block_size);
				l.unlock();
				p.storage->write_impl(buf.get(), p.piece, (std::min)(
					i * m_block_size, piece_size) - buffer_size, buffer_size);
				l.lock();
				++m_cache_stats.writes;
//				std::cerr << " flushing p: " << p.piece << " bytes: " << buffer_size << std::endl;
				buffer_size = 0;
				offset = 0;
				continue;
			}
			int block_size = (std::min)(piece_size - i * m_block_size, m_block_size);
			TORRENT_ASSERT(offset + block_size <= piece_size);
			TORRENT_ASSERT(offset + block_size > 0);
			if (!buf)
			{
				l.unlock();
				p.storage->write_impl(p.blocks[i], p.piece, i * m_block_size, block_size);
				l.lock();
				++m_cache_stats.writes;
			}
			else
			{
				std::memcpy(buf.get() + offset, p.blocks[i], block_size);
				offset += m_block_size;
				buffer_size += block_size;
			}
			free_buffer(p.blocks[i]);
			p.blocks[i] = 0;
			TORRENT_ASSERT(p.num_blocks > 0);
			--p.num_blocks;
			++m_cache_stats.blocks_written;
			--m_cache_stats.cache_size;
		}
		TORRENT_ASSERT(buffer_size == 0);
//		std::cerr << " flushing p: " << p.piece << " cached_blocks: " << m_cache_stats.cache_size << std::endl;
#ifdef TORRENT_DEBUG
		for (int i = 0; i < blocks_in_piece; ++i)
			TORRENT_ASSERT(p.blocks[i] == 0);
#endif
	}

	// returns -1 on failure
	int disk_io_thread::cache_block(disk_io_job& j, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(find_cached_piece(m_pieces, j, l) == m_pieces.end());
		TORRENT_ASSERT((j.offset & (m_block_size-1)) == 0);
		cached_piece_entry p;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		p.piece = j.piece;
		p.storage = j.storage;
		p.last_use = time_now();
		p.num_blocks = 1;
		p.blocks.reset(new (std::nothrow) char*[blocks_in_piece]);
		if (!p.blocks) return -1;
		std::memset(&p.blocks[0], 0, blocks_in_piece * sizeof(char*));
		int block = j.offset / m_block_size;
//		std::cerr << " adding cache entry for p: " << j.piece << " block: " << block << " cached_blocks: " << m_cache_stats.cache_size << std::endl;
		p.blocks[block] = j.buffer;
		++m_cache_stats.cache_size;
		m_pieces.push_back(p);
		return 0;
	}

	// fills a piece with data from disk, returns the total number of bytes
	// read or -1 if there was an error
	int disk_io_thread::read_into_piece(cached_piece_entry& p, int start_block, mutex_t::scoped_lock& l)
	{
		int piece_size = p.storage->info()->piece_size(p.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		int end_block = start_block;
		for (int i = start_block; i < blocks_in_piece
			&& m_cache_stats.cache_size < m_cache_size; ++i)
		{
			// this is a block that is already allocated
			// stop allocating and don't read more than
			// what we've allocated now
			if (p.blocks[i]) break;
			p.blocks[i] = allocate_buffer();

			// the allocation failed, break
			if (p.blocks[i] == 0) break;
			++p.num_blocks;
			++m_cache_stats.cache_size;
			++m_cache_stats.read_cache_size;
			++end_block;
		}

		if (end_block == start_block) return -2;

		// the buffer_size is the size of the buffer we need to read
		// all these blocks.
		const int buffer_size = (std::min)((end_block - start_block) * m_block_size
			, piece_size - start_block * m_block_size);
		TORRENT_ASSERT(buffer_size <= piece_size);
		TORRENT_ASSERT(buffer_size + start_block * m_block_size <= piece_size);
		boost::scoped_array<char> buf;
		if (m_coalesce_reads) buf.reset(new (std::nothrow) char[buffer_size]);
		int ret = 0;
		if (buf)
		{
			l.unlock();
			ret += p.storage->read_impl(buf.get(), p.piece, start_block * m_block_size, buffer_size);
			l.lock();
			if (p.storage->error()) { return -1; }
			++m_cache_stats.reads;
		}
		
		int piece_offset = start_block * m_block_size;
		int offset = 0;
		for (int i = start_block; i < end_block; ++i)
		{
			int block_size = (std::min)(piece_size - piece_offset, m_block_size);
			if (p.blocks[i] == 0) break;
			TORRENT_ASSERT(offset <= buffer_size);
			TORRENT_ASSERT(piece_offset <= piece_size);
			TORRENT_ASSERT(offset + block_size <= buffer_size);
			if (buf)
			{
				std::memcpy(p.blocks[i], buf.get() + offset, block_size);
			}
			else
			{
				l.unlock();
				ret += p.storage->read_impl(p.blocks[i], p.piece, piece_offset, block_size);
				if (p.storage->error()) { return -1; }
				l.lock();
				++m_cache_stats.reads;
			}
			offset += m_block_size;
			piece_offset += m_block_size;
		}
		TORRENT_ASSERT(ret <= buffer_size);
		return (ret != buffer_size) ? -1 : ret;
	}
	
	bool disk_io_thread::make_room(int num_blocks
		, cache_t::iterator ignore
		, mutex_t::scoped_lock& l)
	{
		if (m_cache_size - m_cache_stats.cache_size < num_blocks)
		{
			// there's not enough room in the cache, clear a piece
			// from the read cache
			if (!clear_oldest_read_piece(ignore, l)) return false;
		}

		return m_cache_size - m_cache_stats.cache_size >= num_blocks;
	}

	// returns -1 on read error, -2 if there isn't any space in the cache
	// or the number of bytes read
	int disk_io_thread::cache_read_block(disk_io_job const& j, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		int start_block = j.offset / m_block_size;

		if (!make_room(blocks_in_piece - start_block
			, m_read_pieces.end(), l)) return -2;

		cached_piece_entry p;
		p.piece = j.piece;
		p.storage = j.storage;
		p.last_use = time_now();
		p.num_blocks = 0;
		p.blocks.reset(new (std::nothrow) char*[blocks_in_piece]);
		if (!p.blocks) return -1;
		std::memset(&p.blocks[0], 0, blocks_in_piece * sizeof(char*));
		int ret = read_into_piece(p, start_block, l);
		
		if (ret < 0)
			free_piece(p, l);
		else
			m_read_pieces.push_back(p);

		return ret;
	}

#ifdef TORRENT_DEBUG
	void disk_io_thread::check_invariant() const
	{
		int cached_write_blocks = 0;
		for (cache_t::const_iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i)
		{
			cached_piece_entry const& p = *i;
			TORRENT_ASSERT(p.blocks);
			
			if (!p.storage) continue;
			int piece_size = p.storage->info()->piece_size(p.piece);
			int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
			int blocks = 0;
			for (int k = 0; k < blocks_in_piece; ++k)
			{
				if (p.blocks[k])
				{
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					TORRENT_ASSERT(is_disk_buffer(p.blocks[k]));
#endif
					++blocks;
				}
			}
//			TORRENT_ASSERT(blocks == p.num_blocks);
			cached_write_blocks += blocks;
		}
	
		int cached_read_blocks = 0;
		for (cache_t::const_iterator i = m_read_pieces.begin()
			, end(m_read_pieces.end()); i != end; ++i)
		{
			cached_piece_entry const& p = *i;
			TORRENT_ASSERT(p.blocks);
			
			int piece_size = p.storage->info()->piece_size(p.piece);
			int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
			int blocks = 0;
			for (int k = 0; k < blocks_in_piece; ++k)
			{
				if (p.blocks[k])
				{
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					TORRENT_ASSERT(is_disk_buffer(p.blocks[k]));
#endif
					++blocks;
				}
			}
//			TORRENT_ASSERT(blocks == p.num_blocks);
			cached_read_blocks += blocks;
		}

		TORRENT_ASSERT(cached_read_blocks + cached_write_blocks == m_cache_stats.cache_size);
		TORRENT_ASSERT(cached_read_blocks == m_cache_stats.read_cache_size);

		// when writing, there may be a one block difference, right before an old piece
		// is flushed
		TORRENT_ASSERT(m_cache_stats.cache_size <= m_cache_size + 1);
	}
#endif

	int disk_io_thread::try_read_from_cache(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.buffer);

		mutex_t::scoped_lock l(m_piece_mutex);
		if (!m_use_read_cache) return -2;

		cache_t::iterator p
			= find_cached_piece(m_read_pieces, j, l);

		bool hit = true;
		int ret = 0;

		// if the piece cannot be found in the cache,
		// read the whole piece starting at the block
		// we got a request for.
		if (p == m_read_pieces.end())
		{
			ret = cache_read_block(j, l);
			hit = false;
			if (ret < 0) return ret;
			p = m_read_pieces.end();
			--p;
			TORRENT_ASSERT(!m_read_pieces.empty());
			TORRENT_ASSERT(p->piece == j.piece);
			TORRENT_ASSERT(p->storage == j.storage);
		}

		if (p != m_read_pieces.end())
		{
			// copy from the cache and update the last use timestamp
			int block = j.offset / m_block_size;
			int block_offset = j.offset % m_block_size;
			int buffer_offset = 0;
			int size = j.buffer_size;
			if (p->blocks[block] == 0)
			{
				int piece_size = j.storage->info()->piece_size(j.piece);
				int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
				int end_block = block;
				while (end_block < blocks_in_piece && p->blocks[end_block] == 0) ++end_block;
				if (!make_room(end_block - block, p, l)) return -2;
				ret = read_into_piece(*p, block, l);
				hit = false;
				if (ret < 0) return ret;
				TORRENT_ASSERT(p->blocks[block]);
			}
			
			p->last_use = time_now();
			while (size > 0)
			{
				TORRENT_ASSERT(p->blocks[block]);
				int to_copy = (std::min)(m_block_size
					- block_offset, size);
				std::memcpy(j.buffer + buffer_offset
					, p->blocks[block] + block_offset
					, to_copy);
				size -= to_copy;
				block_offset = 0;
				buffer_offset += to_copy;
				++block;
			}
			ret = j.buffer_size;
			++m_cache_stats.blocks_read;
			if (hit) ++m_cache_stats.blocks_read_hit;
		}
		return ret;
	}

	void disk_io_thread::add_job(disk_io_job const& j
		, boost::function<void(int, disk_io_job const&)> const& f)
	{
		TORRENT_ASSERT(!m_abort);
		TORRENT_ASSERT(!j.callback);
		TORRENT_ASSERT(j.storage);
		TORRENT_ASSERT(j.buffer_size <= m_block_size);
		mutex_t::scoped_lock l(m_queue_mutex);

		std::list<disk_io_job>::reverse_iterator i = m_jobs.rbegin();
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

		std::list<disk_io_job>::iterator k = m_jobs.insert(i.base(), j);
		k->callback.swap(const_cast<boost::function<void(int, disk_io_job const&)>&>(f));
		if (j.action == disk_io_job::write)
			m_queue_buffer_size += j.buffer_size;
		TORRENT_ASSERT(j.storage.get());
		m_signal.notify_all();
	}

#ifdef TORRENT_DEBUG
	bool disk_io_thread::is_disk_buffer(char* buffer) const
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		return true;
#else
		mutex_t::scoped_lock l(m_pool_mutex);
#ifdef TORRENT_DISK_STATS
		if (m_buf_to_category.find(buffer)
			== m_buf_to_category.end()) return false;
#endif
		return m_pool.is_from(buffer);
#endif
	}
#endif

	char* disk_io_thread::allocate_buffer()
	{
		mutex_t::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_STATS
		++m_allocations;
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		return (char*)malloc(m_block_size);
#else
		m_pool.set_next_size(16);
		return (char*)m_pool.ordered_malloc();
#endif
	}

	void disk_io_thread::free_buffer(char* buf)
	{
		mutex_t::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_STATS
		--m_allocations;
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		free(buf);
#else
		m_pool.ordered_free(buf);
#endif
	}

	bool disk_io_thread::test_error(disk_io_job& j)
	{
		error_code const& ec = j.storage->error();
		if (ec)
		{
			j.buffer = 0;
			j.str = ec.message();
			j.error = ec;
			j.error_file = j.storage->error_file();
			j.storage->clear_error();
#ifdef TORRENT_DEBUG
			std::cout << "ERROR: '" << j.str << "' " << j.error_file << std::endl;
#endif
			return true;
		}
		return false;
	}

	void disk_io_thread::operator()()
	{
		for (;;)
		{
#ifdef TORRENT_DISK_STATS
			m_log << log_time() << " idle" << std::endl;
#endif
			mutex_t::scoped_lock jl(m_queue_mutex);

			while (m_jobs.empty() && !m_abort)
				m_signal.wait(jl);
			if (m_abort && m_jobs.empty())
			{
				jl.unlock();

				mutex_t::scoped_lock l(m_piece_mutex);
				// flush all disk caches
				for (cache_t::iterator i = m_pieces.begin()
					, end(m_pieces.end()); i != end; ++i)
					flush(i, l);
				for (cache_t::iterator i = m_read_pieces.begin()
					, end(m_read_pieces.end()); i != end; ++i)
					free_piece(*i, l);
				m_pieces.clear();
				m_read_pieces.clear();
				// release the io_service to allow the run() call to return
				// we do this once we stop posting new callbacks to it.
				m_work.reset();
				return;
			}

			// if there's a buffer in this job, it will be freed
			// when this holder is destructed, unless it has been
			// released.
			disk_buffer_holder holder(*this
				, m_jobs.front().action != disk_io_job::check_fastresume
				? m_jobs.front().buffer : 0);

			boost::function<void(int, disk_io_job const&)> handler;
			handler.swap(m_jobs.front().callback);

			disk_io_job j = m_jobs.front();
			m_jobs.pop_front();
			m_queue_buffer_size -= j.buffer_size;
			jl.unlock();

			flush_expired_pieces();

			int ret = 0;

			TORRENT_ASSERT(j.storage || j.action == disk_io_job::abort_thread);
#ifdef TORRENT_DISK_STATS
			ptime start = time_now();
#endif
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif

			switch (j.action)
			{
				case disk_io_job::abort_torrent:
				{
					mutex_t::scoped_lock jl(m_queue_mutex);
					for (std::list<disk_io_job>::iterator i = m_jobs.begin();
						i != m_jobs.end();)
					{
						if (i->storage != j.storage)
						{
							++i;
							continue;
						}
						if (i->action == disk_io_job::check_files)
						{
							if (i->callback) m_ios.post(boost::bind(i->callback
									, piece_manager::disk_check_aborted, *i));
							m_jobs.erase(i++);
							continue;
						}
						++i;
					}
					jl.unlock();

					mutex_t::scoped_lock l(m_piece_mutex);

					for (cache_t::iterator i = m_read_pieces.begin();
						i != m_read_pieces.end();)
					{
						if (i->storage == j.storage)
						{
							free_piece(*i, l);
							i = m_read_pieces.erase(i);
						}
						else
						{
							++i;
						}
					}
					l.unlock();

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					{
						mutex_t::scoped_lock l(m_pool_mutex);
						m_pool.release_memory();
					}
#endif
					break;
				}
				case disk_io_job::abort_thread:
				{
					mutex_t::scoped_lock jl(m_queue_mutex);

					for (std::list<disk_io_job>::iterator i = m_jobs.begin();
							i != m_jobs.end();)
					{
						if (i->action == disk_io_job::read)
						{
							if (i->callback) m_ios.post(boost::bind(i->callback, -1, *i));
							m_jobs.erase(i++);
							continue;
						}
						if (i->action == disk_io_job::check_files)
						{
							if (i->callback) m_ios.post(bind(i->callback
								, piece_manager::disk_check_aborted, *i));
							m_jobs.erase(i++);
							continue;
						}
						++i;
					}

					m_abort = true;
					break;
				}
				case disk_io_job::read:
				{
					if (test_error(j))
					{
						ret = -1;
						break;
					}
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " read " << j.buffer_size << std::endl;
#endif
					INVARIANT_CHECK;
					TORRENT_ASSERT(j.buffer == 0);
					j.buffer = allocate_buffer();
					TORRENT_ASSERT(j.buffer_size <= m_block_size);
					if (j.buffer == 0)
					{
						ret = -1;
						j.error = error_code(ENOMEM, get_posix_category());
						j.str = j.error.message();
						break;
					}

					disk_buffer_holder read_holder(*this, j.buffer);
					ret = try_read_from_cache(j);

					// -2 means there's no space in the read cache
					// or that the read cache is disabled
					if (ret == -1)
					{
						test_error(j);
						break;
					}
					else if (ret == -2)
					{
						ret = j.storage->read_impl(j.buffer, j.piece, j.offset
							, j.buffer_size);
						if (ret < 0)
						{
							test_error(j);
							break;
						}
						++m_cache_stats.blocks_read;
					}
					TORRENT_ASSERT(j.buffer == read_holder.get());
					read_holder.release();
					break;
				}
				case disk_io_job::write:
				{
					if (test_error(j))
					{
						ret = -1;
						break;
					}
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " write " << j.buffer_size << std::endl;
#endif
					mutex_t::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;
					cache_t::iterator p
						= find_cached_piece(m_pieces, j, l);
					int block = j.offset / m_block_size;
					TORRENT_ASSERT(j.buffer);
					TORRENT_ASSERT(j.buffer_size <= m_block_size);
					if (p != m_pieces.end())
					{
						TORRENT_ASSERT(p->blocks[block] == 0);

						if (p->blocks[block])
						{
							free_buffer(p->blocks[block]);
							--p->num_blocks;
						}
						p->blocks[block] = j.buffer;
						++m_cache_stats.cache_size;
						++p->num_blocks;
						p->last_use = time_now();
					}
					else
					{
						if (cache_block(j, l) < 0)
						{
							ret = j.storage->write_impl(j.buffer, j.piece, j.offset, j.buffer_size);
							if (ret < 0)
							{
								test_error(j);
								break;
							}
							break;
						}
					}
					// we've now inserted the buffer
					// in the cache, we should not
					// free it at the end
					holder.release();
					if (m_cache_stats.cache_size >= m_cache_size)
						flush_oldest_piece(l);
					break;
				}
				case disk_io_job::hash:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " hash" << std::endl;
#endif
					mutex_t::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					cache_t::iterator i
						= find_cached_piece(m_pieces, j, l);
					if (i != m_pieces.end())
					{
						flush_and_remove(i, l);
						if (test_error(j))
						{
							ret = -1;
							j.storage->mark_failed(j.piece);
							break;
						}
					}
					l.unlock();
					sha1_hash h = j.storage->hash_for_piece_impl(j.piece);
					if (test_error(j))
					{
						ret = -1;
						j.storage->mark_failed(j.piece);
						break;
					}
					ret = (j.storage->info()->hash_for_piece(j.piece) == h)?0:-2;
					if (ret == -2) j.storage->mark_failed(j.piece);
					break;
				}
				case disk_io_job::move_storage:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " move" << std::endl;
#endif
					TORRENT_ASSERT(j.buffer == 0);
					ret = j.storage->move_storage_impl(j.str);
					if (ret != 0)
					{
						test_error(j);
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
					TORRENT_ASSERT(j.buffer == 0);

					mutex_t::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					for (cache_t::iterator i = m_pieces.begin(); i != m_pieces.end();)
					{
						if (i->storage == j.storage)
						{
							flush(i, l);
							i = m_pieces.erase(i);
						}
						else
						{
							++i;
						}
					}
					l.unlock();
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					{
						mutex_t::scoped_lock l(m_pool_mutex);
						TORRENT_ASSERT(m_magic == 0x1337);
						m_pool.release_memory();
					}
#endif
					ret = j.storage->release_files_impl();
					if (ret != 0) test_error(j);
					break;
				}
				case disk_io_job::clear_read_cache:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " clear-cache" << std::endl;
#endif
					TORRENT_ASSERT(j.buffer == 0);

					mutex_t::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					for (cache_t::iterator i = m_read_pieces.begin();
						i != m_read_pieces.end();)
					{
						if (i->storage == j.storage)
						{
							free_piece(*i, l);
							i = m_read_pieces.erase(i);
						}
						else
						{
							++i;
						}
					}
					l.unlock();
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					{
						mutex_t::scoped_lock l(m_pool_mutex);
						m_pool.release_memory();
					}
#endif
					ret = 0;
					break;
				}
				case disk_io_job::delete_files:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " delete" << std::endl;
#endif
					TORRENT_ASSERT(j.buffer == 0);

					mutex_t::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					cache_t::iterator i = std::remove_if(
						m_pieces.begin(), m_pieces.end(), bind(&cached_piece_entry::storage, _1) == j.storage);

					for (cache_t::iterator k = i; k != m_pieces.end(); ++k)
					{
						torrent_info const& ti = *k->storage->info();
						int blocks_in_piece = (ti.piece_size(k->piece) + m_block_size - 1) / m_block_size;
						for (int j = 0; j < blocks_in_piece; ++j)
						{
							if (k->blocks[j] == 0) continue;
							free_buffer(k->blocks[j]);
							k->blocks[j] = 0;
							--m_cache_stats.cache_size;
						}
					}
					m_pieces.erase(i, m_pieces.end());
					l.unlock();
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					{
						mutex_t::scoped_lock l(m_pool_mutex);
						m_pool.release_memory();
					}
#endif
					ret = j.storage->delete_files_impl();
					if (ret != 0) test_error(j);
					break;
				}
				case disk_io_job::check_fastresume:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " check fastresume" << std::endl;
#endif
					lazy_entry const* rd = (lazy_entry const*)j.buffer;
					TORRENT_ASSERT(rd != 0);
					ret = j.storage->check_fastresume(*rd, j.str);
					break;
				}
				case disk_io_job::check_files:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " check files" << std::endl;
#endif
					int piece_size = j.storage->info()->piece_length();
					for (int processed = 0; processed < 4 * 1024 * 1024; processed += piece_size)
					{
						ret = j.storage->check_files(j.piece, j.offset, j.str);

#ifndef BOOST_NO_EXCEPTIONS
						try {
#endif
							TORRENT_ASSERT(handler);
							if (handler && ret == piece_manager::need_full_check)
								m_ios.post(bind(handler, ret, j));
#ifndef BOOST_NO_EXCEPTIONS
						} catch (std::exception&) {}
#endif
						if (ret != piece_manager::need_full_check) break;
					}
					if (test_error(j))
					{
						ret = piece_manager::fatal_disk_error;
						break;
					}
					TORRENT_ASSERT(ret != -2 || !j.str.empty());
					
					// if the check is not done, add it at the end of the job queue
					if (ret == piece_manager::need_full_check)
					{
						add_job(j, handler);
						continue;
					}
					break;
				}
				case disk_io_job::save_resume_data:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " save resume data" << std::endl;
#endif
					j.resume_data.reset(new entry(entry::dictionary_t));
					j.storage->write_resume_data(*j.resume_data);
					ret = 0;
					break;
				}
				case disk_io_job::rename_file:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " rename file" << std::endl;
#endif
					ret = j.storage->rename_file_impl(j.piece, j.str);
					if (ret != 0)
					{
						test_error(j);
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
				TORRENT_ASSERT(ret != -2 || !j.str.empty()
					|| j.action == disk_io_job::hash);
				if (handler) m_ios.post(boost::bind(handler, ret, j));
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&)
			{
				TORRENT_ASSERT(false);
			}
#endif
		}
		TORRENT_ASSERT(false);
	}
}

