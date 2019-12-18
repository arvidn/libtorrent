/*

Copyright (c) 2003-2018, Arvid Norberg
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
#include "libtorrent/aux_/storage_piece_set.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/part_file.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/vector.hpp"

// OVERVIEW
//
// libtorrent provides a customization point for storage of data. By default,
// (``default_storage``) downloaded files are saved to disk according with the
// general conventions of bittorrent clients, mimicking the original file layout
// when the torrent was created. The libtorrent user may define a custom
// storage to store piece data in a different way.
//
// A custom storage implementation must derive from and implement the
// storage_interface. You must also provide a function that constructs the
// custom storage object and provide this function to the add_torrent() call
// via add_torrent_params. Either passed in to the constructor or by setting
// the add_torrent_params::storage field.
//
// This is an example storage implementation that stores all pieces in a
// ``std::map``, i.e. in RAM. It's not necessarily very useful in practice, but
// illustrates the basics of implementing a custom storage.
//
// .. include:: ../examples/custom_storage.cpp
//	:code: c++
//	:tab-width: 2
//	:start-after: -- example begin
//	:end-before: // -- example end
namespace libtorrent {

	namespace aux { struct session_settings; }

	// The storage interface is a pure virtual class that can be implemented to
	// customize how and where data for a torrent is stored. The default storage
	// implementation uses regular files in the filesystem, mapping the files in
	// the torrent in the way one would assume a torrent is saved to disk.
	// Implementing your own storage interface makes it possible to store all
	// data in RAM, or in some optimized order on disk (the order the pieces are
	// received for instance), or saving multi file torrents in a single file in
	// order to be able to take advantage of optimized disk-I/O.
	//
	// It is also possible to write a thin class that uses the default storage
	// but modifies some particular behavior, for instance encrypting the data
	// before it's written to disk, and decrypting it when it's read again.
	//
	// The storage interface is based on pieces. Every read and write operation
	// happens in the piece-space. Each piece fits ``piece_size`` number
	// of bytes. All access is done by writing and reading whole or partial
	// pieces.
	//
	// libtorrent comes with two built-in storage implementations;
	// ``default_storage`` and ``disabled_storage``. Their constructor functions
	// are called default_storage_constructor() and
	// ``disabled_storage_constructor`` respectively. The disabled storage does
	// just what it sounds like. It throws away data that's written, and it
	// reads garbage. It's useful mostly for benchmarking and profiling purpose.
	//
	struct TORRENT_EXPORT storage_interface
		: std::enable_shared_from_this<storage_interface>
		, aux::disk_job_fence
		, aux::storage_piece_set
	{
		explicit storage_interface(file_storage const& fs) : m_files(fs) {}

		storage_interface(storage_interface const&) = delete;
		storage_interface& operator=(storage_interface const&) = delete;

		// This function is called when the *storage* on disk is to be
		// initialized. The default storage will create directories and empty
		// files at this point. If ``allocate_files`` is true, it will also
		// ``ftruncate`` all files to their target size.
		//
		// This function may be called multiple time on a single instance. When a
		// torrent is force-rechecked, the storage is re-initialized to trigger
		// the re-check from scratch.
		//
		// The function is not necessarily called before other member functions.
		// For instance has_any_files() and verify_resume_data() are
		// called early to determine whether we may have to check all files or
		// not. If we're doing a full check of the files every piece will be
		// hashed, causing readv() to be called as well.
		//
		// Any required internals that need initialization should be done in the
		// constructor. This function is called before the torrent starts to
		// download.
		//
		// If an error occurs, ``storage_error`` should be set to reflect it.
		virtual void initialize(storage_error& ec) = 0;

		// These functions should read and write the data in or to the given
		// ``piece`` at the given ``offset``. It should read or write
		// ``num_bufs`` buffers sequentially, where the size of each buffer is
		// specified in the buffer array ``bufs``. The iovec_t type has the
		// following members::
		//
		//	struct iovec_t { void* iov_base; size_t iov_len; };
		//
		// These functions may be called simultaneously from multiple threads.
		// Make sure they are thread safe. The ``file`` in libtorrent is thread
		// safe when it can fall back to ``pread``, ``preadv`` or the windows
		// equivalents. On targets where read operations cannot be thread safe
		// (i.e one has to seek first and then read), only one disk thread is
		// used.
		//
		// The ``offset`` is aligned to 16 kiB boundaries  *most of the time*, but
		// there are rare exceptions when it's not. Specifically if the read
		// cache is disabled/or full and a peer requests unaligned data. Most
		// clients request aligned data.
		//
		// The number of bytes read or written should be returned, or -1 on
		// error. If there's an error, the ``storage_error`` must be filled out
		// to represent the error that occurred.
		//
		// For possible values of ``flags``, see open_mode_t.
		virtual int readv(span<iovec_t const> bufs
			, piece_index_t piece, int offset, open_mode_t flags, storage_error& ec) = 0;
		virtual int writev(span<iovec_t const> bufs
			, piece_index_t piece, int offset, open_mode_t flags, storage_error& ec) = 0;

		// This function is called when first checking (or re-checking) the
		// storage for a torrent. It should return true if any of the files that
		// is used in this storage exists on disk. If so, the storage will be
		// checked for existing pieces before starting the download.
		//
		// If an error occurs, ``storage_error`` should be set to reflect it.
		virtual bool has_any_file(storage_error& ec) = 0;

		// change the priorities of files. This is a fenced job and is
		// guaranteed to be the only running function on this storage
		// when called
		virtual void set_file_priority(aux::vector<download_priority_t, file_index_t>& prio
			, storage_error& ec) = 0;

		// This function should move all the files belonging to the storage to
		// the new save_path. The default storage moves the single file or the
		// directory of the torrent.
		//
		// Before moving the files, any open file handles may have to be closed,
		// like ``release_files()``.
		//
		//If an error occurs, ``storage_error`` should be set to reflect it.
		virtual status_t move_storage(std::string const& save_path
			, move_flags_t flags, storage_error& ec) = 0;

		// This function should verify the resume data ``rd`` with the files
		// on disk. If the resume data seems to be up-to-date, return true. If
		// not, set ``error`` to a description of what mismatched and return false.
		//
		// The default storage may compare file sizes and time stamps of the files.
		//
		// If an error occurs, ``storage_error`` should be set to reflect it.
		//
		// This function should verify the resume data ``rd`` with the files
		// on disk. If the resume data seems to be up-to-date, return true. If
		// not, set ``error`` to a description of what mismatched and return false.
		//
		// If the ``links`` pointer is non-empty, it has the same number
		// of elements as there are files. Each element is either empty or contains
		// the absolute path to a file identical to the corresponding file in this
		// torrent. The storage must create hard links (or copy) those files. If
		// any file does not exist or is inaccessible, the disk job must fail.
		virtual bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error& ec) = 0;

		// This function should release all the file handles that it keeps open
		// to files belonging to this storage. The default implementation just
		// calls file_pool::release_files().
		//
		// If an error occurs, ``storage_error`` should be set to reflect it.
		//
		virtual void release_files(storage_error& ec) = 0;

		// Rename the file with index ``file`` to name ``new_name``.
		//
		// If an error occurs, ``storage_error`` should be set to reflect it.
		//
		virtual void rename_file(file_index_t index, std::string const& new_filename
			, storage_error& ec) = 0;

		// This function should delete some or all of the storage for this torrent.
		// The ``options`` parameter specifies whether to delete all files or just
		// the partfile. ``options`` are set to the same value as the options
		// passed to session::remove_torrent().
		//
		// If an error occurs, ``storage_error`` should be set to reflect it.
		//
		// The ``disk_buffer_pool`` is used to allocate and free disk buffers. It
		// has the following members:
		//
		// .. code:: c++
		//
		//		struct disk_buffer_pool
		//		{
		//			char* allocate_buffer(char const* category);
		//			void free_buffer(char* buf);
		//
		//			char* allocate_buffers(int blocks, char const* category);
		//			void free_buffers(char* buf, int blocks);
		//
		//			int block_size() const { return m_block_size; }
		//
		//		};
		virtual void delete_files(remove_flags_t options, storage_error& ec) = 0;

		// called periodically (useful for deferred flushing). When returning
		// false, it means no more ticks are necessary. Any disk job submitted
		// will re-enable ticking. The default will always turn ticking back
		// off again.
		virtual bool tick() { return false; }

		file_storage const& files() const { return m_files; }

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

		// access global session_settings
		aux::session_settings const& settings() const { return *m_settings; }

		// hidden
		virtual ~storage_interface() {}

		// initialized in disk_io_thread::perform_async_job
		aux::session_settings const* m_settings = nullptr;

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
		// the storage_interface destructs. This is because
		// the file_storage object is owned by the torrent.
		std::shared_ptr<void> m_torrent;

		storage_index_t m_storage_index{0};

		// the number of block_cache_reference objects referencing this storage
		std::atomic<int> m_references{1};
	};

	// The default implementation of storage_interface. Behaves as a normal
	// bittorrent client. It is possible to derive from this class in order to
	// override some of its behavior, when implementing a custom storage.
	class TORRENT_EXPORT default_storage : public storage_interface
	{
	public:
		// constructs the default_storage based on the give file_storage (fs).
		// ``mapped`` is an optional argument (it may be nullptr). If non-nullptr it
		// represents the file mapping that have been made to the torrent before
		// adding it. That's where files are supposed to be saved and looked for
		// on disk. ``save_path`` is the root save folder for this torrent.
		// ``file_pool`` is the cache of file handles that the storage will use.
		// All files it opens will ask the file_pool to open them. ``file_prio``
		// is a vector indicating the priority of files on startup. It may be
		// an empty vector. Any file whose index is not represented by the vector
		// (because the vector is too short) are assumed to have priority 1.
		// this is used to treat files with priority 0 slightly differently.
		explicit default_storage(storage_params const& params, file_pool&);

		// hidden
		~default_storage() override;

		bool has_any_file(storage_error& ec) override;
		void set_file_priority(aux::vector<download_priority_t, file_index_t>& prio
			, storage_error& ec) override;
		void rename_file(file_index_t index, std::string const& new_filename
			, storage_error& ec) override;
		void release_files(storage_error& ec) override;
		void delete_files(remove_flags_t options, storage_error& ec) override;
		void initialize(storage_error& ec) override;
		status_t move_storage(std::string const& save_path
			, move_flags_t flags, storage_error& ec) override;
		bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error& error) override;
		bool tick() override;

		int readv(span<iovec_t const> bufs
			, piece_index_t piece, int offset, open_mode_t flags, storage_error& ec) override;
		int writev(span<iovec_t const> bufs
			, piece_index_t piece, int offset, open_mode_t flags, storage_error& ec) override;

		// if the files in this storage are mapped, returns the mapped
		// file_storage, otherwise returns the original file_storage object.
		file_storage const& files() const
		{
			return m_mapped_files ? *m_mapped_files : storage_interface::files();
		}

	private:

		void need_partfile();

		std::unique_ptr<file_storage> m_mapped_files;

		// in order to avoid calling stat() on each file multiple times
		// during startup, cache the results in here, and clear it all
		// out once the torrent starts (to avoid getting stale results)
		// each entry represents the size and timestamp of the file
		mutable stat_cache m_stat_cache;

		// helper function to open a file in the file pool with the right mode
		file_handle open_file(file_index_t file, open_mode_t mode, storage_error& ec) const;
		file_handle open_file_impl(file_index_t file, open_mode_t mode, error_code& ec) const;

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
		file_pool& m_pool;

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
