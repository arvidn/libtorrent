/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_JOB_HPP
#define TORRENT_DISK_JOB_HPP

#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/tailqueue.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/session_types.hpp"
#include "libtorrent/flags.hpp"

#include <variant>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace libtorrent::aux {

	// internal
	enum class job_action_t : std::uint8_t
	{
		read
		, write
		, hash
		, hash2
		, move_storage
		, release_files
		, delete_files
		, check_fastresume
		, rename_file
		, stop_torrent
		, file_priority
		, clear_piece
		, partial_read
		, kick_hasher
		, num_job_ids
	};

namespace job {

	// partial read jobs are issued when a peer sends unaligned piece requests.
	// i.e. piece offsets that are not aligned to 16 kiB. These are not very
	// common.
	struct partial_read
	{
		std::function<void(disk_buffer_holder block, storage_error const& se)> handler;
		// passed in/out
		disk_buffer_holder buf;

		// passed in
		// The number of bytes to skip into the buffer that we're reading into.
		std::uint16_t buffer_offset;

		// passed in/out
		// number of bytes 'buf' points to
		std::uint16_t buffer_size;

		// passed in
		// the piece to read from
		piece_index_t piece;

		// passed in
		// the offset into the piece the read should start
		// for hash jobs, this is the first block the hash
		// job is still holding a reference to. The end of
		// the range of blocks a hash jobs holds references
		// to is always the last block in the piece.
		std::int32_t offset;
	};

	// read jobs read one block (16 kiB) from a piece.
	struct read
	{
		std::function<void(disk_buffer_holder block, storage_error const& se)> handler;

		// passed out
		disk_buffer_holder buf;
		// passed in/out
		// number of bytes 'buf' points to
		std::uint16_t buffer_size;

		// passed in
		// the piece to read from
		piece_index_t piece;

		// passed in
		// the offset into the piece the read should start
		// for hash jobs, this is the first block the hash
		// job is still holding a reference to. The end of
		// the range of blocks a hash jobs holds references
		// to is always the last block in the piece.
		std::int32_t offset;
	};

	// write jobs write one block (16 kiB) to a piece. These are always aligned
	// to 16 kiB blocks.
	struct write
	{
		std::function<void(storage_error const&)> handler;

		disk_buffer_holder buf;

		// passed in
		// the piece to write to
		piece_index_t piece;

		// passed in
		// the offset into the piece the write should start
		// for hash jobs, this is the first block the hash
		// job is still holding a reference to. The end of
		// the range of blocks a hash jobs holds references
		// to is always the last block in the piece.
		std::int32_t offset;

		// passed in/out
		// number of bytes 'buf' points to
		std::uint16_t buffer_size;
	};

	// the hash jobs computes the SHA-1 hash of a whole piece. If the
	// block_hashes span is non-empty, this job also computes sha-256 hashes for
	// each 16 kiB block. This is used for v2 torrents
	struct hash
	{
		std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler;

		// passed in
		// the piece to hash
		piece_index_t piece;

		// passed in
		span<sha256_hash> block_hashes;

		// passed out
		sha1_hash piece_hash;
	};

	// the hash2 jobs computes the sha-256 hash for a single block (16 kiB).
	// These offsets are always aligned to blocks.
	struct hash2
	{
		using handler_t = std::function<void(piece_index_t, sha256_hash const&, storage_error const&)>;
		handler_t handler;
		// passed in
		// the piece to hash
		piece_index_t piece;

		// this is the first block the hash job is still holding a reference to.
		// The end of the range of blocks a hash jobs holds references to is
		// always the last block in the piece.
		std::int32_t offset;

		// passed out
		sha256_hash piece_hash2;
	};

	// This job requests to move/rename the files on disk for the specified
	// torrent to the new path.
	struct move_storage
	{
		using handler_t = std::function<void(status_t, std::string, storage_error const&)>;
		handler_t handler;

		// passed in
		std::string path;
		// passed in
		move_flags_t move_flags;
	};

	// This job closes the file handles open for this torrent
	struct release_files
	{
		std::function<void()> handler;
	};

	struct delete_files
	{
		std::function<void(storage_error const&)> handler;

		// passed in
		remove_flags_t flags;
	};

	struct check_fastresume
	{
		std::function<void(status_t, storage_error const&)> handler;

		// optional, passed in
		// this is a vector of hard-links to create. Each element corresponds to
		// a file in the file_storage. The string is the absolute path of the
		// identical file to create the hard link to.
		aux::vector<std::string, file_index_t>* links;

		// optional, passed in
		add_torrent_params const* resume_data;
	};

	struct rename_file
	{
		std::function<void(std::string, file_index_t, storage_error const&)> handler;
		// passed in/out
		file_index_t file_index;
		// passed in/out
		std::string name;
	};

	struct stop_torrent
	{
		std::function<void()> handler;
	};

	struct file_priority
	{
		std::function<void(storage_error const&, aux::vector<download_priority_t, file_index_t>)> handler;
		// passed in/out
		aux::vector<download_priority_t, file_index_t> prio;
	};

	struct clear_piece
	{
		std::function<void(piece_index_t)> handler;

		// the piece to clear
		piece_index_t piece;
	};

	struct kick_hasher
	{
		// the piece whose hasher to kick
		piece_index_t piece;
	};
}

	// disk_job is a generic base class to disk io subsystem-specifit jobs (e.g.
	// mmap_disk_job). They are always allocated from the network thread, posted
	// (as pointers) to the disk I/O thread, and then passed back
	// to the network thread for completion handling and to be freed.
	// each disk_job can belong to at most one tailqueue.
	struct TORRENT_EXTRA_EXPORT disk_job : tailqueue_node<disk_job>
	{
		void call_callback();

		// this is set by the storage object when a fence is raised
		// for this job. It means that this no other jobs on the same
		// storage will execute in parallel with this one. It's used
		// to lower the fence when the job has completed
		static inline constexpr disk_job_flags_t fence = 1_bit;

		// this job is currently being performed, or it's hanging
		// on a cache piece that may be flushed soon
		static inline constexpr disk_job_flags_t in_progress = 2_bit;

		// this is set for jobs that we're no longer interested in. Any aborted
		// job that's executed should immediately fail with operation_aborted
		// instead of executing
		static inline constexpr disk_job_flags_t aborted = 6_bit;

		// flags controlling this job
		disk_job_flags_t flags;

		// passed out
		// return value of operation
		status_t ret{};

		// the error code from the file operation
		// on error, this also contains the path of the
		// file the disk operation failed on
		storage_error error;

		std::variant<job::read
			, job::write
			, job::hash
			, job::hash2
			, job::move_storage
			, job::release_files
			, job::delete_files
			, job::check_fastresume
			, job::rename_file
			, job::stop_torrent
			, job::file_priority
			, job::clear_piece
			, job::partial_read
			, job::kick_hasher
		> action;

		// the type of job this is
		job_action_t get_type() const { return job_action_t(action.index()); }

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

#endif // TORRENT_DISK_JOB_HPP

