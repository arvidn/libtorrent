/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2018, Alden Torres
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
		// opened with. For possible flags, see file_open_mode_t.
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
	// use custom disk I/O. All functions on the disk subsystem (implementing
	// disk_interface) are called from within libtorrent's network thread. For
	// disk I/O to be performed in a separate thread, the disk subsystem has to
	// manage that itself.
	//
	// Although the functions are called ``async_*``, they do not technically
	// *have* to be asynchronous, but they support being asynchronous, by
	// expecting the result passed back into a callback. The callbacks must be
	// posted back onto the network thread via the io_context object passed into
	// the constructor. The callbacks will be run in the network thread.
	struct TORRENT_EXPORT disk_interface
	{
		// force making a copy of the cached block, rather than getting a
		// reference to a block already in the cache. This is used the block is
		// expected to be overwritten very soon, by async_write()`, and we need
		// access to the previous content.
		static constexpr disk_job_flags_t force_copy = 0_bit;

		// hint that there may be more disk operations with sequential access to
		// the file
		static constexpr disk_job_flags_t sequential_access = 3_bit;

		// don't keep the read block in cache. This is a hint that this block is
		// unlikely to be read again anytime soon, and caching it would be
		// wasteful.
		static constexpr disk_job_flags_t volatile_read = 4_bit;

		// compute a v1 piece hash. This is only used by the async_hash() call.
		// If this flag is not set in the async_hash() call, the SHA-1 piece
		// hash does not need to be computed.
		static constexpr disk_job_flags_t v1_hash = 5_bit;

		// this is called when a new torrent is added. The shared_ptr can be
		// used to hold the internal torrent object alive as long as there are
		// outstanding disk operations on the storage.
		// The returned storage_holder is an owning reference to the underlying
		// storage that was just created. It is fundamentally a storage_index_t
		virtual storage_holder new_torrent(storage_params const& p
			, std::shared_ptr<void> const& torrent) = 0;

		// remove the storage with the specified index. This is not expected to
		// delete any files from disk, just to clean up any resources associated
		// with the specified storage.
		virtual void remove_torrent(storage_index_t) = 0;

		// perform a read or write operation from/to the specified storage
		// index and the specified request. When the operation completes, call
		// handler possibly with a disk_buffer_holder, holding the buffer with
		// the result. Flags may be set to affect the read operation. See
		// disk_job_flags_t.
		//
		// The disk_observer is a callback to indicate that
		// the store buffer/disk write queue is below the watermark to let peers
		// start writing buffers to disk again. When ``async_write()`` returns
		// ``true``, indicating the write queue is full, the peer will stop
		// further writes and wait for the passed-in ``disk_observer`` to be
		// notified before resuming.
		//
		// Note that for ``async_read``, the peer_request (``r``) is not
		// necessarily aligned to blocks (but it is most of the time). However,
		// all writes (passed to ``async_write``) are guaranteed to be block
		// aligned.
		virtual void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder, storage_error const&)> handler
			, disk_job_flags_t flags = {}) = 0;
		virtual bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer> o
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t flags = {}) = 0;

		// Compute hash(es) for the specified piece. Unless the v1_hash flag is
		// set (in ``flags``), the SHA-1 hash of the whole piece does not need
		// to be computed.
		//
		// The `v2` span is optional and can be empty, which means v2 hashes
		// should not be computed. If v2 is non-empty it must be at least large
		// enough to hold all v2 blocks in the piece, and this function will
		// fill in the span with the SHA-256 block hashes of the piece.
		virtual void async_hash(storage_index_t storage, piece_index_t piece, span<sha256_hash> v2
			, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) = 0;

		// computes the v2 hash (SHA-256) of a single block. The block at
		// ``offset`` in piece ``piece``.
		virtual void async_hash2(storage_index_t storage, piece_index_t piece, int offset, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha256_hash const&, storage_error const&)> handler) = 0;

		// called to request the files for the specified storage/torrent be
		// moved to a new location. It is the disk I/O object's responsibility
		// to synchronize this with any currently outstanding disk operations to
		// the storage. Whether files are replaced at the destination path or
		// not is controlled by ``flags`` (see move_flags_t).
		virtual void async_move_storage(storage_index_t storage, std::string p, move_flags_t flags
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) = 0;

		// This is called on disk I/O objects to request they close all open
		// files for the specified storage/torrent. If file handles are not
		// pooled/cached, it can be a no-op. For truly asynchronous disk I/O,
		// this should provide at least one point in time when all files are
		// closed. It is possible that later asynchronous operations will
		// re-open some of the files, by the time this completion handler is
		// called, that's fine.
		virtual void async_release_files(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) = 0;

		// this is called when torrents are added to validate their resume data
		// against the files on disk. This function is expected to do a few things:
		//
		// if ``links`` is non-empty, it contains a string for each file in the
		// torrent. The string being a path to an existing identical file. The
		// default behavior is to create hard links of those files into the
		// storage of the new torrent (specified by ``storage``). An empty
		// string indicates that there is no known identical file. This is part
		// of the "mutable torrent" feature, where files can be reused from
		// other torrents.
		//
		// The ``resume_data`` points the resume data passed in by the client.
		//
		// If the ``resume_data->flags`` field has the seed_mode flag set, all
		// files/pieces are expected to be on disk already. This should be
		// verified. Not just the existence of the file, but also that it has
		// the correct size.
		//
		// Any file with a piece set in the ``resume_data->have_pieces`` bitmask
		// should exist on disk, this should be verified. Pad files and files
		// with zero priority may be skipped.
		virtual void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t> links
			, std::function<void(status_t, storage_error const&)> handler) = 0;

		// This is called when a torrent is stopped. It gives the disk I/O
		// object an opportunity to flush any data to disk that's currently kept
		// cached. This function should at least do the same thing as
		// async_release_files().
		virtual void async_stop_torrent(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) = 0;

		// This function is called when the name of a file in the specified
		// storage has been requested to be renamed. The disk I/O object is
		// responsible for renaming the file without racing with other
		// potentially outstanding operations against the file (such as read,
		// write, move, etc.).
		virtual void async_rename_file(storage_index_t storage
			, file_index_t index, std::string name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) = 0;

		// This function is called when some file(s) on disk have been requested
		// to be removed by the client. ``storage`` indicates which torrent is
		// referred to. See session_handle for ``remove_flags_t`` flags
		// indicating which files are to be removed.
		// e.g. session_handle::delete_files - delete all files
		// session_handle::delete_partfile - only delete part file.
		virtual void async_delete_files(storage_index_t storage, remove_flags_t options
			, std::function<void(storage_error const&)> handler) = 0;

		// This is called to set the priority of some or all files. Changing the
		// priority from or to 0 may involve moving data to and from the
		// partfile. The disk I/O object is responsible for correctly
		// synchronizing this work to not race with any potentially outstanding
		// asynchronous operations affecting these files.
		//
		// ``prio`` is a vector of the file priority for all files. If it's
		// shorter than the total number of files in the torrent, they are
		// assumed to be set to the default priority.
		virtual void async_set_file_priority(storage_index_t storage
			, aux::vector<download_priority_t, file_index_t> prio
			, std::function<void(storage_error const&
				, aux::vector<download_priority_t, file_index_t>)> handler) = 0;

		// This is called when a piece fails the hash check, to ensure there are
		// no outstanding disk operations to the piece before blocks are
		// re-requested from peers to overwrite the existing blocks. The disk I/O
		// object does not need to perform any action other than synchronize
		// with all outstanding disk operations to the specified piece before
		// posting the result back.
		virtual void async_clear_piece(storage_index_t storage, piece_index_t index
			, std::function<void(piece_index_t)> handler) = 0;

		// update_stats_counters() is called to give the disk storage an
		// opportunity to update gauges in the ``c`` stats counters, that aren't
		// updated continuously as operations are performed. This is called
		// before a snapshot of the counters are passed to the client.
		virtual void update_stats_counters(counters& c) const = 0;

		// Return a list of all the files that are currently open for the
		// specified storage/torrent. This is is just used for the client to
		// query the currently open files, and which modes those files are open
		// in.
		virtual std::vector<open_file_state> get_status(storage_index_t) const = 0;

		// this is called when the session is starting to shut down. The disk
		// I/O object is expected to flush any outstanding write jobs, cancel
		// hash jobs and initiate tearing down of any internal threads. If
		// ``wait`` is true, this should be asynchronous. i.e. this call should
		// not return until all threads have stopped and all jobs have either
		// been aborted or completed and the disk I/O object is ready to be
		// destructed.
		virtual void abort(bool wait) = 0;

		// This will be called after a batch of disk jobs has been issues (via
		// the ``async_*`` ). It gives the disk I/O object an opportunity to
		// notify any potential condition variables to wake up the disk
		// thread(s). The ``async_*`` calls can of course also notify condition
		// variables, but doing it in this call allows for batching jobs, by
		// issuing the notification once for a collection of jobs.
		virtual void submit_jobs() = 0;

		// This is called to notify the disk I/O object that the settings have
		// been updated. In the disk io constructor, a settings_interface
		// reference is passed in. Whenever these settings are updated, this
		// function is called to allow the disk I/O object to react to any
		// changed settings relevant to its operations.
		virtual void settings_updated() = 0;

		// hidden
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
			if (&rhs == this) return *this;
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
