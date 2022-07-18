/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PREAD_STORAGE_HPP
#define TORRENT_PREAD_STORAGE_HPP

#include "libtorrent/config.hpp"

#include <mutex>
#include <memory>

#include "libtorrent/fwd.hpp"
#include "libtorrent/aux_/disk_job_fence.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/aux_/part_file.hpp"
#include "libtorrent/aux_/stat_cache.hpp"
#include "libtorrent/aux_/file_pool.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/open_mode.hpp" // for aux::open_mode_t
#include "libtorrent/disk_interface.hpp" // for disk_job_flags_t

namespace libtorrent::aux {

	struct session_settings;
	struct file_view;

	struct TORRENT_EXTRA_EXPORT pread_storage
		: std::enable_shared_from_this<pread_storage>
		, aux::disk_job_fence
	{
		// constructs the pread_storage based on the given storage_params.
		// ``file_pool`` is the cache of file handles that the storage will use.
		// All files it opens will ask the file_pool to open them.
		pread_storage(storage_params const& params, aux::file_pool&);

		// hidden
		~pread_storage();
		pread_storage(pread_storage const&) = delete;
		pread_storage& operator=(pread_storage const&) = delete;

		void abort_jobs();

		bool has_any_file(storage_error&);
		void set_file_priority(settings_interface const&
			, aux::vector<download_priority_t, file_index_t>& prio
			, storage_error&);
		void rename_file(file_index_t index, std::string const& new_filename
			, storage_error&);
		void release_files(storage_error&);
		void delete_files(remove_flags_t options, storage_error&);
		status_t initialize(settings_interface const&, storage_error&);
		std::pair<status_t, std::string> move_storage(std::string save_path
			, move_flags_t, storage_error&);
		bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error&);
		bool tick();

		int read(settings_interface const&, span<char> buffer
			, piece_index_t piece, int offset, aux::open_mode_t mode
			, disk_job_flags_t flags
			, storage_error&);
		int write(settings_interface const&, span<char> buffer
			, piece_index_t piece, int offset, aux::open_mode_t mode
			, disk_job_flags_t flags
			, storage_error&);
		int hash(settings_interface const&, hasher& ph, std::ptrdiff_t len
			, piece_index_t piece, int offset, aux::open_mode_t mode
			, disk_job_flags_t flags, storage_error&);
		int hash2(settings_interface const&, hasher256& ph, std::ptrdiff_t len
			, piece_index_t piece, int offset, aux::open_mode_t mode
			, disk_job_flags_t flags, storage_error&);

		// if the files in this storage are mapped, returns the mapped
		// file_storage, otherwise returns the original file_storage object.
		file_storage const& files() const { return m_mapped_files ? *m_mapped_files : m_files; }
		file_storage const& orig_files() const { return m_files; }

		bool set_need_tick()
		{
			bool const prev = m_need_tick;
			m_need_tick = true;
			return prev;
		}

		void do_tick()
		{
			m_need_tick = false;
			tick();
		}

		void set_owner(std::shared_ptr<void> const& tor) { m_torrent = tor; }

		storage_index_t storage_index() const { return m_storage_index; }
		void set_storage_index(storage_index_t st) { m_storage_index = st; }

	private:

		bool m_need_tick = false;
		file_storage const& m_files;

		// the reason for this to be a void pointer
		// is to avoid creating a dependency on the
		// torrent. This shared_ptr is here only
		// to keep the torrent object alive until
		// the storage destructs. This is because
		// the file_storage object is owned by the torrent.
		std::shared_ptr<void> m_torrent;

		storage_index_t m_storage_index{0};

		void need_partfile();

		std::unique_ptr<file_storage> m_mapped_files;

		// in order to avoid calling stat() on each file multiple times
		// during startup, cache the results in here, and clear it all
		// out once the torrent starts (to avoid getting stale results)
		// each entry represents the size and timestamp of the file
		mutable aux::stat_cache m_stat_cache;

		// helper function to open a file in the file pool with the right mode
		std::shared_ptr<aux::file_handle> open_file(settings_interface const&, file_index_t
			, aux::open_mode_t, storage_error&) const;
		std::shared_ptr<aux::file_handle> open_file_impl(settings_interface const&
			, file_index_t, aux::open_mode_t, storage_error&) const;

		bool use_partfile(file_index_t index) const;
		void use_partfile(file_index_t index, bool b);

		aux::vector<download_priority_t, file_index_t> m_file_priority;
		std::string m_save_path;
		std::string m_part_file_name;

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

		// the file pool is a member of the disk_io_thread
		// to make all storage instances share the pool
		aux::file_pool& m_pool;

		// used for skipped files
		std::unique_ptr<part_file> m_part_file;

		// this is a bitfield with one bit per file. A bit being set means
		// we've written to that file previously. If we do write to a file
		// whose bit is 0, we set the file size, to make the file allocated
		// on disk (in full allocation mode) and just sparsely allocated in
		// case of sparse allocation mode
		mutable std::mutex m_file_created_mutex;
		mutable typed_bitfield<file_index_t> m_file_created;

		bool m_allocate_files;
	};

}

#endif // TORRENT_PREAD_STORAGE_HPP
