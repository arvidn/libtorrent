/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

#include "libtorrent/aux_/disk_job_fence.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/allocator.hpp"
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

	class session;

namespace aux {
	struct session_settings;
	struct file_view_pool;
	struct file_view;
}

	struct add_torrent_params;
	class hasher;

#ifndef TORRENT_NO_DEPRECATE
	struct storage_interface;
#endif

	struct disk_io_thread;

	struct TORRENT_EXTRA_EXPORT default_storage
		: std::enable_shared_from_this<default_storage>
		, aux::disk_job_fence
		, boost::noncopyable
	{
		friend struct write_fileop;
		friend struct read_fileop;
	public:
		// constructs the default_storage based on the give file_storage (fs).
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
		default_storage(storage_params const& params, aux::file_view_pool&);

		// hidden
		~default_storage();

		bool has_any_file(storage_error& ec);
		void set_file_priority(aux::session_settings const&
			, aux::vector<std::uint8_t, file_index_t> const& prio
			, storage_error& ec);
		void rename_file(file_index_t index, std::string const& new_filename
			, storage_error& ec);
		void release_files(storage_error& ec);
		void delete_files(remove_flags_t options, storage_error& ec);
		void initialize(aux::session_settings const&, storage_error& ec);
		status_t move_storage(std::string const& save_path, move_flags_t flags
			, storage_error& ec);
		bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error& error);
		bool tick();

		int readv(aux::session_settings const&, span<iovec_t const> bufs
			, piece_index_t piece, int offset, aux::open_mode_t flags, storage_error& ec);
		int writev(aux::session_settings const&, span<iovec_t const> bufs
			, piece_index_t piece, int offset, aux::open_mode_t flags, storage_error& ec);
		int hashv(aux::session_settings const&, hasher& ph, std::size_t len, piece_index_t piece, int offset, aux::open_mode_t flags
			, storage_error& ec);

		// if the files in this storage are mapped, returns the mapped
		// file_storage, otherwise returns the original file_storage object.
		file_storage const& files() const { return m_mapped_files ? *m_mapped_files : m_files; }

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

		int dec_refcount()
		{
			TORRENT_ASSERT(m_references > 0);
			return --m_references;
		}
		void inc_refcount() { ++m_references; }
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

		storage_index_t m_storage_index;

		// the number of block_cache_reference objects referencing this storage
		std::atomic<int> m_references{1};

		void delete_one_file(std::string const& p, error_code& ec);

		void need_partfile();

		std::unique_ptr<file_storage> m_mapped_files;

		// in order to avoid calling stat() on each file multiple times
		// during startup, cache the results in here, and clear it all
		// out once the torrent starts (to avoid getting stale results)
		// each entry represents the size and timestamp of the file
		mutable stat_cache m_stat_cache;

		// helper function to open a file in the file pool with the right mode
		boost::optional<aux::file_view> open_file(aux::session_settings const&, file_index_t
			, aux::open_mode_t, storage_error&) const;
		boost::optional<aux::file_view> open_file_impl(aux::session_settings const&
			, file_index_t, aux::open_mode_t, error_code&) const;

		aux::vector<std::uint8_t, file_index_t> m_file_priority;
		std::string m_save_path;
		std::string m_part_file_name;
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

		bool m_allocate_files;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED
