/*

Copyright (c) 2003, Daniel Wallin
Copyright (c) 2003, 2009, 2011, 2013-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2018-2019, Steven Siloti
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

#ifndef TORRENT_STORAGE_HPP_INCLUDE
#define TORRENT_STORAGE_HPP_INCLUDE

#include "libtorrent/config.hpp"

#include <mutex>
#include <atomic>
#include <memory>

#include "libtorrent/fwd.hpp"
#include "libtorrent/aux_/disk_job_fence.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/part_file.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/open_mode.hpp" // for aux::open_mode_t

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/optional.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

namespace aux {
	struct session_settings;
	struct file_view_pool;
	struct file_view;
}

	struct TORRENT_EXTRA_EXPORT mmap_storage
		: std::enable_shared_from_this<mmap_storage>
		, aux::disk_job_fence
	{
		// constructs the mmap_storage based on the give file_storage (fs).
		// ``mapped`` is an optional argument (it may be nullptr). If non-nullptr it
		// represents the file mapping that have been made to the torrent before
		// adding it. That's where files are supposed to be saved and looked for
		// on disk. ``save_path`` is the root save folder for this torrent.
		// ``file_view_pool`` is the cache of file mappings that the storage will use.
		// All files it opens will ask the file_view_pool to open them. ``file_prio``
		// is a vector indicating the priority of files on startup. It may be
		// an empty vector. Any file whose index is not represented by the vector
		// (because the vector is too short) are assumed to have priority 1.
		// this is used to treat files with priority 0 slightly differently.
		mmap_storage(storage_params const& params, aux::file_view_pool&);

		// hidden
		~mmap_storage();
		mmap_storage(mmap_storage const&) = delete;
		mmap_storage& operator=(mmap_storage const&) = delete;

		void abort_jobs();

		bool has_any_file(storage_error&);
		void set_file_priority(settings_interface const&
			, aux::vector<download_priority_t, file_index_t>& prio
			, storage_error&);
		void rename_file(file_index_t index, std::string const& new_filename
			, storage_error&);
		void release_files(storage_error&);
		void delete_files(remove_flags_t options, storage_error&);
		void initialize(settings_interface const&, storage_error&);
		std::pair<status_t, std::string> move_storage(std::string save_path
			, move_flags_t, storage_error&);
		bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error&);
		bool tick();

		int readv(settings_interface const&, span<iovec_t const> bufs
			, piece_index_t piece, int offset, aux::open_mode_t flags, storage_error&);
		int writev(settings_interface const&, span<iovec_t const> bufs
			, piece_index_t piece, int offset, aux::open_mode_t flags, storage_error&);
		int hashv(settings_interface const&, hasher& ph, std::ptrdiff_t len
			, piece_index_t piece, int offset, aux::open_mode_t flags
			, storage_error&);
		int hashv2(settings_interface const&, hasher256& ph, std::ptrdiff_t len
			, piece_index_t piece, int offset, aux::open_mode_t flags, storage_error&);

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
		mutable stat_cache m_stat_cache;

		// helper function to open a file in the file pool with the right mode
		boost::optional<aux::file_view> open_file(settings_interface const&, file_index_t
			, aux::open_mode_t, storage_error&) const;
		boost::optional<aux::file_view> open_file_impl(settings_interface const&
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
		aux::file_view_pool& m_pool;

		// used for skipped files
		std::unique_ptr<part_file> m_part_file;

		// this is a bitfield with one bit per file. A bit being set means
		// we've written to that file previously. If we do write to a file
		// whose bit is 0, we set the file size, to make the file allocated
		// on disk (in full allocation mode) and just sparsely allocated in
		// case of sparse allocation mode
		mutable std::mutex m_file_created_mutex;
		mutable typed_bitfield<file_index_t> m_file_created;

#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		// Windows has a race condition when unmapping a view while a new
		// view or mapping object is being created in a different thread.
		// The race can cause a page of written data to be zeroed out before
		// it is written out to disk. To avoid the race these calls must be
		// serialized on a per-file basis. See github issue #3842 for details.

		// This array stores a mutex for each file in the storage object
		// It must be aquired before calling CreateFileMapping or UnmapViewOfFile
		mutable std::shared_ptr<std::mutex> m_file_open_unmap_lock;
#endif

		bool m_allocate_files;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED
