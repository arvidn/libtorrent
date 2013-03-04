/*

Copyright (c) 2003-2012, Arvid Norberg
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

#include <vector>
#include <sys/types.h>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/function/function2.hpp>
#include <boost/function/function0.hpp>
#include <boost/limits.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/unordered_set.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif


#include "libtorrent/piece_picker.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/atomic.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/file_pool.hpp" // pool_file_status
#include "libtorrent/part_file.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/lazy_entry.hpp"

namespace libtorrent
{
	class session;
	struct file_pool;
	struct disk_io_job;
	struct disk_buffer_pool;
	struct cache_status;
	namespace aux { struct session_settings; }
	struct cached_piece_entry;

	TORRENT_EXTRA_EXPORT std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		file_storage const& t
		, std::string const& p);

	TORRENT_EXTRA_EXPORT bool match_filesizes(
		file_storage const& t
		, std::string const& p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes
		, bool compact_mode
		, std::string* error = 0);

	TORRENT_EXTRA_EXPORT int bufs_size(file::iovec_t const* bufs, int num_bufs);

	struct TORRENT_EXPORT storage_interface
	{
		storage_interface(): m_settings(0) {}

		// create directories and set file sizes
		virtual void initialize(storage_error& ec) = 0;

		virtual int readv(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec) = 0;
		virtual int writev(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec) = 0;

		virtual bool has_any_file(storage_error& ec) = 0;

		// change the priorities of files. This is a fenced job and is
		// guaranteed to be the only running function on this storage
		virtual void set_file_priority(std::vector<boost::uint8_t> const& prio, storage_error& ec) = 0;

		// non-zero return value indicates an error
		virtual void move_storage(std::string const& save_path, storage_error& ec) = 0;

		// verify storage dependent fast resume entries
		virtual bool verify_resume_data(lazy_entry const& rd, storage_error& ec) = 0;

		// write storage dependent fast resume entries
		virtual void write_resume_data(entry& rd, storage_error& ec) const = 0;

		// this will close all open files that are opened for
		// writing. This is called when a torrent has finished
		// downloading.
		// non-zero return value indicates an error
		virtual void release_files(storage_error& ec) = 0;

		// this will rename the file specified by index.
		virtual void rename_file(int index, std::string const& new_filenamem, storage_error& ec) = 0;

		// this will close all open files and delete them
		// non-zero return value indicates an error
		virtual void delete_files(storage_error& ec) = 0;

#ifndef TORRENT_NO_DEPRECATE
		// called for every file when it completes downloading
		// used on windows to turn off the sparse flag
		virtual void finalize_file(int, storage_error&) {}
#endif

		// called periodically (useful for deferred flushing). When returning
		// false, it means no more ticks are necessary. Any disk job submitted
		// will re-enable ticking. The default will always turn ticking back
		// off again.
		virtual bool tick() { return false; }

		aux::session_settings const& settings() const { return *m_settings; }

		virtual ~storage_interface() {}

		// initialized in disk_io_thread::perform_async_job
		aux::session_settings* m_settings;
	};

	class TORRENT_EXPORT default_storage : public storage_interface, boost::noncopyable
	{
	public:
		default_storage(storage_params const& params);
		~default_storage();

#ifndef TORRENT_NO_DEPRECATE
		void finalize_file(int file, storage_error& ec);
#endif
		bool has_any_file(storage_error& ec);
		void set_file_priority(std::vector<boost::uint8_t> const& prio, storage_error& ec);
		void rename_file(int index, std::string const& new_filename, storage_error& ec);
		void release_files(storage_error& ec);
		void delete_files(storage_error& ec);
		void initialize(storage_error& ec);
		void move_storage(std::string const& save_path, storage_error& ec);
		int sparse_end(int start) const;
		bool verify_resume_data(lazy_entry const& rd, storage_error& error);
		void write_resume_data(entry& rd, storage_error& ec) const;
		bool tick();

		int readv(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec);
		int writev(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec);

		// this identifies a read or write operation
		// so that default_storage::readwritev() knows what to
		// do when it's actually touching the file
		struct fileop
		{
			// file operation
			size_type (file::*op)(size_type file_offset
				, file::iovec_t const* bufs, int num_bufs, error_code& ec, int flags);
			int cache_setting;
			// file open mode (file::read_only, file::write_only etc.)
			// this is used to open the file, but also passed along as the
			// flags argument to the file operation (readv or writev)
			int mode;
			// used for error reporting
			int operation_type;
		};

		void delete_one_file(std::string const& p, error_code& ec);
		int readwritev(file::iovec_t const* bufs, int slot, int offset
			, int num_bufs, fileop const& op, storage_error& ec);

		file_storage const& files() const { return m_mapped_files?*m_mapped_files:m_files; }

		void need_partfile();

		boost::scoped_ptr<file_storage> m_mapped_files;
		file_storage const& m_files;

		// in order to avoid calling stat() on each file multiple times
		// during startup, cache the results in here, and clear it all
		// out once the torrent starts (to avoid getting stale results)
		// each slot represents the size and timestamp of the file
		mutable stat_cache m_stat_cache;

		// helper function to open a file in the file pool with the right mode
		boost::intrusive_ptr<file> open_file(file_storage::iterator fe, int mode
			, error_code& ec) const;

		std::vector<boost::uint8_t> m_file_priority;
		std::string m_save_path;
		std::string m_part_file_name;
		// the file pool is typically stored in
		// the session, to make all storage
		// instances use the same pool
		file_pool& m_pool;

		// used for skipped files
		boost::scoped_ptr<part_file> m_part_file;

		bool m_allocate_files;
	};

	// this storage implementation does not write anything to disk
	// and it pretends to read, and just leaves garbage in the buffers
	// this is useful when simulating many clients on the same machine
	// or when running stress tests and want to take the cost of the
	// disk I/O out of the picture. This cannot be used for any kind
	// of normal bittorrent operation, since it will just send garbage
	// to peers and throw away all the data it downloads. It would end
	// up being banned immediately
	class disabled_storage : public storage_interface, boost::noncopyable
	{
	public:
		disabled_storage(int piece_size) : m_piece_size(piece_size) {}
		bool has_any_file(storage_error&) { return false; }
		void set_file_priority(std::vector<boost::uint8_t> const& prio, storage_error& ec) {}
		void rename_file(int, std::string const&, storage_error&) {}
		void release_files(storage_error&) {}
		void delete_files(storage_error&) {}
		void initialize(storage_error&) {}
		void move_storage(std::string const&, storage_error&) {}

		int readv(file::iovec_t const* bufs, int num_bufs, int piece
			, int offset, int flags, storage_error& ec);
		int writev(file::iovec_t const* bufs, int num_bufs, int piece
			, int offset, int flags, storage_error& ec);

		bool verify_resume_data(lazy_entry const& rd, storage_error& error) { return false; }
		void write_resume_data(entry& rd, storage_error& ec) const {}

		int m_piece_size;
	};

	// this storage implementation always reads zeroes, and always discards
	// anything written to it
	struct zero_storage : storage_interface
	{
		virtual void initialize(storage_error& ec) {}

		virtual int readv(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec);
		virtual int writev(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec);

		virtual bool has_any_file(storage_error& ec) { return false; }
		virtual void set_file_priority(std::vector<boost::uint8_t> const& prio, storage_error& ec) {}
		virtual void move_storage(std::string const& save_path, storage_error& ec) {}
		virtual bool verify_resume_data(lazy_entry const& rd, storage_error& ec) { return false; }
		virtual void write_resume_data(entry& rd, storage_error& ec) const {}
		virtual void release_files(storage_error& ec) {}
		virtual void rename_file(int index, std::string const& new_filenamem, storage_error& ec) {}
		virtual void delete_files(storage_error& ec) {}
	};

	struct disk_io_thread;

	// implements the disk I/O job fence used by the piece_manager
	// to provide to the disk thread. Whenever a disk job needs
	// exclusive access to the storage for that torrent, it raises
	// the fence, blocking all new jobs, until there are no longer
	// any outstanding jobs on the torrent, then the fence is lowered
	// and it can be performed, along with the backlog of jobs that
	// accrued while the fence was up
	struct TORRENT_EXTRA_EXPORT disk_job_fence
	{
		disk_job_fence();

		// returns one of the fence_* enums.
		// if there are no outstanding jobs on the
		// storage, fence_post_fence is returned, the flush job is expected
		// to be discarded by the caller.
		// fence_post_flush is returned if the fence job was blocked and queued,
		// but the flush job should be posted (i.e. put on the job queue)
		// fence_post_none if both the fence and the flush jobs were queued.
		enum { fence_post_fence = 0, fence_post_flush = 1, fence_post_none = 2 };
		int raise_fence(disk_io_job* fence_job, disk_io_job* flush_job, atomic_count* blocked_counter);
		bool has_fence() const;

		// called whenever a job completes and is posted back to the
		// main network thread. the tailqueue of jobs will have the
		// backed-up jobs prepended to it in case this resulted in the
		// fence being lowered.
		int job_complete(disk_io_job* j, tailqueue& job_queue);
		int num_outstanding_jobs() const { return m_outstanding_jobs; }

		// if there is a fence up, returns true and adds the job
		// to the queue of blocked jobs
		bool is_blocked(disk_io_job* j, bool ignore_fence = false);
		
		// the number of blocked jobs
		int num_blocked() const;

	private:
		// when > 0, this storage is blocked for new async
		// operations until all outstanding jobs have completed.
		// at that point, the m_blocked_jobs are issued
		// the count is the number of fence job currently in the queue
		int m_has_fence;

		// when there's a fence up, jobs are queued up in here
		// until the fence is lowered
		tailqueue m_blocked_jobs;

		// the number of disk_io_job objects there are, belonging
		// to this torrent, currently pending, hanging off of
		// cached_piece_entry objects. This is used to determine
		// when the fence can be lowered
		atomic_count m_outstanding_jobs;

		// must be held when accessing m_has_fence and
		// m_blocked_jobs
		mutable mutex m_mutex;
	};

	// this class keeps track of which pieces, belonging to
	// a specific storage, are in the cache right now. It's
	// used for quickly being able to evict all pieces for a
	// specific torrent
	struct TORRENT_EXTRA_EXPORT storage_piece_set
	{
		void add_piece(cached_piece_entry* p);
		void remove_piece(cached_piece_entry* p);
		bool has_piece(cached_piece_entry* p) const;
		int num_pieces() const { return m_cached_pieces.size(); }
		boost::unordered_set<cached_piece_entry*> const& cached_pieces() const
		{ return m_cached_pieces; }
	private:
		// these are cached pieces belonging to this storage
		boost::unordered_set<cached_piece_entry*> m_cached_pieces;
	};

	class TORRENT_EXTRA_EXPORT piece_manager
		: public intrusive_ptr_base<piece_manager>
		, public disk_job_fence
		, public storage_piece_set
		, boost::noncopyable
	{
	friend struct disk_io_thread;
	public:

		piece_manager(
			storage_interface* storage_impl
			, boost::shared_ptr<void> const& torrent
			, file_storage* files);

		~piece_manager();

		file_storage const* files() const { return &m_files; }

		enum return_t
		{
			// return values from check_fastresume
			no_error = 0,
			fatal_disk_error = -1,
			need_full_check = -2,
			disk_check_aborted = -3
		};

		storage_interface* get_storage_impl() { return m_storage.get(); }

		void write_resume_data(entry& rd, storage_error& ec) const;

	private:

		// if error is set and return value is 'no_error' or 'need_full_check'
		// the error message indicates that the fast resume data was rejected
		// if 'fatal_disk_error' is returned, the error message indicates what
		// when wrong in the disk access
		int check_fastresume(lazy_entry const& rd, storage_error& error);

		// helper functions for check_fastresume	
		int check_no_fastresume(storage_error& error);
		int check_init_storage(storage_error& error);

#ifdef TORRENT_DEBUG
		std::string name() const { return m_files.name(); }
#endif

#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		void check_invariant() const;
#endif
		file_storage const& m_files;

		boost::scoped_ptr<storage_interface> m_storage;

		// the reason for this to be a void pointer
		// is to avoid creating a dependency on the
		// torrent. This shared_ptr is here only
		// to keep the torrent object alive until
		// the piece_manager destructs. This is because
		// the torrent_info object is owned by the torrent.
		boost::shared_ptr<void> m_torrent;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED

