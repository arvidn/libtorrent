/*

Copyright (c) 2015-2018, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_READ_RESUME_DATA_HPP_INCLUDE
#define TORRENT_READ_RESUME_DATA_HPP_INCLUDE

#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/torrent_info.hpp" // for load_torrent_limits

namespace lt {

	// these functions are used to parse resume data and populate the appropriate
	// fields in an add_torrent_params object. This object can then be used to add
	// the actual torrent_info object to and pass to session::add_torrent() or
	// session::async_add_torrent().
	//
	// If the client wants to override any field that was loaded from the resume
	// data, e.g. save_path, those fields must be changed after loading resume
	// data but before adding the torrent.
	//
	// The ``piece_limit`` parameter determines the largest number of pieces
	// allowed in the torrent that may be loaded as part of the resume data, if
	// it contains an ``info`` field. The overloads that take a flat buffer are
	// instead configured with limits on torrent sizes via load_torrent limits.
	//
	// In order to support large torrents, it may also be necessary to raise the
	// settings_pack::max_piece_count setting and pass a higher limit to calls
	// to torrent_info::parse_info_section().
	TORRENT_EXPORT add_torrent_params read_resume_data(bdecode_node const& rd
		, error_code& ec, int piece_limit = 0x200000);
	TORRENT_EXPORT add_torrent_params read_resume_data(span<char const> buffer
		, error_code& ec, load_torrent_limits const& cfg = {});
	TORRENT_EXPORT add_torrent_params read_resume_data(bdecode_node const& rd
		, int piece_limit = 0x200000);
	TORRENT_EXPORT add_torrent_params read_resume_data(span<char const> buffer
		, load_torrent_limits const& cfg = {});
}

#endif
