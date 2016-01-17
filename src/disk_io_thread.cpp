/*

Copyright (c) 2007-2016, Arvid Norberg
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
#include "libtorrent/error.hpp"
#include "libtorrent/file_pool.hpp"
#include <boost/scoped_array.hpp>
#include <boost/bind.hpp>

#include "libtorrent/time.hpp"

#if TORRENT_USE_MLOCK && !defined TORRENT_WINDOWS
#include <sys/mman.h>
#endif

#ifdef TORRENT_BSD
#include <sys/sysctl.h>
#endif

#if TORRENT_USE_RLIMIT
#include <sys/resource.h>
#endif

#ifdef TORRENT_LINUX
#include <linux/unistd.h>
#endif

namespace libtorrent
{
	bool should_cancel_on_abort(disk_io_job const& j);
	bool is_read_operation(disk_io_job const& j);
	bool operation_has_buffer(disk_io_job const& j);

// ------- disk_io_thread ------

	disk_io_thread::disk_io_thread(io_service& ios
		, boost::function<void()> const& queue_callback
		, file_pool& fp
		, int block_size)
		: disk_buffer_pool(block_size)
		, m_abort(false)
		, m_waiting_to_shutdown(false)
		, m_queue_buffer_size(0)
		, m_last_file_check(time_now_hires())
		, m_last_stats_flip(time_now())
		, m_physical_ram(0)
		, m_exceeded_write_queue(false)
		, m_ios(ios)
		, m_queue_callback(queue_callback)
		, m_work(io_service::work(m_ios))
		, m_file_pool(fp)
#if TORRENT_USE_ASSERTS
		, m_magic(0x1337)
#endif
		, m_disk_io_thread(boost::bind(&disk_io_thread::thread_fun, this))
	{
		// don't do anything in here. Essentially all members
		// of this object are owned by the newly created thread.
		// initialize stuff in thread_fun().
	}

	disk_io_thread::~disk_io_thread()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if TORRENT_USE_ASSERTS
		m_magic = 0xdead;
#endif
		TORRENT_ASSERT(m_abort == true);
	}

	void disk_io_thread::abort()
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		mutex::scoped_lock l(m_queue_mutex);
		disk_io_job j;
		m_waiting_to_shutdown = true;
		j.action = disk_io_job::abort_thread;
		j.start_time = time_now_hires();

		TORRENT_ASSERT(l.locked());
		m_jobs.insert(m_jobs.begin(), j);
		m_signal.signal(l);
	}

	void disk_io_thread::join()
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		m_disk_io_thread.join();
		mutex::scoped_lock l(m_queue_mutex);
		TORRENT_ASSERT(m_abort == true);
		m_jobs.clear();
	}

	bool disk_io_thread::can_write() const
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		mutex::scoped_lock l(m_queue_mutex);
		return !m_exceeded_write_queue;
	}

	void disk_io_thread::flip_stats(ptime now)
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		// calling mean() will actually reset the accumulators
		m_cache_stats.average_queue_time = m_queue_time.mean();
		m_cache_stats.average_read_time = m_read_time.mean();
		m_cache_stats.average_write_time = m_write_time.mean();
		m_cache_stats.average_hash_time = m_hash_time.mean();
		m_cache_stats.average_job_time = m_job_time.mean();
		m_cache_stats.average_sort_time = m_sort_time.mean();

		m_last_stats_flip = now;
	}

	void disk_io_thread::get_cache_info(sha1_hash const& ih, std::vector<cached_piece_info>& ret) const
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		mutex::scoped_lock l(m_piece_mutex);
		ret.clear();
		ret.reserve(m_pieces.size());
		for (cache_t::const_iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i)
		{
			torrent_info const& ti = *i->storage->info();
			if (ti.info_hash() != ih) continue;
			cached_piece_info info;
			info.next_to_hash = i->next_block_to_hash;
			info.piece = i->piece;
			info.last_use = i->expire;
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
			info.next_to_hash = i->next_block_to_hash;
			info.piece = i->piece;
			info.last_use = i->expire;
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
		mutex::scoped_lock l(m_piece_mutex);
		m_cache_stats.total_used_buffers = in_use();
		m_cache_stats.queued_bytes = m_queue_buffer_size;

		cache_status ret = m_cache_stats;

		ret.job_queue_length = m_jobs.size() + m_sorted_read_jobs.size();
		ret.read_queue_size = m_sorted_read_jobs.size();

		return ret;
	}

	// aborts read operations
	void disk_io_thread::stop(boost::intrusive_ptr<piece_manager> s)
	{
		mutex::scoped_lock l(m_queue_mutex);
		// read jobs are aborted, write and move jobs are syncronized
		for (std::deque<disk_io_job>::iterator i = m_jobs.begin();
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
				post_callback(*i, -3);
				i = m_jobs.erase(i);
				continue;
			}
			++i;
		}
		disk_io_job j;
		j.action = disk_io_job::abort_torrent;
		j.storage = s;
		add_job(j, l);
	}

	struct update_last_use
	{
		update_last_use(int exp): expire(exp) {}
		void operator()(disk_io_thread::cached_piece_entry& p)
		{
			TORRENT_ASSERT(p.storage);
			p.expire = time_now() + seconds(expire);
		}
		int expire;
	};

	disk_io_thread::cache_piece_index_t::iterator disk_io_thread::find_cached_piece(
		disk_io_thread::cache_t& cache
		, disk_io_job const& j, mutex::scoped_lock& l)
	{
		cache_piece_index_t& idx = cache.get<0>();
		cache_piece_index_t::iterator i
			= idx.find(std::pair<void*, int>(j.storage.get(), j.piece));
		TORRENT_ASSERT(i == idx.end() || (i->storage == j.storage && i->piece == j.piece));
		return i;
	}
	
	void disk_io_thread::flush_expired_pieces()
	{
		ptime now = time_now();

		mutex::scoped_lock l(m_piece_mutex);

		INVARIANT_CHECK;
		// flush write cache
		cache_lru_index_t& widx = m_pieces.get<1>();
		cache_lru_index_t::iterator i = widx.begin();
		time_duration cut_off = seconds(m_settings.cache_expiry);
		while (i != widx.end() && now - i->expire > cut_off)
		{
			TORRENT_ASSERT(i->storage);
			flush_range(const_cast<cached_piece_entry&>(*i), 0, INT_MAX, l);
			TORRENT_ASSERT(i->num_blocks == 0);

			// we want to keep the piece in here to have an accurate
			// number for next_block_to_hash, if we're in avoid_readback mode

			bool erase = m_settings.disk_cache_algorithm != session_settings::avoid_readback;
			if (!erase)
			{
				// however, if we've already hashed the whole piece, in-order
				// there's no need to keep it around
				int piece_size = i->storage->info()->piece_size(i->piece);
				int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
				erase = i->next_block_to_hash == blocks_in_piece;
			}

			if (erase) widx.erase(i++);
			else ++i;
		}

		if (m_settings.explicit_read_cache) return;

		// flush read cache
		std::vector<char*> bufs;
		cache_lru_index_t& ridx = m_read_pieces.get<1>();
		i = ridx.begin();
		while (i != ridx.end() && now - i->expire > cut_off)
		{
			drain_piece_bufs(const_cast<cached_piece_entry&>(*i), bufs, l);
			ridx.erase(i++);
		}
		if (!bufs.empty()) free_multiple_buffers(&bufs[0], bufs.size());
	}

	int disk_io_thread::drain_piece_bufs(cached_piece_entry& p, std::vector<char*>& buf
		, mutex::scoped_lock& l)
	{
		int piece_size = p.storage->info()->piece_size(p.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		int ret = 0;

		for (int i = 0; i < blocks_in_piece; ++i)
		{
			if (p.blocks[i].buf == 0) continue;
			buf.push_back(p.blocks[i].buf);
			++ret;
			p.blocks[i].buf = 0;
			--p.num_blocks;
			--m_cache_stats.cache_size;
			--m_cache_stats.read_cache_size;
		}
		return ret;
	}

	// returns the number of blocks that were freed
	int disk_io_thread::free_piece(cached_piece_entry& p, mutex::scoped_lock& l)
	{
		int piece_size = p.storage->info()->piece_size(p.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		int ret = 0;

		// build a vector of all the buffers we need to free
		// and free them all in one go
		std::vector<char*> buffers;
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			if (p.blocks[i].buf == 0) continue;
			buffers.push_back(p.blocks[i].buf);
			++ret;
			p.blocks[i].buf = 0;
			--p.num_blocks;
			--m_cache_stats.cache_size;
			--m_cache_stats.read_cache_size;
		}
		if (!buffers.empty()) free_multiple_buffers(&buffers[0], buffers.size());
		return ret;
	}

	// returns the number of blocks that were freed
	int disk_io_thread::clear_oldest_read_piece(
		int num_blocks, ignore_t ignore, mutex::scoped_lock& l)
	{
		INVARIANT_CHECK;

		cache_lru_index_t& idx = m_read_pieces.get<1>();
		if (idx.empty()) return 0;

		cache_lru_index_t::iterator i = idx.begin();
		if (i->piece == ignore.piece && i->storage == ignore.storage)
		{
			++i;
			if (i == idx.end()) return 0;
		}

		// don't replace an entry that hasn't expired yet
		if (time_now() < i->expire) return 0;
		int blocks = 0;

		// build a vector of all the buffers we need to free
		// and free them all in one go
		std::vector<char*> buffers;
		if (num_blocks >= i->num_blocks)
		{
			blocks = drain_piece_bufs(const_cast<cached_piece_entry&>(*i), buffers, l);
		}
		else
		{
			// delete blocks from the start and from the end
			// until num_blocks have been freed
			int end = (i->storage->info()->piece_size(i->piece) + m_block_size - 1) / m_block_size - 1;
			int start = 0;

			while (num_blocks)
			{
				// if we have a volatile read cache, only clear
				// from the end, since we're already clearing
				// from the start as blocks are read
				if (!m_settings.volatile_read_cache)
				{
					while (i->blocks[start].buf == 0 && start <= end) ++start;
					if (start > end) break;
					buffers.push_back(i->blocks[start].buf);
					i->blocks[start].buf = 0;
					++blocks;
					--const_cast<cached_piece_entry&>(*i).num_blocks;
					--m_cache_stats.cache_size;
					--m_cache_stats.read_cache_size;
					--num_blocks;
					if (!num_blocks) break;
				}

				while (i->blocks[end].buf == 0 && start <= end) --end;
				if (start > end) break;
				buffers.push_back(i->blocks[end].buf);
				i->blocks[end].buf = 0;
				++blocks;
				--const_cast<cached_piece_entry&>(*i).num_blocks;
				--m_cache_stats.cache_size;
				--m_cache_stats.read_cache_size;
				--num_blocks;
			}
		}
		if (i->num_blocks == 0) idx.erase(i);

		if (!buffers.empty()) free_multiple_buffers(&buffers[0], buffers.size());
		return blocks;
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

	int disk_io_thread::flush_contiguous_blocks(cached_piece_entry& p
		, mutex::scoped_lock& l, int lower_limit, bool avoid_readback)
	{
		// first find the largest range of contiguous  blocks
		int len = 0;
		int current = 0;
		int pos = 0;
		int start = 0;
		int blocks_in_piece = (p.storage->info()->piece_size(p.piece)
			+ m_block_size - 1) / m_block_size;

		if (avoid_readback)
		{
			start = p.next_block_to_hash;
			for (int i = p.next_block_to_hash; i < blocks_in_piece; ++i)
			{
				if (p.blocks[i].buf) ++current;
				else break;
			}
		}
		else
		{
			for (int i = 0; i < blocks_in_piece; ++i)
			{
				if (p.blocks[i].buf) ++current;
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
		}
		if (current > len)
		{
			len = current;
			pos = start;
		}

		if (len < lower_limit || len <= 0) return 0;
		len = flush_range(p, pos, pos + len, l);
		return len;
	}

	bool cmp_contiguous(disk_io_thread::cached_piece_entry const& lhs
		, disk_io_thread::cached_piece_entry const& rhs)
	{
		return lhs.num_contiguous_blocks < rhs.num_contiguous_blocks;
	}

	// flushes 'blocks' blocks from the cache
	int disk_io_thread::flush_cache_blocks(mutex::scoped_lock& l
		, int blocks, ignore_t ignore, int options)
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

		if (blocks == 0) return ret;

		if (options & dont_flush_write_blocks) return ret;

		// if we don't have any blocks in the cache, no need to go look for any
		if (m_cache_stats.cache_size == 0) return ret;

		if (m_settings.disk_cache_algorithm == session_settings::lru)
		{
			cache_lru_index_t& idx = m_pieces.get<1>();
			while (blocks > 0)
			{
				cache_lru_index_t::iterator i = idx.begin();
				if (i == idx.end()) return ret;
				tmp = flush_range(const_cast<cached_piece_entry&>(*i), 0, INT_MAX, l);
				idx.erase(i);
				blocks -= tmp;
				ret += tmp;
			}
		}
		else if (m_settings.disk_cache_algorithm == session_settings::largest_contiguous)
		{
			cache_lru_index_t& idx = m_pieces.get<1>();
			while (blocks > 0)
			{
				cache_lru_index_t::iterator i = std::max_element(idx.begin(), idx.end(), &cmp_contiguous);
				if (i == idx.end()) return ret;
				tmp = flush_contiguous_blocks(const_cast<cached_piece_entry&>(*i), l);
				if (i->num_blocks == 0) idx.erase(i);
				blocks -= tmp;
				ret += tmp;
			}
		}
		else if (m_settings.disk_cache_algorithm == session_settings::avoid_readback)
		{
			cache_lru_index_t& idx = m_pieces.get<1>();
			for (cache_lru_index_t::iterator i = idx.begin(); i != idx.end();)
			{
				cached_piece_entry& p = const_cast<cached_piece_entry&>(*i);
				cache_lru_index_t::iterator piece = i;
				++i;

				if (!piece->blocks[p.next_block_to_hash].buf) continue;
				int piece_size = p.storage->info()->piece_size(p.piece);
				int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
				int start = p.next_block_to_hash;
				int end = start + 1;
				while (end < blocks_in_piece && p.blocks[end].buf) ++end;
				tmp = flush_range(p, start, end, l);
				p.num_contiguous_blocks = contiguous_blocks(p);
				if (p.num_blocks == 0 && p.next_block_to_hash == blocks_in_piece)
					idx.erase(piece);
				blocks -= tmp;
				ret += tmp;
				if (blocks <= 0) break;
			}

			// if we still need to flush blocks, flush the largest contiguous blocks
			// regardless of if we'll have to read them back later
			while (blocks > 0)
			{
				cache_lru_index_t::iterator i = std::max_element(idx.begin(), idx.end(), &cmp_contiguous);
				if (i == idx.end() || i->num_blocks == 0) return ret;
				tmp = flush_contiguous_blocks(const_cast<cached_piece_entry&>(*i), l);
				// at this point, we will for sure need a read-back for
				// this piece anyway. We might as well save some time looping
				// over the disk cache by deleting the entry
				if (i->num_blocks == 0) idx.erase(i);
				blocks -= tmp;
				ret += tmp;
			}
		}
		return ret;
	}

	int disk_io_thread::flush_range(cached_piece_entry& p
		, int start, int end, mutex::scoped_lock& l)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(start < end);

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
		int num_write_calls = 0;
		ptime write_start = time_now_hires();
		for (int i = start; i <= end; ++i)
		{
			if (i == end || p.blocks[i].buf == 0)
			{
				if (buffer_size == 0) continue;
			
				TORRENT_ASSERT(buffer_size <= i * m_block_size);
				l.unlock();
				if (iov)
				{
					int ret = p.storage->write_impl(iov, p.piece, (std::min)(
						i * m_block_size, piece_size) - buffer_size, iov_counter);
					iov_counter = 0;
					if (ret > 0) ++num_write_calls;
				}
				else
				{
					TORRENT_ASSERT(buf);
					file::iovec_t b = { buf.get(), size_t(buffer_size) };
					int ret = p.storage->write_impl(&b, p.piece, (std::min)(
						i * m_block_size, piece_size) - buffer_size, 1);
					if (ret > 0) ++num_write_calls;
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
			if (iov)
			{
				TORRENT_ASSERT(!buf);
				iov[iov_counter].iov_base = p.blocks[i].buf;
				iov[iov_counter].iov_len = block_size;
				++iov_counter;
			}
			else
			{
				TORRENT_ASSERT(buf);
				TORRENT_ASSERT(iov == 0);
				std::memcpy(buf.get() + offset, p.blocks[i].buf, block_size);
				offset += m_block_size;
			}
			buffer_size += block_size;
			TORRENT_ASSERT(p.num_blocks > 0);
			--p.num_blocks;
			++m_cache_stats.blocks_written;
			--m_cache_stats.cache_size;
			if (i == p.next_block_to_hash) ++p.next_block_to_hash;
		}

		ptime done = time_now_hires();

		int ret = 0;
		disk_io_job j;
		j.storage = p.storage;
		j.action = disk_io_job::write;
		j.buffer = 0;
		j.piece = p.piece;
		test_error(j);
		std::vector<char*> buffers;
		for (int i = start; i < end; ++i)
		{
			if (p.blocks[i].buf == 0) continue;
			j.buffer_size = (std::min)(piece_size - i * m_block_size, m_block_size);
			int result = j.error ? -1 : j.buffer_size;
			j.offset = i * m_block_size;
			j.callback = p.blocks[i].callback;
			buffers.push_back(p.blocks[i].buf);
			post_callback(j, result);
			p.blocks[i].callback.clear();
			p.blocks[i].buf = 0;
			++ret;
		}
		if (!buffers.empty()) free_multiple_buffers(&buffers[0], buffers.size());

		if (num_write_calls > 0)
		{
			m_write_time.add_sample(total_microseconds(done - write_start) / num_write_calls);
			m_cache_stats.cumulative_write_time += total_milliseconds(done - write_start);
		}
		if (ret > 0)
			p.num_contiguous_blocks = contiguous_blocks(p);

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
		, int cache_expire
		, mutex::scoped_lock& l)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(find_cached_piece(m_pieces, j, l) == m_pieces.end());
		TORRENT_ASSERT((j.offset & (m_block_size-1)) == 0);
		TORRENT_ASSERT(j.cache_min_time >= 0);
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
		p.expire = time_now() + seconds(j.cache_min_time);
		p.num_blocks = 1;
		p.num_contiguous_blocks = 1;
		p.next_block_to_hash = 0;
		p.blocks.reset(new (std::nothrow) cached_block_entry[blocks_in_piece]);
		if (!p.blocks) return -1;
		int block = j.offset / m_block_size;
//		std::cerr << " adding cache entry for p: " << j.piece << " block: " << block << " cached_blocks: " << m_cache_stats.cache_size << std::endl;
		p.blocks[block].buf = j.buffer;
		p.blocks[block].callback.swap(handler);
		++m_cache_stats.cache_size;
		cache_lru_index_t& idx = m_pieces.get<1>();
		TORRENT_ASSERT(p.storage);
		idx.insert(p);
		return 0;
	}

	// fills a piece with data from disk, returns the total number of bytes
	// read or -1 if there was an error
	int disk_io_thread::read_into_piece(cached_piece_entry& p, int start_block
		, int options, int num_blocks, mutex::scoped_lock& l)
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
			// free it and allocate a new one
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
			file::iovec_t b = { buf.get(), size_t(buffer_size) };
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
				char msg[70];
				snprintf(msg, sizeof(msg), "reading p: %d b: %d s: %d (read: %d)", p.piece, start_block, buffer_size, ret);
				p.storage->get_storage_impl()->set_error(msg, errors::file_too_short);
				free_piece(p, l);
				return -1;
			}
		
			int offset = 0;
			for (int i = 0; i < iov_counter; ++i)
			{
				TORRENT_ASSERT(iov[i].iov_base);
				TORRENT_ASSERT(iov[i].iov_len > 0);
				TORRENT_ASSERT(int(offset + iov[i].iov_len) <= buffer_size);
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
				char msg[70];
				snprintf(msg, sizeof(msg), "reading p: %d b: %d s: %d (read: %d)", p.piece, start_block, buffer_size, ret);
				p.storage->get_storage_impl()->set_error(msg, errors::file_too_short);
				free_piece(p, l);
				return -1;
			}
		}

		TORRENT_ASSERT(ret == buffer_size);
		return ret;
	}

	// returns -1 on read error, -2 if there isn't any space in the cache
	// or the number of bytes read
	int disk_io_thread::cache_read_block(disk_io_job const& j, mutex::scoped_lock& l)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(j.cache_min_time >= 0);

		// this function will create a new cached_piece_entry
		// and requires that it doesn't already exist
		cache_piece_index_t& idx = m_read_pieces.get<0>();
		TORRENT_ASSERT(find_cached_piece(m_read_pieces, j, l) == idx.end());

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		int start_block = j.offset / m_block_size;

		int blocks_to_read = blocks_in_piece - start_block;
		blocks_to_read = (std::min)(blocks_to_read, (std::max)((m_settings.cache_size
			+ m_cache_stats.read_cache_size - in_use())/2, 3));
		blocks_to_read = (std::min)(blocks_to_read, m_settings.read_cache_line_size);
		if (j.max_cache_line > 0) blocks_to_read = (std::min)(blocks_to_read, j.max_cache_line);

		if (in_use() + blocks_to_read > m_settings.cache_size)
		{
			int clear = in_use() + blocks_to_read - m_settings.cache_size;
			if (flush_cache_blocks(l, clear, ignore_t(j.piece, j.storage.get())
				, dont_flush_write_blocks) < clear)
				return -2;
		}

		cached_piece_entry p;
		p.piece = j.piece;
		p.storage = j.storage;
		p.expire = time_now() + seconds(j.cache_min_time);
		p.num_blocks = 0;
		p.num_contiguous_blocks = 0;
		p.next_block_to_hash = 0;
		p.blocks.reset(new (std::nothrow) cached_block_entry[blocks_in_piece]);
		if (!p.blocks) return -1;

		int ret = read_into_piece(p, start_block, 0, blocks_to_read, l);

		TORRENT_ASSERT(p.storage);
		if (ret >= 0) idx.insert(p);

		return ret;
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void disk_io_thread::check_invariant() const
	{
		int cached_write_blocks = 0;
		cache_piece_index_t const& idx = m_pieces.get<0>();
		for (cache_piece_index_t::const_iterator i = idx.begin()
			, end(idx.end()); i != end; ++i)
		{
			cached_piece_entry const& p = *i;
			TORRENT_ASSERT(p.blocks);
//			TORRENT_ASSERT(p.num_contiguous_blocks == contiguous_blocks(p));
			
			TORRENT_ASSERT(p.storage);
			int piece_size = p.storage->info()->piece_size(p.piece);
			int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
			int blocks = 0;
			for (int k = 0; k < blocks_in_piece; ++k)
			{
				if (p.blocks[k].buf)
				{
#if !defined TORRENT_DISABLE_POOL_ALLOCATOR && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
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
#if !defined TORRENT_DISABLE_POOL_ALLOCATOR && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
					TORRENT_ASSERT(is_disk_buffer(p.blocks[k].buf));
#endif
					++blocks;
				}
			}
//			TORRENT_ASSERT(blocks == p.num_blocks);
			cached_read_blocks += blocks;
		}

		TORRENT_ASSERT(cached_read_blocks == m_cache_stats.read_cache_size);
		TORRENT_ASSERT(cached_read_blocks + cached_write_blocks == m_cache_stats.cache_size);

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

	// reads the full piece specified by j into the read cache
	// returns the iterator to it and whether or not it already
	// was in the cache (hit).
	int disk_io_thread::cache_piece(disk_io_job const& j, cache_piece_index_t::iterator& p
		, bool& hit, int options, mutex::scoped_lock& l)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(j.cache_min_time >= 0);

		cache_piece_index_t& idx = m_read_pieces.get<0>();
		p = find_cached_piece(m_read_pieces, j, l);

		hit = true;
		int ret = 0;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		if (p != m_read_pieces.end() && p->num_blocks != blocks_in_piece)
		{
			INVARIANT_CHECK;
			// we have the piece in the cache, but not all of the blocks
			ret = read_into_piece(const_cast<cached_piece_entry&>(*p), 0
				, options, blocks_in_piece, l);
			hit = false;
			if (ret < 0) return ret;
			idx.modify(p, update_last_use(j.cache_min_time));
		}
		else if (p == m_read_pieces.end())
		{
			INVARIANT_CHECK;
			// if the piece cannot be found in the cache,
			// read the whole piece starting at the block
			// we got a request for.

			cached_piece_entry pe;
			pe.piece = j.piece;
			pe.storage = j.storage;
			pe.expire = time_now() + seconds(j.cache_min_time);
			pe.num_blocks = 0;
			pe.num_contiguous_blocks = 0;
			pe.next_block_to_hash = 0;
			pe.blocks.reset(new (std::nothrow) cached_block_entry[blocks_in_piece]);
			if (!pe.blocks) return -1;
			ret = read_into_piece(pe, 0, options, INT_MAX, l);

			hit = false;
			if (ret < 0) return ret;
			TORRENT_ASSERT(pe.storage);
			p = idx.insert(pe).first;
		}
		else
		{
			idx.modify(p, update_last_use(j.cache_min_time));
		}
		TORRENT_ASSERT(!m_read_pieces.empty());
		TORRENT_ASSERT(p->piece == j.piece);
		TORRENT_ASSERT(p->storage == j.storage);
		return ret;
	}

	// cache the entire piece and hash it
	int disk_io_thread::read_piece_from_cache_and_hash(disk_io_job const& j, sha1_hash& h)
	{
		TORRENT_ASSERT(j.buffer);

		TORRENT_ASSERT(j.cache_min_time >= 0);

		mutex::scoped_lock l(m_piece_mutex);

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		if (in_use() + blocks_in_piece >= m_settings.cache_size)
		{
			flush_cache_blocks(l, in_use() - m_settings.cache_size + blocks_in_piece);
		}
	
		cache_piece_index_t::iterator p;
		bool hit;
		int ret = cache_piece(j, p, hit, ignore_cache_size, l);
		if (ret < 0) return ret;

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

		ret = copy_from_piece(const_cast<cached_piece_entry&>(*p), hit, j, l);
		TORRENT_ASSERT(ret > 0);
		if (ret < 0) return ret;
		cache_piece_index_t& idx = m_read_pieces.get<0>();
		if (p->num_blocks == 0) idx.erase(p);
		else idx.modify(p, update_last_use(j.cache_min_time));

		// if read cache is disabled or we exceeded the
		// limit, remove this piece from the cache
		// also, if the piece wasn't in the cache when
		// the function was called, and we're using an
		// explicit read cache, remove it again
		if (in_use() >= m_settings.cache_size
			|| !m_settings.use_read_cache
			|| (m_settings.explicit_read_cache && !hit))
		{
			TORRENT_ASSERT(!m_read_pieces.empty());
			TORRENT_ASSERT(p->piece == j.piece);
			TORRENT_ASSERT(p->storage == j.storage);
			if (p != m_read_pieces.end())
			{
				free_piece(const_cast<cached_piece_entry&>(*p), l);
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
	bool disk_io_thread::is_cache_hit(cached_piece_entry& p
		, disk_io_job const& j, mutex::scoped_lock& l)
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
		if (p.blocks[start_block].buf != 0 && min_blocks_to_read > 1)
			++start_block;

#ifdef TORRENT_DEBUG	
		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		TORRENT_ASSERT(start_block < blocks_in_piece);
#endif

		return p.blocks[start_block].buf != 0;
	}

	int disk_io_thread::copy_from_piece(cached_piece_entry& p, bool& hit
		, disk_io_job const& j, mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(j.buffer);

		// copy from the cache and update the last use timestamp
		int block = j.offset / m_block_size;
		int block_offset = j.offset & (m_block_size-1);
		int buffer_offset = 0;
		int size = j.buffer_size;
		int min_blocks_to_read = block_offset > 0 && (size > m_block_size - block_offset) ? 2 : 1;
		TORRENT_ASSERT(size <= m_block_size);
		int start_block = block;
		if (p.blocks[start_block].buf != 0 && min_blocks_to_read > 1)
			++start_block;

		int piece_size = j.storage->info()->piece_size(j.piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
		TORRENT_ASSERT(start_block < blocks_in_piece);

		// if block_offset > 0, we need to read two blocks, and then
		// copy parts of both, because it's not aligned to the block
		// boundaries
		if (p.blocks[start_block].buf == 0)
		{
			// if we use an explicit read cache, pretend there's no
			// space to force hitting disk without caching anything
			if (m_settings.explicit_read_cache) return -2;

			int end_block = start_block;
			while (end_block < blocks_in_piece && p.blocks[end_block].buf == 0) ++end_block;

			int blocks_to_read = end_block - block;
			blocks_to_read = (std::min)(blocks_to_read, (std::max)((m_settings.cache_size
				+ m_cache_stats.read_cache_size - in_use())/2, 3));
			blocks_to_read = (std::min)(blocks_to_read, m_settings.read_cache_line_size);
			blocks_to_read = (std::max)(blocks_to_read, min_blocks_to_read);
			if (j.max_cache_line > 0) blocks_to_read = (std::min)(blocks_to_read, j.max_cache_line);
			
			// if we don't have enough space for the new piece, try flushing something else
			if (in_use() + blocks_to_read > m_settings.cache_size)
			{
				int clear = in_use() + blocks_to_read - m_settings.cache_size;
				if (flush_cache_blocks(l, clear, ignore_t(p.piece, p.storage.get())
					, dont_flush_write_blocks) < clear)
					return -2;
			}

			int ret = read_into_piece(p, block, 0, blocks_to_read, l);
			hit = false;
			if (ret < 0) return ret;
			if (ret < size + block_offset) return -2;
			TORRENT_ASSERT(p.blocks[block].buf);
		}

		// build a vector of all the buffers we need to free
		// and free them all in one go
		std::vector<char*> buffers;
		while (size > 0)
		{
			TORRENT_ASSERT(p.blocks[block].buf);
			int to_copy = (std::min)(m_block_size
					- block_offset, size);
			std::memcpy(j.buffer + buffer_offset
				, p.blocks[block].buf + block_offset
				, to_copy);
			size -= to_copy;
			block_offset = 0;
			buffer_offset += to_copy;
			if (m_settings.volatile_read_cache)
			{
				// if volatile read cache is set, the assumption is
				// that no other peer is likely to request the same
				// piece. Therefore, for each request out of the cache
				// we clear the block that was requested and any blocks
				// the peer skipped
				for (int i = block; i >= 0 && p.blocks[i].buf; --i)
				{
					buffers.push_back(p.blocks[i].buf);
					p.blocks[i].buf = 0;
					--p.num_blocks;
					--m_cache_stats.cache_size;
					--m_cache_stats.read_cache_size;
				}
			}
			++block;
		}
		if (!buffers.empty()) free_multiple_buffers(&buffers[0], buffers.size());
		return j.buffer_size;
	}

	int disk_io_thread::try_read_from_cache(disk_io_job const& j, bool& hit, int flags)
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		TORRENT_ASSERT(j.buffer);
		TORRENT_ASSERT(j.cache_min_time >= 0);

		mutex::scoped_lock l(m_piece_mutex);
		if (!m_settings.use_read_cache)
		{
			hit = false;
			return -2;
		}

		cache_piece_index_t& idx = m_read_pieces.get<0>();
		cache_piece_index_t::iterator p
			= find_cached_piece(m_read_pieces, j, l);

		hit = true;
		int ret = 0;

		// if the piece cannot be found in the cache,
		// read the whole piece starting at the block
		// we got a request for.
		if (p == idx.end())
		{
			if (flags & cache_only) return -2;
			// if we use an explicit read cache and we
			// couldn't find the block in the cache,
			// pretend that there's not enough space
			// to cache it, to force the read operation
			// go go straight to disk
			if (m_settings.explicit_read_cache) return -2;

			ret = cache_read_block(j, l);
			hit = false;
			if (ret < 0) return ret;

			p = find_cached_piece(m_read_pieces, j, l);
			TORRENT_ASSERT(!m_read_pieces.empty());
			TORRENT_ASSERT(p->piece == j.piece);
			TORRENT_ASSERT(p->storage == j.storage);
		}

		TORRENT_ASSERT(p != idx.end());

		ret = copy_from_piece(const_cast<cached_piece_entry&>(*p), hit, j, l);
		if (ret < 0) return ret;
		if (p->num_blocks == 0) idx.erase(p);
		else idx.modify(p, update_last_use(j.cache_min_time));

		ret = j.buffer_size;
		++m_cache_stats.blocks_read;
		if (hit) ++m_cache_stats.blocks_read_hit;
		return ret;
	}

	size_type disk_io_thread::queue_buffer_size() const
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		mutex::scoped_lock l(m_queue_mutex);
		return m_queue_buffer_size;
	}

	typedef std::list<std::pair<disk_io_job, int> > job_queue_t;
	void completion_queue_handler(job_queue_t* completed_jobs)
	{
		boost::shared_ptr<job_queue_t> holder(completed_jobs);

		for (job_queue_t::iterator i = completed_jobs->begin()
			, end(completed_jobs->end()); i != end; ++i)
		{
			TORRENT_TRY
			{
				i->first.callback(i->second, i->first);
			}
			TORRENT_CATCH(std::exception& e)
			{}
		}
	}

	int disk_io_thread::add_job(disk_io_job const& j
		, mutex::scoped_lock& l
		, boost::function<void(int, disk_io_job const&)> const& f)
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		const_cast<disk_io_job&>(j).start_time = time_now_hires();

		if (j.action == disk_io_job::write)
		{
			m_queue_buffer_size += j.buffer_size;
			if (m_queue_buffer_size >= m_settings.max_queued_disk_bytes
				&& m_settings.max_queued_disk_bytes > 0)
				m_exceeded_write_queue = true;
		}
/*
		else if (j.action == disk_io_job::read)
		{
			// if this is a cache hit, return it right away!
			// this is OK because the cache is actually protected by
			// the m_piece_mutex
			bool hit = false;
			if (j.buffer == 0)
			{
				// this is OK because the disk_buffer pool has its
				// own mutex to protect the pool allocator
				const_cast<disk_io_job&>(j).buffer = allocate_buffer("send buffer");
			}
			int ret = try_read_from_cache(j, hit, cache_only);
			if (hit && ret >= 0)
			{
				TORRENT_ASSERT(f);
				const_cast<disk_io_job&>(j).callback.swap(
					const_cast<boost::function<void(int, disk_io_job const&)>&>(f));
				job_queue_t* q = new job_queue_t;
				q->push_back(std::make_pair(j, ret));
				m_ios.post(boost::bind(completion_queue_handler, q));
				return m_queue_buffer_size;
			}
			free_buffer(j.buffer);
			const_cast<disk_io_job&>(j).buffer = 0;
		}
*/
		TORRENT_ASSERT(l.locked());
		m_jobs.push_back(j);
		m_jobs.back().callback.swap(const_cast<boost::function<void(int, disk_io_job const&)>&>(f));

		m_signal.signal(l);
		return m_queue_buffer_size;
	}

	int disk_io_thread::add_job(disk_io_job const& j
		, boost::function<void(int, disk_io_job const&)> const& f)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(!m_abort);
		TORRENT_ASSERT(j.storage
			|| j.action == disk_io_job::abort_thread
			|| j.action == disk_io_job::update_settings);
		TORRENT_ASSERT(j.buffer_size <= m_block_size);
		mutex::scoped_lock l(m_queue_mutex);
		TORRENT_ASSERT(m_magic == 0x1337);
		return add_job(j, l, f);
	}

	bool disk_io_thread::test_error(disk_io_job& j)
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		TORRENT_ASSERT(j.storage);
		error_code const& ec = j.storage->error();
		if (ec)
		{
			j.buffer = 0;
			j.str.clear();
			j.error = ec;
			j.error_file = j.storage->error_file();
			j.storage->clear_error();
			return true;
		}
		return false;
	}

	void disk_io_thread::post_callback(disk_io_job const& j, int ret)
	{
		if (!j.callback) return;
		m_queued_completions.push_back(std::make_pair(j, ret));
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
		, cancel_on_abort // check_files
		, 0 // save_resume_data
		, 0 // rename_file
		, 0 // abort_thread
		, 0 // clear_read_cache
		, 0 // abort_torrent
		, cancel_on_abort // update_settings
		, read_operation + cancel_on_abort // read_and_hash
		, read_operation + cancel_on_abort // cache_piece
		, 0 // file_priority
#ifndef TORRENT_NO_DEPRECATE
		, 0 // finalize_file
#endif
	};

	bool should_cancel_on_abort(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.action >= 0 && j.action < int(sizeof(action_flags)));
		return action_flags[j.action] & cancel_on_abort;
	}

	bool is_read_operation(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.action >= 0 && j.action < int(sizeof(action_flags)));
		return action_flags[j.action] & read_operation;
	}

	bool operation_has_buffer(disk_io_job const& j)
	{
		TORRENT_ASSERT(j.action >= 0 && j.action < int(sizeof(action_flags)));
		return action_flags[j.action] & buffer_operation;
	}

	void disk_io_thread::thread_fun()
	{
#ifdef TORRENT_DISK_STATS
		m_log.open("disk_io_thread.log", std::ios::trunc);
#endif

		// figure out how much physical RAM there is in
		// this machine. This is used for automatically
		// sizing the disk cache size when it's set to
		// automatic.
#ifdef TORRENT_BSD
#ifdef HW_MEMSIZE
		int mib[2] = { CTL_HW, HW_MEMSIZE };
#else
		// not entirely sure this sysctl supports 64
		// bit return values, but it's probably better
		// than not building
		int mib[2] = { CTL_HW, HW_PHYSMEM };
#endif
		size_t len = sizeof(m_physical_ram);
		if (sysctl(mib, 2, &m_physical_ram, &len, NULL, 0) != 0)
			m_physical_ram = 0;
#elif defined TORRENT_WINDOWS
		MEMORYSTATUSEX ms;
		ms.dwLength = sizeof(MEMORYSTATUSEX);
		if (GlobalMemoryStatusEx(&ms))
			m_physical_ram = ms.ullTotalPhys;
		else
			m_physical_ram = 0;
#elif defined TORRENT_LINUX
		m_physical_ram = sysconf(_SC_PHYS_PAGES);
		m_physical_ram *= sysconf(_SC_PAGESIZE);
#elif defined TORRENT_AMIGA
		m_physical_ram = AvailMem(MEMF_PUBLIC);
#endif

#if TORRENT_USE_RLIMIT
		if (m_physical_ram > 0)
		{
			struct rlimit r;
			if (getrlimit(RLIMIT_AS, &r) == 0 && r.rlim_cur != RLIM_INFINITY)
			{
				if (m_physical_ram > r.rlim_cur)
					m_physical_ram = r.rlim_cur;
			}
		}
#endif

		// 1 = forward in list, -1 = backwards in list
		int elevator_direction = 1;

		read_jobs_t::iterator elevator_job_pos = m_sorted_read_jobs.begin();
		size_type last_elevator_pos = 0;
		bool need_update_elevator_pos = false;
		int immediate_jobs_in_row = 0;

		for (;;)
		{
#ifdef TORRENT_DISK_STATS
			m_log << log_time() << " idle" << std::endl;
#endif

			TORRENT_ASSERT(m_magic == 0x1337);

			mutex::scoped_lock jl(m_queue_mutex);

			if (m_queued_completions.size() >= 30 || (m_jobs.empty() && !m_queued_completions.empty()))
			{
				job_queue_t* q = new job_queue_t;
				q->swap(m_queued_completions);
				m_ios.post(boost::bind(completion_queue_handler, q));
			}


			ptime job_start;
			while (m_jobs.empty() && m_sorted_read_jobs.empty() && !m_abort)
			{
				// if there hasn't been an event in one second
				// see if we should flush the cache
//				if (!m_signal.timed_wait(jl, boost::posix_time::seconds(1)))
//					flush_expired_pieces();
				m_signal.wait(jl);
				m_signal.clear(jl);

				job_start = time_now();
				if (job_start >= m_last_stats_flip + seconds(1)) flip_stats(job_start);
			}

			if (m_abort && m_jobs.empty())
			{
				jl.unlock();

				mutex::scoped_lock l(m_piece_mutex);
				// flush all disk caches
				cache_piece_index_t& widx = m_pieces.get<0>();
				for (cache_piece_index_t::iterator i = widx.begin()
					, end(widx.end()); i != end; ++i)
					flush_range(const_cast<cached_piece_entry&>(*i), 0, INT_MAX, l);

#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
				// since we're aborting the thread, we don't actually
				// need to free all the blocks individually. We can just
				// clear the piece list and the memory will be freed when we
				// destruct the m_pool. If we're not using a pool, we actually
				// have to free everything individually though
				cache_piece_index_t& idx = m_read_pieces.get<0>();
				for (cache_piece_index_t::iterator i = idx.begin()
					, end(idx.end()); i != end; ++i)
					free_piece(const_cast<cached_piece_entry&>(*i), l);
#endif

				m_pieces.clear();
				m_read_pieces.clear();
				// release the io_service to allow the run() call to return
				// we do this once we stop posting new callbacks to it.
				m_work.reset();

				TORRENT_ASSERT(m_magic == 0x1337);

				return;
			}

			disk_io_job j;

			ptime now = time_now_hires();
			ptime operation_start = now;

			// make sure we don't starve out the read queue by just issuing
			// write jobs constantly, mix in a read job every now and then
			// with a configurable ratio
			// this rate must increase to every other jobs if the queued
			// up read jobs increases too far.
			int read_job_every = m_settings.read_job_every;

			int unchoke_limit = m_settings.unchoke_slots_limit;
			if (unchoke_limit < 0) unchoke_limit = 100;

			if (int(m_sorted_read_jobs.size()) > unchoke_limit * 2)
			{
				int range = unchoke_limit;
				int exceed = m_sorted_read_jobs.size() - range * 2;
				read_job_every = (exceed * 1 + (range - exceed) * read_job_every) / 2;
				if (read_job_every < 1) read_job_every = 1;
			}

			bool pick_read_job = m_jobs.empty()
				|| (immediate_jobs_in_row >= read_job_every
					&& !m_sorted_read_jobs.empty());

			if (!pick_read_job)
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
					
					if (m_exceeded_write_queue)
					{
						int low_watermark = m_settings.max_queued_disk_bytes_low_watermark == 0
							|| m_settings.max_queued_disk_bytes_low_watermark >= m_settings.max_queued_disk_bytes
							? size_type(m_settings.max_queued_disk_bytes) * 7 / 8
							: m_settings.max_queued_disk_bytes_low_watermark;

						if (m_queue_buffer_size < low_watermark
							|| m_settings.max_queued_disk_bytes == 0)
						{
							m_exceeded_write_queue = false;
							// we just dropped below the high watermark of number of bytes
							// queued for writing to the disk. Notify the session so that it
							// can trigger all the connections waiting for this event
							if (m_queue_callback) m_ios.post(m_queue_callback);
						}
					}
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
						mutex::scoped_lock l(m_piece_mutex);
						cache_piece_index_t::iterator p
							= find_cached_piece(m_read_pieces, j, l);
				
						cache_piece_index_t& idx = m_read_pieces.get<0>();
						// if it's a cache hit, process the job immediately
						if (p != idx.end() && is_cache_hit(const_cast<cached_piece_entry&>(*p), j, l))
							defer = false;
					}
				}

				if (m_settings.use_disk_read_ahead && defer)
				{
					j.storage->hint_read_impl(j.piece, j.offset, j.buffer_size);
				}

				TORRENT_ASSERT(j.offset >= 0);
				if (m_settings.allow_reordered_disk_operations && defer)
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " sorting_job" << std::endl;
#endif
					ptime sort_start = time_now_hires();

					size_type phys_off = j.storage->physical_offset(j.piece, j.offset);
					need_update_elevator_pos = need_update_elevator_pos || m_sorted_read_jobs.empty();
					m_sorted_read_jobs.insert(std::pair<size_type, disk_io_job>(phys_off, j));

					ptime now = time_now_hires();
					m_sort_time.add_sample(total_microseconds(now - sort_start));
					m_job_time.add_sample(total_microseconds(now - operation_start));
					m_cache_stats.cumulative_sort_time += total_milliseconds(now - sort_start);
					m_cache_stats.cumulative_job_time += total_milliseconds(now - operation_start);
					continue;
				}

				++immediate_jobs_in_row;
			}
			else
			{
				// the job queue is empty, pick the next read job
				// from the sorted job list. So we don't need the
				// job queue lock anymore
				jl.unlock();

				immediate_jobs_in_row = 0;

				TORRENT_ASSERT(!m_sorted_read_jobs.empty());

				// if m_sorted_read_jobs used to be empty,
				// we need to update the elevator position
				if (need_update_elevator_pos)
				{
					elevator_job_pos = m_sorted_read_jobs.lower_bound(last_elevator_pos);
					need_update_elevator_pos = false;
				}

				// if we've reached the end, change the elevator direction
				if (elevator_job_pos == m_sorted_read_jobs.end())
				{
					elevator_direction = -1;
					--elevator_job_pos;
				}
				TORRENT_ASSERT(!m_sorted_read_jobs.empty());

				TORRENT_ASSERT(elevator_job_pos != m_sorted_read_jobs.end());
				j = elevator_job_pos->second;
				read_jobs_t::iterator to_erase = elevator_job_pos;

				// if we've reached the begining of the sorted list,
				// change the elvator direction
				if (elevator_job_pos == m_sorted_read_jobs.begin())
					elevator_direction = 1;

				// move the elevator before erasing the job we're processing
				// to keep the iterator valid
				if (elevator_direction > 0) ++elevator_job_pos;
				else --elevator_job_pos;

				TORRENT_ASSERT(to_erase != elevator_job_pos);
				last_elevator_pos = to_erase->first;
				m_sorted_read_jobs.erase(to_erase);
			}

			m_queue_time.add_sample(total_microseconds(now - j.start_time));

			// if there's a buffer in this job, it will be freed
			// when this holder is destructed, unless it has been
			// released.
			disk_buffer_holder holder(*this
				, operation_has_buffer(j) ? j.buffer : 0);

			flush_expired_pieces();

			int ret = 0;

			TORRENT_ASSERT(j.storage
				|| j.action == disk_io_job::abort_thread
				|| j.action == disk_io_job::update_settings);
#ifdef TORRENT_DISK_STATS
			ptime start = time_now();
#endif

			if (j.cache_min_time < 0)
				j.cache_min_time = j.cache_min_time == 0 ? m_settings.default_cache_min_age
					: (std::max)(m_settings.default_cache_min_age, j.cache_min_time);

			TORRENT_TRY
			{

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
					session_settings const* s = ((session_settings*)j.buffer);
					TORRENT_ASSERT(s->cache_size >= -1);
					TORRENT_ASSERT(s->cache_expiry > 0);

#if defined TORRENT_WINDOWS
					if (m_settings.low_prio_disk != s->low_prio_disk)
					{
						m_file_pool.set_low_prio_io(s->low_prio_disk);
						// we need to close all files, since the prio
						// only takes affect when files are opened
						m_file_pool.release(0);
					}
#endif
					m_settings = *s;
					delete s;

					m_file_pool.resize(m_settings.file_pool_size);
#if defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
					setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_THREAD
						, m_settings.low_prio_disk ? IOPOL_THROTTLE : IOPOL_DEFAULT);
#elif defined IOPRIO_WHO_PROCESS
					syscall(ioprio_set, IOPRIO_WHO_PROCESS, getpid(), IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE
						, m_settings.get_bool(settings_pack::low_prio_disk) ? 7: 0));
#endif
					if (m_settings.cache_size == -1)
					{
						// the cache size is set to automatic. Make it
						// depend on the amount of physical RAM
						// if we don't know how much RAM we have, just set the
						// cache size to 16 MiB (1024 blocks)
						if (m_physical_ram == 0)
						{
							m_settings.cache_size = 1024;
						}
						else if (sizeof(void*) == 4)
						{
							// 32 bit builds should  capped below 2 GB of memory, even
							// when more actual ram is available, because we're still
							// constrained by the 32 bit virtual address space.
							m_settings.cache_size = (std::min)(boost::uint64_t(2) * 1024
								* 1024 * 1024 * 3 / 4, m_physical_ram / 8) / m_block_size;
						}
						else
						{
							m_settings.cache_size = m_physical_ram / 8 / m_block_size;
						}
					}
					break;
				}
				case disk_io_job::abort_torrent:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " abort_torrent " << std::endl;
#endif
					mutex::scoped_lock jl(m_queue_mutex);
					for (std::deque<disk_io_job>::iterator i = m_jobs.begin();
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
							post_callback(*i, -3);
							i = m_jobs.erase(i);
							continue;
						}
						++i;
					}
					// now clear all the read jobs
					for (read_jobs_t::iterator i = m_sorted_read_jobs.begin();
						i != m_sorted_read_jobs.end();)
					{
						if (i->second.storage != j.storage)
						{
							++i;
							continue;
						}
						post_callback(i->second, -3);
						if (elevator_job_pos == i) ++elevator_job_pos;
						m_sorted_read_jobs.erase(i++);
					}
					jl.unlock();

					mutex::scoped_lock l(m_piece_mutex);

					// build a vector of all the buffers we need to free
					// and free them all in one go
					std::vector<char*> buffers;
					for (cache_t::iterator i = m_read_pieces.begin();
						i != m_read_pieces.end();)
					{
						if (i->storage == j.storage)
						{
							drain_piece_bufs(const_cast<cached_piece_entry&>(*i), buffers, l);
							i = m_read_pieces.erase(i);
						}
						else
						{
							++i;
						}
					}
					l.unlock();
					if (!buffers.empty()) free_multiple_buffers(&buffers[0], buffers.size());
					release_memory();
					break;
				}
				case disk_io_job::abort_thread:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " abort_thread " << std::endl;
#endif
					// clear all read jobs
					mutex::scoped_lock jl(m_queue_mutex);

					for (std::deque<disk_io_job>::iterator i = m_jobs.begin();
						i != m_jobs.end();)
					{
						if (should_cancel_on_abort(*i))
						{
							if (i->action == disk_io_job::write)
							{
								TORRENT_ASSERT(m_queue_buffer_size >= i->buffer_size);
								m_queue_buffer_size -= i->buffer_size;
							}
							post_callback(*i, -3);
							i = m_jobs.erase(i);
							continue;
						}
						++i;
					}
					jl.unlock();

					for (read_jobs_t::iterator i = m_sorted_read_jobs.begin();
						i != m_sorted_read_jobs.end();)
					{
						if (i->second.storage != j.storage)
						{
							++i;
							continue;
						}
						post_callback(i->second, -3);
						if (elevator_job_pos == i) ++elevator_job_pos;
						m_sorted_read_jobs.erase(i++);
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
						j.error = error::no_memory;
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
#ifndef TORRENT_NO_DEPRECATE
				case disk_io_job::finalize_file:
					break;
#endif
				case disk_io_job::read:
				{
					if (test_error(j))
					{
						ret = -1;
						break;
					}
#ifdef TORRENT_DISK_STATS
					m_log << log_time();
#endif
					INVARIANT_CHECK;
					if (j.buffer == 0) j.buffer = allocate_buffer("send buffer");
					TORRENT_ASSERT(j.buffer_size <= m_block_size);
					if (j.buffer == 0)
					{
#ifdef TORRENT_DISK_STATS
						m_log << " read 0" << std::endl;
#endif
						ret = -1;
#if BOOST_VERSION == 103500
						j.error = error_code(boost::system::posix_error::not_enough_memory
							, get_posix_category());
#elif BOOST_VERSION > 103500
						j.error = error_code(boost::system::errc::not_enough_memory
							, get_posix_category());
#else
						j.error = error::no_memory;
#endif
						j.str.clear();
						break;
					}

					disk_buffer_holder read_holder(*this, j.buffer);

					bool hit;
					ret = try_read_from_cache(j, hit);

#ifdef TORRENT_DISK_STATS
					m_log << (hit?" read-cache-hit ":" read ") << j.buffer_size << std::endl;
#endif
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
						file::iovec_t b = { j.buffer, size_t(j.buffer_size) };
						ret = j.storage->read_impl(&b, j.piece, j.offset, 1);
						if (ret < 0)
						{
							test_error(j);
							break;
						}
						if (ret != j.buffer_size)
						{
							char msg[70];
							snprintf(msg, sizeof(msg), "reading p: %d o: %d s: %d (read: %d)", j.piece, j.offset, j.buffer_size, ret);

							// this means the file wasn't big enough for this read
							j.buffer = 0;
							j.error = errors::file_too_short;
							j.error_file = msg;
							j.str.clear();
							ret = -1;
							break;
						}
						++m_cache_stats.blocks_read;
						hit = false;
					}
					if (!hit)
					{
						ptime now = time_now_hires();
						m_read_time.add_sample(total_microseconds(now - operation_start));
						m_cache_stats.cumulative_read_time += total_milliseconds(now - operation_start);
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
					mutex::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					TORRENT_ASSERT(!j.storage->error());
					TORRENT_ASSERT(j.cache_min_time >= 0);

					if (in_use() >= m_settings.cache_size)
					{
						flush_cache_blocks(l, in_use() - m_settings.cache_size + 1);
						if (test_error(j)) break;
					}
					TORRENT_ASSERT(!j.storage->error());

					cache_piece_index_t& idx = m_pieces.get<0>();
					cache_piece_index_t::iterator p = find_cached_piece(m_pieces, j, l);
					int block = j.offset / m_block_size;
					TORRENT_ASSERT(j.buffer);
					TORRENT_ASSERT(j.buffer_size <= m_block_size);
					int piece_size = j.storage->info()->piece_size(j.piece);
					int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;
					if (p != idx.end())
					{
						bool recalc_contiguous = false;
						TORRENT_ASSERT(p->blocks[block].buf == 0);
						if (p->blocks[block].buf)
						{
							free_buffer(p->blocks[block].buf);
							--m_cache_stats.cache_size;
							--const_cast<cached_piece_entry&>(*p).num_blocks;
						}
						else if ((block > 0 && p->blocks[block-1].buf)
							|| (block < blocks_in_piece-1 && p->blocks[block+1].buf)
							|| p->num_blocks == 0)
						{
							// update the contiguous blocks counter for this piece. Only if it has
							// an adjacent block. If it doesn't, we already know it couldn't have
							// increased the largest contiguous block span in this piece
							recalc_contiguous = true;
						}
						p->blocks[block].buf = j.buffer;
						p->blocks[block].callback.swap(j.callback);
#ifdef TORRENT_DISK_STATS
						rename_buffer(j.buffer, "write cache");
#endif
						++m_cache_stats.cache_size;
						++const_cast<cached_piece_entry&>(*p).num_blocks;
						if (recalc_contiguous)
						{
							const_cast<cached_piece_entry&>(*p).num_contiguous_blocks = contiguous_blocks(*p);
						}
						idx.modify(p, update_last_use(j.cache_min_time));
						// we might just have created a contiguous range
						// that meets the requirement to be flushed. try it
						// if we're in avoid_readback mode, don't do this. Only flush
						// pieces when we need more space in the cache (which will avoid
						// flushing blocks out-of-order) or when we issue a hash job,
						// wich indicates the piece is completely downloaded
						flush_contiguous_blocks(const_cast<cached_piece_entry&>(*p)
							, l, m_settings.write_cache_line_size
							, m_settings.disk_cache_algorithm == session_settings::avoid_readback);

						if (p->num_blocks == 0 && p->next_block_to_hash == 0) idx.erase(p);
						test_error(j);
						TORRENT_ASSERT(!j.storage->error());
					}
					else
					{
						TORRENT_ASSERT(!j.storage->error());
						if (cache_block(j, j.callback, j.cache_min_time, l) < 0)
						{
							l.unlock();
							ptime start = time_now_hires();
							file::iovec_t iov = {j.buffer, size_t(j.buffer_size) };
							ret = j.storage->write_impl(&iov, j.piece, j.offset, 1);
							l.lock();
							if (ret < 0)
							{
								test_error(j);
								break;
							}
							ptime done = time_now_hires();
							m_write_time.add_sample(total_microseconds(done - start));
							m_cache_stats.cumulative_write_time += total_milliseconds(done - start);
							// we successfully wrote the block. Ignore previous errors
							j.storage->clear_error();
							break;
						}
						TORRENT_ASSERT(!j.storage->error());
					}
					// we've now inserted the buffer
					// in the cache, we should not
					// free it at the end
					holder.release();

					if (in_use() > m_settings.cache_size)
					{
						flush_cache_blocks(l, in_use() - m_settings.cache_size);
						test_error(j);
					}
					TORRENT_ASSERT(!j.storage->error());

					break;
				}
				case disk_io_job::cache_piece:
				{
					mutex::scoped_lock l(m_piece_mutex);

					if (test_error(j))
					{
						ret = -1;
						break;
					}
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " cache " << j.piece << std::endl;
#endif
					INVARIANT_CHECK;
					TORRENT_ASSERT(j.buffer == 0);

					cache_piece_index_t::iterator p;
					bool hit;
					ret = cache_piece(j, p, hit, 0, l);
					if (ret == -2) ret = -1;

					if (ret < 0) test_error(j);
					break;
				}
				case disk_io_job::hash:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " hash" << std::endl;
#endif
					TORRENT_ASSERT(!j.storage->error());
					mutex::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					cache_piece_index_t& idx = m_pieces.get<0>();
					cache_piece_index_t::iterator i = find_cached_piece(m_pieces, j, l);
					if (i != idx.end())
					{
						TORRENT_ASSERT(i->storage);
						ret = flush_range(const_cast<cached_piece_entry&>(*i), 0, INT_MAX, l);
						idx.erase(i);
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

					ptime hash_start = time_now_hires();

					int readback = 0;
					sha1_hash h = j.storage->hash_for_piece_impl(j.piece, &readback);
					if (test_error(j))
					{
						ret = -1;
						j.storage->mark_failed(j.piece);
						break;
					}

					m_cache_stats.total_read_back += readback / m_block_size;

					ret = (j.storage->info()->hash_for_piece(j.piece) == h)?0:-2;
					if (ret == -2) j.storage->mark_failed(j.piece);

					ptime done = time_now_hires();
					m_hash_time.add_sample(total_microseconds(done - hash_start));
					m_cache_stats.cumulative_hash_time += total_milliseconds(done - hash_start);
					break;
				}
				case disk_io_job::move_storage:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " move" << std::endl;
#endif
					TORRENT_ASSERT(j.buffer == 0);
					ret = j.storage->move_storage_impl(j.str, j.piece);
					if (ret == piece_manager::file_exist)
					{
						j.error = error_code(boost::system::errc::file_exists, get_system_category());
						j.error_file = -1;
						j.buffer = NULL;
						break;
					}
					if (ret != piece_manager::no_error && ret != piece_manager::need_full_check)
					{
						test_error(j);
						break;
					}
					j.str = j.storage->save_path();
					break;
				}
				case disk_io_job::release_files:
				{
#ifdef TORRENT_DISK_STATS
					m_log << log_time() << " release" << std::endl;
#endif
					TORRENT_ASSERT(j.buffer == 0);

					mutex::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					for (cache_t::iterator i = m_pieces.begin(); i != m_pieces.end();)
					{
						if (i->storage == j.storage)
						{
							flush_range(const_cast<cached_piece_entry&>(*i), 0, INT_MAX, l);
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

					mutex::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

					for (cache_t::iterator i = m_read_pieces.begin();
						i != m_read_pieces.end();)
					{
						if (i->storage == j.storage)
						{
							free_piece(const_cast<cached_piece_entry&>(*i), l);
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

					mutex::scoped_lock l(m_piece_mutex);
					INVARIANT_CHECK;

 					// delete all write cache entries for this storage
					cache_piece_index_t& idx = m_pieces.get<0>();
					cache_piece_index_t::iterator start = idx.lower_bound(std::pair<void*, int>(j.storage.get(), 0));
					cache_piece_index_t::iterator end = idx.upper_bound(std::pair<void*, int>(j.storage.get(), INT_MAX));

					// build a vector of all the buffers we need to free
					// and free them all in one go
					std::vector<char*> buffers;
					torrent_info const& ti = *j.storage->info();
					for (cache_piece_index_t::iterator i = start; i != end; ++i)
					{
						int blocks_in_piece = (ti.piece_size(i->piece) + m_block_size - 1) / m_block_size;
						cached_piece_entry& e = const_cast<cached_piece_entry&>(*i);
						for (int j = 0; j < blocks_in_piece; ++j)
						{
							if (i->blocks[j].buf == 0) continue;
							buffers.push_back(i->blocks[j].buf);
							i->blocks[j].buf = 0;
							--m_cache_stats.cache_size;
							TORRENT_ASSERT(e.num_blocks > 0);
							--e.num_blocks;
						}
						TORRENT_ASSERT(i->num_blocks == 0);
					}
					idx.erase(start, end);
					l.unlock();
					if (!buffers.empty()) free_multiple_buffers(&buffers[0], buffers.size());
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
					test_error(j);
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
	
							sleep(sleep_time);
						}
						m_last_file_check = time_now_hires();
#endif

						ptime hash_start = time_now_hires();
						if (m_waiting_to_shutdown) break;

						ret = j.storage->check_files(j.piece, j.offset, j.error);

						ptime done = time_now_hires();
						m_hash_time.add_sample(total_microseconds(done - hash_start));
						m_cache_stats.cumulative_hash_time += total_milliseconds(done - hash_start);

						TORRENT_TRY {
							TORRENT_ASSERT(j.callback);
							if (j.callback && ret == piece_manager::need_full_check)
								post_callback(j, ret);
						} TORRENT_CATCH(std::exception&) {}
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
					}
					break;
				}
				case disk_io_job::file_priority:
				{
					std::vector<boost::uint8_t>* p
						= reinterpret_cast<std::vector<boost::uint8_t>*>(j.buffer);
					j.storage->set_file_priority_impl(*p);
					delete p;
					ret = 0;
					break;
				}
				}
			}
			TORRENT_CATCH(std::exception& e)
			{
				TORRENT_DECLARE_DUMMY(std::exception, e);
				ret = -1;
				TORRENT_TRY {
					j.str = e.what();
				} TORRENT_CATCH(std::exception&) {}
			}

			TORRENT_ASSERT(!j.storage || !j.storage->error());

			ptime done = time_now_hires();
			m_job_time.add_sample(total_microseconds(done - operation_start));
			m_cache_stats.cumulative_job_time += total_milliseconds(done - operation_start);

//			if (!j.callback) std::cerr << "DISK THREAD: no callback specified" << std::endl;
//			else std::cerr << "DISK THREAD: invoking callback" << std::endl;
			TORRENT_TRY {
				TORRENT_ASSERT(ret != -2 || j.error
					|| j.action == disk_io_job::hash);
#if TORRENT_DISK_STATS
				if ((j.action == disk_io_job::read || j.action == disk_io_job::read_and_hash)
					&& j.buffer != 0)
					rename_buffer(j.buffer, "posted send buffer");
#endif
				post_callback(j, ret);
			} TORRENT_CATCH(std::exception&) {
				TORRENT_ASSERT(false);
			}
		}
		TORRENT_ASSERT(false);
	}
}

