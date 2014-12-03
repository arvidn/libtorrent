/*

Copyright (c) 2003-2014, Arvid Norberg
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
#include <boost/limits.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif


#include "libtorrent/torrent_info.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/bitfield.hpp"

// OVERVIEW
//
// libtorrent provides a customization point for storage of data. By default,
// (``default_storage``) downloaded files are saved to disk according with the
// general conventions of bittorrent clients, mimicing the original file layout
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
//		void set_file_priority(std::vector<boost::uint8_t> const& prio) {}
//		virtual bool initialize(bool allocate_files) { return false; }
//		virtual bool has_any_file() { return false; }
//		virtual int read(char* buf, int slot, int offset, int size)
//		{
//			std::map<int, std::vector<char> >::const_iterator i = m_file_data.find(slot);
//			if (i == m_file_data.end()) return 0;
//			int available = i->second.size() - offset;
//			if (available <= 0) return 0;
//			if (available > size) available = size;
//			memcpy(buf, &i->second[offset], available);
//			return available;
//		}
//		virtual int write(const char* buf, int slot, int offset, int size)
//		{
//			std::vector<char>& data = m_file_data[slot];
//			if (data.size() < offset + size) data.resize(offset + size);
//			std::memcpy(&data[offset], buf, size);
//			return size;
//		}
//		virtual bool rename_file(int file, std::string const& new_name)
//		{ assert(false); return false; }
//		virtual bool move_storage(std::string const& save_path) { return false; }
//		virtual bool verify_resume_data(lazy_entry const& rd, error_code& error) { return false; }
//		virtual bool write_resume_data(entry& rd) const { return false; }
//		virtual bool move_slot(int src_slot, int dst_slot) { assert(false); return false; }
//		virtual bool swap_slots(int slot1, int slot2) { assert(false); return false; }
//		virtual bool swap_slots3(int slot1, int slot2, int slot3) { assert(false); return false; }
//		virtual size_type physical_offset(int slot, int offset)
//		{ return slot * m_files.piece_length() + offset; };
//		virtual sha1_hash hash_for_slot(int slot, partial_hash& ph, int piece_size)
//		{
//			int left = piece_size - ph.offset;
//			assert(left >= 0);
//			if (left > 0)
//			{
//				std::vector<char>& data = m_file_data[slot];
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
//		std::map<int, std::vector<char> > m_file_data;
//		file_storage m_files;
//	};
//
//	storage_interface* temp_storage_constructor(
//		file_storage const& fs, file_storage const* mapped
//		, std::string const& path, file_pool& fp
//		, std::vector<boost::uint8_t> const& prio)
//	{
//		return new temp_storage(fs);
//	}

namespace libtorrent
{
	class session;
	struct file_pool;
	struct disk_io_job;
	struct disk_buffer_pool;
	struct session_settings;

	TORRENT_EXTRA_EXPORT std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		file_storage const& t
		, std::string const& p);

	TORRENT_EXTRA_EXPORT bool match_filesizes(
		file_storage const& t
		, std::string const& p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes
		, bool compact_mode
		, std::string* error = 0);

	struct TORRENT_EXTRA_EXPORT partial_hash
	{
		partial_hash(): offset(0) {}
		// the number of bytes in the piece that has been hashed
		int offset;
		// the sha-1 context
		hasher h;
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
	// The storage interface is based on slots, each slot is 'piece_size' number
	// of bytes. All access is done by writing and reading whole or partial
	// slots. One slot is one piece in the torrent, but the data in the slot
	// does not necessarily correspond to the piece with the same index (in
	// compact allocation mode it won't).
	// 
	// libtorrent comes with two built-in storage implementations;
	// ``default_storage`` and ``disabled_storage``. Their constructor functions
	// are called default_storage_constructor() and
	// ``disabled_storage_constructor`` respectively. The disabled storage does
	// just what it sounds like. It throws away data that's written, and it
	// reads garbage. It's useful mostly for benchmarking and profiling purpose.
	//
	struct TORRENT_EXPORT storage_interface
	{
		// hidden
		storage_interface(): m_disk_pool(0), m_settings(0) {}


		// This function is called when the storage is to be initialized. The
		// default storage will create directories and empty files at this point.
		// If ``allocate_files`` is true, it will also ``ftruncate`` all files to
		// their target size.
		//
		// Returning ``true`` indicates an error occurred.
		virtual bool initialize(bool allocate_files) = 0;

		// This function is called when first checking (or re-checking) the
		// storage for a torrent. It should return true if any of the files that
		// is used in this storage exists on disk. If so, the storage will be
		// checked for existing pieces before starting the download.
		virtual bool has_any_file() = 0;


		// change the priorities of files.
		virtual void set_file_priority(std::vector<boost::uint8_t> const& prio) = 0;

		// These functions should read or write the data in or to the given
		// ``slot`` at the given ``offset``. It should read or write ``num_bufs``
		// buffers sequentially, where the size of each buffer is specified in
		// the buffer array ``bufs``. The file::iovec_t type has the following
		// members::
		// 
		//	struct iovec_t { void* iov_base; size_t iov_len; };
		// 
		// The return value is the number of bytes actually read or written, or
		// -1 on failure. If it returns -1, the error code is expected to be set
		// to
		// 
		// Every buffer in ``bufs`` can be assumed to be page aligned and be of a
		// page aligned size, except for the last buffer of the torrent. The
		// allocated buffer can be assumed to fit a fully page aligned number of
		// bytes though. This is useful when reading and writing the last piece
		// of a file in unbuffered mode.
		// 
		// The ``offset`` is aligned to 16 kiB boundries  *most of the time*, but
		// there are rare exceptions when it's not. Specifically if the read
		// cache is disabled/or full and a client requests unaligned data, or the
		// file itself is not aligned in the torrent. Most clients request
		// aligned data.
		virtual int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs, int flags = file::random_access);
		virtual int writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs, int flags = file::random_access);

		// This function is called when a read job is queued. It gives the
		// storage wrapper an opportunity to hint the operating system about this
		// coming read. For instance, the storage may call
		// ``posix_fadvise(POSIX_FADV_WILLNEED)`` or ``fcntl(F_RDADVISE)``.
		virtual void hint_read(int, int, int) {}

		// negative return value indicates an error
		virtual int read(char* buf, int slot, int offset, int size) = 0;

		// negative return value indicates an error
		virtual int write(const char* buf, int slot, int offset, int size) = 0;

		// returns the offset on the physical storage medium for the
		// byte at offset ``offset`` in slot ``slot``.
		virtual size_type physical_offset(int slot, int offset) = 0;

		// This function is optional. It is supposed to return the first piece,
		// starting at ``start`` that is fully contained within a data-region on
		// disk (i.e. non-sparse region). The purpose of this is to skip parts of
		// files that can be known to contain zeros when checking files.
		virtual int sparse_end(int start) const { return start; }

		// This function should move all the files belonging to the storage to
		// the new save_path. The default storage moves the single file or the
		// directory of the torrent.
		// 
		// Before moving the files, any open file handles may have to be closed,
		// like ``release_files()``.
		// 
		// returns one of:
		// | no_error = 0
		// | need_full_check = -1
		// | fatal_disk_error = -2
		// | file_exist = -4
		virtual int move_storage(std::string const& save_path, int flags) = 0;

		// This function should verify the resume data ``rd`` with the files
		// on disk. If the resume data seems to be up-to-date, return true. If
		// not, set ``error`` to a description of what mismatched and return false.
		//
		// The default storage may compare file sizes and time stamps of the files.
		//
		// Returning ``false`` indicates an error occurred.
		virtual bool verify_resume_data(lazy_entry const& rd, error_code& error) = 0;

		// This function should fill in resume data, the current state of the
		// storage, in ``rd``. The default storage adds file timestamps and
		// sizes.
		// 
		// Returning ``true`` indicates an error occurred.
		virtual bool write_resume_data(entry& rd) const = 0;

		// This function should copy or move the data in slot ``src_slot`` to
		// the slot ``dst_slot``. This is only used in compact mode.
		// 
		// If the storage caches slots, this could be implemented more
		// efficient than reading and writing the data.
		// 
		// Returning ``true`` indicates an error occurred.
		virtual bool move_slot(int src_slot, int dst_slot) = 0;

		// This function should swap the data in ``slot1`` and ``slot2``. The
		// default storage uses a scratch buffer to read the data into, then
		// moving the other slot and finally writing back the temporary slot's
		// data
		// 
		// This is only used in compact mode.
		// 
		// Returning ``true`` indicates an error occurred.
		virtual bool swap_slots(int slot1, int slot2) = 0;

		// This function should do a 3-way swap, or shift of the slots. ``slot1``
		// should move to ``slot2``, which should be moved to ``slot3`` which in
		// turn should be moved to ``slot1``.
		// 
		// This is only used in compact mode.
		// 
		// Returning ``true`` indicates an error occurred.
		virtual bool swap_slots3(int slot1, int slot2, int slot3) = 0;

		// This function should release all the file handles that it keeps open to files
		// belonging to this storage. The default implementation just calls
		// ``file_pool::release_files(this)``.
		// 
		// Returning ``true`` indicates an error occurred.
		virtual bool release_files() = 0;

		// Rename file with index ``file`` to the thame ``new_name``. If there is an error,
		// ``true`` should be returned.
		virtual bool rename_file(int index, std::string const& new_filename) = 0;

		// This function should delete all files and directories belonging to
		// this storage.
		// 
		// Returning ``true`` indicates an error occurred.
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
		virtual bool delete_files() = 0;

#ifndef TORRENT_NO_DEPRECATE
		// This function is called each time a file is completely downloaded. The
		// storage implementation can perform last operations on a file. The file
		// will not be opened for writing after this.
		//
		//	``index`` is the index of the file that completed.
		//	
		// On windows the default storage implementation clears the sparse file
		// flag on the specified file.
		virtual void finalize_file(int) {}
#endif

		// access global disk_buffer_pool, for allocating and freeing disk buffers
		disk_buffer_pool* disk_pool() { return m_disk_pool; }

		// access global session_settings
		session_settings const& settings() const { return *m_settings; }

		// called by the storage implementation to set it into an
		// error state. Typically whenever a critical file operation
		// fails.
		void set_error(std::string const& file, error_code const& ec) const;

		// returns the currently set error code and file path associated with it,
		// if set.
		error_code const& error() const { return m_error; }
		std::string const& error_file() const { return m_error_file; }

		// reset the error state to allow continuing reading and writing
		// to the storage
		virtual void clear_error() { m_error = error_code(); m_error_file.resize(0); }

		// hidden
		mutable error_code m_error;
		mutable std::string m_error_file;

		// hidden
		virtual ~storage_interface() {}

		// hidden
		disk_buffer_pool* m_disk_pool;
		session_settings* m_settings;
	};

	// The default implementation of storage_interface. Behaves as a normal
	// bittorrent client. It is possible to derive from this class in order to
	// override some of its behavior, when implementing a custom storage.
	class TORRENT_EXPORT default_storage : public storage_interface, boost::noncopyable
	{
	public:
		// constructs the default_storage based on the give file_storage (fs).
		// ``mapped`` is an optional argument (it may be NULL). If non-NULL it
		// represents the file mappsing that have been made to the torrent before
		// adding it. That's where files are supposed to be saved and looked for
		// on disk. ``save_path`` is the root save folder for this torrent.
		// ``file_pool`` is the cache of file handles that the storage will use.
		// All files it opens will ask the file_pool to open them. ``file_prio``
		// is a vector indicating the priority of files on startup. It may be
		// an empty vector. Any file whose index is not represented by the vector
		// (because the vector is too short) are assumed to have priority 1.
		// this is used to treat files with priority 0 slightly differently.
		default_storage(file_storage const& fs, file_storage const* mapped
			, std::string const& path, file_pool& fp
			, std::vector<boost::uint8_t> const& file_prio);

		// hidden
		~default_storage();

		// hidden
		void set_file_priority(std::vector<boost::uint8_t> const& prio);
#ifndef TORRENT_NO_DEPRECATE
		void finalize_file(int file);
#endif
		bool has_any_file();
		bool rename_file(int index, std::string const& new_filename);
		bool release_files();
		bool delete_files();
		bool initialize(bool allocate_files);
		int move_storage(std::string const& save_path, int flags);
		int read(char* buf, int slot, int offset, int size);
		int write(char const* buf, int slot, int offset, int size);
		int sparse_end(int start) const;
		void hint_read(int slot, int offset, int len);
		int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs, int flags = file::random_access);
		int writev(file::iovec_t const* buf, int slot, int offset, int num_bufs, int flags = file::random_access);
		size_type physical_offset(int slot, int offset);
		bool move_slot(int src_slot, int dst_slot);
		bool swap_slots(int slot1, int slot2);
		bool swap_slots3(int slot1, int slot2, int slot3);
		bool verify_resume_data(lazy_entry const& rd, error_code& error);
		bool write_resume_data(entry& rd) const;

		// if the files in this storage are mapped, returns the mapped
		// file_storage, otherwise returns the original file_storage object.
		file_storage const& files() const { return m_mapped_files?*m_mapped_files:m_files; }

	private:

		// this identifies a read or write operation
		// so that default_storage::readwritev() knows what to
		// do when it's actually touching the file
		struct fileop
		{
			size_type (file::*regular_op)(size_type file_offset
				, file::iovec_t const* bufs, int num_bufs, error_code& ec);
			size_type (default_storage::*unaligned_op)(boost::intrusive_ptr<file> const& f
				, size_type file_offset, file::iovec_t const* bufs, int num_bufs
				, error_code& ec);
			int cache_setting;
			int mode;
		};

		void delete_one_file(std::string const& p);
		int readwritev(file::iovec_t const* bufs, int slot, int offset
			, int num_bufs, fileop const&);

		size_type read_unaligned(boost::intrusive_ptr<file> const& file_handle
			, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec);
		size_type write_unaligned(boost::intrusive_ptr<file> const& file_handle
			, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec);

		boost::scoped_ptr<file_storage> m_mapped_files;
		file_storage const& m_files;

		// helper function to open a file in the file pool with the right mode
		boost::intrusive_ptr<file> open_file(int file, int mode
			, error_code& ec) const;

		std::vector<boost::uint8_t> m_file_priority;
		std::string m_save_path;
		// the file pool is typically stored in
		// the session, to make all storage
		// instances use the same pool
		file_pool& m_pool;

		// this is a bitfield with one bit per file. A bit being set means
		// we've written to that file previously. If we do write to a file
		// whose bit is 0, we set the file size, to make the file allocated
		// on disk (in full allocation mode) and just sparsely allocated in
		// case of sparse allocation mode
		bitfield m_file_created;

		int m_page_size;
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
		void set_file_priority(std::vector<boost::uint8_t> const&) {}
		bool has_any_file() { return false; }
		bool rename_file(int, std::string const&) { return false; }
		bool release_files() { return false; }
		bool delete_files() { return false; }
		bool initialize(bool) { return false; }
		int move_storage(std::string const&, int) { return 0; }
		int read(char*, int, int, int size) { return size; }
		int write(char const*, int, int, int size) { return size; }
		size_type physical_offset(int, int) { return 0; }
		int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs, int flags = file::random_access);
		int writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs, int flags = file::random_access);
		bool move_slot(int, int) { return false; }
		bool swap_slots(int, int) { return false; }
		bool swap_slots3(int, int, int) { return false; }
		bool verify_resume_data(lazy_entry const&, error_code&) { return false; }
		bool write_resume_data(entry&) const { return false; }

		int m_piece_size;
	};

	// flags for async_move_storage
	enum move_flags_t
	{
		// replace any files in the destination when copying
		// or moving the storage
		always_replace_files,

		// if any files that we want to copy exist in the destination
		// exist, fail the whole operation and don't perform
		// any copy or move. There is an inherent race condition
		// in this mode. The files are checked for existence before
		// the operation starts. In between the check and performing
		// the copy, the destination files may be created, in which
		// case they are replaced.
		fail_if_exist,

		// if any file exist in the target, take those files instead
		// of the ones we may have in the source.
		dont_replace
	};

	struct disk_io_thread;

	class TORRENT_EXTRA_EXPORT piece_manager
		: public intrusive_ptr_base<piece_manager>
		, boost::noncopyable
	{
	friend class invariant_access;
	friend struct disk_io_thread;
	public:

		piece_manager(
			boost::shared_ptr<void> const& torrent
			, boost::intrusive_ptr<torrent_info const> info
			, std::string const& path
			, file_pool& fp
			, disk_io_thread& io
			, storage_constructor_type sc
			, storage_mode_t sm
			, std::vector<boost::uint8_t> const& file_prio);

		~piece_manager();

		boost::intrusive_ptr<torrent_info const> info() const { return m_info; }
		void write_resume_data(entry& rd) const;

		void async_check_fastresume(lazy_entry const* resume_data
			, boost::function<void(int, disk_io_job const&)> const& handler);
		
		void async_check_files(boost::function<void(int, disk_io_job const&)> const& handler);

		void async_rename_file(int index, std::string const& name
			, boost::function<void(int, disk_io_job const&)> const& handler);

		void async_read(
			peer_request const& r
			, boost::function<void(int, disk_io_job const&)> const& handler
			, int cache_line_size = 0
			, int cache_expiry = 0);

		void async_read_and_hash(
			peer_request const& r
			, boost::function<void(int, disk_io_job const&)> const& handler
			, int cache_expiry = 0);

		void async_cache(int piece
			, boost::function<void(int, disk_io_job const&)> const& handler
			, int cache_expiry = 0);

		// returns the write queue size
		int async_write(
			peer_request const& r
			, disk_buffer_holder& buffer
			, boost::function<void(int, disk_io_job const&)> const& f);

		void async_hash(int piece, boost::function<void(int, disk_io_job const&)> const& f);

		void async_release_files(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void abort_disk_io();

		void async_clear_read_cache(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void async_delete_files(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void async_move_storage(std::string const& p, int flags
			, boost::function<void(int, disk_io_job const&)> const& handler);

		void async_set_file_priority(
			std::vector<boost::uint8_t> const& prios
			, boost::function<void(int, disk_io_job const&)> const& handler);

		void async_save_resume_data(
			boost::function<void(int, disk_io_job const&)> const& handler);

		enum return_t
		{
			// return values from check_fastresume and check_files
			no_error = 0,
			need_full_check = -1,
			fatal_disk_error = -2,
			disk_check_aborted = -3,
			file_exist = -4
		};

		storage_interface* get_storage_impl() { return m_storage.get(); }

	private:

		std::string save_path() const;

		bool verify_resume_data(lazy_entry const& rd, error_code& e)
		{ return m_storage->verify_resume_data(rd, e); }

		bool is_allocating() const
		{ return m_state == state_expand_pieces; }

		void mark_failed(int index);

		error_code const& error() const { return m_storage->error(); }
		std::string const& error_file() const { return m_storage->error_file(); }
		int last_piece() const { return m_last_piece; }
		void clear_error() { m_storage->clear_error(); }

		int slot_for(int piece) const;
		int piece_for(int slot) const;
	
		// helper functions for check_dastresume	
		int check_no_fastresume(error_code& error);
		int check_init_storage(error_code& error);
		
		// if error is set and return value is 'no_error' or 'need_full_check'
		// the error message indicates that the fast resume data was rejected
		// if 'fatal_disk_error' is returned, the error message indicates what
		// when wrong in the disk access
		int check_fastresume(lazy_entry const& rd, error_code& error);

		// this function returns true if the checking is complete
		int check_files(int& current_slot, int& have_piece, error_code& error);

#ifndef TORRENT_NO_DEPRECATE
		bool compact_allocation() const
		{ return m_storage_mode == storage_mode_compact; }
#endif

#ifdef TORRENT_DEBUG
		std::string name() const { return m_info->name(); }
#endif

		bool allocate_slots_impl(int num_slots, mutex::scoped_lock& l, bool abort_on_disk = false);

		// updates the ph.h hasher object with the data at the given slot
		// and optionally a 'small hash' as well, the hash for
		// the partial slot. Returns the number of bytes read
		int hash_for_slot(int slot, partial_hash& h, int piece_size
			, int small_piece_size = 0, sha1_hash* small_hash = 0);

		void hint_read_impl(int piece_index, int offset, int size);

		int read_impl(
			file::iovec_t* bufs
			, int piece_index
			, int offset
			, int num_bufs);

		int write_impl(
			file::iovec_t* bufs
			, int piece_index
			, int offset
			, int num_bufs);

		size_type physical_offset(int piece_index, int offset);

		// returns the number of pieces left in the
		// file currently being checked
		int skip_file() const;
		// -1=error 0=ok >0=skip this many pieces
		int check_one_piece(int& have_piece);
		int identify_data(
			sha1_hash const& large_hash
			, sha1_hash const& small_hash
			, int current_slot);

		void switch_to_full_mode();
		sha1_hash hash_for_piece_impl(int piece, int* readback = 0);

		int release_files_impl() { return m_storage->release_files(); }
		int delete_files_impl() { return m_storage->delete_files(); }
		int rename_file_impl(int index, std::string const& new_filename)
		{ return m_storage->rename_file(index, new_filename); }
		void set_file_priority_impl(std::vector<boost::uint8_t> const& p)
		{ m_storage->set_file_priority(p); }

		int move_storage_impl(std::string const& save_path, int flags);

		int allocate_slot_for_piece(int piece_index);
#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant() const;
#endif
#ifdef TORRENT_STORAGE_DEBUG
		void debug_log() const;
#endif
		boost::intrusive_ptr<torrent_info const> m_info;
		file_storage const& m_files;

		boost::scoped_ptr<storage_interface> m_storage;

		storage_mode_t m_storage_mode;

		// slots that haven't had any file storage allocated
		std::vector<int> m_unallocated_slots;
		// slots that have file storage, but isn't assigned to a piece
		std::vector<int> m_free_slots;

		enum
		{
			has_no_slot = -3 // the piece has no storage
		};

		// maps piece indices to slots. If a piece doesn't
		// have any storage, it is set to 'has_no_slot'
		std::vector<int> m_piece_to_slot;

		enum
		{
			unallocated = -1, // the slot is unallocated
			unassigned = -2   // the slot is allocated but not assigned to a piece
		};

		// maps slots to piece indices, if a slot doesn't have a piece
		// it can either be 'unassigned' or 'unallocated'
		std::vector<int> m_slot_to_piece;

		std::string m_save_path;

		mutable mutex m_mutex;

		enum {
			// the default initial state
			state_none,
			// the file checking is complete
			state_finished,
			// checking the files
			state_full_check,
			// move pieces to their final position
			state_expand_pieces
		} m_state;
		int m_current_slot;
		// used during check. If any piece is found
		// that is not in its final position, this
		// is set to true
		bool m_out_of_place;
		// used to move pieces while expanding
		// the storage from compact allocation
		// to full allocation
		aligned_holder m_scratch_buffer;
		aligned_holder m_scratch_buffer2;
		// the piece that is in the scratch buffer
		int m_scratch_piece;

		// the last piece we wrote to or read from
		int m_last_piece;

		// this is saved in case we need to instantiate a new
		// storage (osed when remapping files)
		storage_constructor_type m_storage_constructor;

		// this maps a piece hash to piece index. It will be
		// build the first time it is used (to save time if it
		// isn't needed)
		std::multimap<sha1_hash, int> m_hash_to_piece;
	
		// this map contains partial hashes for downloading
		// pieces. This is only accessed from within the
		// disk-io thread.
		std::map<int, partial_hash> m_piece_hasher;

		disk_io_thread& m_io_thread;

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

