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

#include "libtorrent/storage.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/sliding_average.hpp"

#include <boost/function/function0.hpp>
#include <boost/function/function2.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_array.hpp>
#include <deque>
#include "libtorrent/config.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/disk_buffer_pool.hpp"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

namespace libtorrent
{
	using boost::multi_index::multi_index_container;
	using boost::multi_index::ordered_non_unique;
	using boost::multi_index::ordered_unique;
	using boost::multi_index::indexed_by;
	using boost::multi_index::member;
	using boost::multi_index::const_mem_fun;

	struct cached_piece_info
	{
		int piece;
		std::vector<bool> blocks;
		ptime last_use;
		int next_to_hash;
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
			, max_cache_line(0)
			, cache_min_time(0)
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
			, cache_piece
#ifndef TORRENT_NO_DEPRECATE
			, finalize_file
#endif
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

		// if this is > 0, it specifies the max number of blocks to read
		// ahead in the read cache for this access. This is only valid
		// for 'read' actions
		int max_cache_line;

		// if this is > 0, it may increase the minimum time the cache
		// line caused by this operation stays in the cache
		int cache_min_time;

		boost::shared_ptr<entry> resume_data;

		// the error code from the file operation
		error_code error;

		// this is called when operation completes
		boost::function<void(int, disk_io_job const&)> callback;

		// the time when this job was issued. This is used to
		// keep track of disk I/O congestion
		ptime start_time;
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
			, average_queue_time(0)
			, average_read_time(0)
			, average_write_time(0)
			, average_hash_time(0)
			, average_job_time(0)
			, average_sort_time(0)
			, job_queue_length(0)
			, cumulative_job_time(0)
			, cumulative_read_time(0)
			, cumulative_write_time(0)
			, cumulative_hash_time(0)
			, cumulative_sort_time(0)
			, total_read_back(0)
			, read_queue_size(0)
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

		// times in microseconds
		int average_queue_time;
		int average_read_time;
		int average_write_time;
		int average_hash_time;
		int average_job_time;
		int average_sort_time;
		int job_queue_length;

		boost::uint32_t cumulative_job_time;
		boost::uint32_t cumulative_read_time;
		boost::uint32_t cumulative_write_time;
		boost::uint32_t cumulative_hash_time;
		boost::uint32_t cumulative_sort_time;
		int total_read_back;
		int read_queue_size;
	};
	
	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct TORRENT_EXTRA_EXPORT disk_io_thread : disk_buffer_pool
	{
		disk_io_thread(io_service& ios
			, boost::function<void()> const& queue_callback
			, file_pool& fp
			, int block_size = 16 * 1024);
		~disk_io_thread();

		void abort();
		void join();

		// aborts read operations
		void stop(boost::intrusive_ptr<piece_manager> s);

		// returns the disk write queue size
		int add_job(disk_io_job const& j
			, boost::function<void(int, disk_io_job const&)> const& f
			= boost::function<void(int, disk_io_job const&)>());

		// keep track of the number of bytes in the job queue
		// at any given time. i.e. the sum of all buffer_size.
		// this is used to slow down the download global download
		// speed when the queue buffer size is too big.
		size_type queue_buffer_size() const;
		bool can_write() const;

		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

		cache_status status() const;

		void thread_fun();

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
			// the pointers to the block data
			boost::shared_array<cached_block_entry> blocks;
			// the last time a block was writting to this piece
			// plus the minimum amount of time the block is guaranteed
			// to stay in the cache
			ptime expire;
			// the number of blocks in the cache for this piece
			int num_blocks;
			// used to determine if this piece should be flushed
			int num_contiguous_blocks;
			// this is the first block that has not yet been hashed
			// by the partial hasher. When minimizing read-back, this
			// is used to determine if flushing a range would force us
			// to read it back later when hashing
			int next_block_to_hash;
			
			std::pair<void*, int> storage_piece_pair() const
			{ return std::pair<void*, int>(storage.get(), piece); }
		};

		typedef multi_index_container<
			cached_piece_entry, indexed_by<
				ordered_unique<const_mem_fun<cached_piece_entry, std::pair<void*, int>
				, &cached_piece_entry::storage_piece_pair> >
				, ordered_non_unique<member<cached_piece_entry, ptime
					, &cached_piece_entry::expire> >
				> 
			> cache_t;

		typedef cache_t::nth_index<0>::type cache_piece_index_t;
		typedef cache_t::nth_index<1>::type cache_lru_index_t;

	private:

		int add_job(disk_io_job const& j
			, mutex::scoped_lock& l
			, boost::function<void(int, disk_io_job const&)> const& f
			= boost::function<void(int, disk_io_job const&)>());

		bool test_error(disk_io_job& j);
		void post_callback(disk_io_job const& j, int ret);

		// cache operations
		cache_piece_index_t::iterator find_cached_piece(
			cache_t& cache, disk_io_job const& j
			, mutex::scoped_lock& l);
		bool is_cache_hit(cached_piece_entry& p
			, disk_io_job const& j, mutex::scoped_lock& l);
		int copy_from_piece(cached_piece_entry& p, bool& hit
			, disk_io_job const& j, mutex::scoped_lock& l);

		struct ignore_t
		{
			ignore_t(): piece(-1), storage(0) {}
			ignore_t(int idx, piece_manager* st): piece(idx), storage(st) {}
			int piece;
			piece_manager* storage;
		};

		// write cache operations
		enum options_t { dont_flush_write_blocks = 1, ignore_cache_size = 2 };
		int flush_cache_blocks(mutex::scoped_lock& l
			, int blocks, ignore_t ignore = ignore_t(), int options = 0);
		void flush_expired_pieces();
		int flush_contiguous_blocks(cached_piece_entry& p
			, mutex::scoped_lock& l, int lower_limit = 0, bool avoid_readback = false);
		int flush_range(cached_piece_entry& p, int start, int end, mutex::scoped_lock& l);
		int cache_block(disk_io_job& j
			, boost::function<void(int,disk_io_job const&)>& handler
			, int cache_expire
			, mutex::scoped_lock& l);

		// read cache operations
		int clear_oldest_read_piece(int num_blocks, ignore_t ignore
			, mutex::scoped_lock& l);
		int read_into_piece(cached_piece_entry& p, int start_block
			, int options, int num_blocks, mutex::scoped_lock& l);
		int cache_read_block(disk_io_job const& j, mutex::scoped_lock& l);
		int free_piece(cached_piece_entry& p, mutex::scoped_lock& l);
		int drain_piece_bufs(cached_piece_entry& p, std::vector<char*>& buf
			, mutex::scoped_lock& l);

		enum cache_flags_t {
			cache_only = 1
		};
		int try_read_from_cache(disk_io_job const& j, bool& hit, int flags = 0);
		int read_piece_from_cache_and_hash(disk_io_job const& j, sha1_hash& h);
		int cache_piece(disk_io_job const& j, cache_piece_index_t::iterator& p
			, bool& hit, int options, mutex::scoped_lock& l);

		// this mutex only protects m_jobs, m_queue_buffer_size,
		// m_exceeded_write_queue and m_abort
		mutable mutex m_queue_mutex;
		event m_signal;
		bool m_abort;
		bool m_waiting_to_shutdown;
		std::deque<disk_io_job> m_jobs;
		size_type m_queue_buffer_size;

		ptime m_last_file_check;

		// this protects the piece cache and related members
		mutable mutex m_piece_mutex;
		// write cache
		cache_t m_pieces;
		
		// read cache
		cache_t m_read_pieces;

		void flip_stats(ptime now);

		// total number of blocks in use by both the read
		// and the write cache. This is not supposed to
		// exceed m_cache_size
		cache_status m_cache_stats;

		// keeps average queue time for disk jobs (in microseconds)
		average_accumulator m_queue_time;

		// average read time for cache misses (in microseconds)
		average_accumulator m_read_time;

		// average write time (in microseconds)
		average_accumulator m_write_time;

		// average hash time (in microseconds)
		average_accumulator m_hash_time;

		// average time to serve a job (any job) in microseconds
		average_accumulator m_job_time;

		// average time to ask for physical offset on disk
		// and insert into queue
		average_accumulator m_sort_time;

		// the last time we reset the average time and store the
		// latest value in m_cache_stats
		ptime m_last_stats_flip;

		typedef std::multimap<size_type, disk_io_job> read_jobs_t;
		read_jobs_t m_sorted_read_jobs;

#ifdef TORRENT_DISK_STATS
		std::ofstream m_log;
#endif

		// the amount of physical ram in the machine
		boost::uint64_t m_physical_ram;

		// if we exceeded the max queue disk write size
		// this is set to true. It remains true until the
		// queue is smaller than the low watermark
		bool m_exceeded_write_queue;

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
		boost::optional<io_service::work> m_work;

		// reference to the file_pool which is a member of
		// the session_impl object
		file_pool& m_file_pool;

		// when completion notifications are queued, they're stuck
		// in this list
		std::list<std::pair<disk_io_job, int> > m_queued_completions;

		// thread for performing blocking disk io operations
		thread m_disk_io_thread;
	};

}

#endif

