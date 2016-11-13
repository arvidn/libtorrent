/*

Copyright (c) 2012-2016, Arvid Norberg
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

#ifndef TORRENT_DISK_INTERFACE_HPP
#define TORRENT_DISK_INTERFACE_HPP

#include "libtorrent/bdecode.hpp"

#include <string>

namespace libtorrent
{
	struct disk_io_job;
	struct storage_interface;
	struct peer_request;
	struct disk_observer;
	struct file_pool;
	struct add_torrent_params;
	struct cache_status;

	struct TORRENT_EXTRA_EXPORT disk_interface
	{
		enum return_t
		{
			// return values from check_fastresume, and move_storage
			no_error = 0,
			fatal_disk_error = -1,
			need_full_check = -2,
			disk_check_aborted = -3,
			file_exist = -4
		};

		virtual void async_read(storage_interface* storage, peer_request const& r
			, std::function<void(disk_io_job const*)> handler, void* requester
			, int flags = 0) = 0;
		virtual void async_write(storage_interface* storage, peer_request const& r
			, disk_buffer_holder buffer
			, std::function<void(disk_io_job const*)> handler
			, int flags = 0) = 0;
		virtual void async_hash(storage_interface* storage, int piece, int flags
			, std::function<void(disk_io_job const*)> handler, void* requester) = 0;
		virtual void async_move_storage(storage_interface* storage, std::string const& p, int flags
			, std::function<void(disk_io_job const*)> handler) = 0;
		virtual void async_release_files(storage_interface* storage
			, std::function<void(disk_io_job const*)> handler
			= std::function<void(disk_io_job const*)>()) = 0;
		virtual void async_check_files(storage_interface* storage
			, add_torrent_params const* resume_data
			, std::vector<std::string>& links
			, std::function<void(disk_io_job const*)> handler) = 0;
		virtual void async_flush_piece(storage_interface* storage, int piece
			, std::function<void(disk_io_job const*)> handler
			= std::function<void(disk_io_job const*)>()) = 0;
		virtual void async_stop_torrent(storage_interface* storage
			, std::function<void(disk_io_job const*)> handler)= 0;
		virtual void async_rename_file(storage_interface* storage, int index, std::string const& name
			, std::function<void(disk_io_job const*)> handler) = 0;
		virtual void async_delete_files(storage_interface* storage, int options
			, std::function<void(disk_io_job const*)> handler) = 0;
		virtual void async_set_file_priority(storage_interface* storage
			, std::vector<std::uint8_t> const& prio
			, std::function<void(disk_io_job const*)> handler) = 0;

		virtual void async_clear_piece(storage_interface* storage, int index
			, std::function<void(disk_io_job const*)> handler) = 0;
		virtual void clear_piece(storage_interface* storage, int index) = 0;

		virtual void update_stats_counters(counters& c) const = 0;
		virtual void get_cache_info(cache_status* ret, bool no_pieces = true
			, storage_interface const* storage = 0) const = 0;

		virtual file_pool& files() = 0;

#if TORRENT_USE_ASSERTS
		virtual bool is_disk_buffer(char* buffer) const = 0;
#endif
	protected:
		~disk_interface() {}
	};
}

#endif
