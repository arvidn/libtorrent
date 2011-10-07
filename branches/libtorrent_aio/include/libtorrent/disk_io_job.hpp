/*

Copyright (c) 2010, Arvid Norberg
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

#include <string>
#include "libtorrent/ptime.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/tailqueue.hpp"
#include "libtorrent/peer_id.hpp"
#include <boost/function/function2.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

namespace libtorrent
{
	class entry;
	class piece_manager;
	struct cached_piece_entry;

	struct block_cache_reference
	{
//		block_cache_reference(): storage(0), piece(-1), block(-1) {}
		void* storage;
		int piece;
		int block;
	};

	// #error turn this into a union to make it smaller

	// disk_io_jobs are allocated in a pool allocator in aiocb_pool
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
	struct disk_io_job : tailqueue_node, boost::noncopyable
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
			, abort_thread
			, clear_read_cache
			, abort_torrent
			, update_settings
			, cache_piece
			, finalize_file
			, get_cache_info
			, hash_complete
			, file_status
			, reclaim_block
			, clear_piece
			, sync_piece
			, flush_piece
			, trim_cache

			, num_job_ids
		};

		enum flags_t
		{
			// these flags coexist with flags from file class
			volatile_read = 0x100,
			need_uncork = 0x200,
			cache_hit = 0x400,
			// force making a copy of the cached block, rather
			// than getting a reference to the block already in
			// the cache
			force_copy = 0x800,
		};

		// the time when this job was queued. This is used to
		// keep track of disk I/O congestion
		ptime start_time;

		// for write, this points to the data to write,
		// for read, the data read is returned here
		// for other jobs, it may point to other job-specific types
		// for move_storage and rename_file this is a string allocated
		// with malloc()
		// an entry* for save_resume_data
		char* buffer;

		// the disk storage this job applies to (if applicable)
		boost::intrusive_ptr<piece_manager> storage;

		// this is called when operation completes
		boost::function<void(int, disk_io_job const&)> callback;

		// the error code from the file operation
		// on error, this also contains the path of the
		// file the disk operation failed on
		storage_error error;

		union
		{
			// result for hash jobs
			char piece_hash[20];

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

			// if this is > 0, it specifies the max number of blocks to read
			// ahead in the read cache for this access. This is only valid
			// for 'read' actions
			boost::uint8_t max_cache_line;

			} io;
		} d;

		// arguments used for read and write
		// the piece this job applies to
		boost::uint32_t piece:24;

		// the type of job this is
		boost::uint32_t action:8;

		// flags controlling this job
		boost::uint16_t flags;

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		bool in_use:1;
		// set to true when the callback has been called once
		// used to make sure we don't call it twice
		bool callback_called:1;
#endif
	};

}

#endif // TORRENT_DISK_IO_JOB_HPP

