/*

Copyright (c) 2015-2019, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
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

#include "libtorrent/resolve_links.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace libtorrent {

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
resolve_links::resolve_links(std::shared_ptr<torrent_info> ti)
	: m_torrent_file(ti)
{
	TORRENT_ASSERT(ti);

	int piece_size = ti->piece_length();

	file_storage const& fs = ti->files();
	m_file_sizes.reserve(aux::numeric_cast<std::size_t>(fs.num_files()));
	for (auto const i : fs.file_range())
	{
		// don't match pad-files, and don't match files that aren't aligned to
		// pieces. Files are matched by comparing piece hashes, so pieces must
		// be aligned and the same size
		if (fs.pad_file_at(i)) continue;
		if ((fs.file_offset(i) % piece_size) != 0) continue;

		m_file_sizes.insert(std::make_pair(fs.file_size(i), i));
	}

	m_links.resize(m_torrent_file->num_files());
}

void resolve_links::match(std::shared_ptr<const torrent_info> const& ti
	, std::string const& save_path)
{
	if (!ti) return;

	// only torrents with the same piece size
	if (ti->piece_length() != m_torrent_file->piece_length()) return;

	int piece_size = ti->piece_length();

	file_storage const& fs = ti->files();
	m_file_sizes.reserve(aux::numeric_cast<std::size_t>(fs.num_files()));
	for (auto const i : fs.file_range())
	{
		// for every file in the other torrent, see if we have one that match
		// it in m_torrent_file

		// if the file base is not aligned to pieces, we're not going to match
		// it anyway (we only compare piece hashes)
		if ((fs.file_offset(i) % piece_size) != 0) continue;
		if (fs.pad_file_at(i)) continue;

		std::int64_t const file_size = fs.file_size(i);

		auto const range = m_file_sizes.equal_range(file_size);
		for (auto iter = range.first; iter != range.second; ++iter)
		{
			TORRENT_ASSERT(iter->second >= file_index_t(0));
			TORRENT_ASSERT(iter->second < m_torrent_file->files().end_file());

			// if we already have found a duplicate for this file, no need
			// to keep looking
			if (m_links[iter->second].ti) continue;

			// files are aligned and have the same size, now start comparing
			// piece hashes, to see if the files are identical

			// the pieces of the incoming file
			piece_index_t their_piece = fs.map_file(i, 0, 0).piece;
			// the pieces of "this" file (from m_torrent_file)
			piece_index_t our_piece = m_torrent_file->files().map_file(
				iter->second, 0, 0).piece;

			int num_pieces = int((file_size + piece_size - 1) / piece_size);

			bool match = true;
			for (int p = 0; p < num_pieces; ++p, ++their_piece, ++our_piece)
			{
				if (m_torrent_file->hash_for_piece(our_piece)
					!= ti->hash_for_piece(their_piece))
				{
					match = false;
					break;
				}
			}
			if (!match) continue;

			m_links[iter->second].ti = ti;
			m_links[iter->second].save_path = save_path;
			m_links[iter->second].file_idx = i;

			// since we have a duplicate for this file, we may as well remove
			// it from the file-size map, so we won't find it again.
			m_file_sizes.erase(iter);
			break;
		}
	}

}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

} // namespace libtorrent
