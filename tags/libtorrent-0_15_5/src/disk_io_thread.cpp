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

/*
	Disk queue elevator patch by Morten Husveit
*/

#include "libtorrent/storage.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_pool.hpp"
#include <boost/scoped_array.hpp>

#ifdef TORRENT_DISK_STATS
#include "libtorrent/time.hpp"
#endif

#if TORRENT_USE_MLOCK && !defined TORRENT_WINDOWS
#include <sys/mman.h>
#endif

#ifdef TORRENT_LINUX
#include <linux/unistd.h>
#endif

namespace libtorrent
{
	bool should_cancel_on_abort(disk_io_job const& j);
	bool is_read_operation(disk_io_job const& j);
	bool operation_has_buffer(disk_io_job const& j);

	disk_buffer_pool::disk_buffer_pool(int block_size)
		: m_block_size(block_size)
		, m_in_use(0)
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		, m_pool(block_size, m_settings.cache_buffer_chunk_size)
#endif
	{
#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
		m_allocations = 0;
#endif
#ifdef TORRENT_DISK_STATS
		m_log.open("disk_buffers.log", std::ios::trunc);
		m_categories["read cache"] = 0;
		m_categories["write cache"] = 0;

		m_disk_access_log.open("disk_access.log", std::ios::trunc);
#endif
#ifdef TORRENT_DEBUG
		m_magic = 0x1337;
#endif
	}

#ifdef TORRENT_DEBUG
	disk_buffer_pool::~disk_buffer_pool()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_magic = 0;
	}
#endif

#if defined TORRENT_DEBUG || defined TORRENT_DISK_STATS
	bool disk_buffer_pool::is_disk_buffer(char* buffer
		,boost::mutex::scoped_lock& l) const
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_DISK_STATS
		if (m_buf_to_category.find(buffer)
			== m_buf_to_category.end()) return false;
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		return true;
#else
		return m_pool.is_from(buffer);
#endif
	}

	bool disk_buffer_pool::is_disk_buffer(char* buffer) const
	{
		mutex_t::scoped_lock l(m_pool_mutex);
		return is_disk_buffer(buffer, l);
	}
#endif

	char* disk_buffer_pool::allocate_buffer(char const* category)
	{
		mutex_t::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		char* ret = page_aligned_allocator::malloc(m_block_size);
#else
		char* ret = (char*)m_pool.ordered_malloc();
		m_pool.set_next_size(m_settings.cache_buffer_chunk_size);
#endif
		++m_in_use;
#if TORRENT_USE_MLOCK
		if (m_settings.lock_disk_cache)
		{
#ifdef TORRENT_WINDOWS
			VirtualLock(ret, m_block_size);
#else
			mlock(ret, m_block_size);
#endif		
		}
#endif

#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
		++m_allocations;
#endif
#ifdef TORRENT_DISK_STATS
		++m_categories[category];
		m_buf_to_category[ret] = category;
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
#endif
		TORRENT_ASSERT(ret == 0 || is_disk_buffer(ret, l));
		return ret;
	}

#ifdef TORRENT_DISK_STATS
	void disk_buffer_pool::rename_buffer(char* buf, char const* category)
	{
		mutex_t::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
		TORRENT_ASSERT(m_categories.find(m_buf_to_category[buf])
			!= m_categories.end());
		std::string const& prev_category = m_buf_to_category[buf];
		--m_categories[prev_category];
		m_log << log_time() << " " << prev_category << ": " << m_categories[prev_category] << "\n";

		++m_categories[category];
		m_buf_to_category[buf] = category;
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
		TORRENT_ASSERT(m_categories.find(m_buf_to_category[buf])
			!= m_categories.end());
	}
#endif

	void disk_buffer_pool::free_buffer(char* buf)
	{
		TORRENT_ASSERT(buf);
		mutex_t::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
		--m_allocations;
#endif
#ifdef TORRENT_DISK_STATS
		TORRENT_ASSERT(m_categories.find(m_buf_to_category[buf])
			!= m_categories.end());
		std::string const& category = m_buf_to_category[buf];
		--m_categories[category];
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
		m_buf_to_category.erase(buf);
#endif
#if TORRENT_USE_MLOCK
		if (m_settings.lock_disk_cache)
		{
#ifdef TORRENT_WINDOWS
			VirtualUnlock(buf, m_block_size);
#else
			munlock(buf, m_block_size);
#endif		
		}
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		page_aligned_allocator::free(buf);
#else
		m_pool.ordered_free(buf);
#endif
		--m_in_use;
	}

	char* disk_buffer_pool::allocate_buffers(int num_blocks, char const* category)
	{
		mutex_t::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		char* ret = page_aligned_allocator::malloc(m_block_size * num_blocks);
#else
		char* ret = (char*)m_pool.ordered_malloc(num_blocks);
		m_pool.set_next_size(m_settings.cache_buffer_chunk_size);
#endif
		m_in_use += num_blocks;
#if TORRENT_USE_MLOCK
		if (m_settings.lock_disk_cache)
		{
#ifdef TORRENT_WINDOWS
			VirtualLock(ret, m_block_size * num_blocks);
#else
			mlock(ret, m_block_size * num_blocks);
#endif		
		}
#endif
#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
		m_allocations += num_blocks;
#endif
#ifdef TORRENT_DISK_STATS
		m_categories[category] += num_blocks;
		m_buf_to_category[ret] = category;
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
#endif
		TORRENT_ASSERT(ret == 0 || is_disk_buffer(ret, l));
		return ret;
	}

	void disk_buffer_pool::free_buffers(char* buf, int num_blocks)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(num_blocks >= 1);
		mutex_t::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
		m_allocations -= num_blocks;
#endif
#ifdef TORRENT_DISK_STATS
		TORRENT_ASSERT(m_categories.find(m_buf_to_category[buf])
			!= m_categories.end());
		std::string const& category = m_buf_to_category[buf];
		m_categories[category] -= num_blocks;
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
		m_buf_to_category.erase(buf);
#endif
#if TORRENT_USE_MLOCK
		if (m_settings.lock_disk_cache)
		{
#ifdef TORRENT_WINDOWS
			VirtualUnlock(buf, m_block_size * num_blocks);
#else
			munlock(buf, m_block_size * num_blocks);
#endif		
		}
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		page_aligned_allocator::free(buf);
#else
		m_pool.ordered_free(buf, num_blocks);
#endif
		m_in_use -= num_blocks;
	}

	void disk_buffer_pool::release_memory()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		mutex_t::scoped_lock l(m_pool_mutex);
		m_pool.release_memory();
#endif
	}

// ------- disk_io_thread ------


	disk_io_thread::disk_io_thread(asio::io_service& ios
		, boost::function<void()> const& queue_callback
		, file_pool& fp
		, int block_size)
		: disk_buffer_pool(block_size)
		, m_abort(false)
		, m_waiting_to_shutdown(false)
		, m_queue_buffer_size(0)
		, m_last_file_check(time_now_hires())
		, m_ios(ios)
		, m_queue_callback(queue_callback)
		, m_work(io_service::work(m_ios))
		, m_file_pool(fp)
		, m_disk_io_thread(boost::ref(*this))
	{
#ifdef TORRENT_DISK_STATS
		m_log.open("disk_io_thread.log", std::ios::trunc);
#endif
	}

	disk_io_thread::~disk_io_thread()
	{
		TORRENT_ASSERT(m_abort == true);
	}

	void disk_io_thread::join()
	{
		mutex_t::scoped_lock l(m_queue_mutex);
		disk_io_job j;
		m_waiting_to_shutdown = true;
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
				if (i->blocks[b].buf) info.blocks[b] = true;
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
				if (i->blocks[b].buf) info.blocks[b] = true;
			ret.push_back(info);
		}
	}
	
	cache_status disk_io_thread::status() const
	{
		mutex_t::scoped_lock l(m_piece_mutex);
		m_cache_stats.total_used_buffers = in_use();
		m_cache_stats.queued_bytes = m_queue_buffer_size;
		return m_cache_stats;
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
			if (should_cancel_on_abort(*i))
			{
				if (i->action == disk_io_job::write)
				{
					TORRENT_ASSERT(m_queue_buffer_size >= i->buffer_size);
					m_queue_buffer_size -= i->buffer_size;
				}
				post_callback(i->callback, *i, -3);
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
		// flush write cache
		for (;;)
		{
			cache_t::iterator i = std::min_element(
				m_pieces.begin(), m_pieces.end()
				, boost::bind(&cached_piece_entry::last_use, _1)
				< boost::bind(&cached_piece_entry::last_use, _2));
			if (i == m_pieces.end()) break;
			int age = total_seconds(now - i->last_use);
			if (age < m_settings.cache_expiry) break;
			flush_and_remove(i, l);
		}

		// flush read cache
		for (;;)
		{
			cache_t::iterator i = std::min_element(
				m_read_pieces.begin(), m_read_pieces.end()
				, boost::bind(&cached_piece_entry::last_use, _1)
				< boost::bind(&cached_piece_entry::last_use, _2));
			if (i == m_read_pieces.end()) break;
			int age = total_seconds(now - i->last_use);
			if (age < m_settings.cache_expiry) break;
			free_piece(*i, l);
			m_read_pieces.erase(i);
		}
	}

	// returns the number of blocks that were freed
	int disk_io_thread::free_piece(cached_piece_entry& p, mutex_t::scoped_lock& l)
	{
		int piece_size = p.storage->info()->piece_size(p.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		int ret = 0;

		for (int i = 0; i < blocks_in_piece; ++i)
		{
			if (p.blocks[i].buf == 0) continue;
			free_buffer(p.blocks[i].buf);
			++ret;
			p.blocks[i].buf = 0;
			--p.num_blocks;
			--m_cache_stats.cache_size;
			--m_cache_stats.read_cache_size;
		}
		return ret;
	}

	// returns the number of blocks that were freed
	int disk_io_thread::clear_oldest_read_piece(
		int num_blocks
		, cache_t::iterator ignore
		, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;

		cache_t::iterator i = std::min_element(
			m_read_pieces.begin(), m_read_pieces.end()
			, boost::bind(&cached_piece_entry::last_use, _1)
			< boost::bind(&cached_piece_entry::last_use, _2));
		if (i != m_read_pieces.end() && i != ignore)
		{
			// don't replace an entry that is less than one second old
			if (time_now() - i->last_use < seconds(1)) return 0;
			int blocks = 0;
			if (num_blocks >= i->num_blocks)
			{
				blocks = free_piece(*i, l);
			}
			else
			{
				// delete blocks from the start and from the end
				// until num_blocks have been freed
				int end = (i->storage->info()->piece_size(i->piece) + m_block_size - 1) / m_block_size - 1;
				int start = 0;

				while (num_blocks)
				{
					while (i->blocks[start].buf == 0 && start <= end) ++start;
					if (start > end) break;
					free_buffer(i->blocks[start].buf);
					i->blocks[start].buf = 0;
					++blocks;
					--i->num_blocks;
					--m_cache_stats.cache_size;
					--m_cache_stats.read_cache_size;
					--num_blocks;
					if (!num_blocks) break;

					while (i->blocks[end].buf == 0 && start <= end) --end;
					if (start > end) break;
					free_buffer(i->blocks[end].buf);
					i->blocks[end].buf = 0;
					++blocks;
					--i->num_blocks;
					--m_cache_stats.cache_size;
					--m_cache_stats.read_cache_size;
					--num_blocks;
				}
			
			}
			if (i->num_blocks == 0) m_read_pieces.erase(i);
			return blocks;
		}
		return 0;
	}

	int contiguous_blocks(disk_io_thread::cached_piece_entry const& b)
	{
		int ret = 0;
		int current = 0;
		int blocks_in_piece = (b.storage->info()->piece_size(b.piece) + 16 * 1024 - 1) / (16 * 1024);
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			if (b.blocks[i].buf) ++current;
			else
			{
				if (current > ret) ret = current;
				current = 0;
			}
		}
		if (current > ret) ret = current;
		return ret;
	}

	int disk_io_thread::flush_contiguous_blocks(disk_io_thread::cache_t::iterator e
		, mutex_t::scoped_lock& l, int lower_limit)
	{
		// first find the largest range of contiguous  blocks
		int len = 0;
		int current = 0;
		int pos = 0;
		int start = 0;
		int blocks_in_piece = (e->storage->info()->piece_size(e->piece)
			+ m_block_size - 1) / m_block_size;
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			if (e->blocks[i].buf) ++current;
			else
			{
				if (current > len)
				{
					len = current;
					pos = start;
				}
				current = 0;
				start = i + 1;
			}
		}
		if (current > len)
		{
			len = current;
			pos = start;
		}

		if (len < lower_limit || len <= 0) return 0;
		len = flush_range(e, pos, pos + len, l);
		if (e->num_blocks == 0) m_pieces.erase(e);
		return len;
	}

	// flushes 'blocks' blocks from the cache
	int disk_io_thread::flush_cache_blocks(mutex_t::scoped_lock& l
		, int blocks, cache_t::iterator ignore, int options)
	{
		// first look if there are any read cache entries that can
		// be cleared
		int ret = 0;
		int tmp = 0;
		do {
			tmp = clear_oldest_read_piece(blocks, ignore, l);
			blocks -= tmp;
			ret += tmp;
		} while (tmp > 0 && blocks > 0);

		if (options & dont_flush_write_blocks) return ret;

		if (m_settings.disk_cache_algorithm == session_settings::lru)
		{
			while (blocks > 0)
			{
				cache_t::iterator i = std::min_element(
					m_pieces.begin(), m_pieces.end()
					, boost::bind(&cached_piece_entry::last_use, _1)
					< boost::bind(&cached_piece_entry::last_use, _2));
				if (i == m_pieces.end()) return ret;
				tmp = flush_and_remove(i, l);
				blocks -= tmp;
				ret += tmp;
			}
		}
		else if (m_settings.disk_cache_algorithm == session_settings::largest_contiguous)
		{
			while (blocks > 0)
			{
				cache_t::iterator i = std::max_element(
					m_pieces.begin(), m_pieces.end()
					, boost::bind(&contiguous_blocks, _1)
					< boost::bind(&contiguous_blocks, _2));
				if (i == m_pieces.end()) return ret;
				tmp = flush_contiguous_blocks(i, l);
				blocks -= tmp;
				ret += tmp;
			}
		}
		return ret;
	}

	int disk_io_thread::flush_and_remove(disk_io_thread::cache_t::iterator e
		, mutex_t::scoped_lock& l)
	{
		int ret = flush_range(e, 0, INT_MAX, l);
		m_pieces.erase(e);
		return ret;
	}

	int disk_io_thread::flush_range(disk_io_thread::cache_t::iterator e
		, int start, int end, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(start < end);

		// TODO: copy *e and unlink it before unlocking
		cached_piece_entry& p = *e;
		int piece_size = p.storage->info()->piece_size(p.piece);
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " flushing " << piece_size << std::endl;
#endif
		TORRENT_ASSERT(piece_size > 0);
		
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		int buffer_size = 0;
		int offset = 0;

		boost::scoped_array<char> buf;
		file::iovec_t* iov = 0;
		int iov_counter = 0;
		if (m_settings.coalesce_writes) buf.reset(new (std::nothrow) char[piece_size]);
		else iov = TORRENT_ALLOCA(file::iovec_t, blocks_in_piece);

		end = (std::min)(end, blocks_in_piece);
		for (int i = start; i <= end; ++i)
		{
			if (i == end || p.blocks[i].buf == 0)
			{
				if (buffer_size == 0) continue;
			
				TORRENT_ASSERT(buffer_size <= i * m_block_size);
				l.unlock();
				if (iov)
				{
					p.storage->write_impl(iov, p.piece, (std::min)(
						i * m_block_size, piece_size) - buffer_size, iov_counter);
					iov_counter = 0;
				}
				else
				{
					TORRENT_ASSERT(buf);
					file::iovec_t b = { buf.get(), buffer_size };
					p.storage->write_impl(&b, p.piece, (std::min)(
						i * m_block_size, piece_size) - buffer_size, 1);
				}
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
				iov[iov_counter].iov_base = p.blocks[i].buf;
				iov[iov_counter].iov_len = block_size;
				++iov_counter;
			}
			else
			{
				std::memcpy(buf.get() + offset, p.blocks[i].buf, block_size);
				offset += m_block_size;
			}
			buffer_size += block_size;
			TORRENT_ASSERT(p.num_blocks > 0);
			--p.num_blocks;
			++m_cache_stats.blocks_written;
			--m_cache_stats.cache_size;
		}

		int ret = 0;
		disk_io_job j;
		j.storage = p.storage;
		j.action = disk_io_job::write;
		j.buffer = 0;
		j.piece = p.piece;
		test_error(j);
		for (int i = start; i < end; ++i)
		{
			if (p.blocks[i].buf == 0) continue;
			j.buffer_size = (std::min)(piece_size - i * m_block_size, m_block_size);
			int result = j.error ? -1 : j.buffer_size;
			j.offset = i * m_block_size;
			free_buffer(p.blocks[i].buf);
			post_callback(p.blocks[i].callback, j, result);
			p.blocks[i].callback.clear();
			p.blocks[i].buf = 0;
			++ret;
		}

		TORRENT_ASSERT(buffer_size == 0);
//		std::cerr << " flushing p: " << p.piece << " cached_blocks: " << m_cache_stats.cache_size << std::endl;
#ifdef TORRENT_DEBUG
		for (int i = start; i < end; ++i)
			TORRENT_ASSERT(p.blocks[i].buf == 0);
#endif
		return ret;
	}

	// returns -1 on failure
	int disk_io_thread::cache_block(disk_io_job& j
		, boost::function<void(int,disk_io_job const&)>& handler
		, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(find_cached_piece(m_pieces, j, l) == m_pieces.end());
		TORRENT_ASSERT((j.offset & (m_block_size-1)) == 0);
		cached_piece_entry p;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		// there's no point in caching the piece if
		// there's only one block in it
		if (blocks_in_piece <= 1) return -1;

#ifdef TORRENT_DISK_STATS
		rename_buffer(j.buffer, "write cache");
#endif

		p.piece = j.piece;
		p.storage = j.storage;
		p.last_use = time_now();
		p.num_blocks = 1;
		p.blocks.reset(new (std::nothrow) cached_block_entry[blocks_in_piece]);
		if (!p.blocks) return -1;
		int block = j.offset / m_block_size;
//		std::cerr << " adding cache entry for p: " << j.piece << " block: " << block << " cached_blocks: " << m_cache_stats.cache_size << std::endl;
		p.blocks[block].buf = j.buffer;
		p.blocks[block].callback.swap(handler);
		++m_cache_stats.cache_size;
		m_pieces.push_back(p);
		return 0;
	}

	// fills a piece with data from disk, returns the total number of bytes
	// read or -1 if there was an error
	int disk_io_thread::read_into_piece(cached_piece_entry& p, int start_block
		, int options, int num_blocks, mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(num_blocks > 0);
		int piece_size = p.storage->info()->piece_size(p.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		int end_block = start_block;
		int num_read = 0;

		int iov_counter = 0;
		file::iovec_t* iov = TORRENT_ALLOCA(file::iovec_t, (std::min)(blocks_in_piece - start_block, num_blocks));

		int piece_offset = start_block * m_block_size;

		int ret = 0;

		boost::scoped_array<char> buf;
		for (int i = start_block; i < blocks_in_piece
			&& ((options & ignore_cache_size)
				|| in_use() < m_settings.cache_size); ++i)
		{
			int block_size = (std::min)(piece_size - piece_offset, m_block_size);
			TORRENT_ASSERT(piece_offset <= piece_size);

			// this is a block that is already allocated
			// free it an allocate a new one
			if (p.blocks[i].buf)
			{
				free_buffer(p.blocks[i].buf);
				--p.num_blocks;
				--m_cache_stats.cache_size;
				--m_cache_stats.read_cache_size;
			}
			p.blocks[i].buf = allocate_buffer("read cache");

			// the allocation failed, break
			if (p.blocks[i].buf == 0)
			{
				free_piece(p, l);
				return -1;
			}
			++p.num_blocks;
			++m_cache_stats.cache_size;
			++m_cache_stats.read_cache_size;
			++end_block;
			++num_read;
			iov[iov_counter].iov_base = p.blocks[i].buf;
			iov[iov_counter].iov_len = block_size;
			++iov_counter;
			piece_offset += m_block_size;
			if (num_read >= num_blocks) break;
		}

		if (end_block == start_block)
		{
			// something failed. Free all buffers
			// we just allocated
			free_piece(p, l);
			return -2;
		}

		TORRENT_ASSERT(iov_counter <= (std::min)(blocks_in_piece - start_block, num_blocks));

		// the buffer_size is the size of the buffer we need to read
		// all these blocks.
		const int buffer_size = (std::min)((end_block - start_block) * m_block_size
			, piece_size - start_block * m_block_size);
		TORRENT_ASSERT(buffer_size > 0);
		TORRENT_ASSERT(buffer_size <= piece_size);
		TORRENT_ASSERT(buffer_size + start_block * m_block_size <= piece_size);

		if (m_settings.coalesce_reads)
			buf.reset(new (std::nothrow) char[buffer_size]);

		if (buf)
		{
			l.unlock();
			file::iovec_t b = { buf.get(), buffer_size };
			ret = p.storage->read_impl(&b, p.piece, start_block * m_block_size, 1);
			l.lock();
			++m_cache_stats.reads;
			if (p.storage->error())
			{
				free_piece(p, l);
				return -1;
			}

			if (ret != buffer_size)
			{
				// this means the file wasn't big enough for this read
				p.storage->get_storage_impl()->set_error(""
					, errors::file_too_short);
				free_piece(p, l);
				return -1;
			}
		
			int offset = 0;
			for (int i = 0; i < iov_counter; ++i)
			{
				TORRENT_ASSERT(iov[i].iov_base);
				TORRENT_ASSERT(iov[i].iov_len > 0);
				TORRENT_ASSERT(offset + iov[i].iov_len <= buffer_size);
				std::memcpy(iov[i].iov_base, buf.get() + offset, iov[i].iov_len);
				offset += iov[i].iov_len;
			}
		}
		else
		{
			l.unlock();
			ret = p.storage->read_impl(iov, p.piece, start_block * m_block_size, iov_counter);
			l.lock();
			++m_cache_stats.reads;
			if (p.storage->error())
			{
				free_piece(p, l);
				return -1;
			}

			if (ret != buffer_size)
			{
				// this means the file wasn't big enough for this read
				p.storage->get_storage_impl()->set_error(""
					, errors::file_too_short);
				free_piece(p, l);
				return -1;
			}
		}

		TORRENT_ASSERT(ret == buffer_size);
		return ret;
	}
	
	// returns -1 on read error, -2 on out of memory error or the number of bytes read
	// this function ignores the cache size limit, it will read the entire
	// piece regardless of the offset in j
	// this is used for seed-mode, where we need to read the entire piece to calculate
	// the hash
	int disk_io_thread::cache_read_piece(disk_io_job const& j, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		if (in_use() + blocks_in_piece > m_settings.cache_size)
			flush_cache_blocks(l, in_use() + blocks_in_piece - m_settings.cache_size, m_read_pieces.end());

		cached_piece_entry p;
		p.piece = j.piece;
		p.storage = j.storage;
		p.last_use = time_now();
		p.num_blocks = 0;
		p.blocks.reset(new (std::nothrow) cached_block_entry[blocks_in_piece]);
		if (!p.blocks) return -1;
		int ret = read_into_piece(p, 0, ignore_cache_size, INT_MAX, l);
		
		if (ret >= 0) m_read_pieces.push_back(p);

		return ret;
	}

	// returns -1 on read error, -2 if there isn't any space in the cache
	// or the number of bytes read
	int disk_io_thread::cache_read_block(disk_io_job const& j, mutex_t::scoped_lock& l)
	{
		INVARIANT_CHECK;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		int start_block = j.offset / m_block_size;

		int blocks_to_read = blocks_in_piece - start_block;
		blocks_to_read = (std::min)(blocks_to_read, (std::max)((m_settings.cache_size
			+ m_cache_stats.read_cache_size - in_use())/2, 3));
		blocks_to_read = (std::min)(blocks_to_read, m_settings.read_cache_line_size);

		if (in_use() + blocks_to_read > m_settings.cache_size)
			if (flush_cache_blocks(l, in_use() + blocks_to_read - m_settings.cache_size
				, m_read_pieces.end(), dont_flush_write_blocks) == 0)
				return -2;

		cached_piece_entry p;
		p.piece = j.piece;
		p.storage = j.storage;
		p.last_use = time_now();
		p.num_blocks = 0;
		p.blocks.reset(new (std::nothrow) cached_block_entry[blocks_in_piece]);
		if (!p.blocks) return -1;
		int ret = read_into_piece(p, start_block, 0, blocks_to_read, l);
		
		if (ret >= 0) m_read_pieces.push_back(p);

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
				if (p.blocks[k].buf)
				{
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					TORRENT_ASSERT(is_disk_buffer(p.blocks[k].buf));
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
				if (p.blocks[k].buf)
				{
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
					TORRENT_ASSERT(is_disk_buffer(p.blocks[k].buf));
#endif
					++blocks;
				}
			}
//			TORRENT_ASSERT(blocks == p.num_blocks);
			cached_read_blocks += blocks;
		}

		TORRENT_ASSERT(cached_read_blocks + cached_write_blocks == m_cache_stats.cache_size);
		TORRENT_ASSERT(cached_read_blocks == m_cache_stats.read_cache_size);

#ifdef TORRENT_DISK_STATS
		int read_allocs = m_categories.find(std::string("read cache"))->second;
		int write_allocs = m_categories.find(std::string("write cache"))->second;
		TORRENT_ASSERT(cached_read_blocks == read_allocs);
		TORRENT_ASSERT(cached_write_blocks == write_allocs);
#endif

		// when writing, there may be a one block difference, right before an old piece
		// is flushed
		TORRENT_ASSERT(m_cache_stats.cache_size <= m_settings.cache_size + 1);
	}
#endif

	int disk_io_thread::read_piece_from_cache_and_hash(disk_io_job const& j, sha1_hash& h)
	{
		TORRENT_ASSERT(j.buffer);

		mutex_t::scoped_lock l(m_piece_mutex);
	
		cache_t::iterator p
			= find_cached_piece(m_read_pieces, j, l);

		bool hit = true;
		int ret = 0;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		if (p != m_read_pieces.end() && p->num_blocks != blocks_in_piece)
		{
			// we have the piece in the cache, but not all of the blocks
			ret = read_into_piece(*p, 0, ignore_cache_size, blocks_in_piece, l);
			hit = false;
			if (ret < 0) return ret;
			TORRENT_ASSERT(!m_read_pieces.empty());
			TORRENT_ASSERT(p->piece == j.piece);
			TORRENT_ASSERT(p->storage == j.storage);
		}

		// if the piece cannot be found in the cache,
		// read the whole piece starting at the block
		// we got a request for.
		if (p == m_read_pieces.end())
		{
			ret = cache_read_piece(j, l);
			hit = false;
			if (ret < 0) return ret;
			p = m_read_pieces.end();
			--p;
			TORRENT_ASSERT(!m_read_pieces.empty());
			TORRENT_ASSERT(p->piece == j.piece);
			TORRENT_ASSERT(p->storage == j.storage);
		}

		if (!m_settings.disable_hash_checks)
		{
			hasher ctx;

			for (int i = 0; i < blocks_in_piece; ++i)
			{
				TORRENT_ASSERT(p->blocks[i].buf);
				ctx.update((char const*)p->blocks[i].buf, (std::min)(piece_size, m_block_size));
				piece_size -= m_block_size;
			}
			h = ctx.final();
		}

		ret = copy_from_piece(p, hit, j, l);
		TORRENT_ASSERT(ret > 0);
		if (ret < 0) return ret;

		// if read cache is disabled or we exceeded the
		// limit, remove this piece from the cache
		if (in_use() >= m_settings.cache_size
			|| !m_settings.use_read_cache)
		{
			TORRENT_ASSERT(!m_read_pieces.empty());
			TORRENT_ASSERT(p->piece == j.piece);
			TORRENT_ASSERT(p->storage == j.storage);
			if (p != m_read_pieces.end())
			{
				free_piece(*p, l);
				m_read_pieces.erase(p);
			}
		}

		ret = j.buffer_size;
		++m_cache_stats.blocks_read;
		if (hit) ++m_cache_stats.blocks_read_hit;
		return ret;
	}

	// this doesn't modify the read cache, it only
	// checks to see if the given read request can
	// be fully satisfied from the given cached piece
	// this is similar to copy_from_piece() but it
	// doesn't do anything but determining if it's a
	// cache hit or not
	bool disk_io_thread::is_cache_hit(cache_t::iterator p
		, disk_io_job const& j, mutex_t::scoped_lock& l)
	{
		int block = j.offset / m_block_size;
		int block_offset = j.offset & (m_block_size-1);
		int size = j.buffer_size;
		int min_blocks_to_read = block_offset > 0 && (size > m_block_size - block_offset) ? 2 : 1;
		TORRENT_ASSERT(size <= m_block_size);
		int start_block = block;
		// if we have to read more than one block, and
		// the first block is there, make sure we test
		// for the second block
		if (p->blocks[start_block].buf != 0 && min_blocks_to_read > 1)
			++start_block;

#ifdef TORRENT_DEBUG
		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		TORRENT_ASSERT(start_block < blocks_in_piece);
#endif

		return p->blocks[start_block].buf != 0;
	}

	int disk_io_thread::copy_from_piece(cache_t::iterator p, bool& hit
		, disk_io_job const& j, mutex_t::scoped_lock& l)
	{
		TORRENT_ASSERT(j.buffer);

		// update timestamp early so that we
		// don't run the risk of evicting our own piece
		// when making more room in the cache
		p->last_use = time_now();

		// copy from the cache and update the last use timestamp
		int block = j.offset / m_block_size;
		int block_offset = j.offset & (m_block_size-1);
		int buffer_offset = 0;
		int size = j.buffer_size;
		int min_blocks_to_read = block_offset > 0 && (size > m_block_size - block_offset) ? 2 : 1;
		TORRENT_ASSERT(size <= m_block_size);
		int start_block = block;
		if (p->blocks[start_block].buf != 0 && min_blocks_to_read > 1)
			++start_block;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		TORRENT_ASSERT(start_block < blocks_in_piece);

		// if block_offset > 0, we need to read two blocks, and then
		// copy parts of both, because it's not aligned to the block
		// boundaries
		if (p->blocks[start_block].buf == 0)
		{
			int end_block = start_block;
			while (end_block < blocks_in_piece && p->blocks[end_block].buf == 0) ++end_block;

			int blocks_to_read = end_block - block;
			blocks_to_read = (std::min)(blocks_to_read, (std::max)((m_settings.cache_size
				+ m_cache_stats.read_cache_size - in_use())/2, 3));
			blocks_to_read = (std::min)(blocks_to_read, m_settings.read_cache_line_size);
			blocks_to_read = (std::max)(blocks_to_read, min_blocks_to_read);
			if (in_use() + blocks_to_read > m_settings.cache_size)
				if (flush_cache_blocks(l, in_use() + blocks_to_read - m_settings.cache_size
					, p, dont_flush_write_blocks) == 0)
					return -2;

			int ret = read_into_piece(*p, block, 0, blocks_to_read, l);
			hit = false;
			if (ret < 0) return ret;
			if (ret < size + block_offset) return -2;
			TORRENT_ASSERT(p->blocks[block].buf);
		}

		while (size > 0)
		{
			TORRENT_ASSERT(p->blocks[block].buf);
			int to_copy = (std::min)(m_block_size
					- block_offset, size);
			std::memcpy(j.buffer + buffer_offset
					, p->blocks[block].buf + block_offset
					, to_copy);
			size -= to_copy;
			block_offset = 0;
			buffer_offset += to_copy;
			++block;
		}
		return j.buffer_size;
	}

	int disk_io_thread::try_read_from_cache(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.buffer);

		mutex_t::scoped_lock l(m_piece_mutex);
		if (!m_settings.use_read_cache) return -2;

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

		if (p == m_read_pieces.end()) return ret;

		ret = copy_from_piece(p, hit, j, l);
		if (ret < 0) return ret;

		ret = j.buffer_size;
		++m_cache_stats.blocks_read;
		if (hit) ++m_cache_stats.blocks_read_hit;
		return ret;
	}

	size_type disk_io_thread::queue_buffer_size() const
	{
		mutex_t::scoped_lock l(m_queue_mutex);
		return m_queue_buffer_size;
	}

	void disk_io_thread::add_job(disk_io_job const& j
		, boost::function<void(int, disk_io_job const&)> const& f)
	{
		TORRENT_ASSERT(!m_abort);
		TORRENT_ASSERT(j.storage
			|| j.action == disk_io_job::abort_thread
			|| j.action == disk_io_job::update_settings);
		TORRENT_ASSERT(j.buffer_size <= m_block_size);
		mutex_t::scoped_lock l(m_queue_mutex);

		m_jobs.push_back(j);
		m_jobs.back().callback = f;
		if (j.action == disk_io_job::write)
			m_queue_buffer_size += j.buffer_size;
		m_signal.notify_all();
	}

	bool disk_io_thread::test_error(disk_io_job& j)
	{
		TORRENT_ASSERT(j.storage);
		error_code const& ec = j.storage->error();
		if (ec)
		{
			j.buffer = 0;
			j.str.clear();
			j.error = ec;
			j.error_file = j.storage->error_file();
#ifdef TORRENT_DEBUG
			std::cout << "ERROR: '" << ec.message() << " in " 
				<< j.error_file << std::endl;
#endif
			j.storage->clear_error();
			return true;
		}
		return false;
	}

	void disk_io_thread::post_callback(
		boost::function<void(int, disk_io_job const&)> const& handler
		, disk_io_job const& j, int ret)
	{
		if (!handler) return;

		m_ios.post(boost::bind(handler, ret, j));
	}

	enum action_flags_t
	{
		read_operation = 1
		, buffer_operation = 2
		, cancel_on_abort = 4
	};

	static const boost::uint8_t action_flags[] =
	{
		read_operation + buffer_operation + cancel_on_abort // read
		, buffer_operation // write
		, 0 // hash
		, 0 // move_storage
		, 0 // release_files
		, 0 // delete_files
		, 0 // check_fastresume
		, read_operation + cancel_on_abort // check_files
		, 0 // save_resume_data
		, 0 // rename_file
		, 0 // abort_thread
		, 0 // clear_read_cache
		, 0 // abort_torrent
		, cancel_on_abort // update_settings
		, read_operation + cancel_on_abort // read_and_hash
	};

	bool should_cancel_on_abort(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.action >= 0 && j.action < sizeof(action_flags));
		return action_flags[j.action] & cancel_on_abort;
	}

	bool is_read_operation(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.action >= 0 && j.action < sizeof(action_flags));
		return action_flags[j.action] & read_operation;
	}

	bool operation_has_buffer(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.action >= 0 && j.action < sizeof(action_flags));
		return action_flags[j.action] & buffer_operation;
	}

	void disk_io_thread::operator()()
	{
		// 1 = forward in list, -1 = backwards in list
		int elevator_direction = 1;

		typedef std::multimap<size_type, disk_io_job> read_jobs_t;
		read_jobs_t sorted_read_jobs;
		read_jobs_t::iterator elevator_job_pos = sorted_read_jobs.begin();
		size_type last_elevator_pos = 0;
		bool need_update_elevator_pos = false;

		for (;;)
		{
#ifdef TORRENT_DISK_STATS
			m_log << log_time() << " idle" << std::endl;
#endif
			mutex_t::scoped_lock jl(m_queue_mutex);

			while (m_jobs.empty() && sorted_read_jobs.empty() && !m_abort)
			{
				// if there hasn't been an event in one second
				// see if we should flush the cache
//				if (!m_signal.timed_wait(jl, boost::posix_time::seconds(1)))
//					flush_expired_pieces();
				m_signal.wait(jl);
			}

			if (m_abort && m_jobs.empty())
			{
				jl.unlock();

				mutex_t::scoped_lock l(m_piece_mutex);
				// flush all disk caches
				for (cache_t::iterator i = m_pieces.begin()
					, end(m_pieces.end()); i != end; ++i)
					flush_range(i, 0, INT_MAX, l);

#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
				// since we're aborting the thread, we don't actually
				// need to free all the blocks individually. We can just
				// clear the piece list and the memory will be freed when we
				// destruct the m_pool. If we're not using a pool, we actually
				// have to free everything individually though
				for (cache_t::iterator i = m_read_pieces.begin()
					, end(m_read_pieces.end()); i != end; ++i)
					free_piece(*i, l);
#endif

				m_pieces.clear();
				m_read_pieces.clear();
				// release the io_service to allow the run() call to return
				// we do this once we stop posting new callbacks to it.
				m_work.reset();
				return;
			}

			disk_io_job j;

			if (!m_jobs.empty())
			{
				// we have a job in the job queue. If it's
				// a read operation and we are allowed to
				// reorder jobs, sort it into the read job
				// list and continue, otherwise just pop it
				// and use it later
				j = m_jobs.front();
				m_jobs.pop_front();
				if (j.action == disk_io_job::write)
				{
					TORRENT_ASSERT(m_queue_buffer_size >= j.buffer_size);
					m_queue_buffer_size -= j.buffer_size;
				}

				jl.unlock();

				bool defer = false;

				if (is_read_operation(j))
				{
					defer = true;

					// at this point the operation we're looking
					// at is a read operation. If this read operation
					// can be fully satisfied by the read cache, handle
					// it immediately
					if (m_settings.use_read_cache)
					{
#ifdef TORRENT_DISK_STATS
						m_log << log_time() << " check_cache_hit" << std::endl;
#endif
						// unfortunately we need to lock the cache
						// if the cache querying function would be
						// made asyncronous, this would not be
						// necessary anymore
						mutex_t::scoped_lock l(m_piece_mutex);
						cache_t::iterator p
							= find_cached_piece(m_read_pieces, j, l);
				
						// if it's a cache hit, process the job immediately
						if (p != m_read_pieces.end() && is_cache_hit(p, j, l))
							defer = false;
					}
				}

				TORRENT_ASSERT(j.offset >= 0);
				if (m_settings.allow_reordered_disk_operations && defer)
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " sorting_job" << std::endl;
#endif
					size_type phys_off = j.storage->physical_offset(j.piece, j.offset);
					need_update_elevator_pos = need_update_elevator_pos || sorted_read_jobs.empty();
					sorted_read_jobs.insert(std::pair<size_type, disk_io_job>(phys_off, j));
					continue;
				}
			}
			else
			{
				// the job queue is empty, pick the next read job
				// from the sorted job list. So we don't need the
				// job queue lock anymore
				jl.unlock();

				TORRENT_ASSERT(!sorted_read_jobs.empty());

				// if sorted_read_jobs used to be empty,
				// we need to update the elevator position
				if (need_update_elevator_pos)
				{
					elevator_job_pos = sorted_read_jobs.lower_bound(last_elevator_pos);
					need_update_elevator_pos = false;
				}

				// if we've reached the end, change the elevator direction
				if (elevator_job_pos == sorted_read_jobs.end())
				{
					elevator_direction = -1;
					--elevator_job_pos;
				}

				j = elevator_job_pos->second;
				read_jobs_t::iterator to_erase = elevator_job_pos;

				// if we've reached the begining of the sorted list,
				// change the elvator direction
				if (elevator_job_pos == sorted_read_jobs.begin())
					elevator_direction = 1;

				// move the elevator before erasing the job we're processing
				// to keep the iterator valid
				if (elevator_direction > 0) ++elevator_job_pos;
				else --elevator_job_pos;

				TORRENT_ASSERT(to_erase != elevator_job_pos);
				last_elevator_pos = to_erase->first;
				sorted_read_jobs.erase(to_erase);
			}

			// if there's a buffer in this job, it will be freed
			// when this holder is destructed, unless it has been
			// released.
			disk_buffer_holder holder(*this
				, operation_has_buffer(j) ? j.buffer : 0);
  
			bool post = false;
			if (m_queue_buffer_size + j.buffer_size >= m_settings.max_queued_disk_bytes
				&& m_queue_buffer_size < m_settings.max_queued_disk_bytes
				&& m_queue_callback
				&& m_settings.max_queued_disk_bytes > 0)
			{
				// we just dropped below the high watermark of number of bytes
				// queued for writing to the disk. Notify the session so that it
				// can trigger all the connections waiting for this event
				post = true;
			}

			if (post) m_ios.post(m_queue_callback);

			flush_expired_pieces();

			int ret = 0;

			TORRENT_ASSERT(j.storage
				|| j.action == disk_io_job::abort_thread
				|| j.action == disk_io_job::update_settings);
#ifdef TORRENT_DISK_STATS
			ptime start = time_now();
#endif
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif

			if (j.storage && j.storage->get_storage_impl()->m_settings == 0)
				j.storage->get_storage_impl()->m_settings = &m_settings;

			switch (j.action)
			{
				case disk_io_job::update_settings:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " update_settings " << std::endl;
#endif
					TORRENT_ASSERT(j.buffer);
					session_settings const& s = *((session_settings*)j.buffer);
					TORRENT_ASSERT(s.cache_size >= 0);
					TORRENT_ASSERT(s.cache_expiry > 0);

#if defined TORRENT_WINDOWS
					if (m_settings.low_prio_disk != s.low_prio_disk)
					{
						m_file_pool.set_low_prio_io(s.low_prio_disk);
						// we need to close all files, since the prio
						// only takes affect when files are opened
						m_file_pool.release(0);
					}
#endif
					m_settings = s;
					m_file_pool.resize(m_settings.file_pool_size);
#if defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
					setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_THREAD
						, m_settings.low_prio_disk ? IOPOL_THROTTLE : IOPOL_DEFAULT);
#elif defined IOPRIO_WHO_PROCESS
					syscall(ioprio_set, IOPRIO_WHO_PROCESS, getpid());
#endif
					break;
				}
				case disk_io_job::abort_torrent:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " abort_torrent " << std::endl;
#endif
					mutex_t::scoped_lock jl(m_queue_mutex);
					for (std::list<disk_io_job>::iterator i = m_jobs.begin();
						i != m_jobs.end();)
					{
						if (i->storage != j.storage)
						{
							++i;
							continue;
						}
						if (should_cancel_on_abort(*i))
						{
							if (i->action == disk_io_job::write)
							{
								TORRENT_ASSERT(m_queue_buffer_size >= i->buffer_size);
								m_queue_buffer_size -= i->buffer_size;
							}
							post_callback(i->callback, *i, -3);
							m_jobs.erase(i++);
							continue;
						}
						++i;
					}
					// now clear all the read jobs
					for (read_jobs_t::iterator i = sorted_read_jobs.begin();
						i != sorted_read_jobs.end();)
					{
						if (i->second.storage != j.storage)
						{
							++i;
							continue;
						}
						post_callback(i->second.callback, i->second, -3);
						if (elevator_job_pos == i) ++elevator_job_pos;
						sorted_read_jobs.erase(i++);
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
					release_memory();
					break;
				}
				case disk_io_job::abort_thread:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " abort_thread " << std::endl;
#endif
					// clear all read jobs
					mutex_t::scoped_lock jl(m_queue_mutex);

					for (std::list<disk_io_job>::iterator i = m_jobs.begin();
						i != m_jobs.end();)
					{
						if (should_cancel_on_abort(*i))
						{
							if (i->action == disk_io_job::write)
							{
								TORRENT_ASSERT(m_queue_buffer_size >= i->buffer_size);
								m_queue_buffer_size -= i->buffer_size;
							}
							post_callback(i->callback, *i, -3);
							m_jobs.erase(i++);
							continue;
						}
						++i;
					}
					jl.unlock();

					for (read_jobs_t::iterator i = sorted_read_jobs.begin();
						i != sorted_read_jobs.end();)
					{
						if (i->second.storage != j.storage)
						{
							++i;
							continue;
						}
						post_callback(i->second.callback, i->second, -3);
						if (elevator_job_pos == i) ++elevator_job_pos;
						sorted_read_jobs.erase(i++);
  					}
					m_abort = true;
					break;
				}
				case disk_io_job::read_and_hash:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " read_and_hash " << j.buffer_size << std::endl;
#endif
					INVARIANT_CHECK;
					TORRENT_ASSERT(j.buffer == 0);
					j.buffer = allocate_buffer("send buffer");
					TORRENT_ASSERT(j.buffer_size <= m_block_size);
					if (j.buffer == 0)
					{
						ret = -1;
#if BOOST_VERSION == 103500
						j.error = error_code(boost::system::posix_error::not_enough_memory
							, get_posix_category());
#elif BOOST_VERSION > 103500
						j.error = error_code(boost::system::errc::not_enough_memory
							, get_posix_category());
#else
						j.error = asio::error::no_memory;
#endif
						j.str.clear();
						break;
					}

					disk_buffer_holder read_holder(*this, j.buffer);

					// read the entire piece and verify the piece hash
					// since we need to check the hash, this function
					// will ignore the cache size limit (at least for
					// reading and hashing, not for keeping it around)
					sha1_hash h;
					ret = read_piece_from_cache_and_hash(j, h);

					// -2 means there's no space in the read cache
					// or that the read cache is disabled
					if (ret == -1)
					{
						test_error(j);
						break;
					}
					if (!m_settings.disable_hash_checks)
						ret = (j.storage->info()->hash_for_piece(j.piece) == h)?ret:-3;
					if (ret == -3)
					{
						j.storage->mark_failed(j.piece);
						j.error = errors::failed_hash_check;
						j.str.clear();
						j.buffer = 0;
						break;
					}

					TORRENT_ASSERT(j.buffer == read_holder.get());
					read_holder.release();
#if TORRENT_DISK_STATS
					rename_buffer(j.buffer, "released send buffer");
#endif
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
					j.buffer = allocate_buffer("send buffer");
					TORRENT_ASSERT(j.buffer_size <= m_block_size);
					if (j.buffer == 0)
					{
						ret = -1;
#if BOOST_VERSION == 103500
						j.error = error_code(boost::system::posix_error::not_enough_memory
							, get_posix_category());
#elif BOOST_VERSION > 103500
						j.error = error_code(boost::system::errc::not_enough_memory
							, get_posix_category());
#else
						j.error = asio::error::no_memory;
#endif
						j.str.clear();
						break;
					}

					disk_buffer_holder read_holder(*this, j.buffer);

					ret = try_read_from_cache(j);

					// -2 means there's no space in the read cache
					// or that the read cache is disabled
					if (ret == -1)
					{
						j.buffer = 0;
						test_error(j);
						break;
					}
					else if (ret == -2)
					{
						file::iovec_t b = { j.buffer, j.buffer_size };
						ret = j.storage->read_impl(&b, j.piece, j.offset, 1);
						if (ret < 0)
						{
							test_error(j);
							break;
						}
						if (ret != j.buffer_size)
						{
							// this means the file wasn't big enough for this read
							j.buffer = 0;
							j.error = errors::file_too_short;
							j.error_file.clear();
							j.str.clear();
							ret = -1;
							break;
						}
						++m_cache_stats.blocks_read;
					}
					TORRENT_ASSERT(j.buffer == read_holder.get());
					read_holder.release();
#if TORRENT_DISK_STATS
					rename_buffer(j.buffer, "released send buffer");
#endif
					break;
				}
				case disk_io_job::write:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " write " << j.buffer_size << std::endl;
#endif
					mutex_t::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					if (in_use() >= m_settings.cache_size)
					{
						flush_cache_blocks(l, in_use() - m_settings.cache_size + 1, m_read_pieces.end());
						if (test_error(j)) break;
					}

					cache_t::iterator p
						= find_cached_piece(m_pieces, j, l);
					int block = j.offset / m_block_size;
					TORRENT_ASSERT(j.buffer);
					TORRENT_ASSERT(j.buffer_size <= m_block_size);
					if (p != m_pieces.end())
					{
						TORRENT_ASSERT(p->blocks[block].buf == 0);
						if (p->blocks[block].buf)
						{
							free_buffer(p->blocks[block].buf);
							--m_cache_stats.cache_size;
							--p->num_blocks;
						}
						p->blocks[block].buf = j.buffer;
						p->blocks[block].callback.swap(j.callback);
#ifdef TORRENT_DISK_STATS
						rename_buffer(j.buffer, "write cache");
#endif
						++m_cache_stats.cache_size;
						++p->num_blocks;
						p->last_use = time_now();
						// we might just have created a contiguous range
						// that meets the requirement to be flushed. try it
						flush_contiguous_blocks(p, l, m_settings.write_cache_line_size);
						test_error(j);
					}
					else
					{
						if (cache_block(j, j.callback, l) < 0)
						{
							l.unlock();
							file::iovec_t iov = {j.buffer, j.buffer_size};
							ret = j.storage->write_impl(&iov, j.piece, j.offset, 1);
							l.lock();
							if (ret < 0)
							{
								test_error(j);
								break;
							}
							// we successfully wrote the block. Ignore previous errors
							j.storage->clear_error();
							break;
						}
					}
					// we've now inserted the buffer
					// in the cache, we should not
					// free it at the end
					holder.release();

					if (in_use() > m_settings.cache_size)
					{
						flush_cache_blocks(l, in_use() - m_settings.cache_size, m_read_pieces.end());
						test_error(j);
					}

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
					if (m_settings.disable_hash_checks)
					{
						ret = 0;
						break;
					}
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
							flush_range(i, 0, INT_MAX, l);
							i = m_pieces.erase(i);
						}
						else
						{
							++i;
						}
					}
					l.unlock();
					release_memory();

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
					release_memory();
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
						m_pieces.begin(), m_pieces.end(), boost::bind(&cached_piece_entry::storage, _1) == j.storage);

					for (cache_t::iterator k = i; k != m_pieces.end(); ++k)
					{
						torrent_info const& ti = *k->storage->info();
						int blocks_in_piece = (ti.piece_size(k->piece) + m_block_size - 1) / m_block_size;
						for (int j = 0; j < blocks_in_piece; ++j)
						{
							if (k->blocks[j].buf == 0) continue;
							free_buffer(k->blocks[j].buf);
							k->blocks[j].buf = 0;
							--m_cache_stats.cache_size;
						}
					}
					m_pieces.erase(i, m_pieces.end());
					l.unlock();
					release_memory();

					ret = j.storage->delete_files_impl();
					if (ret != 0) test_error(j);
					break;
				}
				case disk_io_job::check_fastresume:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " check_fastresume" << std::endl;
#endif
					lazy_entry const* rd = (lazy_entry const*)j.buffer;
					TORRENT_ASSERT(rd != 0);
					ret = j.storage->check_fastresume(*rd, j.error);
					break;
				}
				case disk_io_job::check_files:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " check_files" << std::endl;
#endif
					int piece_size = j.storage->info()->piece_length();
					for (int processed = 0; processed < 4 * 1024 * 1024; processed += piece_size)
					{
						ptime now = time_now_hires();
						TORRENT_ASSERT(now >= m_last_file_check);
						// this happens sometimes on windows for some reason
						if (now < m_last_file_check) now = m_last_file_check;

#if BOOST_VERSION > 103600
						if (now - m_last_file_check < milliseconds(m_settings.file_checks_delay_per_block))
						{
							int sleep_time = m_settings.file_checks_delay_per_block
								* (piece_size / (16 * 1024))
								- total_milliseconds(now - m_last_file_check);
							if (sleep_time < 0) sleep_time = 0;
							TORRENT_ASSERT(sleep_time < 5 * 1000);
	
							boost::thread::sleep(boost::get_system_time()
								+ boost::posix_time::milliseconds(sleep_time));
						}
						m_last_file_check = time_now_hires();
#endif

						if (m_waiting_to_shutdown) break;

						ret = j.storage->check_files(j.piece, j.offset, j.error);

#ifndef BOOST_NO_EXCEPTIONS
						try {
#endif
							TORRENT_ASSERT(j.callback);
							if (j.callback && ret == piece_manager::need_full_check)
								post_callback(j.callback, j, ret);
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
					TORRENT_ASSERT(ret != -2 || j.error);
					
					// if the check is not done, add it at the end of the job queue
					if (ret == piece_manager::need_full_check)
					{
						// offset needs to be reset to 0 so that the disk
						// job sorting can be done correctly
						j.offset = 0;
						add_job(j, j.callback);
						continue;
					}
					break;
				}
				case disk_io_job::save_resume_data:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " save_resume_data" << std::endl;
#endif
					j.resume_data.reset(new entry(entry::dictionary_t));
					j.storage->write_resume_data(*j.resume_data);
					ret = 0;
					break;
				}
				case disk_io_job::rename_file:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " rename_file" << std::endl;
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
			}
			catch (std::exception& e)
			{
				ret = -1;
				try
				{
					j.str = e.what();
				}
				catch (std::exception&) {}
			}
#endif

//			if (!j.callback) std::cerr << "DISK THREAD: no callback specified" << std::endl;
//			else std::cerr << "DISK THREAD: invoking callback" << std::endl;
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				TORRENT_ASSERT(ret != -2 || j.error
					|| j.action == disk_io_job::hash);
#if TORRENT_DISK_STATS
				if ((j.action == disk_io_job::read || j.action == disk_io_job::read_and_hash)
					&& j.buffer != 0)
					rename_buffer(j.buffer, "posted send buffer");
#endif
				post_callback(j.callback, j, ret);
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

