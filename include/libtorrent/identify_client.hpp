/*

Copyright (c) 2003, 2006, 2013-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_IDENTIFY_CLIENT_HPP_INCLUDED
#define TORRENT_IDENTIFY_CLIENT_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_ABI_VERSION == 1
#include <optional>
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/fingerprint.hpp"

// TODO: hide this declaration when deprecated functions are disabled, and
// remove its internal use
namespace lt {
namespace aux {

	TORRENT_EXTRA_EXPORT
	std::string identify_client_impl(const peer_id& p);

}

	// these functions don't really need to be public. This mechanism of
	// advertising client software and version is also out-dated.

	// This function can can be used to extract a string describing a client
	// version from its peer-id. It will recognize most clients that have this
	// kind of identification in the peer-id.
	TORRENT_DEPRECATED_EXPORT
	std::string identify_client(const peer_id& p);

#if TORRENT_ABI_VERSION == 1

#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

	// Returns an optional fingerprint if any can be identified from the peer
	// id. This can be used to automate the identification of clients. It will
	// not be able to identify peers with non- standard encodings. Only Azureus
	// style, Shadow's style and Mainline style.
	TORRENT_DEPRECATED_EXPORT
	std::optional<fingerprint>
		client_fingerprint(peer_id const& p);

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif // TORRENT_ABI_VERSION

}

#endif // TORRENT_IDENTIFY_CLIENT_HPP_INCLUDED
