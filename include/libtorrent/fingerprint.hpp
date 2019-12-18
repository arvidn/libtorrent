/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_FINGERPRINT_HPP_INCLUDED
#define TORRENT_FINGERPRINT_HPP_INCLUDED

#include <string>
#include <cstdio>

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"

namespace libtorrent {

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
