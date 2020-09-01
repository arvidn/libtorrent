/*

Copyright (c) 2015-2017, Steven Siloti
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_IP_HELPERS_HPP_INCLUDED
#define TORRENT_IP_HELPERS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/export.hpp"

namespace libtorrent {
namespace aux {

	TORRENT_EXTRA_EXPORT bool is_global(address const& a);
	TORRENT_EXTRA_EXPORT bool is_local(address const& a);
	TORRENT_EXTRA_EXPORT bool is_link_local(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_teredo(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_ip_address(std::string const& host);

	// internal
	template <typename Endpoint>
	bool is_v4(Endpoint const& ep)
	{
		return ep.protocol() == Endpoint::protocol_type::v4();
	}
	template <typename Endpoint>
	bool is_v6(Endpoint const& ep)
	{
		return ep.protocol() == Endpoint::protocol_type::v6();
	}

	TORRENT_EXTRA_EXPORT address ensure_v6(address const& a);

}
}

#endif
