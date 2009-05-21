/*

Copyright (c) 2008, Arvid Norberg
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

#include <boost/version.hpp>

#include "libtorrent/error_code.hpp"

namespace libtorrent
{
#if BOOST_VERSION >= 103500

	const char* libtorrent_error_category::name() const
	{
		return "libtorrent error";
	}

	std::string libtorrent_error_category::message(int ev) const
	{
		static char const* msgs[] =
		{
			"no error",
			"torrent file collides with file from another torrent",
			"hash check failed",
			"torrent file is not a dictionary",
			"missing or invalid 'info' section in torrent file",
			"'info' entry is not a dictionary",
			"invalid or missing 'piece length' entry in torrent file",
			"missing name in torrent file",
			"invalid 'name' of torrent (possible exploit attempt)",
			"invalid length of torrent",
			"failed to parse files from torrent file",
			"invalid or missing 'pieces' entry in torrent file",
			"incorrect number of piece hashes in torrent file",
			"too many pieces in torrent",
			"invalid metadata received from swarm",
			"invalid bencoding",
			"no files in torrent",
			"invalid escaped string",
			"session is closing",
			"torrent already exists in session",
			"invalid torrent handle used",
			"invalid type requested from entry",
			"missing info-hash from URI",
			"file too short",
		};
		if (ev < 0 || ev >= sizeof(msgs)/sizeof(msgs[0]))
			return "Unknown error";
		return msgs[ev];
	}

	TORRENT_EXPORT libtorrent_error_category libtorrent_category;

#else

	::asio::error::error_category libtorrent_category = asio::error::error_category(20);

#endif

}

