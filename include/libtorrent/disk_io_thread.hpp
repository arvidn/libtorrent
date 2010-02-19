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

#ifndef TORRENT_DISK_IO_THREAD
#define TORRENT_DISK_IO_THREAD

#ifdef TORRENT_DISK_STATS
#include <fstream>
#endif

#include "libtorrent/storage.hpp"
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_array.hpp>
#include <list>
#include "libtorrent/config.hpp"
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include <boost/pool/pool.hpp>
#endif

namespace libtorrent
{

	struct cached_piece_info
	{
		int piece;
		std::vector<bool> blocks;
		ptime last_use;
		enum kind_t { read_cache = 0, write_cache = 1 };
		kind_t kind;
	};
	
	struct disk_io_job
	{
		disk_io_job()
			: action(read)
			, buffer(0)
			, buffer_size(0)
			, piece(0)
			, offset(0)
			, priority(0)
		{}

		enum action_t
		{
			read
			, write
			, hash
			, move_storage
			, release_files
			, delete_files
			, check_fastresume
			, check_files
			, save_resume_data
			, rename_file
			, abort_thread
			, clear_read_cache
			, abort_torrent
		};

		action_t action;

		char* buffer;
		int buffer_size;
		boost::intrusive_ptr<piece_manager> storage;
		// arguments used for read and write
		int piece, offset;
		// used for move_storage and rename_file. On errors, this is set
		// to the error message
		std::string str;

		// on error, this is set to the path of the
		// file the disk operation failed on
		std::string error_file;

		// priority decides whether or not this
		// job will skip entries in the queue or
		// not. It always skips in front of entries
		// with lower priority
		int priority;

		boost::shared_ptr<entry> resume_data;

		// the error code from the file operation
		error_code error;

		// this is called when operation completes
		boost::function<void(int, disk_io_job const&)> callback;
	};

	struct cache_status
	{
		cache_status()
			: blocks_written(0)
			, writes(0)
			, blocks_read(0)
			, blocks_read_hit(0)
			, reads(0)
			, cache_size(0)
			, read_cache_size(0)
		{}

		// the number of 16kB blocks written
		size_type blocks_written;
		// the number of write operations used
		size_type writes;
		// (blocks_written - writes) / blocks_written represents the
		// "cache hit" ratio in the write cache
		// the number of blocks read

		// the number of blocks passed back to the bittorrent engine
		size_type blocks_read;
		// the number of blocks that was just copied from the read cache
		size_type blocks_read_hit;
		// the number of read operations used
		size_type reads;

		// the number of blocks in the cache (both read and write)
		int cache_size;

		// the number of blocks in the cache used for read cache
		int read_cache_size;
	};
	
	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct disk_io_thread : boost::noncopyable
	{
		disk_io_thread(io_service& ios, int block_size = 16 * 1024);
		~disk_io_thread();

#ifdef TORRENT_STATS
		int disk_allocations() const
		{ return m_allocations; }
#endif

		void join();

		// aborts read operations
		void stop(boost::intrusive_ptr<piece_manager> s);
		void add_job(disk_io_job const& j
			, boost::function<void(int, disk_io_job const&)> const& f
			= boost::function<void(int, disk_io_job const&)>());

		// keep track of the number of bytes in the job queue
		// at any given time. i.e. the sum of all buffer_size.
		// this is used to slow down the download global download
		// speed when the queue buffer size is too big.
		size_type queue_buffer_size() const
		{ return m_queue_buffer_size; }

		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

		cache_status status() const;
		void set_cache_size(int s);
		void set_cache_expiry(int ex);

		void operator()();

#ifdef TORRENT_DEBUG
		bool is_disk_buffer(char* buffer) const;
#endif

		char* allocate_buffer();
		void free_buffer(char* buf);

#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif
		
	private:

		struct cached_piece_entry
		{
			int piece;
			// storage this piece belongs to
			boost::intrusive_ptr<piece_manager> storage;
			// the last time a block was writting to this piece
			ptime last_use;
			// the number of blocks in the cache for this piece
			int num_blocks;
			// the pointers to the block data
			boost::shared_array<char*> blocks;
		};

		typedef boost::recursive_mutex mutex_t;
		typedef std::list<cached_piece_entry> cache_t;

		bool test_error(disk_io_job& j);
		void post_callback(boost::function<void(int, disk_io_job const&)> const& handler
			, disk_io_job const& j, int ret);

		// cache operations
		cache_t::iterator find_cached_piece(
			cache_t& cache, disk_io_job const& j
			, mutex_t::scoped_lock& l);

		// write cache operations
		void flush_oldest_piece(mutex_t::scoped_lock& l);
		void flush_expired_pieces();
		void flush_and_remove(cache_t::iterator i, mutex_t::scoped_lock& l);
		void flush(cache_t::iterator i, mutex_t::scoped_lock& l);
		int cache_block(disk_io_job& j, mutex_t::scoped_lock& l);

		// read cache operations
		bool clear_oldest_read_piece(cache_t::iterator ignore
			, mutex_t::scoped_lock& l);
		int read_into_piece(cached_piece_entry& p, int start_block, mutex_t::scoped_lock& l);
		int cache_read_block(disk_io_job const& j, mutex_t::scoped_lock& l);
		void free_piece(cached_piece_entry& p, mutex_t::scoped_lock& l);
		bool make_room(int num_blocks
			, cache_t::iterator ignore
			, mutex_t::scoped_lock& l);
		int try_read_from_cache(disk_io_job const& j);

		// this mutex only protects m_jobs, m_queue_buffer_size
		// and m_abort
		mutable mutex_t m_queue_mutex;
		boost::condition m_signal;
		bool m_abort;
		std::list<disk_io_job> m_jobs;
		size_type m_queue_buffer_size;

		// this protects the piece cache and related members
		mutable mutex_t m_piece_mutex;
		// write cache
		cache_t m_pieces;
		
		// read cache
		cache_t m_read_pieces;

		// total number of blocks in use by both the read
		// and the write cache. This is not supposed to
		// exceed m_cache_size
		cache_status m_cache_stats;

		// in (16kB) blocks
		int m_cache_size;

		// expiration time of cache entries in seconds
		int m_cache_expiry;

		// if set to true, each piece flush will allocate
		// one piece worth of temporary memory on the heap
		// and copy the block data into it, and then perform
		// a single write operation from that buffer.
		// if memory is constrained, that temporary buffer
		// might is avoided by setting this to false.
		// in case the allocation fails, the piece flush
		// falls back to writing each block separately.
		bool m_coalesce_writes;
		bool m_coalesce_reads;
		bool m_use_read_cache;

		// this only protects the pool allocator
		mutable mutex_t m_pool_mutex;
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		// memory pool for read and write operations
		// and disk cache
		boost::pool<> m_pool;
#endif

		// number of bytes per block. The BitTorrent
		// protocol defines the block size to 16 KiB.
		int m_block_size;

#ifdef TORRENT_DISK_STATS
		std::ofstream m_log;
#endif
#ifdef TORRENT_STATS
		int m_allocations;
#endif

		size_type m_writes;
		size_type m_blocks_written;

		io_service& m_ios;

		// this keeps the io_service::run() call blocked from
		// returning. When shutting down, it's possible that
		// the event queue is drained before the disk_io_thread
		// has posted its last callback. When this happens, the
		// io_service will have a pending callback from the
		// disk_io_thread, but the event loop is not running.
		// this means that the event is destructed after the
		// disk_io_thread. If the event refers to a disk buffer
		// it will try to free it, but the buffer pool won't
		// exist anymore, and crash. This prevents that.
		boost::optional<asio::io_service::work> m_work;

		// thread for performing blocking disk io operations
		boost::thread m_disk_io_thread;
#ifdef TORRENT_DEBUG
		int m_magic;
#endif
	};

}

#endif

