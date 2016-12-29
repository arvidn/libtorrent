/*

Copyright (c) 2017, Arvid Norberg
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

#ifndef TORRENT_DISABLED_DISK_IO
#define TORRENT_DISABLED_DISK_IO

#include "libtorrent/config.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/aux_/session_settings.hpp"

#include <vector>

namespace libtorrent {

	struct counters;

	// This is a dummy implementation of the disk_interface. It discards any data
	// written to it and when reading, it returns zeroes
	struct TORRENT_EXTRA_EXPORT disabled_disk_io final
		: disk_interface
		, buffer_allocator_interface
	{
		disabled_disk_io(io_service& ios
			, counters& cnt
			, int block_size = 16 * 1024);

		void set_settings(settings_pack const* sett) override;
		storage_holder new_torrent(storage_params params
			, std::shared_ptr<void> const& torrent) override;
		void remove_torrent(storage_index_t) override;

		void abort(bool) override {}

		void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder, storage_error const&)> handler
			, disk_job_flags_t flags = {}) override;
		bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer> o
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t flags = {}) override;
		void async_hash(storage_index_t storage, piece_index_t piece, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) override;
		void async_move_storage(storage_index_t storage, std::string p, move_flags_t flags
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) override;
		void async_release_files(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) override;
		void async_delete_files(storage_index_t storage, remove_flags_t options
			, std::function<void(storage_error const&)> handler) override;
		void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t>& links
			, std::function<void(status_t, storage_error const&)> handler) override;
		void async_rename_file(storage_index_t storage, file_index_t index, std::string name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) override;
		void async_stop_torrent(storage_index_t storage
			, std::function<void()> handler) override;
		void async_set_file_priority(storage_index_t storage
			, aux::vector<std::uint8_t, file_index_t> prio
			, std::function<void(storage_error const&)> handler) override;

		void async_clear_piece(storage_index_t storage, piece_index_t index
			, std::function<void(piece_index_t)> handler) override;

		// implements buffer_allocator_interface
		void free_disk_buffer(char* b, aux::block_cache_reference const&) override
		{ m_buffer_pool.free_buffer(b); }

		void update_stats_counters(counters& c) const override;

		std::vector<open_file_state> get_status(storage_index_t) const override
		{ return {}; }

		// this submits all queued up jobs to the thread
		void submit_jobs() override {}

	private:

		aux::session_settings m_settings;

		// disk cache
		disk_buffer_pool m_buffer_pool;

		// this is the main thread io_service. Callbacks are
		// posted on this in order to have them execute in
		// the main thread.
		io_service& m_ios;
	};

	TORRENT_EXPORT std::unique_ptr<disk_interface> disabled_disk_io_constructor(
		io_service& ios, counters& cnt);
}

#endif

