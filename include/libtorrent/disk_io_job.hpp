/*

Copyright (c) 2010-2018, Arvid Norberg
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

#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/tailqueue.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/session_types.hpp"
#include "libtorrent/flags.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/variant/variant.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace libtorrent {

	struct cached_piece_entry;

	// internal
	enum class job_action_t : std::uint8_t
	{
		read
		, write
		, hash
		, move_storage
		, release_files
		, delete_files
		, check_fastresume
		, rename_file
		, stop_torrent
		, flush_piece
		, flush_hashed
		, flush_storage
		, trim_cache
		, file_priority
		, clear_piece
		, num_job_ids
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
	{
		disk_io_job();
		disk_io_job(disk_io_job const&) = delete;
		disk_io_job& operator=(disk_io_job const&) = delete;

		void call_callback();

		// this is set by the storage object when a fence is raised
		// for this job. It means that this no other jobs on the same
		// storage will execute in parallel with this one. It's used
		// to lower the fence when the job has completed
		static constexpr disk_job_flags_t fence = 1_bit;

		// this job is currently being performed, or it's hanging
		// on a cache piece that may be flushed soon
		static constexpr disk_job_flags_t in_progress = 2_bit;

		// this is set for jobs that we're no longer interested in. Any aborted
		// job that's executed should immediately fail with operation_aborted
		// instead of executing
		static constexpr disk_job_flags_t aborted = 6_bit;

		// for write jobs, returns true if its block
		// is not dirty anymore
		bool completed(cached_piece_entry const* pe);

		// for read and write, this is the disk_buffer_holder
		// for other jobs, it may point to other job-specific types
		// for move_storage and rename_file this is a string
		boost::variant<disk_buffer_holder
			, std::string
			, add_torrent_params const*
			, aux::vector<download_priority_t, file_index_t>
			, remove_flags_t
			> argument;

		// the disk storage this job applies to (if applicable)
		std::shared_ptr<storage_interface> storage;

		// this is called when operation completes

		using read_handler = std::function<void(disk_buffer_holder block, disk_job_flags_t flags, storage_error const& se)>;
		using write_handler = std::function<void(storage_error const&)>;
		using hash_handler = std::function<void(piece_index_t, sha1_hash const&, storage_error const&)>;
		using move_handler = std::function<void(status_t, std::string, storage_error const&)>;
		using release_handler = std::function<void()>;
		using check_handler = std::function<void(status_t, storage_error const&)>;
		using rename_handler = std::function<void(std::string, file_index_t, storage_error const&)>;
		using clear_piece_handler = std::function<void(piece_index_t)>;
		using set_file_prio_handler = std::function<void(storage_error const&, aux::vector<download_priority_t, file_index_t>)>;

		boost::variant<read_handler
			, write_handler
			, hash_handler
			, move_handler
			, release_handler
			, check_handler
			, rename_handler
			, clear_piece_handler
			, set_file_prio_handler> callback;

		// the error code from the file operation
		// on error, this also contains the path of the
		// file the disk operation failed on
		storage_error error;

		union un
		{
			un() {}
			// result for hash jobs
			sha1_hash piece_hash;

			// this is used for check_fastresume to pass in a vector of hard-links
			// to create. Each element corresponds to a file in the file_storage.
			// The string is the absolute path of the identical file to create
			// the hard link to.
			aux::vector<std::string, file_index_t>* links;

			struct io_args
			{
			// for read and write, the offset into the piece
			// the read or write should start
			// for hash jobs, this is the first block the hash
			// job is still holding a reference to. The end of
			// the range of blocks a hash jobs holds references
			// to is always the last block in the piece.
			std::int32_t offset;

			// number of bytes 'buffer' points to. Used for read & write
			std::uint16_t buffer_size;
			} io;
		} d;

		// arguments used for read and write
		// the piece this job applies to
		union {
			piece_index_t piece;
			file_index_t file_index;
		};

		// the type of job this is
		job_action_t action = job_action_t::read;

		// return value of operation
		status_t ret = status_t::no_error;

		// flags controlling this job
		disk_job_flags_t flags = disk_job_flags_t{};

		move_flags_t move_flags = move_flags_t::always_replace_files;

#if TORRENT_USE_ASSERTS
		bool in_use = false;

		// set to true when the job is added to the completion queue.
		// to make sure we don't add it twice
		mutable bool job_posted = false;

		// set to true when the callback has been called once
		// used to make sure we don't call it twice
		mutable bool callback_called = false;

		// this is true when the job is blocked by a storage_fence
		mutable bool blocked = false;
#endif
	};

}

#endif // TORRENT_DISK_IO_JOB_HPP
