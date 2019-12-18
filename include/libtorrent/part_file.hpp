/*

Copyright (c) 2012-2018, Arvid Norberg
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

#ifndef TORRENT_PART_FILE_HPP_INCLUDE
#define TORRENT_PART_FILE_HPP_INCLUDE

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <memory>

#include "libtorrent/config.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/units.hpp"

namespace libtorrent {

	using slot_index_t = aux::strong_typedef<int, struct slot_index_tag_t>;

	struct TORRENT_EXTRA_EXPORT part_file
	{
		// create a part file at ``path``, that can hold ``num_pieces`` pieces.
		// each piece being ``piece_size`` number of bytes
		part_file(std::string const& path, std::string const& name, int num_pieces, int piece_size);
		~part_file();

		int writev(span<iovec_t const> bufs, piece_index_t piece, int offset, error_code& ec);
		int readv(span<iovec_t const> bufs, piece_index_t piece, int offset, error_code& ec);

		// free the slot the given piece is stored in. We no longer need to store this
		// piece in the part file
		void free_piece(piece_index_t piece);

		void move_partfile(std::string const& path, error_code& ec);

		// the function is called for every block of data belonging to the
		// specified range that's in the part_file. The first parameter is the
		// offset within the range
		void export_file(std::function<void(std::int64_t, span<char>)> f
			, std::int64_t offset, std::int64_t size, error_code& ec);

		// flush the metadata
		void flush_metadata(error_code& ec);

	private:

		void open_file(open_mode_t mode, error_code& ec);
		void flush_metadata_impl(error_code& ec);

		std::int64_t slot_offset(slot_index_t const slot) const
		{ return m_header_size + static_cast<int>(slot) * m_piece_size; }

		std::string m_path;
		std::string const m_name;

		// allocate a slot and return the slot index
		slot_index_t allocate_slot(piece_index_t piece);

		// this mutex must be held while accessing the data
		// structure. Not while reading or writing from the file though!
		// it's important to support multithreading
		std::mutex m_mutex;

		// this is a list of unallocated slots in the part file
		// within the m_num_allocated range
		std::vector<slot_index_t> m_free_slots;

		// this is the number of slots allocated
		slot_index_t m_num_allocated{0};

		// the max number of pieces in the torrent this part file is
		// backing
		int const m_max_pieces;

		// number of bytes each piece contains
		int const m_piece_size;

		// this is the size of the part_file header, it is added
		// to offsets when calculating the offset to read and write
		// payload data from
		int const m_header_size;

		// if this is true, the metadata in memory has changed since
		// we last saved or read it from disk. It means that we
		// need to flush the metadata before closing the file
		bool m_dirty_metadata = false;

		// maps a piece index to the part-file slot it is stored in
		std::unordered_map<piece_index_t, slot_index_t> m_piece_map;

		// this is the file handle to the part file
		// it's allocated on the heap and reference counted, to allow it to be
		// closed and re-opened while other threads are still using it
		std::shared_ptr<file> m_file;
	};
}

#endif
