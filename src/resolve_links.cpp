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
#include "libtorrent/aux_/ceil_div.hpp"

namespace libtorrent::aux {

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS

// piece hashes can only be compared between torrents if they don't also
// cover bytes of other files
bool piece_aligned(file_storage const& fs, file_index_t f)
{
	std::int64_t const piece_size = fs.piece_length();
	if ((fs.file_offset(f) % piece_size) != 0)
		return false;
	if (((fs.file_offset(f) + fs.file_size(f)) % piece_size) == 0)
		return true;
	++f;
	if (f == fs.end_file())
		return true;
	return fs.pad_file_at(f) && ((fs.file_offset(f) + fs.file_size(f)) % piece_size) == 0;
}

resolve_links::resolve_links(std::shared_ptr<torrent_info const> ti,
	bool const enforce_trust_domain,
	sha256_hash const& trust_domain)
	: m_torrent_file(std::move(ti))
	, m_enforce_trust_domain(enforce_trust_domain)
	, m_trust_domain(trust_domain)
{
	TORRENT_ASSERT(m_torrent_file);

	int const piece_size = m_torrent_file->piece_length();

	bool const v1 = m_torrent_file->v1();
	bool const v2 = m_torrent_file->v2();

	file_storage const& fs = m_torrent_file->layout();
	m_file_sizes.reserve(aux::numeric_cast<std::size_t>(fs.num_files()));
	for (auto const i : fs.file_range())
	{
		// don't match pad-files, and don't match files that aren't aligned to
		// pieces. Files are matched by comparing piece hashes, so pieces must
		// be aligned and the same size
		if (fs.pad_file_at(i)) continue;
		if (fs.file_size(i) == 0)
			continue;
		if ((fs.file_offset(i) % piece_size) != 0) continue;

		if (v1 && piece_aligned(fs, i))
			m_file_sizes.insert({fs.file_size(i), i});

		if (v2 && !v1)
			m_file_roots.insert({{fs.root(i), fs.file_size(i)}, i});
	}

	m_links.resize(m_torrent_file->num_files());
}

void resolve_links::match(torrent_info const& ti,
	filenames const& fs,
	std::string const& save_path,
	sha256_hash const& trust_domain)
{
	// an SSL torrent's data must not end up seeded into a swarm trusting a
	// different certificate authority just because the piece hashes match.
	// Torrents that aren't SSL torrents share the same all-zero trust
	// domain and are unrestricted by this check.
	if (m_enforce_trust_domain && m_trust_domain != trust_domain)
		return;

	// a linked file must pass m_torrent_file's hash check, so every kind of
	// hash it has must be compared. ti is a seed, its data is known to match
	// all of its hashes
	if (m_torrent_file->v1() && !ti.v1())
		return;
	if (m_torrent_file->v2() && !ti.v2())
		return;

	if (m_torrent_file->v1())
		match_v1(ti, fs, save_path);
	else
		match_v2(fs, save_path);
}

void resolve_links::match_v1(
	torrent_info const& ti
	, filenames const& fs
	, std::string const& save_path)
{
	// only torrents with the same piece size
	if (fs.piece_length() != m_torrent_file->piece_length()) return;

	int const piece_size = fs.piece_length();
	file_storage const& their_layout = ti.layout();

	for (auto const i : fs.file_range())
	{
		// for every file in the other torrent, see if we have one that match
		// it in m_torrent_file

		// if the file base is not aligned to pieces, we're not going to match
		// it anyway (we only compare piece hashes)
		if (!piece_aligned(their_layout, i))
			continue;
		if (fs.pad_file_at(i)) continue;

		std::int64_t const file_size = fs.file_size(i);

		auto const range = m_file_sizes.equal_range(file_size);
		for (auto iter = range.first; iter != range.second; ++iter)
		{
			auto const idx = iter->second;
			TORRENT_ASSERT(idx >= file_index_t(0));
			TORRENT_ASSERT(idx < m_torrent_file->layout().end_file());

			// if we already have found a duplicate for this file, no need
			// to keep looking
			if (!m_links[idx].empty()) continue;

			// a hybrid torrent's v1 and v2 hashes aren't known to agree until
			// its data is checked, so both must match
			if (m_torrent_file->v2() && fs.root(i) != m_torrent_file->layout().root(idx))
				continue;

			// files are aligned and have the same size, now start comparing
			// piece hashes, to see if the files are identical

			// the pieces of the incoming file
			piece_index_t their_piece = fs.map_file(i, 0, 0).piece;
			// the pieces of "this" file (from m_torrent_file)
			piece_index_t our_piece = m_torrent_file->layout().map_file(
				idx, 0, 0).piece;

			int const num_pieces =
				static_cast<int>(aux::ceil_div(file_size, std::int64_t(piece_size)));

			// the last piece may be padded in one torrent and end the other,
			// matching hashes can't be correct for both
			piece_index_t::diff_type const last(num_pieces - 1);
			if (num_pieces > 0
				&& m_torrent_file->layout().piece_size(our_piece + last)
					!= their_layout.piece_size(their_piece + last))
				continue;

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

void resolve_links::match_v2(filenames const& fs, std::string const& save_path)
{
	for (auto const i : fs.file_range())
	{
		// for every file in the other torrent, see if we have one that match
		// it in m_torrent_file
		if (fs.pad_file_at(i)) continue;

		// BEP 52 doesn't distinguish leaf hashes from interior nodes, so a
		// matching root alone doesn't imply identical content
		auto const iter = m_file_roots.find({fs.root(i), fs.file_size(i)});
		if (iter == m_file_roots.end()) continue;

		auto const idx = iter->second;

		TORRENT_ASSERT(idx >= file_index_t(0));
		TORRENT_ASSERT(idx < m_torrent_file->layout().end_file());

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
