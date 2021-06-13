/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_APPLY_PAD_FILES_HPP_INCLUDED
#define TORRENT_APPLY_PAD_FILES_HPP_INCLUDED

#include "libtorrent/file_storage.hpp"
#include "libtorrent/units.hpp"

namespace libtorrent {
namespace aux {

// calls fun for every piece that overlaps a pad file, passing in the number
// of bytes, in that piece, that's a padfile
template <typename Fun>
void apply_pad_files(file_storage const& fs, Fun&& fun)
{
	for (auto const i : fs.file_range())
	{
		if (!fs.pad_file_at(i) || fs.file_size(i) == 0) continue;

		// pr points to the last byte of the pad file
		peer_request const pr = fs.map_file(i, fs.file_size(i) - 1, 0);

		int const piece_size = fs.piece_length();

		// This pad file may be the last file in the torrent, and the
		// last piece may have an odd size.
		if ((pr.start + 1) % piece_size != 0 && i < prev(fs.end_file()))
		{
			// this is a pre-requisite of the piece picker. Pad files
			// that don't align with pieces are kind of useless anyway.
			// They probably aren't real padfiles, treat them as normal
			// files.
			continue;
		}

		// A pad file may span multiple pieces. This is especially
		// likely in v2 torrents where file sizes are aligned to powers
		// of two pieces. We loop from the end of the pad file
		//
		// For example, we may have this situation:
		//
		//                  pr.start
		//                   |
		//                   v
		// +-----+-----+-----+
		// |   ##|#####|#####|
		// +-----+-----+-----+
		//     \             /
		//      - file_size -
		//
		// We need to declare all #-parts of the pieces as pad bytes to
		// the piece picker.

		piece_index_t piece = pr.piece;
		std::int64_t pad_bytes_left = fs.file_size(i);

		while (pad_bytes_left > 0)
		{
			// The last piece may have an odd size, that's why
			// we ask for the piece size for every piece. (it would be
			// odd, but it's still possible).
			int const bytes = int(std::min(pad_bytes_left, std::int64_t(fs.piece_size(piece))));
			TORRENT_ASSERT(bytes > 0);
			fun(piece, bytes);
			pad_bytes_left -= bytes;
			--piece;
		}
	}
}

} // namespace aux
} // namespace libtorrent

#endif
