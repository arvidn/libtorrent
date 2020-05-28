/*

Copyright (c) 2018-2019, Arvid Norberg
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

#include "libtorrent/aux_/generate_peer_id.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/string_util.hpp" // for url_random

#include <string>

namespace libtorrent { namespace aux {

peer_id generate_peer_id(session_settings const& sett)
{
	peer_id ret;
	std::string print = sett.get_str(settings_pack::peer_fingerprint);
	if (std::ptrdiff_t(print.size()) > ret.size())
		print.resize(std::size_t(ret.size()));

	// the client's fingerprint
	std::copy(print.begin(), print.end(), ret.begin());
	if (std::ptrdiff_t(print.size()) < ret.size())
		url_random(span<char>(ret).subspan(std::ptrdiff_t(print.length())));
	return ret;
}

}}
