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

#include <ctime>
#include <iostream>
#include <ios>
#include <algorithm>
#include <vector>
#include <set>
#include <list>
#include <fstream>

#include <boost/limits.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/policy.hpp"

/*
 * This file declares the following functions:
 *
 *----------------------------------
 *
 *
 */

namespace libtorrent
{
	namespace detail
	{
		class piece_checker_data;
	}
	class session;

	struct file_allocation_failed: std::exception
	{
		file_allocation_failed(const char* error_msg): m_msg(error_msg) {}
		virtual const char* what() const throw() { return m_msg.c_str(); }
		virtual ~file_allocation_failed() throw() {}
		std::string m_msg;
	};

	// wraps access to pieces with a file-like interface
	class piece_file
	{
	friend class storage;
	public:

		piece_file(): m_piece_index(-1), m_storage(0) {}
		~piece_file()
		{
			if (m_piece_index >= 0) close();
		}

		enum open_mode { in, out };

		// opens a piece with the given index from storage s
		void open(storage* s, int index, open_mode m, int seek_offset = 0, bool lock_ = true);
		void close()
		{
			//std::cout << std::clock() << "close " << m_piece_index << "\n";
			m_file.close();
			m_piece_index = -1;
			m_storage = 0;
		}

		void write(const char* buf, int size, bool lock_ = true);
		int read(char* buf, int size, bool lock_ = true);
		void seek_forward(int step, bool lock_ = true);

		// tells the position in the file
		int tell() const { return m_piece_offset; }
		int left() const { return m_piece_size - m_piece_offset; }

		int index() const { return m_piece_index; }
		void lock(bool lock_ = true);
		
	private:

		void reopen();

		// the file itself
		boost::filesystem::fstream m_file;

		// the mode with which this file was opened
		open_mode m_mode;

		// the mode the fstream object was opened in
		std::ios_base::openmode m_file_mode;

		// file we're currently reading from/writing to
		std::vector<file>::const_iterator m_file_iter;

		// the global position
		entry::integer_type m_position;
		
		// the position we're at in the current file
		std::size_t m_file_offset;

		// the byte offset in the current piece
		std::size_t m_piece_offset;

		// the size of the current piece
		int m_piece_size;

		// the index of the piece, -1 means the piece isn't open
		int m_piece_index;

		storage* m_storage;

	};

	class storage
	{
	friend class piece_file;
	friend class piece_sorter;
	public:

		void initialize_pieces(torrent* t,
			const boost::filesystem::path& path,
			detail::piece_checker_data* data,
			boost::mutex& mutex);

		int bytes_left() const { return m_bytes_left; }

		unsigned int num_pieces() const { return m_torrent_file->num_pieces(); }
		bool have_piece(unsigned int index) const { return m_have_piece[index]; }

		bool verify_piece(piece_file& index);

		const std::vector<bool>& pieces() const { return m_have_piece; }

		entry::integer_type piece_storage(int piece);
		void allocate_pieces(int num);

	private:

		void libtorrent::storage::invariant() const;

		// total number of bytes left to be downloaded
		entry::integer_type m_bytes_left;

		// the location where the downloaded file should be stored
		boost::filesystem::path m_save_path;

		// a bitmask representing the pieces we have
		std::vector<bool> m_have_piece;

		const torrent_info* m_torrent_file;

		// allocated pieces in file
		std::vector<entry::integer_type> m_allocated_pieces;
		// unallocated blocks at the end of files
		std::vector<entry::integer_type> m_free_blocks;
		// allocated blocks whose checksum doesn't match
		std::vector<entry::integer_type> m_free_pieces;

		// index here is a slot number in the file
		// -1 : the slot is unallocated
		// -2 : the slot is allocated but not assigned to a piece
		//  * : the slot is assigned to this piece
		std::vector<int> m_slot_to_piece;

		// synchronization
		boost::mutex m_locked_pieces_monitor;
		boost::condition m_unlocked_pieces;
		std::vector<bool> m_locked_pieces;

		boost::recursive_mutex m_mutex;

		torrent* m_torrent;
	};

	class piece_sorter
	{
	public:
		void operator()();

	private:
		typedef std::vector<int> pieces_type;

		storage* m_storage;	
		boost::mutex m_monitor;
		boost::condition m_more_pieces;
		pieces_type m_pieces;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED

