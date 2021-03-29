/*

Copyright (c) 2003, 2006, 2009, 2013, 2016-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FINGERPRINT_HPP_INCLUDED
#define TORRENT_FINGERPRINT_HPP_INCLUDED

#include <string>
#include <cstdio>

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"

namespace lt {

	// This is a utility function to produce a client ID fingerprint formatted to
	// the most common convention. The fingerprint can be set via the
	// ``peer_fingerprint`` setting, in settings_pack.
	//
	// The name string should contain exactly two characters. These are the
	// characters unique to your client, used to identify it. Make sure not to
	// clash with anybody else. Here are some taken id's:
	//
	// +----------+-----------------------+
	// | id chars | client                |
	// +==========+=======================+
	// | LT       | libtorrent (default)  |
	// +----------+-----------------------+
	// | UT       | uTorrent              |
	// +----------+-----------------------+
	// | UM       | uTorrent Mac          |
	// +----------+-----------------------+
	// | qB       | qBittorrent           |
	// +----------+-----------------------+
	// | BP       | BitTorrent Pro        |
	// +----------+-----------------------+
	// | BT       | BitTorrent            |
	// +----------+-----------------------+
	// | DE       | Deluge                |
	// +----------+-----------------------+
	// | AZ       | Azureus               |
	// +----------+-----------------------+
	// | TL       | Tribler               |
	// +----------+-----------------------+
	//
	// There's an informal directory of client id's here_.
	//
	// .. _here: http://wiki.theory.org/BitTorrentSpecification#peer_id
	//
	// The ``major``, ``minor``, ``revision`` and ``tag`` parameters are used to
	// identify the version of your client.
	TORRENT_EXPORT std::string generate_fingerprint(std::string name
		, int major, int minor = 0, int revision = 0, int tag = 0);

	// The fingerprint class represents information about a client and its version. It is used
	// to encode this information into the client's peer id.
	struct TORRENT_DEPRECATED_EXPORT fingerprint
	{
		fingerprint(const char* id_string, int major, int minor, int revision, int tag);

#if TORRENT_ABI_VERSION == 1
		// generates the actual string put in the peer-id, and return it.
		std::string to_string() const;
#endif

		char name[2];
		int major_version;
		int minor_version;
		int revision_version;
		int tag_version;
	};

}

#endif // TORRENT_FINGERPRINT_HPP_INCLUDED
