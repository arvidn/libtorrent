/*

Copyright (c) 2010-2016, Arvid Norberg
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

#ifndef TORRENT_DISK_IO_JOB_HPP
#define TORRENT_DISK_IO_JOB_HPP

#include "libtorrent/time.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/tailqueue.hpp"
#include "libtorrent/peer_id.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <string>
#include <boost/function/function1.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	class entry;
	class piece_manager;
	struct cached_piece_entry;
	struct bdecode_node;
	class torrent_info;

	struct block_cache_reference
	{
		void* storage;
		int piece;
		int block;
	};

	// disk_io_jobs are allocated in a pool allocator in disk_io_thread
	// they are always allocated from the network thread, posted
	// (as pointers) to the disk I/O thread, and then passed back
	// to the network thread for completion handling and to be freed.
	// each disk_io_job can belong to one tailqueue. The job queue
	// in the disk thread, is one, the jobs waiting on completion
	// on a cache piece (in block_cache) is another, and a job
	// waiting for a storage fence to be lowered is another. Jobs
	// are never in more than one queue at a time. Only passing around
	// pointers and chaining them back and forth into lists saves
	// a lot of heap allocation churn of using general purpose
	// containers.
	struct TORRENT_EXTRA_EXPORT disk_io_job : tailqueue_node<disk_io_job>
		, boost::noncopyable
	{
		disk_io_job();
		~disk_io_job();

		enum action_t
		{
			read
			, write
			, hash
			, move_storage
			, release_files
			, delete_files
			, check_fastresume
			, save_resume_data
			, rename_file
			, stop_torrent
#ifndef TORRENT_NO_DEPRECATE
			, cache_piece
			, finalize_file
#endif
			, flush_piece
			, flush_hashed
			, flush_storage
			, trim_cache
			, file_priority
			, load_torrent
			, clear_piece
			, tick_storage
			, resolve_links

			, num_job_ids
		};

		enum flags_t
		{
			sequential_access = 0x1,

			// this flag is set on a job when a read operation did
			// not hit the disk, but found the data in the read cache.
			cache_hit = 0x2,

			// force making a copy of the cached block, rather
			// than getting a reference to the block already in
			// the cache.
			force_copy = 0x4,

			// this is set by the storage object when a fence is raised
			// for this job. It means that this no other jobs on the same
			// storage will execute in parallel with this one. It's used
			// to lower the fence when the job has completed
			fence = 0x8,

			// don't keep the read block in cache
			volatile_read = 0x10,

			// this job is currently being performed, or it's hanging
			// on a cache piece that may be flushed soon
			in_progress = 0x20
		};

		// for write jobs, returns true if its block
		// is not dirty anymore
		bool completed(cached_piece_entry const* pe, int block_size);

		// unique identifier for the peer when reading
		void* requester;

		// for write, this points to the data to write,
		// for read, the data read is returned here
		// for other jobs, it may point to other job-specific types
		// for move_storage and rename_file this is a string allocated
		// with malloc()
		// an entry* for save_resume_data
		// for aiocb_complete this points to the aiocb that completed
		// for get_cache_info this points to a cache_status object which
		// is filled in
		union
		{
			char* disk_block;
			char* string;
			entry* resume_data;
			bdecode_node const* check_resume_data;
			std::vector<boost::uint8_t>* priorities;
			torrent_info* torrent_file;
			int delete_options;
		} buffer;

		// the disk storage this job applies to (if applicable)
		boost::shared_ptr<piece_manager> storage;

		// this is called when operation completes
		boost::function<void(disk_io_job const*)> callback;

		// the error code from the file operation
		// on error, this also contains the path of the
		// file the disk operation failed on
		storage_error error;

		union
		{
			// result for hash jobs
			char piece_hash[20];

			// this is used for check_fastresume to pass in a vector of hard-links
			// to create. Each element corresponds to a file in the file_storage.
			// The string is the absolute path of the identical file to create
			// the hard link to.
			std::vector<std::string>* links;

			struct io_args
			{
			// if this is set, the read operation is required to
			// release the block references once it's done sending
			// the buffer. For aligned block requests (by far the
			// most common) the buffers are not actually copied
			// into the send buffer, but simply referenced. When this
			// is set in a response to a read, the buffer needs to
			// be de-referenced by sending a reclaim_block message
			// back to the disk thread
			block_cache_reference ref;

			// for read and write, the offset into the piece
			// the read or write should start
			// for hash jobs, this is the first block the hash
			// job is still holding a reference to. The end of
			// the range of blocks a hash jobs holds references
			// to is always the last block in the piece.
			boost::uint32_t offset;

			// number of bytes 'buffer' points to. Used for read & write
			boost::uint16_t buffer_size;
			} io;
		} d;

		// arguments used for read and write
		// the piece this job applies to
		boost::uint32_t piece:24;

		// the type of job this is
		boost::uint32_t action:8;

		enum { operation_failed = -1 };

		// return value of operation
		boost::int32_t ret;

		// flags controlling this job
		boost::uint8_t flags;

#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
		bool in_use:1;

		// set to true when the job is added to the completion queue.
		// to make sure we don't add it twice
		mutable bool job_posted:1;

		// set to true when the callback has been called once
		// used to make sure we don't call it twice
		mutable bool callback_called:1;

		// this is true when the job is blocked by a storage_fence
		mutable bool blocked:1;
#endif
	};

}

#endif // TORRENT_DISK_IO_JOB_HPP

