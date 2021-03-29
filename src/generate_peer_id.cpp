/*

Copyright (c) 2018-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/generate_peer_id.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/aux_/string_util.hpp" // for url_random

#include <string>

namespace lt::aux {

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

}
