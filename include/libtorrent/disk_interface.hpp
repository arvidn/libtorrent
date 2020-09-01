/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_INTERFACE_HPP
#define TORRENT_DISK_INTERFACE_HPP

#include "libtorrent/bdecode.hpp"

#include <string>
#include <memory>

#include "libtorrent/fwd.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/session_types.hpp"

// OVERVIEW
//
// The disk I/O can be customized in libtorrent. In previous versions, the
// customization was at the level of each torrent. Now, the customization point
// is at the session level. All torrents added to a session will use the same
// disk I/O subsystem, as determined by the disk_io_constructor (in
// session_params).
//
// This allows the disk subsystem to also customize threading and disk job
// management.
//
// To customize the disk subsystem, implement disk_interface and provide a
// factory function to the session constructor (via session_params).
//
// Example use:
//
// .. include:: ../examples/custom_storage.cpp
// 	:code: c++
// 	:tab-width: 2
// 	:start-after: -- example begin
// 	:end-before: // -- example end
namespace libtorrent {

	struct disk_observer;
	struct counters;

	struct storage_holder;

	using file_open_mode_t = flags::bitfield_flag<std::uint8_t, struct file_open_mode_tag>;

	// internal
	// this is a bittorrent constant
	constexpr int default_block_size = 0x4000;

namespace file_open_mode {
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

#if TORRENT_ABI_VERSION == 1
	// prevent the file from being opened by another process
	// while it's still being held open by this handle
	constexpr file_open_mode_t locked TORRENT_DEPRECATED = 6_bit;
#endif
}

	// this contains information about a file that's currently open by the
	// libtorrent disk I/O subsystem. It's associated with a single torrent.
	struct TORRENT_EXPORT open_file_state
	{
		// the index of the file this entry refers to into the ``file_storage``
		// file list of this torrent. This starts indexing at 0.
		file_index_t file_index;

		// ``open_mode`` is a bitmask of the file flags this file is currently
		// opened with. These are the flags used in the ``file::open()`` function.
		// For possible flags, see file_open_mode_t.
		//
		// Note that the read/write mode is not a bitmask. The two least significant bits are used
		// to represent the read/write mode. Those bits can be masked out using the ``rw_mask`` constant.
		file_open_mode_t open_mode;

		// a (high precision) timestamp of when the file was last used.
		time_point last_use;
	};

	using disk_job_flags_t = flags::bitfield_flag<std::uint8_t, struct disk_job_flags_tag>;

	// The disk_interface is the customization point for disk I/O in libtorrent.
	// implement this interface and provide a factory function to the session constructor
	// use custom disk I/O.
	struct TORRENT_EXPORT disk_interface
	{
		// force making a copy of the cached block, rather
		// than getting a reference to the block already in
		// the cache.
		static inline constexpr disk_job_flags_t force_copy = 0_bit;

		// hint that there may be more disk operations with sequential access to
		// the file
		static inline constexpr disk_job_flags_t sequential_access = 3_bit;

		// don't keep the read block in cache
		static inline constexpr disk_job_flags_t volatile_read = 4_bit;

		// compute a v1 piece hash
		static inline constexpr disk_job_flags_t v1_hash = 5_bit;

		virtual storage_holder new_torrent(storage_params const& p
			, std::shared_ptr<void> const& torrent) = 0;

		virtual void remove_torrent(storage_index_t) = 0;

		virtual void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder, storage_error const&)> handler
			, disk_job_flags_t flags = {}) = 0;
		virtual bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer> o
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t flags = {}) = 0;
		// if v2 is non-empty it must be at least large enough to hold all v2 blocks in the piece
		virtual void async_hash(storage_index_t storage, piece_index_t piece, span<sha256_hash> v2
			, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) = 0;
		// async_hash2 computes the v2 hash of a single block
		virtual void async_hash2(storage_index_t storage, piece_index_t piece, int offset, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha256_hash const&, storage_error const&)> handler) = 0;
		virtual void async_move_storage(storage_index_t storage, std::string p, move_flags_t flags
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) = 0;
		virtual void async_release_files(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) = 0;
		virtual void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t> links
			, std::function<void(status_t, storage_error const&)> handler) = 0;
		virtual void async_stop_torrent(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) = 0;
		virtual void async_rename_file(storage_index_t storage
			, file_index_t index, std::string name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) = 0;
		virtual void async_delete_files(storage_index_t storage, remove_flags_t options
			, std::function<void(storage_error const&)> handler) = 0;
		virtual void async_set_file_priority(storage_index_t storage
			, aux::vector<download_priority_t, file_index_t> prio
			, std::function<void(storage_error const&
				, aux::vector<download_priority_t, file_index_t>)> handler) = 0;

		virtual void async_clear_piece(storage_index_t storage, piece_index_t index
			, std::function<void(piece_index_t)> handler) = 0;

		virtual void update_stats_counters(counters& c) const = 0;

		virtual std::vector<open_file_state> get_status(storage_index_t) const = 0;

		virtual void abort(bool wait) = 0;
		virtual void submit_jobs() = 0;
		virtual void settings_updated() = 0;

		virtual ~disk_interface() {}
	};

	// a unique, owning, reference to the storage of a torrent in a disk io
	// subsystem (class that implements disk_interface). This is held by the
	// internal libtorrent torrent object to tie the storage object allocated
	// for a torrent to the lifetime of the internal torrent object. When a
	// torrent is removed from the session, this holder is destructed and will
	// inform the disk object.
	struct TORRENT_EXPORT storage_holder
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

} // namespace libtorrent

#endif
