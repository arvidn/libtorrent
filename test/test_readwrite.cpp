/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "test_utils.hpp"

#include "libtorrent/aux_/readwrite.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/units.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

using namespace lt;

namespace {

// fill buf with bytes 0, 1, 2, ... (wrapping at 256), starting from offset
void fill_bytes(span<char> buf, int offset = 0)
{
	for (char& v : buf)
		v = char((offset++) & 0xff);
}

// return true if buf contains the expected sequence starting at offset
bool check_bytes(span<char const> buf, int offset)
{
	for (char const v : buf)
	{
		if (v != char(offset & 0xff)) return false;
		++offset;
	}
	return true;
}

struct advance_test_case
{
	char const* name;
	std::vector<int> buf_sizes; // sizes of the input buffers
	int bytes; // how many bytes to advance
	std::vector<int> expected; // expected sizes of remaining buffers after advance
};

std::vector<advance_test_case> const advance_cases = {
	// advance within the first buffer
	{ "partial_first", {10, 20, 30}, 5, {5, 20, 30} },
	// advance exactly one buffer -- must not leave a zero-size leading span
	{ "exact_first", {10, 20, 30}, 10, {20, 30} },
	// advance past the first buffer, stopping in the middle of the second
	{ "into_second", {10, 20, 30}, 15, {15, 30} },
	// advance exactly two buffers -- another exact-boundary edge case
	{ "exact_two", {10, 20, 30}, 30, {30} },
	// advance all bytes - nothing remains
	{ "all", {10, 20, 30}, 60, {} },
	// single-buffer: advance partway
	{ "single_partial", {20}, 7, {13} },
	// single-buffer: advance exactly - nothing remains
	{ "single_exact", {20}, 20, {} },
	// equal-size buffers: advance exactly two (common in disk path: 16 KiB blocks)
	{ "equal_exact_two", {16, 16, 16, 16}, 32, {16, 16} },
	// equal-size buffers: advance into the third
	{ "equal_into_third", {16, 16, 16, 16}, 40, {8, 16} },
	// advance zero bytes - must be a no-op
	{ "zero", {10, 20, 30}, 0, {10, 20, 30} },
};

TORRENT_TEST(advance_bufs)
{
	for (auto const& tc : advance_cases)
	{
		// allocate and fill buffers
		int total = 0;
		for (int s : tc.buf_sizes) total += s;
		std::vector<char> backing(static_cast<std::size_t>(total));
		fill_bytes(backing);

		std::vector<span<char const>> spans;
		spans.reserve(tc.buf_sizes.size());
		int off = 0;
		for (int s : tc.buf_sizes)
		{
			spans.push_back(span<char const>(backing.data() + off, s));
			off += s;
		}

		span<span<char const>> bufs(spans.data()
			, static_cast<std::ptrdiff_t>(spans.size()));
		bufs = aux::advance_bufs(bufs, tc.bytes);

		// check sizes
		TEST_EQUAL(int(bufs.size()), int(tc.expected.size()));
		if (int(bufs.size()) != int(tc.expected.size()))
		{
			std::cout << "advance_bufs case '" << tc.name << "' FAILED: got "
				<< bufs.size() << " bufs, expected " << tc.expected.size() << '\n';
			continue;
		}

		for (std::ptrdiff_t i = 0; i < bufs.size(); ++i)
		{
			if (int(bufs[i].size()) != tc.expected[std::size_t(i)])
			{
				std::cout << "advance_bufs case '" << tc.name << "' FAILED: buf["
					<< i << "].size()=" << bufs[i].size() << ", expected " << tc.expected[std::size_t(i)] << '\n';
				TEST_EQUAL(int(bufs[i].size()), tc.expected[std::size_t(i)]);
			}
		}

		// check that no leading span is zero-size (the edge case that was buggy)
		if (!bufs.empty())
			TEST_CHECK(bufs[0].size() > 0);

		// check content: remaining bytes should start at position tc.bytes
		int content_offset = tc.bytes;
		for (auto const& b : bufs)
		{
			TEST_CHECK(check_bytes(b, content_offset));
			content_offset += int(b.size());
		}
		TEST_EQUAL(content_offset, total);
	}
}

// per-file call record: what file, at what offset, and which bytes
struct write_record
{
	file_index_t file;
	std::int64_t offset;
	std::vector<char> data;
};

file_storage make_fs(std::vector<std::int64_t> const& file_sizes, int const piece_length)
{
	file_storage fs;
	int i = 0;
	for (std::int64_t const sz : file_sizes)
		fs.add_file(combine_path("t", "f" + std::to_string(i++)), sz);
	fs.set_piece_length(piece_length);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	return fs;
}

// readwrite_single_vs_vec
//
// For each case: run readwrite() (single flat buffer) and readwrite_vec() (same
// data as a single-element span) and assert the per-file call logs are identical.

struct rw_case
{
	char const* name;
	std::vector<std::int64_t> file_sizes;
	int piece_length;
	int piece;
	int offset;
	int length;
};

std::vector<rw_case> const single_vs_vec_cases = {
	// whole single file, piece-aligned
	{ "single_file", {1000}, 512, 0, 0, 1000 },
	// two files, fits in one piece
	{ "two_files", {300, 700}, 1024, 0, 0, 1000 },
	// four files across multiple pieces (original case)
	{ "four_files_multi_piece", {3, 9, 81, 6561}, 4096, 0, 0, 3 + 9 + 81 + 6561 },
	// non-zero offset, transfer crosses a file boundary
	{ "nonzero_offset", {300, 300}, 512, 0, 100, 200 },
	// start in second piece, transfer crosses a file boundary
	{ "start_second_piece", {600, 600}, 512, 1, 0, 100 },
	// zero-size file in the middle (must be skipped silently)
	{ "zero_size_mid", {100, 0, 200}, 512, 0, 0, 300 },
};

TORRENT_TEST(readwrite_single_vs_vec)
{
	for (auto const& tc : single_vs_vec_cases)
	{
		file_storage const fs = make_fs(tc.file_sizes, tc.piece_length);

		std::vector<char> buf(static_cast<std::size_t>(tc.length));
		fill_bytes(buf);

		std::vector<write_record> single_log;
		std::vector<write_record> vec_log;

		storage_error ec;
		piece_index_t const piece{tc.piece};

		// single-buffer path
		aux::readwrite(fs, span<char const>(buf), piece, tc.offset, ec
			, [&](file_index_t const fi, std::int64_t const fo
				, span<char const> b, storage_error&)
			{
				single_log.push_back({fi, fo, std::vector<char>(b.begin(), b.end())});
				return int(b.size());
			});
		if (ec)
		{
			std::cout << "readwrite_single_vs_vec case '" << tc.name << "' FAILED: single path error\n";
			TEST_CHECK(!ec);
			continue;
		}

		// vec path with a single-element span -- must produce identical calls
		span<char const> single_span{buf};
		aux::readwrite_vec(fs
			, span<span<char const> const>{&single_span, 1}
			, piece, tc.offset, ec
			, [&](file_index_t const fi, std::int64_t const fo
				, span<span<char const> const> bufs, storage_error&)
			{
				// a single input buffer can never produce more than one span per file
				TEST_EQUAL(int(bufs.size()), 1);
				auto const& b = bufs[0];
				vec_log.push_back({fi, fo, std::vector<char>(b.begin(), b.end())});
				return int(b.size());
			});
		if (ec)
		{
			std::cout << "readwrite_single_vs_vec case '" << tc.name << "' FAILED: vec path error\n";
			TEST_CHECK(!ec);
			continue;
		}

		if (int(single_log.size()) != int(vec_log.size()))
		{
			std::cout << "readwrite_single_vs_vec case '" << tc.name << "' FAILED: single="
				<< single_log.size() << " calls, vec=" << vec_log.size() << " calls\n";
			TEST_EQUAL(int(single_log.size()), int(vec_log.size()));
			continue;
		}

		for (std::size_t i = 0; i < single_log.size(); ++i)
		{
			if (static_cast<int>(single_log[i].file) != static_cast<int>(vec_log[i].file)
				|| single_log[i].offset != vec_log[i].offset
				|| single_log[i].data != vec_log[i].data)
			{
				std::cout << "readwrite_single_vs_vec case '" << tc.name << "' FAILED: call[" << i << "] mismatch\n";
				TEST_EQUAL(static_cast<int>(single_log[i].file), static_cast<int>(vec_log[i].file));
				TEST_EQUAL(single_log[i].offset, vec_log[i].offset);
				TEST_CHECK(single_log[i].data == vec_log[i].data);
			}
		}
	}
}

// readwrite_vec_multi_buf
//
// For each case: split the transfer data into multiple input buffers, run
// readwrite_vec(), assemble per-file data from all spans in each lambda call,
// and compare against readwrite() (single flat buffer) as the reference.
// Exercises advance_bufs() when buffer boundaries align with or straddle file
// boundaries.

struct rw_vec_case
{
	char const* name;
	std::vector<std::int64_t> file_sizes;
	int piece_length;
	int piece;
	int offset;
	std::vector<int> buf_sizes; // how to split the transfer data
};

std::vector<rw_vec_case> const vec_multi_buf_cases = {
	// all input buffers reside within a single file
	{ "bufs_all_in_one_file", {200, 200}, 256, 0, 0, {50, 50, 50, 50} },
	// buffer boundary aligns exactly with file boundary -- advance_bufs exact case
	{ "buf_boundary_at_file_boundary", {100, 100}, 256, 0, 0, {100, 100} },
	// buffer boundary falls in the middle of a file
	{ "buf_boundary_mid_file", {150, 150}, 512, 0, 0, {100, 200} },
	// many small buffers spanning two files
	{ "many_small_bufs", {60, 60}, 256, 0, 0, {12, 12, 12, 12, 12, 12, 12, 12, 12, 12} },
	// 16 KiB files with exactly aligned 16 KiB buffers (advance_bufs exact-boundary)
	{ "block_aligned_exact", {16384, 16384}, 16384, 0, 0, {16384, 16384} },
	// 16 KiB files with buffers that straddle the file boundary
	{ "block_aligned_straddled", {16384, 16384}, 16384, 0, 0, {8192, 16384, 8192} },
	// single buffer spanning multiple small files
	{ "single_buf_multi_file", {50, 50, 50, 50}, 512, 0, 0, {200} },
	// unequal file and buffer sizes: one file receives spans from two different buffers
	{ "unequal", {30, 70, 50}, 256, 0, 0, {40, 60, 50} },
};

TORRENT_TEST(readwrite_vec_multi_buf)
{
	for (auto const& tc : vec_multi_buf_cases)
	{
		int total = 0;
		for (int s : tc.buf_sizes) total += s;

		file_storage const fs = make_fs(tc.file_sizes, tc.piece_length);

		std::vector<char> backing(static_cast<std::size_t>(total));
		fill_bytes(backing);

		// build the multi-buffer span array
		std::vector<span<char const>> spans;
		spans.reserve(tc.buf_sizes.size());
		int off = 0;
		for (int s : tc.buf_sizes)
		{
			spans.push_back(span<char const>(backing.data() + off, s));
			off += s;
		}

		piece_index_t const piece{tc.piece};

		// reference: single flat buffer via readwrite()
		std::vector<write_record> ref_log;
		storage_error ec;
		aux::readwrite(fs, span<char const>(backing.data(), total), piece, tc.offset, ec
			, [&](file_index_t const fi, std::int64_t const fo
				, span<char const> b, storage_error&)
			{
				ref_log.push_back({fi, fo, std::vector<char>(b.begin(), b.end())});
				return int(b.size());
			});
		if (ec)
		{
			std::cout << "readwrite_vec_multi_buf case '" << tc.name << "' FAILED: ref path error\n";
			TEST_CHECK(!ec);
			continue;
		}

		// multi-buffer vec path: assemble per-file data from all spans in each call
		std::vector<write_record> vec_log;
		span<span<char const> const> vec_bufs(spans.data()
			, static_cast<std::ptrdiff_t>(spans.size()));
		aux::readwrite_vec(fs, vec_bufs, piece, tc.offset, ec
			, [&](file_index_t const fi, std::int64_t const fo
				, span<span<char const> const> bufs, storage_error&)
			{
				std::vector<char> data;
				for (auto const& b : bufs)
					data.insert(data.end(), b.begin(), b.end());
				int const n = int(data.size());
				vec_log.push_back({fi, fo, std::move(data)});
				return n;
			});
		if (ec)
		{
			std::cout << "readwrite_vec_multi_buf case '" << tc.name << "' FAILED: vec path error\n";
			TEST_CHECK(!ec);
			continue;
		}

		if (int(ref_log.size()) != int(vec_log.size()))
		{
			std::cout << "readwrite_vec_multi_buf case '" << tc.name << "' FAILED: ref="
				<< ref_log.size() << " calls, vec=" << vec_log.size() << " calls\n";
			TEST_EQUAL(int(ref_log.size()), int(vec_log.size()));
			continue;
		}

		for (std::size_t i = 0; i < ref_log.size(); ++i)
		{
			if (static_cast<int>(ref_log[i].file) != static_cast<int>(vec_log[i].file)
				|| ref_log[i].offset != vec_log[i].offset
				|| ref_log[i].data != vec_log[i].data)
			{
				std::cout << "readwrite_vec_multi_buf case '" << tc.name << "' FAILED: call[" << i << "] mismatch\n";
				TEST_EQUAL(static_cast<int>(ref_log[i].file), static_cast<int>(vec_log[i].file));
				TEST_EQUAL(ref_log[i].offset, vec_log[i].offset);
				TEST_CHECK(ref_log[i].data == vec_log[i].data);
			}
		}
	}
}

} // anonymous namespace
