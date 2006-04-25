/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_SESSION_SETTINGS_HPP_INCLUDED
#define TORRENT_SESSION_SETTINGS_HPP_INCLUDED

namespace libtorrent
{

	struct TORRENT_EXPORT session_settings
	{
		session_settings()
			: piece_timeout(120)
			, request_queue_time(3.f)
			, sequenced_download_threshold(7)
			{}

		// the number of seconds from a request is sent until
		// it times out if no piece response is returned.
		int piece_timeout;

		// the length of the request queue given in the number
		// of seconds it should take for the other end to send
		// all the pieces. i.e. the actual number of requests
		// depends on the download rate and this number.
		float request_queue_time;
		
		// this is the limit on how popular a piece has to be
		// (popular == inverse of rarity) to be downloaded
		// in sequence instead of in random (rarest first) order.
		// it can be used to tweak disk performance in settings
		// where the random download property is less necessary.
		// for example, if the threshold is 7, all pieces which 7
		// or more peers have, will be downloaded in index order.
		int sequenced_download_threshold;
	};
}

#endif
