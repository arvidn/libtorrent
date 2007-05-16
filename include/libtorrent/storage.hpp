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
#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif


#include "libtorrent/torrent_info.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent
{
	namespace aux
	{
		struct piece_checker_data;
	}

	class session;
	struct file_pool;

#if defined(_WIN32) && defined(UNICODE)

	TORRENT_EXPORT std::wstring safe_convert(std::string const& s);

#endif
	
	TORRENT_EXPORT std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		torrent_info const& t
		, boost::filesystem::path p);

	TORRENT_EXPORT bool match_filesizes(
		torrent_info const& t
		, boost::filesystem::path p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes
		, std::string* error = 0);

	struct TORRENT_EXPORT file_allocation_failed: std::exception
	{
		file_allocation_failed(const char* error_msg): m_msg(error_msg) {}
		virtual const char* what() const throw() { return m_msg.c_str(); }
		virtual ~file_allocation_failed() throw() {}
		std::string m_msg;
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

		virtual bool move_storage(boost::filesystem::path save_path) = 0;

		virtual bool verify_resume_data(entry& rd, std::string& error) = 0;

		// moves (or copies) the content in src_slot to dst_slot
		virtual void move_slot(int src_slot, int dst_slot) = 0;

		// swaps the data in slot1 and slot2
		virtual void swap_slots(int slot1, int slot2) = 0;

		// swaps the puts the data in slot1 in slot2, the data in slot2
		// in slot3 and the data in slot3 in slot1
		virtual void swap_slots3(int slot1, int slot2, int slot3) = 0;

		// this will close all open files that are opened for
		// writing. This is called when a torrent has finished
		// downloading.
		virtual void release_files() = 0;
		virtual ~storage_interface() {}
	};

	typedef storage_interface* (&storage_constructor_type)(
		torrent_info const&, boost::filesystem::path const&
		, file_pool&);

	TORRENT_EXPORT storage_interface* default_storage_constructor(torrent_info const& ti
		, boost::filesystem::path const& path, file_pool& fp);

	// returns true if the filesystem the path relies on supports
	// sparse files or automatic zero filling of files.
	TORRENT_EXPORT bool supports_sparse_files(boost::filesystem::path const& p);

	class TORRENT_EXPORT piece_manager : boost::noncopyable
	{
	public:

		piece_manager(
			const torrent_info& info
			, const boost::filesystem::path& path
			, file_pool& fp
			, storage_constructor_type sc);

		~piece_manager();

		bool check_fastresume(aux::piece_checker_data& d
			, std::vector<bool>& pieces, int& num_pieces, bool compact_mode);
		std::pair<bool, float> check_files(std::vector<bool>& pieces
			, int& num_pieces, boost::recursive_mutex& mutex);

		void release_files();

		bool verify_resume_data(entry& rd, std::string& error);

		bool is_allocating() const;
		bool allocate_slots(int num_slots, bool abort_on_disk = false);
		void mark_failed(int index);

		unsigned long piece_crc(
			int slot_index
			, int block_size
			, piece_picker::block_info const* bi);
		int slot_for_piece(int piece_index) const;

		size_type read(
			char* buf
			, int piece_index
			, int offset
			, int size);

		void write(
			const char* buf
			, int piece_index
			, int offset
			, int size);

		boost::filesystem::path const& save_path() const;
		bool move_storage(boost::filesystem::path const&);

		// fills the vector that maps all allocated
		// slots to the piece that is stored (or
		// partially stored) there. -2 is the index
		// of unassigned pieces and -1 is unallocated
		void export_piece_map(std::vector<int>& pieces) const;

		bool compact_allocation() const;

	private:
		class impl;
		std::auto_ptr<impl> m_pimpl;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED

