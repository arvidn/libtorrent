/*

Copyright (c) 2015-2016, 2018-2020, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/resolve_links.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace lt::aux {

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

} // namespace lt::aux
