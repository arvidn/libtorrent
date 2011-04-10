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

#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
#include <fstream>
#endif

#include "libtorrent/storage.hpp"
#include "libtorrent/allocator.hpp"
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
#include "libtorrent/session_settings.hpp"

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
			, update_settings
			, read_and_hash
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

	// returns true if the fundamental operation
	// of the given disk job is a read operation
	bool is_read_operation(disk_io_job const& j);

	// this is true if the buffer field in the disk_io_job
	// points to a disk buffer
	bool operation_has_buffer(disk_io_job const& j);

	struct cache_status
	{
		cache_status()
			: blocks_written(0)
			, writes(0)
			, blocks_read(0)
			, blocks_read_hit(0)
			, reads(0)
			, queued_bytes(0)
			, cache_size(0)
			, read_cache_size(0)
			, total_used_buffers(0)
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

		mutable size_type queued_bytes;

		// the number of blocks in the cache (both read and write)
		int cache_size;

		// the number of blocks in the cache used for read cache
		int read_cache_size;

		// the total number of blocks that are currently in use
		// this includes send and receive buffers
		mutable int total_used_buffers;
	};
	
	struct TORRENT_EXPORT disk_buffer_pool : boost::noncopyable
	{
		disk_buffer_pool(int block_size);
#ifdef TORRENT_DEBUG
		~disk_buffer_pool();
#endif

#if defined TORRENT_DEBUG || defined TORRENT_DISK_STATS
		bool is_disk_buffer(char* buffer
			, boost::mutex::scoped_lock& l) const;
		bool is_disk_buffer(char* buffer) const;
#endif

		char* allocate_buffer(char const* category);
		void free_buffer(char* buf);

		char* allocate_buffers(int blocks, char const* category);
		void free_buffers(char* buf, int blocks);

		int block_size() const { return m_block_size; }

#ifdef TORRENT_STATS
		int disk_allocations() const
		{ return m_allocations; }
#endif

#ifdef TORRENT_DISK_STATS
		std::ofstream m_disk_access_log;
#endif

		void release_memory();

		int in_use() const { return m_in_use; }

	protected:

		// number of bytes per block. The BitTorrent
		// protocol defines the block size to 16 KiB.
		const int m_block_size;

		// number of disk buffers currently allocated
		int m_in_use;

		session_settings m_settings;

	private:

		// this only protects the pool allocator
		typedef boost::mutex mutex_t;
		mutable mutex_t m_pool_mutex;
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		// memory pool for read and write operations
		// and disk cache
		boost::pool<page_aligned_allocator> m_pool;
#endif

#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
		int m_allocations;
#endif
#ifdef TORRENT_DISK_STATS
	public:
		void rename_buffer(char* buf, char const* category);
	protected:
		std::map<std::string, int> m_categories;
		std::map<char*, std::string> m_buf_to_category;
		std::ofstream m_log;
	private:
#endif
#ifdef TORRENT_DEBUG
		int m_magic;
#endif
	};

	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct TORRENT_EXPORT disk_io_thread : disk_buffer_pool
	{
		disk_io_thread(io_service& ios
			, boost::function<void()> const& queue_callback
			, file_pool& fp
			, int block_size = 16 * 1024);
		~disk_io_thread();

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
		size_type queue_buffer_size() const;

		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

		cache_status status() const;

		void operator()();

#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif
		
		struct cached_block_entry
		{
			cached_block_entry(): buf(0) {}
			// the buffer pointer (this is a disk_pool buffer)
			// or 0
			char* buf;

			// callback for when this block is flushed to disk
			boost::function<void(int, disk_io_job const&)> callback;
		};

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
			boost::shared_array<cached_block_entry> blocks;
		};

		typedef boost::recursive_mutex mutex_t;

		// TODO: turn this into a multi-index list
		// sorted by piece and last use time
		typedef std::list<cached_piece_entry> cache_t;

	private:

		bool test_error(disk_io_job& j);
		void post_callback(boost::function<void(int, disk_io_job const&)> const& handler
			, disk_io_job const& j, int ret);

		// cache operations
		cache_t::iterator find_cached_piece(
			cache_t& cache, disk_io_job const& j
			, mutex_t::scoped_lock& l);
		bool is_cache_hit(cache_t::iterator p
			, disk_io_job const& j, mutex_t::scoped_lock& l);
		int copy_from_piece(cache_t::iterator p, bool& hit
			, disk_io_job const& j, mutex_t::scoped_lock& l);

		// write cache operations
		enum options_t { dont_flush_write_blocks = 1, ignore_cache_size = 2 };
		int flush_cache_blocks(mutex_t::scoped_lock& l
			, int blocks, cache_t::iterator ignore
			, int options = 0);
		void flush_expired_pieces();
		int flush_and_remove(cache_t::iterator i, mutex_t::scoped_lock& l);
		int flush_contiguous_blocks(disk_io_thread::cache_t::iterator e
			, mutex_t::scoped_lock& l, int lower_limit = 0);
		int flush_range(cache_t::iterator i, int start, int end, mutex_t::scoped_lock& l);
		int cache_block(disk_io_job& j
			, boost::function<void(int,disk_io_job const&)>& handler
			, mutex_t::scoped_lock& l);

		// read cache operations
		int clear_oldest_read_piece(int num_blocks, cache_t::iterator ignore
			, mutex_t::scoped_lock& l);
		int read_into_piece(cached_piece_entry& p, int start_block
			, int options, int num_blocks, mutex_t::scoped_lock& l);
		int cache_read_block(disk_io_job const& j, mutex_t::scoped_lock& l);
		int cache_read_piece(disk_io_job const& j, mutex_t::scoped_lock& l);
		int free_piece(cached_piece_entry& p, mutex_t::scoped_lock& l);
		int try_read_from_cache(disk_io_job const& j);
		int read_piece_from_cache_and_hash(disk_io_job const& j, sha1_hash& h);

		// this mutex only protects m_jobs, m_queue_buffer_size
		// and m_abort
		mutable mutex_t m_queue_mutex;
		boost::condition m_signal;
		bool m_abort;
		bool m_waiting_to_shutdown;
		std::list<disk_io_job> m_jobs;
		size_type m_queue_buffer_size;

		ptime m_last_file_check;

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

#ifdef TORRENT_DISK_STATS
		std::ofstream m_log;
#endif

		io_service& m_ios;

		boost::function<void()> m_queue_callback;

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

		// reference to the file_pool which is a member of
		// the session_impl object
		file_pool& m_file_pool;

		// thread for performing blocking disk io operations
		boost::thread m_disk_io_thread;
	};

}

#endif

