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
#include <boost/function/function2.hpp>
#include <boost/shared_ptr.hpp>

namespace libtorrent
{
	struct entry;

	struct disk_io_job
	{
		disk_io_job()
			: action(read)
			, buffer(0)
			, buffer_size(0)
			, flags(0)
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
			, finalize_file
			, get_cache_info
		};

		enum flags_t
		{
			volatile_read = 1,
			need_uncork = 2,
			cache_hit = 4
		};

		action_t action;

		char* buffer;
		int buffer_size;
		boost::intrusive_ptr<piece_manager> storage;
		// flags controlling this job
		boost::uint32_t flags;
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

		// the time when this job was queued. This is used to
		// keep track of disk I/O congestion
		ptime start_time;
	};

}

#endif // TORRENT_DISK_IO_JOB_HPP

