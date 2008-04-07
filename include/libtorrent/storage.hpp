/*

Copyright (c) 2003, Arvid Norberg
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
#include <bitset>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/limits.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/filesystem/path.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif


#include "libtorrent/torrent_info.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/buffer.hpp"

namespace libtorrent
{
	namespace aux
	{
		struct piece_checker_data;
	}

	namespace fs = boost::filesystem;

	class session;
	struct file_pool;
	struct disk_io_job;

	enum storage_mode_t
	{
		storage_mode_allocate = 0,
		storage_mode_sparse,
		storage_mode_compact
	};
	
#if defined(_WIN32) && defined(UNICODE)

	TORRENT_EXPORT std::wstring safe_convert(std::string const& s);

#endif
	
	TORRENT_EXPORT std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		torrent_info const& t
		, fs::path p);

	TORRENT_EXPORT bool match_filesizes(
		torrent_info const& t
		, fs::path p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes
		, bool compact_mode
		, std::string* error = 0);

	struct TORRENT_EXPORT file_allocation_failed: std::exception
	{
		file_allocation_failed(const char* error_msg): m_msg(error_msg) {}
		virtual const char* what() const throw() { return m_msg.c_str(); }
		virtual ~file_allocation_failed() throw() {}
		std::string m_msg;
	};

	struct TORRENT_EXPORT partial_hash
	{
		partial_hash(): offset(0) {}
		// the number of bytes in the piece that has been hashed
		int offset;
		// the sha-1 context
		hasher h;
	};

	struct TORRENT_EXPORT storage_interface
	{
		// create directories and set file sizes
		// if allocate_files is true. 
		// allocate_files is true if allocation mode
		// is set to full and sparse files are supported
		virtual void initialize(bool allocate_files) = 0;

		// may throw file_error if storage for slot does not exist
		virtual size_type read(char* buf, int slot, int offset, int size) = 0;

		// may throw file_error if storage for slot hasn't been allocated
		virtual void write(const char* buf, int slot, int offset, int size) = 0;

		virtual bool move_storage(fs::path save_path) = 0;

		// verify storage dependent fast resume entries
		virtual bool verify_resume_data(entry& rd, std::string& error) = 0;

		// write storage dependent fast resume entries
		virtual void write_resume_data(entry& rd) const = 0;

		// moves (or copies) the content in src_slot to dst_slot
		virtual void move_slot(int src_slot, int dst_slot) = 0;

		// swaps the data in slot1 and slot2
		virtual void swap_slots(int slot1, int slot2) = 0;

		// swaps the puts the data in slot1 in slot2, the data in slot2
		// in slot3 and the data in slot3 in slot1
		virtual void swap_slots3(int slot1, int slot2, int slot3) = 0;

		// returns the sha1-hash for the data at the given slot
		virtual sha1_hash hash_for_slot(int slot, partial_hash& h, int piece_size) = 0;

		// this will close all open files that are opened for
		// writing. This is called when a torrent has finished
		// downloading.
		virtual void release_files() = 0;

		// this will close all open files and delete them
		virtual void delete_files() = 0;

		virtual ~storage_interface() {}
	};

	typedef storage_interface* (&storage_constructor_type)(
		boost::intrusive_ptr<torrent_info const>, fs::path const&
		, file_pool&);

	TORRENT_EXPORT storage_interface* default_storage_constructor(
		boost::intrusive_ptr<torrent_info const> ti
		, fs::path const& path, file_pool& fp);

	struct disk_io_thread;

	class TORRENT_EXPORT piece_manager
		: public intrusive_ptr_base<piece_manager>
		, boost::noncopyable
	{
	friend class invariant_access;
	friend struct disk_io_thread;
	public:

		piece_manager(
			boost::shared_ptr<void> const& torrent
			, boost::intrusive_ptr<torrent_info const> ti
			, fs::path const& path
			, file_pool& fp
			, disk_io_thread& io
			, storage_constructor_type sc);

		~piece_manager();

		bool check_fastresume(aux::piece_checker_data& d
			, std::vector<bool>& pieces, int& num_pieces, storage_mode_t storage_mode
			, std::string& error_msg);
		std::pair<bool, float> check_files(std::vector<bool>& pieces
			, int& num_pieces, boost::recursive_mutex& mutex);

		// frees a buffer that was returned from a read operation
		void free_buffer(char* buf);

		void write_resume_data(entry& rd) const;
		bool verify_resume_data(entry& rd, std::string& error);

		bool is_allocating() const
		{ return m_state == state_expand_pieces; }

		void mark_failed(int index);

		unsigned long piece_crc(
			int slot_index
			, int block_size
			, piece_picker::block_info const* bi);

		int slot_for(int piece) const;
		int piece_for(int slot) const;
		
		void async_read(
			peer_request const& r
			, boost::function<void(int, disk_io_job const&)> const& handler
			, char* buffer = 0
			, int priority = 0);

		void async_write(
			peer_request const& r
			, char const* buffer
			, boost::function<void(int, disk_io_job const&)> const& f);

		void async_hash(int piece, boost::function<void(int, disk_io_job const&)> const& f);

		fs::path save_path() const;

		void async_release_files(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void async_delete_files(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void async_move_storage(fs::path const& p
			, boost::function<void(int, disk_io_job const&)> const& handler);

		// fills the vector that maps all allocated
		// slots to the piece that is stored (or
		// partially stored) there. -2 is the index
		// of unassigned pieces and -1 is unallocated
		void export_piece_map(std::vector<int>& pieces
			, std::vector<bool> const& have) const;

		bool compact_allocation() const
		{ return m_storage_mode == storage_mode_compact; }

#ifndef NDEBUG
		std::string name() const { return m_info->name(); }
#endif
		
	private:

		bool allocate_slots(int num_slots, bool abort_on_disk = false);

		int identify_data(
			const std::vector<char>& piece_data
			, int current_slot
			, std::vector<bool>& have_pieces
			, int& num_pieces
			, const std::multimap<sha1_hash, int>& hash_to_piece
			, boost::recursive_mutex& mutex);

		size_type read_impl(
			char* buf
			, int piece_index
			, int offset
			, int size);

		void write_impl(
			const char* buf
			, int piece_index
			, int offset
			, int size);

		void switch_to_full_mode();
		sha1_hash hash_for_piece_impl(int piece);

		void release_files_impl() { m_storage->release_files(); }
		void delete_files_impl() { m_storage->delete_files(); }

		bool move_storage_impl(fs::path const& save_path);

		int allocate_slot_for_piece(int piece_index);
#ifndef NDEBUG
		void check_invariant() const;
#ifdef TORRENT_STORAGE_DEBUG
		void debug_log() const;
#endif
#endif
		boost::scoped_ptr<storage_interface> m_storage;

		storage_mode_t m_storage_mode;

		boost::intrusive_ptr<torrent_info const> m_info;

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

		fs::path m_save_path;

		mutable boost::recursive_mutex m_mutex;

		enum {
			// the default initial state
			state_none,
			// the file checking is complete
			state_finished,
			// creating the directories
			state_create_files,
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
		buffer m_scratch_buffer;
		buffer m_scratch_buffer2;
		// the piece that is in the scratch buffer
		int m_scratch_piece;
		
		// this is saved in case we need to instantiate a new
		// storage (osed when remapping files)
		storage_constructor_type m_storage_constructor;

		// temporary buffer used while checking
		std::vector<char> m_piece_data;
		
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
#ifndef NDEBUG
		bool m_resume_data_verified;
#endif
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED

