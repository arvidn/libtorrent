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
	struct storage_interface;
	struct peer_request;
	struct disk_observer;
	struct file_pool;
	struct add_torrent_params;
	struct cache_status;
	struct disk_buffer_holder;
	struct counters;

	enum class status_t : std::uint8_t
	{
		// return values from check_fastresume, and move_storage
		no_error,
		fatal_disk_error,
		need_full_check,
		file_exist
	};

	struct TORRENT_EXTRA_EXPORT disk_interface
	{
		enum flags_t
		{
			sequential_access = 0x1,

			// this flag is set on a job when a read operation did
			// not hit the disk, but found the data in the read cache.
			cache_hit = 0x2,

			// don't keep the read block in cache
			volatile_read = 0x10,
		};

		virtual void async_read(storage_interface* storage, peer_request const& r
			, std::function<void(aux::block_cache_reference ref, char* block
				, int flags, storage_error const& se)> handler, void* requester, std::uint8_t flags = 0) = 0;
		virtual void async_write(storage_interface* storage, peer_request const& r
			, disk_buffer_holder buffer
			, std::function<void(storage_error const&)> handler
			, std::uint8_t flags = 0) = 0;
		virtual void async_hash(storage_interface* storage, int piece, std::uint8_t flags
			, std::function<void(int, sha1_hash const&, storage_error const&)> handler, void* requester) = 0;
		virtual void async_move_storage(storage_interface* storage, std::string const& p, std::uint8_t flags
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) = 0;
		virtual void async_release_files(storage_interface* storage
			, std::function<void()> handler = std::function<void()>()) = 0;
		virtual void async_check_files(storage_interface* storage
			, add_torrent_params const* resume_data
			, std::vector<std::string>& links
			, std::function<void(status_t, storage_error const&)> handler) = 0;
		virtual void async_flush_piece(storage_interface* storage, int piece
			, std::function<void()> handler = std::function<void()>()) = 0;
		virtual void async_stop_torrent(storage_interface* storage
			, std::function<void()> handler = std::function<void()>()) = 0;
		virtual void async_rename_file(storage_interface* storage, int index, std::string const& name
			, std::function<void(std::string const&, int, storage_error const&)> handler) = 0;
		virtual void async_delete_files(storage_interface* storage, int options
			, std::function<void(storage_error const&)> handler) = 0;
		virtual void async_set_file_priority(storage_interface* storage
			, std::vector<std::uint8_t> const& prio
			, std::function<void(storage_error const&)> handler) = 0;

		virtual void async_clear_piece(storage_interface* storage, int index
			, std::function<void(int)> handler) = 0;
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
