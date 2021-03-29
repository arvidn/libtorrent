/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, d-komarov
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
#include "libtorrent/aux_/file.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t

namespace lt::aux {

	using slot_index_t = aux::strong_typedef<int, struct slot_index_tag_t>;

	struct TORRENT_EXTRA_EXPORT part_file
	{
		// create a part file at ``path``, that can hold ``num_pieces`` pieces.
		// each piece being ``piece_size`` number of bytes
		part_file(std::string path, std::string name, int num_pieces, int piece_size);
		~part_file();

		int writev(span<iovec_t const> bufs, piece_index_t piece, int offset, error_code& ec);
		int readv(span<iovec_t const> bufs, piece_index_t piece, int offset, error_code& ec);
		int hashv(hasher& ph, std::ptrdiff_t len, piece_index_t piece, int offset, error_code& ec);
		int hashv2(hasher256& ph, std::ptrdiff_t len, piece_index_t piece, int offset, error_code& ec);

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

		file open_file(aux::open_mode_t mode, error_code& ec);
		void flush_metadata_impl(error_code& ec);

		std::int64_t slot_offset(slot_index_t const slot) const
		{
			return static_cast<int>(slot) * static_cast<std::int64_t>(m_piece_size)
				+ m_header_size;
		}

		template <typename Hasher>
		int do_hashv(Hasher& ph, std::ptrdiff_t len, piece_index_t piece, int offset, error_code& ec);

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
	};
}

#endif
