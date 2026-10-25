/*

Copyright (c) 2017-2020, 2022, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STORAGE_UTILS_HPP_INCLUDE
#define TORRENT_STORAGE_UTILS_HPP_INCLUDE

#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <vector>

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/disk_interface.hpp" // for status_t
#include "libtorrent/session_types.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/disk_job.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp"

namespace libtorrent::aux {

	struct stat_cache;

	// scratch space for copy_file(), reused across calls to avoid
	// re-allocating (and, on POSIX, re-querying the filesystem block size
	// for) the copy buffer for every file. Construct one instance per
	// higher-level operation (e.g. once per move_storage() call, covering
	// all its files) and pass it to every copy_file() call it makes
	struct TORRENT_EXTRA_EXPORT copy_file_buffer
	{
		std::vector<char> buffer;
	};

	// moves the files in file_storage f from ``save_path`` to
	// ``destination_save_path`` according to the rules defined by ``flags``.
	// returns the status code and the new save_path.
	TORRENT_EXTRA_EXPORT std::pair<status_t, std::string>
	move_storage(filenames const& f
		, std::string save_path
		, std::string const& destination_save_path
		, std::function<void(std::string const&, lt::error_code&)> const& move_partfile
		, move_flags_t flags, storage_error& ec);

	// deletes the files on fs from save_path according to options. Options may
	// opt to only delete the partfile
	TORRENT_EXTRA_EXPORT void
	delete_files(filenames const& fs, std::string const& save_path
		, std::string const& part_file, remove_flags_t options, storage_error& ec);

	// renames the file at ``index`` to ``new_filename`` on disk (or, if the
	// file doesn't exist on disk yet, just records the rename in
	// ``renamed`` so it's created under the new name later). A rename that
	// fails because the source and destination are on different
	// filesystems falls back to copying the file and removing the
	// original. ``names`` resolves the file's current on-disk path,
	// honoring any rename already recorded; ``renamed`` is where this
	// call's own rename is recorded.
	TORRENT_EXTRA_EXPORT void rename_file(filenames const& names,
		renamed_files& renamed,
		file_index_t index,
		std::string const& new_filename,
		std::string const& save_path,
		storage_error& ec);

	TORRENT_EXTRA_EXPORT bool
	verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, filenames const& fs
		, aux::vector<download_priority_t, file_index_t> const& file_priority
		, stat_cache& stat
		, std::string const& save_path
		, storage_error& ec);

	// given the save_path, stat all files on file_storage until one exists. If a
	// file exists, return true, otherwise return false.
	TORRENT_EXTRA_EXPORT bool has_any_file(
		filenames const& fs
		, std::string const& save_path
		, stat_cache& cache
		, storage_error& ec);

	// a cleared bit lets the hash check skip the piece, so it must be proof
	// of absence. An empty ``partfile_has_piece`` (no part file open) is
	// not that proof for the dont_download files redirected to it
	TORRENT_EXTRA_EXPORT typed_bitfield<piece_index_t> check_pieces(filenames const& fs,
		aux::vector<download_priority_t, file_index_t> const& file_priority,
		stat_cache& stat,
		std::string const& save_path,
		std::function<bool(file_index_t)> const& use_partfile,
		std::function<bool(piece_index_t)> const& partfile_has_piece);

	// a file and the byte range within it to speculatively advise the OS
	// to prefetch; see get_read_ahead_range()
	struct TORRENT_EXTRA_EXPORT read_ahead_range
	{
		file_index_t file_index;
		std::int64_t file_offset;
		std::int64_t length;
	};

	// computes the byte range to speculatively read ahead into, for
	// read_ahead_blocks blocks beyond piece_offset in piece, truncated to
	// the rest of whichever file piece_offset falls into. read_ahead_blocks
	// is expected to already be bounded to the current piece; this only
	// clamps to the file, since piece and file boundaries don't generally
	// coincide. returns nullopt if there's nothing to advise on: a pad
	// file, or a skipped, zero-priority file served from the part file,
	// neither of which have real on-disk data at this location
	TORRENT_EXTRA_EXPORT std::optional<read_ahead_range> get_read_ahead_range(
		file_storage const& fs,
		aux::vector<download_priority_t, file_index_t> const& file_priority,
		std::function<bool(file_index_t)> const& use_partfile,
		piece_index_t piece,
		int piece_offset,
		int read_ahead_blocks);

	TORRENT_EXTRA_EXPORT int read_zeroes(span<char> bufs);

	TORRENT_EXTRA_EXPORT int hash_zeroes(hasher& ph, std::int64_t size);

	TORRENT_EXTRA_EXPORT void initialize_storage(
		filenames const& fs
		, std::string const& save_path
		, stat_cache& sc
		, aux::vector<download_priority_t, file_index_t> const& file_priority
		, std::function<void(file_index_t, storage_error&)> create_file
		, std::function<void(std::string const&, std::string const&, storage_error&)> create_link
		, std::function<void(file_index_t, std::int64_t)> oversized_file
		, storage_error& ec);

	TORRENT_EXTRA_EXPORT void create_symlink(
		std::string const& target
		, std::string const& link
		, storage_error& ec);

	TORRENT_EXTRA_EXPORT void move_file(
		std::string const& f, std::string const& newf, storage_error& se);

	TORRENT_EXTRA_EXPORT void copy_file(
		std::string const& f, std::string const& newf, storage_error& se, copy_file_buffer& buf);

	// upper bound on how many job::read jobs collect_read_batch() merges into
	// one read call, bounding the iovec array and the queue scan regardless
	// of queue depth.
	constexpr int max_read_batch_blocks = 128;

	// pulls other queued job::read jobs with the same storage, piece and a
	// contiguous offset into batch, stopping at the first non-matching job
	// instead of scanning further, since an unbounded scan held the job
	// mutex long enough to stall hash-thread dispatch and regress v2
	// torrents. Safe to merge since every job here is a cache miss reading
	// flushed, immutable data, so nothing races a buffered write. DiskJob
	// only needs a `storage` member comparable via get().
	// precondition: the job mutex must be held when this is called
	template <typename DiskJob>
	void collect_read_batch(DiskJob* first, disk_io_thread_pool& pool, jobqueue_t& batch)
	{
		auto const& fa = std::get<job::read>(first->action);
		auto const storage = first->storage.get();
		piece_index_t const piece = fa.piece;
		std::int32_t end_offset = fa.offset + std::int32_t(fa.buffer_size);

		pool.extract_matching(batch, max_read_batch_blocks - 1, [&](disk_job* j) {
			if (j->get_type() != job_action_t::read)
				return false;
			if (j->state & disk_job::aborted)
				return false;
			auto* const pj = static_cast<DiskJob*>(j);
			if (pj->storage.get() != storage)
				return false;
			auto const& a = std::get<job::read>(j->action);
			if (a.piece != piece)
				return false;
			if (a.offset != end_offset)
				return false;
			end_offset += std::int32_t(a.buffer_size);
			return true;
		});
	}
}

#endif
