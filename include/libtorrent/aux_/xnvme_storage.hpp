/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
Copyright (c) 2018, Steven Siloti
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

#ifndef TORRENT_XNVME_STORAGE
#define TORRENT_XNVME_STORAGE

#include "libtorrent/config.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/open_mode.hpp" // for aux::open_mode_t
#include "libtorrent/aux_/file_pointer.hpp"
#include "libtorrent/aux_/posix_part_file.hpp"
#include "libtorrent/io_context.hpp"
#include <memory>
#include <string>
#include <unordered_map>

#include <libxnvme_file.h>

namespace libtorrent {
namespace aux {

	struct xnvme_file_queue {
		xnvme_file_queue(xnvme_dev *dev_, xnvme_queue *queue_)
		: dev(dev_)
		, queue(queue_) {}

		xnvme_dev *dev;
		xnvme_queue *queue;
	};

	struct session_settings;

	struct TORRENT_EXTRA_EXPORT xnvme_storage
	{
		explicit xnvme_storage(storage_params const& p, std::string xnvme_storage);
		file_storage const& files() const;
		file_storage const& orig_files() const { return m_files; }
		~xnvme_storage();

		int readv(settings_interface const& sett
			, span<iovec_t const> bufs
			, piece_index_t const piece, int const offset
			, storage_error& error);
		int readv2(settings_interface const& sett
			, span<iovec_t const> bufs
			, piece_index_t const piece
			, int const offset
			, storage_error &error
			, std::function<void()> handler);

		int writev(settings_interface const& sett
			, span<iovec_t const> bufs
			, piece_index_t const piece
			, int const offset
			, storage_error &error
			, std::function<void()> handler);

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

		xnvme_file_queue* open_file_xnvme (file_index_t idx);

		file_pointer open_file(file_index_t idx, open_mode_t mode, std::int64_t offset
			, storage_error& ec);

		void need_partfile();
		bool use_partfile(file_index_t index) const;
		void use_partfile(file_index_t index, bool b);

		file_storage const& m_files;
		std::unique_ptr<file_storage> m_mapped_files;
		std::string m_save_path;
		stat_cache m_stat_cache;

		aux::vector<download_priority_t, file_index_t> m_file_priority;

		std::unordered_map<std::string, xnvme_file_queue*> m_file_handles;
		std::string m_xnvme_backend;

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
		std::unique_ptr<posix_part_file> m_part_file;
	};
}
}
#endif

