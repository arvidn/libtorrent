/*

Copyright (c) 2012-2013, Arvid Norberg
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

#include <boost/function/function1.hpp>
#include <boost/shared_ptr.hpp>

#include "libtorrent/bdecode.hpp"

#include <string>
#include <memory>

namespace libtorrent
{
	struct disk_io_job;
	class piece_manager;
	struct peer_request;
	struct disk_observer;
	struct file_pool;
	struct add_torrent_params;

	struct disk_interface
	{
		virtual void async_read(piece_manager* storage, peer_request const& r
			, boost::function<void(disk_io_job const*)> const& handler, void* requester
			, int flags = 0) = 0;
		virtual void async_write(piece_manager* storage, peer_request const& r
			, disk_buffer_holder& buffer
			, boost::function<void(disk_io_job const*)> const& handler
			, int flags = 0) = 0;
		virtual void async_hash(piece_manager* storage, int piece, int flags
			, boost::function<void(disk_io_job const*)> const& handler, void* requester) = 0;
		virtual void async_move_storage(piece_manager* storage, std::string const& p, int flags
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void async_release_files(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler
			= boost::function<void(disk_io_job const*)>()) = 0;
		virtual void async_check_fastresume(piece_manager* storage
			, bdecode_node const* resume_data
			, std::auto_ptr<std::vector<std::string> >& links
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
#ifndef TORRENT_NO_DEPRECATE
		virtual void async_finalize_file(piece_manager*, int file
			, boost::function<void(disk_io_job const*)> const& handler
			= boost::function<void(disk_io_job const*)>()) = 0;
#endif
		virtual void async_flush_piece(piece_manager* storage, int piece
			, boost::function<void(disk_io_job const*)> const& handler
			= boost::function<void(disk_io_job const*)>()) = 0;
		virtual void async_cache_piece(piece_manager* storage, int piece
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void async_stop_torrent(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler)= 0;
		virtual void async_rename_file(piece_manager* storage, int index, std::string const& name
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void async_delete_files(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void async_save_resume_data(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void async_set_file_priority(piece_manager* storage
			, std::vector<boost::uint8_t> const& prio
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void async_load_torrent(add_torrent_params* params
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void async_tick_torrent(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler) = 0;

		virtual void clear_read_cache(piece_manager* storage) = 0;
		virtual void async_clear_piece(piece_manager* storage, int index
			, boost::function<void(disk_io_job const*)> const& handler) = 0;
		virtual void clear_piece(piece_manager* storage, int index) = 0;

		virtual void update_stats_counters(counters& c) const = 0;
		virtual void get_cache_info(cache_status* ret, bool no_pieces = true
			, piece_manager const* storage = 0) const = 0;

		virtual file_pool& files() = 0;

#if TORRENT_USE_ASSERTS
		virtual bool is_disk_buffer(char* buffer) const = 0;
#endif
	};
}

#endif

