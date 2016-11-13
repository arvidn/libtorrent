/*

Copyright (c) 2015, Arvid Norberg
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

#ifndef TORRENT_FILE_PROGRESS_HPP_INCLUDE
#define TORRENT_FILE_PROGRESS_HPP_INCLUDE

#include <vector>
#include <cstdint>

#include "libtorrent/export.hpp"

#if TORRENT_USE_INVARIANT_CHECKS
#include "libtorrent/bitfield.hpp"
#include "libtorrent/invariant_check.hpp"
#endif

namespace libtorrent
{
class piece_picker;
class file_storage;
class alert_manager;
struct torrent_handle;

namespace aux
{
	struct TORRENT_EXTRA_EXPORT file_progress
	{
		file_progress();

		void init(piece_picker const& picker
			, file_storage const& fs);

		void export_progress(std::vector<std::int64_t> &fp);

		bool empty() const { return m_file_progress.empty(); }
		void clear();

		void update(file_storage const& fs, int index
			, alert_manager* alerts, torrent_handle const& h);

	private:
		// this vector contains the number of bytes completely
		// downloaded (as in passed-hash-check) in each file.
		// this lets us trigger on individual files completing
		// the vector is allocated lazily, when file progress
		// is first queried by the client
		std::vector<std::uint64_t> m_file_progress;

#if TORRENT_USE_INVARIANT_CHECKS
		friend class libtorrent::invariant_access;
		void check_invariant() const;

		// this is used to assert we never add the same piece twice
		bitfield m_have_pieces;

		// to make sure we never say we've downloaded more bytes of a file than
		// its file size
		std::vector<std::uint64_t> m_file_sizes;
#endif
	};
} }

#endif

