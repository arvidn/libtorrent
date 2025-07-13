/*

Copyright (c) 2015-2016, 2018-2022, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/resolve_links.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace libtorrent::aux {

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
resolve_links::resolve_links(std::shared_ptr<torrent_info const> ti)
	: m_torrent_file(std::move(ti))
{
	TORRENT_ASSERT(m_torrent_file);

	int const piece_size = m_torrent_file->piece_length();

	bool const v1 = m_torrent_file->v1();
	bool const v2 = m_torrent_file->v2();

	file_storage const& fs = m_torrent_file->files();
	m_file_sizes.reserve(aux::numeric_cast<std::size_t>(fs.num_files()));
	for (auto const i : fs.file_range())
	{
		// don't match pad-files, and don't match files that aren't aligned to
		// pieces. Files are matched by comparing piece hashes, so pieces must
		// be aligned and the same size
		if (fs.pad_file_at(i)) continue;
		if ((fs.file_offset(i) % piece_size) != 0) continue;

		if (v1)
			m_file_sizes.insert({fs.file_size(i), i});

		if (v2)
			m_file_roots.insert({fs.root(i), i});
	}

	m_links.resize(m_torrent_file->num_files());
}

void resolve_links::match(torrent_info const& ti, file_storage const fs, std::string const& save_path)
{
	if (m_torrent_file->v2() && ti.v2())
	{
		match_v2(fs, save_path);
	}

	if (m_torrent_file->v1() && ti.v1())
	{
		match_v1(ti, fs, save_path);
	}
}

void resolve_links::match_v1(
	torrent_info const& ti
	, file_storage const& fs
	, std::string const& save_path)
{
	// only torrents with the same piece size
	if (fs.piece_length() != m_torrent_file->piece_length()) return;

	int const piece_size = fs.piece_length();

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
			auto const idx = iter->second;
			TORRENT_ASSERT(idx >= file_index_t(0));
			TORRENT_ASSERT(idx < m_torrent_file->files().end_file());

			// if we already have found a duplicate for this file, no need
			// to keep looking
			if (!m_links[idx].empty()) continue;

			// files are aligned and have the same size, now start comparing
			// piece hashes, to see if the files are identical

			// the pieces of the incoming file
			piece_index_t their_piece = fs.map_file(i, 0, 0).piece;
			// the pieces of "this" file (from m_torrent_file)
			piece_index_t our_piece = m_torrent_file->files().map_file(
				idx, 0, 0).piece;

			int const num_pieces = int((file_size + piece_size - 1) / piece_size);

			bool match = true;
			for (int p = 0; p < num_pieces; ++p, ++their_piece, ++our_piece)
			{
				if (m_torrent_file->hash_for_piece(our_piece)
					!= ti.hash_for_piece(their_piece))
				{
					match = false;
					break;
				}
			}
			if (!match) continue;

			m_links[idx] = fs.file_path(i, save_path);

			// since we have a duplicate for this file, we may as well remove
			// it from the file-size map, so we won't find it again.
			m_file_sizes.erase(iter);
			break;
		}
	}
}

void resolve_links::match_v2(file_storage const& fs, std::string const& save_path)
{
	for (auto const i : fs.file_range())
	{
		// for every file in the other torrent, see if we have one that match
		// it in m_torrent_file
		if (fs.pad_file_at(i)) continue;

		auto const iter = m_file_roots.find(fs.root(i));
		if (iter == m_file_roots.end()) continue;

		auto const idx = iter->second;

		TORRENT_ASSERT(idx >= file_index_t(0));
		TORRENT_ASSERT(idx < m_torrent_file->files().end_file());

		// if we already have found a duplicate for this file, no need
		// to keep looking
		if (!m_links[idx].empty()) continue;

		m_links[idx] = fs.file_path(i, save_path);

		// since we have a duplicate for this file, we may as well remove
		// it from the file-size map, so we won't find it again.
		m_file_roots.erase(iter);
	}
}

#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

} // namespace libtorrent::aux
