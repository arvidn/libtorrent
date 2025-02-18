/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_LOAD_TORRENT_HPP_INCLUDED
#define TORRENT_LOAD_TORRENT_HPP_INCLUDED

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/torrent_info.hpp" // for load_torrent_limits
#include "libtorrent/aux_/export.hpp"

namespace libtorrent {

	// These functions load the content of a .torrent file into an
	// add_torrent_params object.
	// The immutable part of a torrent file (the info-dictionary) is stored in
	// the ``ti`` field in the add_torrent_params object (as a torrent_info
	// object).
	// The returned object is suitable to be:
	//
	//   * added to a session via add_torrent() or async_add_torrent()
	//   * saved as a .torrent_file via write_torrent_file()
	//   * turned into a magnet link via make_magnet_uri()
	TORRENT_EXPORT add_torrent_params load_torrent_file(
		std::string const& filename, load_torrent_limits const& cfg);
	TORRENT_EXPORT add_torrent_params load_torrent_file(
		std::string const& filename);
	TORRENT_EXPORT add_torrent_params load_torrent_buffer(
		span<char const> buffer, load_torrent_limits const& cfg);
	TORRENT_EXPORT add_torrent_params load_torrent_buffer(
		span<char const> buffer);
	TORRENT_EXPORT add_torrent_params load_torrent_parsed(
		bdecode_node const& torrent_file, load_torrent_limits const& cfg);
	TORRENT_EXPORT add_torrent_params load_torrent_parsed(
		bdecode_node const& torrent_file);
}

namespace libtorrent::aux {
	std::shared_ptr<torrent_info> parse_torrent_file(bdecode_node const& torrent_file
		, error_code& ec, load_torrent_limits const& cfg, add_torrent_params& out);

#if TORRENT_ABI_VERSION < 4
	using torrent_info_ptr = std::shared_ptr<torrent_info>;
#else
	using torrent_info_ptr = std::shared_ptr<torrent_info const>;
#endif
}

#endif
