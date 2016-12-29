/*

Copyright (c) 2017, Arvid Norberg
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

#ifndef TORRENT_POSIX_STORAGE
#define TORRENT_POSIX_STORAGE

#include "libtorrent/config.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/open_mode.hpp" // for aux::open_mode_t
#include <memory>
#include <string>

namespace libtorrent {
namespace aux {

	struct session_settings;

	struct TORRENT_EXTRA_EXPORT posix_storage
	{
		explicit posix_storage(storage_params p);
		file_storage const& files() const;

		int readv(aux::session_settings const& sett
			, span<iovec_t const> bufs
			, piece_index_t const piece, int const offset
			, storage_error& error);

		int writev(aux::session_settings const& sett
			, span<iovec_t const> bufs
			, piece_index_t const piece, int const offset
			, storage_error& error);

		bool has_any_file(storage_error& error);
		bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error& ec);

		void delete_files(remove_flags_t options, storage_error& error);

		void rename_file(file_index_t const index, std::string const& new_filename, storage_error& ec);

		void initialize(aux::session_settings const&, storage_error& ec);

	private:

		FILE* open_file(file_index_t idx, open_mode_t mode, std::int64_t offset
			, storage_error& ec);

		file_storage const& m_files;
		std::unique_ptr<file_storage> m_mapped_files;
		std::string m_save_path;
		stat_cache m_stat_cache;

		aux::vector<download_priority_t, file_index_t> m_file_priority;

		std::string m_part_file_name;
//		std::unique_ptr<part_file> m_part_file;
	};
}
}
#endif

