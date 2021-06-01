/*

Copyright (c) 2021, Arvid Norberg
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
