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

namespace libtorrent
{
	namespace detail
	{
		struct piece_checker_data;
	}

	class session;

	std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		torrent_info const& t
		, boost::filesystem::path p);

	bool match_filesizes(
		torrent_info const& t
		, boost::filesystem::path p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes);

	struct file_allocation_failed: std::exception
	{
		file_allocation_failed(const char* error_msg): m_msg(error_msg) {}
		virtual const char* what() const throw() { return m_msg.c_str(); }
		virtual ~file_allocation_failed() throw() {}
		std::string m_msg;
	};

	class storage
	{
	public:
		storage(
			const torrent_info& info
		  , const boost::filesystem::path& path);

		void swap(storage&);

		// may throw file_error if storage for slot does not exist
		size_type read(char* buf, int slot, int offset, int size);

		// may throw file_error if storage for slot hasn't been allocated
		void write(const char* buf, int slot, int offset, int size);

		bool move_storage(boost::filesystem::path save_path);

		// this will close all open files that are opened for
		// writing. This is called when a torrent has finished
		// downloading.
		void release_files();

#ifndef NDEBUG
		// overwrites some slots with the
		// contents of others
		void storage::shuffle();
#endif

	private:
		class impl;
		boost::shared_ptr<impl> m_pimpl;
	};

	class piece_manager : boost::noncopyable
	{
	public:

		piece_manager(
			const torrent_info& info
		  , const boost::filesystem::path& path);

		~piece_manager();

		void check_pieces(
			boost::mutex& mutex
		  , detail::piece_checker_data& data
		  , std::vector<bool>& pieces);

		void release_files();

		void allocate_slots(int num_slots);
		void mark_failed(int index);

		unsigned long piece_crc(
			int slot_index
			, int block_size
			, const std::bitset<256>& bitmask);
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

	private:
		class impl;
		std::auto_ptr<impl> m_pimpl;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED

