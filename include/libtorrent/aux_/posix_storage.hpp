/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
Copyright (c) 2018, Steven Siloti
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_POSIX_STORAGE
#define TORRENT_POSIX_STORAGE

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/stat_cache.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/open_mode.hpp" // for aux::open_mode_t
#include "libtorrent/part_file.hpp"
#include <memory>
#include <string>

namespace libtorrent {
namespace aux {

	struct session_settings;

	struct TORRENT_EXTRA_EXPORT posix_storage
	{
		explicit posix_storage(storage_params const& p);
		file_storage const& files() const;
		file_storage const& orig_files() const { return m_files; }
		~posix_storage();

		int readv(settings_interface const& sett
			, span<iovec_t const> bufs
			, piece_index_t const piece, int const offset
			, storage_error& error);

		int writev(settings_interface const& sett
			, span<iovec_t const> bufs
			, piece_index_t const piece, int const offset
			, storage_error& error);

		bool has_any_file(storage_error& error);
		void set_file_priority(aux::vector<download_priority_t, file_index_t>& prio
			, storage_error& ec);
		bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error& ec);

		void release_files();

		void delete_files(remove_flags_t options, storage_error& error);

		std::pair<status_t, std::string> move_storage(std::string const& sp
			, move_flags_t const flags, storage_error& ec);

		void rename_file(file_index_t const index, std::string const& new_filename, storage_error& ec);

		void initialize(settings_interface const&, storage_error& ec);

	private:

		FILE* open_file(file_index_t idx, open_mode_t mode, std::int64_t offset
			, storage_error& ec);

		void need_partfile();
		bool use_partfile(file_index_t index) const;
		void use_partfile(file_index_t index, bool b);

		file_storage const& m_files;
		std::unique_ptr<file_storage> m_mapped_files;
		std::string m_save_path;
		stat_cache m_stat_cache;

		aux::vector<download_priority_t, file_index_t> m_file_priority;

		// this this is an array indexed by file-index. Each slot represents
		// whether this file has the part-file enabled for it. This is used for
		// backwards compatibility with pre-partfile versions of libtorrent. If
		// this vector is empty, the default is that files *do* use the partfile.
		// on startup, any 0-priority file that's found in it's original location
		// is expected to be an old-style (pre-partfile) torrent storage, and
		// those files have their slot set to false in this vector.
		// note that the vector is *sparse*, it's only allocated if a file has its
		// entry set to false, and only indices up to that entry.
		aux::vector<bool, file_index_t> m_use_partfile;

		std::string m_part_file_name;
		std::unique_ptr<part_file> m_part_file;
	};
}
}
#endif

