/*

Copyright (c) 2015-2021, Arvid Norberg
Copyright (c) 2019, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FILE_PROGRESS_HPP_INCLUDE
#define TORRENT_FILE_PROGRESS_HPP_INCLUDE

#include <vector>
#include <cstdint>
#include <functional>

#include "libtorrent/aux_/export.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/fwd.hpp" // for file_storage

#if TORRENT_USE_INVARIANT_CHECKS
#include "libtorrent/bitfield.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#endif

#if TORRENT_USE_INVARIANT_CHECKS
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/bitfield.hpp"
#endif

namespace lt::aux {

	struct piece_picker;

	struct TORRENT_EXTRA_EXPORT file_progress
	{
		file_progress() = default;

		void init(piece_picker const& picker
			, file_storage const& fs);

		void export_progress(vector<std::int64_t, file_index_t> &fp);

		bool empty() const { return m_file_progress.empty(); }
		void clear();

		void update(file_storage const& fs, piece_index_t index
			, std::function<void(file_index_t)> const& completed_cb);

	private:

		// this vector contains the number of bytes completely
		// downloaded (as in passed-hash-check) in each file.
		// this lets us trigger on individual files completing
		// the vector is allocated lazily, when file progress
		// is first queried by the client
		vector<std::int64_t, file_index_t> m_file_progress;

#if TORRENT_USE_INVARIANT_CHECKS
		friend struct lt::invariant_access;
		void check_invariant() const;

		// this is used to assert we never add the same piece twice
		typed_bitfield<piece_index_t> m_have_pieces;

		// to make sure we never say we've downloaded more bytes of a file than
		// its file size
		vector<std::int64_t, file_index_t> m_file_sizes;
#endif
	};
}

#endif
