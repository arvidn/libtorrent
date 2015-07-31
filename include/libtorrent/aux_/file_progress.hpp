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
#include <boost/cstdint.hpp>

#include "libtorrent/export.hpp"

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

		void export_progress(std::vector<boost::int64_t> &fp);

		void clear();

		void update(file_storage const& fs, int index
			, alert_manager* alerts, torrent_handle const& h);

#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant(file_storage const& fs) const;
#endif

	private:

		// this vector contains the number of bytes completely
		// downloaded (as in passed-hash-check) in each file.
		// this lets us trigger on individual files completing
		// the vector is allocated lazily, when file progress
		// is first queried by the client
		std::vector<boost::uint64_t> m_file_progress;
	};
} }

#endif

