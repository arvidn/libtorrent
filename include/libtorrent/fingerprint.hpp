/*

Copyright (c) 2003-2016, Arvid Norberg
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
#include "libtorrent/peer_id.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

	// The fingerprint class represents information about a client and its version. It is used
	// to encode this information into the client's peer id.
	struct fingerprint
	{

		// The constructor takes a ``char const*`` that should point to a string constant containing
		// exactly two characters. These are the characters that should be unique for your client. Make
		// sure not to clash with anybody else. Here are some taken id's:
		// 
		// +----------+-----------------------+
		// | id chars | client                |
		// +==========+=======================+
		// | 'AZ'     | Azureus               |
		// +----------+-----------------------+
		// | 'LT'     | libtorrent (default)  |
		// +----------+-----------------------+
		// | 'BX'     | BittorrentX           |
		// +----------+-----------------------+
		// | 'MT'     | Moonlight Torrent     |
		// +----------+-----------------------+
		// | 'TS'     | Torrent Storm         |
		// +----------+-----------------------+
		// | 'SS'     | Swarm Scope           |
		// +----------+-----------------------+
		// | 'XT'     | Xan Torrent           |
		// +----------+-----------------------+
		// 
		// There's an informal directory of client id's here_.
		// 
		// .. _here: http://wiki.theory.org/BitTorrentSpecification#peer_id
		//
		// The ``major``, ``minor``, ``revision`` and ``tag`` parameters are used to identify the
		// version of your client.
		fingerprint(const char* id_string, int major, int minor, int revision, int tag)
			: major_version(major)
			, minor_version(minor)
			, revision_version(revision)
			, tag_version(tag)
		{
			TORRENT_ASSERT(id_string);
			TORRENT_ASSERT(major >= 0);
			TORRENT_ASSERT(minor >= 0);
			TORRENT_ASSERT(revision >= 0);
			TORRENT_ASSERT(tag >= 0);
			TORRENT_ASSERT(std::strlen(id_string) == 2);
			name[0] = id_string[0];
			name[1] = id_string[1];
		}

		// generates the actual string put in the peer-id, and return it.
		std::string to_string() const
		{
			char s[100];
			snprintf(s, 100, "-%c%c%c%c%c%c-"
				,  name[0], name[1]
				, version_to_char(major_version)
				, version_to_char(minor_version)
				, version_to_char(revision_version)
				, version_to_char(tag_version));
			return s;
		}

		char name[2];
		int major_version;
		int minor_version;
		int revision_version;
		int tag_version;

	private:

		char version_to_char(int v) const
		{
			if (v >= 0 && v < 10) return '0' + v;
			else if (v >= 10) return 'A' + (v - 10);
			TORRENT_ASSERT(false);
			return '0';
		}

	};

}

#endif // TORRENT_FINGERPRINT_HPP_INCLUDED

