/*

Copyright (c) 2017-2018, 2020-2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_WRITE_RESUME_DATA_HPP_INCLUDE
#define TORRENT_WRITE_RESUME_DATA_HPP_INCLUDE

#include <vector>

#include "libtorrent/fwd.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

	// this function turns the resume data in an ``add_torrent_params`` object
	// into a bencoded structure
	TORRENT_EXPORT entry write_resume_data(add_torrent_params const& atp);
	TORRENT_EXPORT std::vector<char> write_resume_data_buf(add_torrent_params const& atp);

	// hidden
	using write_torrent_flags_t = flags::bitfield_flag<std::uint32_t, struct write_torrent_flags_tag>;

	namespace write_flags
	{
		// this makes write_torrent_file() not fail when attempting to write a
		// v2 torrent file that does not have all the piece layers
		constexpr write_torrent_flags_t allow_missing_piece_layer = 0_bit;

		// don't include http seeds in the torrent file, even if some are
		// present in the add_torrent_params object
		constexpr write_torrent_flags_t no_http_seeds = 1_bit;

		// When set, DHT nodes from the add_torrent_params objects are included
		// in the resulting .torrent file
		constexpr write_torrent_flags_t include_dht_nodes = 2_bit;
	}

	// writes only the fields to create a .torrent file. This function may fail
	// with a ``std::system_error`` exception if:
	//
	// * The add_torrent_params object passed to this function does not contain the
	//   info dictionary (the ``ti`` field)
	// * The piece layers are not complete for all files that need them
	//
	// The ``write_torrent_file_buf()`` overload returns the torrent file in
	// bencoded buffer form. This overload may be faster at the expense of lost
	// flexibility to add custom fields.
	TORRENT_EXPORT entry write_torrent_file(add_torrent_params const& atp);
	TORRENT_EXPORT entry write_torrent_file(add_torrent_params const& atp, write_torrent_flags_t flags);
	TORRENT_EXPORT std::vector<char> write_torrent_file_buf(add_torrent_params const& atp
		, write_torrent_flags_t flags);
}

#endif
