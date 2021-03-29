/*

Copyright (c) 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016-2017, 2019, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/piece_picker.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/file_progress.hpp"
#include "libtorrent/aux_/invariant_check.hpp"

namespace lt::aux {

	void file_progress::init(piece_picker const& picker, file_storage const& fs)
	{
		INVARIANT_CHECK;

		if (!m_file_progress.empty()) return;

		int const num_files = fs.num_files();

#if TORRENT_USE_INVARIANT_CHECKS
		int const num_pieces = fs.num_pieces();
		m_have_pieces.clear();
		m_have_pieces.resize(num_pieces, false);
		m_file_sizes.clear();
		m_file_sizes.reserve(num_files);
		for (file_index_t i(0); i < fs.end_file(); ++i)
			m_file_sizes.push_back(fs.file_size(i));
#endif

		m_file_progress.resize(num_files, 0);
		std::fill(m_file_progress.begin(), m_file_progress.end(), 0);

		// initialize the progress of each file

		int const piece_size = fs.piece_length();
		std::int64_t off = 0;
		std::int64_t const total_size = fs.total_size();
		file_index_t file_index(0);
		for (piece_index_t piece(0); piece < fs.end_piece(); ++piece, off += piece_size)
		{
			TORRENT_ASSERT(file_index < fs.end_file());
			std::int64_t file_offset = off - fs.file_offset(file_index);
			TORRENT_ASSERT(file_offset >= 0);
			while (file_offset >= fs.file_size(file_index))
			{
				++file_index;
				TORRENT_ASSERT(file_index < fs.end_file());
				file_offset = off - fs.file_offset(file_index);
				TORRENT_ASSERT(file_offset >= 0);
			}
			TORRENT_ASSERT(file_offset <= fs.file_size(file_index));

			if (!picker.have_piece(piece)) continue;

#if TORRENT_USE_INVARIANT_CHECKS
			m_have_pieces.set_bit(piece);
#endif

			TORRENT_ASSERT(total_size >= off);
			std::int64_t size = std::min(std::int64_t(piece_size), total_size - off);
			TORRENT_ASSERT(size >= 0);

			while (size)
			{
				std::int64_t const add = std::min(size, fs.file_size(file_index) - file_offset);
				TORRENT_ASSERT(add >= 0);
				m_file_progress[file_index] += add;

				TORRENT_ASSERT(m_file_progress[file_index]
					<= fs.file_size(file_index));

				size -= add;
				TORRENT_ASSERT(size >= 0);
				if (size > 0)
				{
					++file_index;
					TORRENT_ASSERT(file_index < fs.end_file());
					file_offset = 0;
				}
			}
		}
	}

	void file_progress::export_progress(vector<std::int64_t, file_index_t>& fp)
	{
		INVARIANT_CHECK;
		fp.resize(m_file_progress.size(), 0);
		std::copy(m_file_progress.begin(), m_file_progress.end(), fp.begin());
	}

	void file_progress::clear()
	{
		INVARIANT_CHECK;
		m_file_progress.clear();
		m_file_progress.shrink_to_fit();
#if TORRENT_USE_INVARIANT_CHECKS
		m_have_pieces.clear();
#endif
	}

	// update the file progress now that we just completed downloading piece
	// 'index'
	void file_progress::update(file_storage const& fs, piece_index_t const index
		, std::function<void(file_index_t)> const& completed_cb)
	{
		INVARIANT_CHECK;
		if (m_file_progress.empty()) return;

#if TORRENT_USE_INVARIANT_CHECKS
		// if this assert fires, we've told the file_progress object that we have
		// a piece twice. That violates its precondition and will cause incorrect
		// accounting
		TORRENT_ASSERT(m_have_pieces.get_bit(index) == false);
		m_have_pieces.set_bit(index);
#endif

		int const piece_size = fs.piece_length();
		std::int64_t off = std::int64_t(static_cast<int>(index)) * piece_size;
		file_index_t file_index = fs.file_index_at_offset(off);
		std::int64_t size = fs.piece_size(index);
		for (; size > 0; ++file_index)
		{
			std::int64_t const file_offset = off - fs.file_offset(file_index);
			TORRENT_ASSERT(file_index != fs.end_file());
			TORRENT_ASSERT(file_offset <= fs.file_size(file_index));
			std::int64_t const add = std::min(fs.file_size(file_index)
				- file_offset, size);
			m_file_progress[file_index] += add;

			TORRENT_ASSERT(m_file_progress[file_index]
				<= fs.file_size(file_index));

			if (m_file_progress[file_index] >= fs.file_size(file_index) && completed_cb)
			{
				if (!fs.pad_file_at(file_index))
					completed_cb(file_index);
			}
			size -= add;
			off += add;
			TORRENT_ASSERT(size >= 0);
		}
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void file_progress::check_invariant() const
	{
		if (m_file_progress.empty()) return;

		file_index_t index(0);
		for (std::int64_t progress : m_file_progress)
		{
			TORRENT_ASSERT(progress <= m_file_sizes[index++]);
		}
	}
#endif
}
