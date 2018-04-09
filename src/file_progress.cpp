/*

Copyright (c) 2015-2018, Arvid Norberg
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

#include "libtorrent/piece_picker.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/alert_manager.hpp"
#include "libtorrent/aux_/file_progress.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"

namespace libtorrent { namespace aux
{

	file_progress::file_progress()
	{
	}

	void file_progress::init(piece_picker const& picker, file_storage const& fs)
	{
		if (!m_file_progress.empty()) return;

		int const num_pieces = fs.num_pieces();
		int const num_files = fs.num_files();

#if TORRENT_USE_INVARIANT_CHECKS
		m_have_pieces.clear();
		m_have_pieces.resize(num_pieces, false);
		m_file_sizes.clear();
		m_file_sizes.reserve(num_files);
		for (int i = 0; i < int(fs.num_files()); ++i)
			m_file_sizes.push_back(fs.file_size(i));
#endif

		m_file_progress.resize(num_files, 0);
		std::fill(m_file_progress.begin(), m_file_progress.end(), 0);

		// initialize the progress of each file

		const int piece_size = fs.piece_length();
		boost::uint64_t off = 0;
		boost::uint64_t total_size = fs.total_size();
		int file_index = 0;
		for (int piece = 0; piece < num_pieces; ++piece, off += piece_size)
		{
			TORRENT_ASSERT(file_index < fs.num_files());
			boost::int64_t file_offset = off - fs.file_offset(file_index);
			TORRENT_ASSERT(file_offset >= 0);
			while (file_offset >= fs.file_size(file_index))
			{
				++file_index;
				TORRENT_ASSERT(file_index < fs.num_files());
				file_offset = off - fs.file_offset(file_index);
				TORRENT_ASSERT(file_offset >= 0);
			}
			TORRENT_ASSERT(file_offset <= fs.file_size(file_index));

			if (!picker.have_piece(piece)) continue;

#if TORRENT_USE_INVARIANT_CHECKS
			m_have_pieces.set_bit(piece);
#endif
			int size = (std::min)(boost::uint64_t(piece_size), total_size - off);
			TORRENT_ASSERT(size >= 0);

			while (size)
			{
				int const add = (std::min)(boost::int64_t(size), fs.file_size(file_index) - file_offset);
				TORRENT_ASSERT(add >= 0);
				m_file_progress[file_index] += add;

				TORRENT_ASSERT(m_file_progress[file_index]
					<= fs.file_size(file_index));

				size -= add;
				TORRENT_ASSERT(size >= 0);
				if (size > 0)
				{
					++file_index;
					TORRENT_ASSERT(file_index < fs.num_files());
					file_offset = 0;
				}
			}
		}
	}

	void file_progress::export_progress(std::vector<boost::int64_t> &fp)
	{
		fp.resize(m_file_progress.size(), 0);
		std::copy(m_file_progress.begin(), m_file_progress.end(), fp.begin());
	}

	void file_progress::clear()
	{
		std::vector<boost::uint64_t>().swap(m_file_progress);
#if TORRENT_USE_INVARIANT_CHECKS
		m_have_pieces.clear();
#endif
	}

	// update the file progress now that we just completed downloading piece
	// 'index'
	void file_progress::update(file_storage const& fs, int index
		, alert_manager* alerts, torrent_handle const& h)
	{
		INVARIANT_CHECK;

#if TORRENT_USE_INVARIANT_CHECKS
		TORRENT_ASSERT(m_have_pieces.get_bit(index) == false);
		m_have_pieces.set_bit(index);
#endif

		if (m_file_progress.empty())
			return;

		const int piece_size = fs.piece_length();
		boost::int64_t off = boost::int64_t(index) * piece_size;
		int file_index = fs.file_index_at_offset(off);
		int size = fs.piece_size(index);
		for (; size > 0; ++file_index)
		{
			boost::int64_t file_offset = off - fs.file_offset(file_index);
			TORRENT_ASSERT(file_index != fs.num_files());
			TORRENT_ASSERT(file_offset <= fs.file_size(file_index));
			int add = (std::min)(fs.file_size(file_index)
				- file_offset, boost::int64_t(size));
			m_file_progress[file_index] += add;

			TORRENT_ASSERT(m_file_progress[file_index]
					<= fs.file_size(file_index));

			// TODO: it would be nice to not depend on alert_manager here
			if (m_file_progress[file_index] >= fs.file_size(file_index) && alerts)
			{
				if (!fs.pad_file_at(file_index))
				{
					if (alerts->should_post<file_completed_alert>())
					{
						// this file just completed, post alert
						alerts->emplace_alert<file_completed_alert>(h, file_index);
					}
				}
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
		for (int i = 0; i < int(m_file_progress.size()); ++i)
		{
			TORRENT_ASSERT(m_file_progress[i] <= m_file_sizes[i]);
		}
	}
#endif
} }


