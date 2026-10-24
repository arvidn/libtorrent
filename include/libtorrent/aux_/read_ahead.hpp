/*

Copyright (c) 2026, ghz
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_READ_AHEAD_HPP_INCLUDED
#define TORRENT_READ_AHEAD_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/storage_defs.hpp" // for storage_index_t
#include "libtorrent/units.hpp" // for piece_index_t
#include "libtorrent/time.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace libtorrent::aux {

// Decides what the pread disk I/O asks the kernel to read ahead for peer
// requests (settings_pack::disk_read_ahead).
//
// Peers request a piece as 16 kiB blocks, which several disk threads read
// concurrently. The kernel sees small, interleaved reads, which its own
// read-ahead doesn't take for a sequential stream, and reads only the
// blocks requested. Instead, a piece being read is split into windows of
// the file system's read-ahead size (1 MiB where it isn't known, and at
// least a 64th of the piece), and a read has its own window and the next
// `ahead` ones hinted, each window once. Like the kernel's read-ahead for
// one sequential reader, that stays a few windows ahead of the reads, here
// for any number of concurrent readers of the piece, and no further.
//
// Near the end of a piece, the windows ahead are those of the pieces
// requested after it from the same storage, in the order they were
// requested. Those reads are queued already: the disk threads get to them
// next. Otherwise, when a piece is no more than a few windows, the threads
// reading it all wait for the same disk read, and the next piece isn't
// hinted until they are done.
//
// Only a run of at least three contiguous blocks of the piece is read
// ahead of: a read of three blocks or more (a batch of coalesced
// requests), or one that makes a run of three with the reads before it,
// in any order. A peer may want only a few scattered blocks of the piece
// (a downloader spreading its requests over many peers, one or two blocks
// per piece from each), and reading ahead of them would read many times
// what is uploaded; two of them may well be next to each other. Like the
// kernel's read-ahead, which waits for sequential reads of a file, here by
// any number of readers of the piece. The last few runs of a piece are
// kept.
//
// At most `max_pieces` pieces are kept: past that, the quarter used
// longest ago is forgotten.
//
// This only keeps the state, and is not thread safe. The caller submits
// the hints.
struct TORRENT_EXTRA_EXPORT read_ahead
{
	// `ahead`: the number of windows hinted past the ones being read.
	// `ttl`: a piece not read for this long is forgotten, and hinted again
	// when read again. `max_pieces`: the most pieces kept
	read_ahead(int ahead, steady_clock::duration ttl, int max_pieces = default_max_pieces);

	enum class action : std::uint8_t
	{
		// nothing to hint
		none,
		// `piece` isn't known: call add_piece() for it, then on_read()
		// again
		lookup,
		// submit `hints`, in order
		hint,
	};

	struct hint
	{
		// the read's piece, or one requested after it
		piece_index_t piece{0};
		// the range to hint, in the piece
		int offset = 0;
		int length = 0;
	};

	struct result
	{
		action what = action::none;
		// the piece to look up
		piece_index_t piece{0};
		std::vector<hint> hints;
	};

	// a read of the piece at `offset` was queued. The pieces of a storage
	// are kept in the order they are first requested, for at least `ttl`.
	// A piece requested again later than that moves to the end
	void on_request(storage_index_t, piece_index_t, int offset, steady_clock::time_point now);

	// a read of [offset, offset + length) of the piece
	result on_read(storage_index_t,
		piece_index_t,
		int piece_size,
		int offset,
		int length,
		steady_clock::time_point now);

	// `limit` is the largest read-ahead hint the kernel takes in one call
	// for the piece's file, in bytes; 0 if it isn't known. The window is
	// that (default_window if 0), but at least a 64th of the piece. -1: the
	// piece has nothing to hint. If the piece has been added already, this
	// does nothing
	void add_piece(storage_index_t,
		piece_index_t,
		int piece_size,
		std::int64_t limit,
		steady_clock::time_point now);

	// forget the pieces of a storage, whose index may be reused
	void remove_storage(storage_index_t);

	// the window size when the limit isn't known: most devices take a
	// POSIX_FADV_WILLNEED of 1 MiB whole
	static constexpr int default_window = 1024 * 1024;
	// the most pieces kept by default: those read or requested within `ttl`
	// at several GiB/s with 1 MiB pieces
	static constexpr int default_max_pieces = 4096;

	std::size_t num_pieces() const { return m_pieces.size(); }
	std::size_t num_requested() const { return m_requested.size(); }

private:
	// the runs of contiguous reads kept per piece
	static constexpr int max_runs = 4;

	struct piece_state
	{
		// bytes per window. 0: the piece isn't hinted. -1: not added yet,
		// only requested
		int window_size = -1;
		int piece_size = 0;
		// the windows hinted, one bit each
		std::uint64_t hinted = 0;
		// the last runs of contiguous reads of the piece, [start, end), since
		// it was added or last not read for `ttl`: the first `num_runs`, and
		// `next_run` is replaced next
		std::array<std::pair<int, int>, max_runs> runs{};
		std::uint8_t num_runs = 0;
		std::uint8_t next_run = 0;
		// the last read of the piece, or hint of a window of it
		steady_clock::time_point used{};
		// the piece's place in m_requested, 0 if it isn't there
		std::uint64_t order = 0;
		// the offset of its first read requested, and when
		int requested_offset = 0;
		steady_clock::time_point requested{};
	};

	// add [start, end) to the runs of the piece: whether it, with the run it
	// extends, is long enough to be read ahead of
	static bool add_run(piece_state& p, int start, int end);
	void sweep(steady_clock::time_point now);
	// past max_pieces, forget the pieces used longest ago, down to three
	// quarters of it; never those used at `now`, the ones of this call
	void trim(steady_clock::time_point now);
	void erase_request(std::uint32_t st, piece_state&);

	// pieces by storage index (high 32 bits) and piece index. Entries
	// neither used nor requested for m_ttl are removed at most once per
	// m_ttl
	std::unordered_map<std::uint64_t, piece_state> m_pieces;
	steady_clock::time_point m_swept{};

	// the pieces requested, by storage index and then in the order they
	// were requested
	std::map<std::pair<std::uint32_t, std::uint64_t>, piece_index_t> m_requested;
	std::uint64_t m_last_order = 0;

	int const m_ahead;
	steady_clock::duration const m_ttl;
	std::size_t const m_max_pieces;
};
}

#endif
