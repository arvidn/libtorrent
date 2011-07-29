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
	struct piece_manager;
	struct cached_piece_entry;

	struct block_cache_reference
	{
		block_cache_reference(): pe(0), block(-1) {}
		cached_piece_entry* pe;
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
	struct disk_io_job : tailqueue_node
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

		action_t action;

		char* buffer;
		int buffer_size;
		boost::intrusive_ptr<piece_manager> storage;

		// flags controlling this job
		boost::uint32_t flags;

		// arguments used for read and write
		// the piece this job applies to
		int piece;

		// for read and write, the offset into the piece
		// the read or write should start
		int offset;

		// if this is > 0, it specifies the max number of blocks to read
		// ahead in the read cache for this access. This is only valid
		// for 'read' actions
		int max_cache_line;

		// if this is > 0, it may increase the minimum time the cache
		// line caused by this operation stays in the cache
		int cache_min_time;

		// used for move_storage and rename_file. On errors, this is set
		// to the error message.
		std::string str;

		// result for hash jobs
		sha1_hash piece_hash;

		boost::shared_ptr<entry> resume_data;

		// the error code from the file operation
		// on error, this also contains the path of the
		// file the disk operation failed on
		storage_error error;

		// this is called when operation completes
		boost::function<void(int, disk_io_job const&)> callback;

		// the time when this job was queued. This is used to
		// keep track of disk I/O congestion
		ptime start_time;

		// if this is set, the read operation is required to
		// release the block references once it's done sending
		// the buffer. For aligned block requests (by far the
		// most common) the buffers are not actually copied
		// into the send buffer, but simply referenced. When this
		// is set in a response to a read, the buffer needs to
		// be de-referenced by sending a reclaim_block message
		// back to the disk thread
		block_cache_reference ref;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		bool in_use;
#endif
	};

}

#endif // TORRENT_DISK_IO_JOB_HPP

