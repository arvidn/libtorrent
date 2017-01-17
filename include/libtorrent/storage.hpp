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
#include <unordered_set>
#include <memory>

#include "libtorrent/piece_picker.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/file_pool.hpp" // pool_file_status
#include "libtorrent/part_file.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/tailqueue.hpp"
#include "libtorrent/disk_io_job.hpp"
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
// .. code:: c++
//
//	struct temp_storage : storage_interface
//	{
//		temp_storage(file_storage const& fs) : m_files(fs) {}
//		virtual bool initialize(storage_error& se) { return false; }
//		virtual bool has_any_file() { return false; }
//		virtual int read(char* buf, int piece, int offset, int size)
//		{
//			std::map<int, std::vector<char>>::const_iterator i = m_file_data.find(piece);
//			if (i == m_file_data.end()) return 0;
//			int available = i->second.size() - offset;
//			if (available <= 0) return 0;
//			if (available > size) available = size;
//			memcpy(buf, &i->second[offset], available);
//			return available;
//		}
//		virtual int write(const char* buf, int piece, int offset, int size)
//		{
//			std::vector<char>& data = m_file_data[piece];
//			if (data.size() < offset + size) data.resize(offset + size);
//			std::memcpy(&data[offset], buf, size);
//			return size;
//		}
//		virtual bool rename_file(file_index_t file, std::string const& new_name)
//		{ assert(false); return false; }
//		virtual status_t move_storage(std::string const& save_path) { return false; }
//		virtual bool verify_resume_data(add_torrent_params const& rd
//			, std::vector<std::string> const* links
//			, storage_error& error) { return false; }
//		virtual std::int64_t physical_offset(int piece, int offset)
//		{ return piece * m_files.piece_length() + offset; };
//		virtual sha1_hash hash_for_slot(int piece, partial_hash& ph, int piece_size)
//		{
//			int left = piece_size - ph.offset;
//			assert(left >= 0);
//			if (left > 0)
//			{
//				std::vector<char>& data = m_file_data[piece];
//				// if there are padding files, those blocks will be considered
//				// completed even though they haven't been written to the storage.
//				// in this case, just extend the piece buffer to its full size
//				// and fill it with zeroes.
//				if (data.size() < piece_size) data.resize(piece_size, 0);
//				ph.h.update(&data[ph.offset], left);
//			}
//			return ph.h.final();
//		}
//		virtual bool release_files() { return false; }
//		virtual bool delete_files() { return false; }
//
//		std::map<int, std::vector<char>> m_file_data;
//		file_storage m_files;
//	};
//
//	storage_interface* temp_storage_constructor(storage_params const& params)
//	{
//		return new temp_storage(*params.files);
//	}
namespace libtorrent
{
	class session;
	struct file_pool;
	struct disk_io_job;
	struct disk_buffer_pool;
	struct cache_status;
	namespace aux { struct session_settings; }
	struct cached_piece_entry;
	struct add_torrent_params;

	TORRENT_EXTRA_EXPORT void clear_bufs(span<iovec_t const> bufs);

	struct disk_io_thread;

	// implements the disk I/O job fence used by the storage_interface
	// to provide to the disk thread. Whenever a disk job needs
	// exclusive access to the storage for that torrent, it raises
	// the fence, blocking all new jobs, until there are no longer
	// any outstanding jobs on the torrent, then the fence is lowered
	// and it can be performed, along with the backlog of jobs that
	// accrued while the fence was up
	struct TORRENT_EXPORT disk_job_fence
	{
		disk_job_fence();
		~disk_job_fence()
		{
			TORRENT_ASSERT(int(m_outstanding_jobs) == 0);
			TORRENT_ASSERT(m_blocked_jobs.size() == 0);
		}

		// returns one of the fence_* enums.
		// if there are no outstanding jobs on the
		// storage, fence_post_fence is returned, the flush job is expected
		// to be discarded by the caller.
		// fence_post_flush is returned if the fence job was blocked and queued,
		// but the flush job should be posted (i.e. put on the job queue)
		// fence_post_none if both the fence and the flush jobs were queued.
		enum { fence_post_fence = 0, fence_post_flush = 1, fence_post_none = 2 };
		int raise_fence(disk_io_job* fence_job, disk_io_job* flush_job
			, counters& cnt);
		bool has_fence() const;

		// called whenever a job completes and is posted back to the
		// main network thread. the tailqueue of jobs will have the
		// backed-up jobs prepended to it in case this resulted in the
		// fence being lowered.
		int job_complete(disk_io_job* j, tailqueue<disk_io_job>& job_queue);
		int num_outstanding_jobs() const { return m_outstanding_jobs; }

		// if there is a fence up, returns true and adds the job
		// to the queue of blocked jobs
		bool is_blocked(disk_io_job* j);

		// the number of blocked jobs
		int num_blocked() const;

	private:
		// when > 0, this storage is blocked for new async
		// operations until all outstanding jobs have completed.
		// at that point, the m_blocked_jobs are issued
		// the count is the number of fence job currently in the queue
		int m_has_fence = 0;

		// when there's a fence up, jobs are queued up in here
		// until the fence is lowered
		tailqueue<disk_io_job> m_blocked_jobs;

		// the number of disk_io_job objects there are, belonging
		// to this torrent, currently pending, hanging off of
		// cached_piece_entry objects. This is used to determine
		// when the fence can be lowered
		std::atomic<int> m_outstanding_jobs{0};

		// must be held when accessing m_has_fence and
		// m_blocked_jobs
		mutable std::mutex m_mutex;
	};

	// this class keeps track of which pieces, belonging to
	// a specific storage, are in the cache right now. It's
	// used for quickly being able to evict all pieces for a
	// specific torrent
	struct TORRENT_EXPORT storage_piece_set
	{
		void add_piece(cached_piece_entry* p);
		void remove_piece(cached_piece_entry* p);
		bool has_piece(cached_piece_entry const* p) const;
		int num_pieces() const { return int(m_cached_pieces.size()); }
		std::unordered_set<cached_piece_entry*> const& cached_pieces() const
		{ return m_cached_pieces; }
	private:
		// these are cached pieces belonging to this storage
		std::unordered_set<cached_piece_entry*> m_cached_pieces;
	};

	// The storage interface is a pure virtual class that can be implemented to
	// customize how and where data for a torrent is stored. The default storage
	// implementation uses regular files in the filesystem, mapping the files in
	// the torrent in the way one would assume a torrent is saved to disk.
	// Implementing your own storage interface makes it possible to store all
	// data in RAM, or in some optimized order on disk (the order the pieces are
	// received for instance), or saving multifile torrents in a single file in
	// order to be able to take advantage of optimized disk-I/O.
	//
	// It is also possible to write a thin class that uses the default storage
	// but modifies some particular behavior, for instance encrypting the data
	// before it's written to disk, and decrypting it when it's read again.
	//
	// The storage interface is based on pieces. Avery read and write operation
	// happens in the piece-space. Each piece fits 'piece_size' number
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
		: public std::enable_shared_from_this<storage_interface>
		, public disk_job_fence
		, public storage_piece_set
		, boost::noncopyable
	{

		// This function is called when the storage is to be initialized. The
		// default storage will create directories and empty files at this point.
		// If ``allocate_files`` is true, it will also ``ftruncate`` all files to
		// their target size.
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
		// Every buffer in ``bufs`` can be assumed to be page aligned and be of a
		// page aligned size, except for the last buffer of the torrent. The
		// allocated buffer can be assumed to fit a fully page aligned number of
		// bytes though. This is useful when reading and writing the last piece
		// of a file in unbuffered mode.
		//
		// The ``offset`` is aligned to 16 kiB boundaries  *most of the time*, but
		// there are rare exceptions when it's not. Specifically if the read
		// cache is disabled/or full and a peer requests unaligned data. Most
		// clients request aligned data.
		//
		// The number of bytes read or written should be returned, or -1 on
		// error. If there's an error, the ``storage_error`` must be filled out
		// to represent the error that occurred.
		virtual int readv(span<iovec_t const> bufs
			, piece_index_t piece, int offset, int flags, storage_error& ec) = 0;
		virtual int writev(span<iovec_t const> bufs
			, piece_index_t piece, int offset, int flags, storage_error& ec) = 0;

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
		virtual void set_file_priority(aux::vector<std::uint8_t, file_index_t> const& prio
			, storage_error& ec) = 0;

		// This function should move all the files belonging to the storage to
		// the new save_path. The default storage moves the single file or the
		// directory of the torrent.
		//
		// Before moving the files, any open file handles may have to be closed,
		// like ``release_files()``.
		//
		//If an error occurs, ``storage_error`` should be set to reflect it.
		virtual status_t move_storage(std::string const& save_path, int flags
			, storage_error& ec) = 0;

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

		// Rename file with index ``index`` to the name ``new_filename``.
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
		// has the following members::
		//
		//	struct disk_buffer_pool : boost::noncopyable
		//	{
		//		char* allocate_buffer(char const* category);
		//		void free_buffer(char* buf);
		//
		//		char* allocate_buffers(int blocks, char const* category);
		//		void free_buffers(char* buf, int blocks);
		//
		//		int block_size() const { return m_block_size; }
		//
		//		void release_memory();
		//	};
		//
		virtual void delete_files(int options, storage_error& ec) = 0;

		// called periodically (useful for deferred flushing). When returning
		// false, it means no more ticks are necessary. Any disk job submitted
		// will re-enable ticking. The default will always turn ticking back
		// off again.
		virtual bool tick() { return false; }

		file_storage const* files() const { return m_files; }

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

		void set_files(file_storage const* f) { m_files = f; }
		void set_owner(std::shared_ptr<void> const& tor) { m_torrent = tor; }

		// access global session_settings
		aux::session_settings const& settings() const { return *m_settings; }

		// hidden
		virtual ~storage_interface() {}

		// initialized in disk_io_thread::perform_async_job
		aux::session_settings* m_settings = nullptr;

		storage_index_t storage_index() const { return m_storage_index; }
		void set_storage_index(storage_index_t st) { m_storage_index = st; }

		int dec_refcount()
		{
			TORRENT_ASSERT(m_references > 0);
			return m_references--;
		}
		void inc_refcount() { ++m_references; }
	private:

		bool m_need_tick = false;
		file_storage const* m_files = nullptr;

		// the reason for this to be a void pointer
		// is to avoid creating a dependency on the
		// torrent. This shared_ptr is here only
		// to keep the torrent object alive until
		// the storage_interface destructs. This is because
		// the file_storage object is owned by the torrent.
		std::shared_ptr<void> m_torrent;

		storage_index_t m_storage_index;

		// the number of block_cache_reference objects referencing this storage
		std::atomic<int> m_references{1};
	};

	// The default implementation of storage_interface. Behaves as a normal
	// bittorrent client. It is possible to derive from this class in order to
	// override some of its behavior, when implementing a custom storage.
	class TORRENT_EXPORT default_storage : public storage_interface
	{
		friend struct write_fileop;
		friend struct read_fileop;
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
		explicit default_storage(storage_params const& params);

		// hidden
		~default_storage();

		virtual bool has_any_file(storage_error& ec) override;
		virtual void set_file_priority(aux::vector<std::uint8_t, file_index_t> const& prio
			, storage_error& ec) override;
		virtual void rename_file(file_index_t index, std::string const& new_filename
			, storage_error& ec) override;
		virtual void release_files(storage_error& ec) override;
		virtual void delete_files(int options, storage_error& ec) override;
		virtual void initialize(storage_error& ec) override;
		virtual status_t move_storage(std::string const& save_path, int flags
			, storage_error& ec) override;
		virtual bool verify_resume_data(add_torrent_params const& rd
			, aux::vector<std::string, file_index_t> const& links
			, storage_error& error) override;
		virtual bool tick() override;

		int readv(span<iovec_t const> bufs
			, piece_index_t piece, int offset, int flags, storage_error& ec) override;
		int writev(span<iovec_t const> bufs
			, piece_index_t piece, int offset, int flags, storage_error& ec) override;

		// if the files in this storage are mapped, returns the mapped
		// file_storage, otherwise returns the original file_storage object.
		file_storage const& files() const { return m_mapped_files ? *m_mapped_files : m_files; }

	private:

		void delete_one_file(std::string const& p, error_code& ec);

		void need_partfile();

		std::unique_ptr<file_storage> m_mapped_files;
		file_storage const& m_files;

		// in order to avoid calling stat() on each file multiple times
		// during startup, cache the results in here, and clear it all
		// out once the torrent starts (to avoid getting stale results)
		// each entry represents the size and timestamp of the file
		mutable stat_cache m_stat_cache;

		// helper function to open a file in the file pool with the right mode
		file_handle open_file(file_index_t file, int mode, storage_error& ec) const;
		file_handle open_file_impl(file_index_t file, int mode, error_code& ec) const;

		aux::vector<std::uint8_t, file_index_t> m_file_priority;
		std::string m_save_path;
		std::string m_part_file_name;
		// the file pool is typically stored in
		// the session, to make all storage
		// instances use the same pool
		file_pool& m_pool;

		// used for skipped files
		std::unique_ptr<part_file> m_part_file;

		// this is a bitfield with one bit per file. A bit being set means
		// we've written to that file previously. If we do write to a file
		// whose bit is 0, we set the file size, to make the file allocated
		// on disk (in full allocation mode) and just sparsely allocated in
		// case of sparse allocation mode
		mutable typed_bitfield<file_index_t> m_file_created;

		bool m_allocate_files;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED
