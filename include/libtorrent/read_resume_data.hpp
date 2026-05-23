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

namespace libtorrent {

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
	// settings_pack::max_piece_count setting and pass a higher limit in the cfg
	// parameter.
	//
	// .. warning::
	//    Resume data is assumed to be trusted input, written by the same client
	//    on the same machine. Many fields parsed here directly control behavior
	//    that affects the host system or the network: ``save_path`` and
	//    ``mapped_files`` determine where files are written on disk (and may be
	//    absolute paths), the embedded ``info`` dict can substitute a different
	//    torrent than the caller expects, and ``trackers``, ``url-list`` and
	//    ``httpseeds`` cause outbound network requests to URLs taken verbatim
	//    from the resume data. A malicious resume file can therefore write to
	//    arbitrary filesystem locations the process has access to, and cause
	//    requests to attacker-controlled hosts.
	//
	//    Client applications are responsible for protecting resume files from
	//    tampering. At minimum, store them with filesystem permissions that
	//    prevent other users on the system from modifying them, and do not load
	//    resume data received from untrusted sources (e.g. downloaded, synced
	//    from a shared location, or supplied by a peer) without independently
	//    validating or overriding the security-sensitive fields after parsing.
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
