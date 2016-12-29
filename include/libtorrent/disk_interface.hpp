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
#include <memory>

#include "libtorrent/units.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/export.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/session_types.hpp"

namespace libtorrent {

	struct storage_interface;
	struct peer_request;
	struct disk_observer;
	struct add_torrent_params;
	struct cache_status;
	struct disk_buffer_holder;
	struct counters;
	struct settings_pack;
	struct storage_params;
	struct storage_error;
	class file_storage;

	struct storage_holder;

	struct file_open_mode_tag;
	using file_open_mode_t = flags::bitfield_flag<std::uint8_t, file_open_mode_tag>;

	namespace file_open_mode
	{
		// open the file for reading only
		constexpr file_open_mode_t read_only{};

		// open the file for writing only
		constexpr file_open_mode_t write_only = 0_bit;

		// open the file for reading and writing
		constexpr file_open_mode_t read_write = 1_bit;

		// the mask for the bits determining read or write mode
		constexpr file_open_mode_t rw_mask = read_only | write_only | read_write;

		// open the file in sparse mode (if supported by the
		// filesystem).
		constexpr file_open_mode_t sparse = 2_bit;

		// don't update the access timestamps on the file (if
		// supported by the operating system and filesystem).
		// this generally improves disk performance.
		constexpr file_open_mode_t no_atime = 3_bit;

		// open the file for random access. This disables read-ahead
		// logic
		constexpr file_open_mode_t random_access = 5_bit;

#ifndef TORRENT_NO_DEPRECATE
		// prevent the file from being opened by another process
		// while it's still being held open by this handle
		constexpr file_open_mode_t TORRENT_DEPRECATED locked = 6_bit;
#endif
	}

	// this contains information about a file that's currently open by the
	// libtorrent disk I/O subsystem. It's associated with a single torent.
	struct TORRENT_EXPORT open_file_state
	{
		// the index of the file this entry refers to into the ``file_storage``
		// file list of this torrent. This starts indexing at 0.
		file_index_t file_index;

		// ``open_mode`` is a bitmask of the file flags this file is currently
		// opened with. These are the flags used in the ``file::open()`` function.
		// The flags used in this bitfield are defined by the file_open_mode enum.
		//
		// Note that the read/write mode is not a bitmask. The two least significant bits are used
		// to represent the read/write mode. Those bits can be masked out using the ``rw_mask`` constant.
		file_open_mode_t open_mode;

		// a (high precision) timestamp of when the file was last used.
		time_point last_use;
	};

#ifndef TORRENT_NO_DEPRECATE
	using pool_file_status = open_file_state;
#endif

	struct disk_job_flags_tag;
	using disk_job_flags_t = flags::bitfield_flag<std::uint8_t, disk_job_flags_tag>;

	struct TORRENT_EXPORT disk_interface
	{
		// force making a copy of the cached block, rather
		// than getting a reference to the block already in
		// the cache.
		static constexpr disk_job_flags_t force_copy = 0_bit;

		// hint that there may be more disk operations with sequential access to
		// the file
		static constexpr disk_job_flags_t sequential_access = 3_bit;

		// don't keep the read block in cache
		static constexpr disk_job_flags_t volatile_read = 4_bit;

		virtual storage_holder new_torrent(storage_params p
			, std::shared_ptr<void> const& torrent) = 0;

		virtual void remove_torrent(storage_index_t) = 0;

		virtual void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder, storage_error const&)> handler
			, disk_job_flags_t flags = {}) = 0;
		virtual bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer> o
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t flags = {}) = 0;
		virtual void async_hash(storage_index_t storage, piece_index_t piece, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) = 0;
		virtual void async_move_storage(storage_index_t storage, std::string p, move_flags_t flags
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) = 0;
		virtual void async_release_files(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) = 0;
		virtual void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t>& links
			, std::function<void(status_t, storage_error const&)> handler) = 0;
		virtual void async_stop_torrent(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) = 0;
		virtual void async_rename_file(storage_index_t storage
			, file_index_t index, std::string name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) = 0;
		virtual void async_delete_files(storage_index_t storage, remove_flags_t options
			, std::function<void(storage_error const&)> handler) = 0;
		virtual void async_set_file_priority(storage_index_t storage
			, aux::vector<std::uint8_t, file_index_t> prio
			, std::function<void(storage_error const&)> handler) = 0;

		virtual void async_clear_piece(storage_index_t storage, piece_index_t index
			, std::function<void(piece_index_t)> handler) = 0;

		virtual void update_stats_counters(counters& c) const = 0;

		virtual std::vector<open_file_state> get_status(storage_index_t) const = 0;

		virtual void abort(bool wait) = 0;
		virtual void submit_jobs() = 0;
		virtual void set_settings(settings_pack const* sett) = 0;

		virtual ~disk_interface() {}
	};

	struct storage_holder
	{
		storage_holder() = default;
		storage_holder(storage_index_t idx, disk_interface& disk_io)
			: m_disk_io(&disk_io)
			, m_idx(idx)
		{}
		~storage_holder()
		{
			if (m_disk_io) m_disk_io->remove_torrent(m_idx);
		}

		explicit operator bool() const { return m_disk_io != nullptr; }

		operator storage_index_t() const
		{
			TORRENT_ASSERT(m_disk_io);
			return m_idx;
		}

		void reset()
		{
			if (m_disk_io) m_disk_io->remove_torrent(m_idx);
			m_disk_io = nullptr;
		}

		storage_holder(storage_holder const&) = delete;
		storage_holder& operator=(storage_holder const&) = delete;

		storage_holder(storage_holder&& rhs) noexcept
			: m_disk_io(rhs.m_disk_io)
			, m_idx(rhs.m_idx)
		{
				rhs.m_disk_io = nullptr;
		}

		storage_holder& operator=(storage_holder&& rhs) noexcept
		{
			if (m_disk_io) m_disk_io->remove_torrent(m_idx);
			m_disk_io = rhs.m_disk_io;
			m_idx = rhs.m_idx;
			rhs.m_disk_io = nullptr;
			return *this;
		}
	private:
		disk_interface* m_disk_io = nullptr;
		storage_index_t m_idx{0};
	};

}

#endif
