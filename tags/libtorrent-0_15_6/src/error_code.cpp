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

#include "libtorrent/config.hpp"
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
			"unsupported URL protocol",
			"failed to parse URL",
			"peer sent 0 length piece",
			"parse failed",
			"invalid file format tag",
			"missing info-hash",
			"mismatching info-hash",
			"invalid hostname",
			"invalid port",
			"port blocked by port-filter",
			"expected closing ] for address",
			"destructing torrent",
			"timed out",
			"upload to upload connection",
			"uninteresting upload-only peer",
			"invalid info-hash",
			"torrent paused",
			"'have'-message with higher index than the number of pieces",
			"bitfield of invalid size",
			"too many piece requests while choked",
			"invalid piece packet",
			"out of memory",
			"torrent aborted",
			"connected to ourselves",
			"invalid piece size",
			"timed out: no interest",
			"timed out: inactivity",
			"timed out: no handshake",
			"timed out: no request",
			"invalid choke message",
			"invalid unchoke message",
			"invalid interested message",
			"invalid not-interested message",
			"invalid request message",
			"invalid hash list",
			"invalid hash piece message",
			"invalid cancel message",
			"invalid dht-port message",
			"invalid suggest piece message",
			"invalid have-all message",
			"invalid have-none message",
			"invalid reject message",
			"invalid allow-fast message",
			"invalid extended message",
			"invalid message",
			"sync hash not found",
			"unable to verify encryption constant",
			"plaintext mode not provided",
			"rc4 mode not provided",
			"unsupported encryption mode",
			"peer selected unsupported encryption mode",
			"invalid encryption pad size",
			"invalid encryption handshake",
			"incoming encrypted connections disabled",
			"incoming regular connections disabled",
			"duplicate peer-id",
			"torrent removed",
			"packet too large",
			"",
			"HTTP error",
			"missing location header",
			"invalid redirection",
			"redirecting",
			"invalid HTTP range",
			"missing content-length",
			"banned by IP filter",
			"too many connections",
			"peer banned",
			"stopping torrent",
			"too many corrupt pieces",
			"torrent is not ready to accept peers",
			"peer is not properly constructed",
			"session is closing",
			"optimistic disconnect",
			"torrent finished",
			"no router found",
			"metadata too large",
			"invalid metadata request",
			"invalid metadata size",
			"invalid metadata offset",
			"invalid metadata message",
			"pex message too large",
			"invalid pex message",
			"invalid lt_tracker message",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
// natpmp errors
			"unsupported protocol version",
			"not authorized to create port map (enable NAT-PMP on your router)",
			"network failure",
			"out of resources",
			"unsupported opcode",
			"",
			"",
			"",
			"",
			"",
// fastresume errors
			"missing or invalid 'file sizes' entry",
			"no files in resume data",
			"missing 'slots' and 'pieces' entry",
			"mismatching number of files",
			"mismatching file size",
			"mismatching file timestamp",
			"not a dictionary",
			"invalid 'blocks per piece' entry",
			"missing slots list",
			"file has more slots than torrent",
			"invalid entry type in slot list",
			"invalid piece index in slot list",
			"pieces needs to be reordered",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
// HTTP errors
			"Invalid HTTP header",
			"missing Location header in HTTP redirect",
			"failed to decompress HTTP response",
			"",
			"",
			"",
			"",
			"",
			"",
			"",
		};
		if (ev < 0 || ev >= int(sizeof(msgs)/sizeof(msgs[0])))
			return "Unknown error";
		return msgs[ev];
	}

#endif

}

