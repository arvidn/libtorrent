/*

Copyright (c) 2006, MassaRoddel
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

#ifndef TORRENT_UT_PEX_EXTENSION_HPP_INCLUDED
#define TORRENT_UT_PEX_EXTENSION_HPP_INCLUDED

#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp" // for endpoint
#include "libtorrent/address.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/shared_ptr.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	struct torrent_plugin;
	struct peer_plugin;
	struct torrent_handle;

	struct TORRENT_EXPORT ut_pex_peer_store
	{
		// stores all peers this peer is connected to. These lists
		// are updated with each pex message and are limited in size
		// to protect against malicious clients. These lists are also
		// used for looking up which peer a peer that supports holepunch
		// came from.
		// these are vectors to save memory and keep the items close
		// together for performance. Inserting and removing is relatively
		// cheap since the lists' size is limited
		using peers4_t = std::vector<std::pair<address_v4::bytes_type, std::uint16_t>>;
		peers4_t m_peers;
#if TORRENT_USE_IPV6
		using peers6_t = std::vector<std::pair<address_v6::bytes_type, std::uint16_t>>;
		peers6_t m_peers6;
#endif

		bool was_introduced_by(tcp::endpoint const& ep);
	};

	// constructor function for the ut_pex extension. The ut_pex
	// extension allows peers to gossip about their connections, allowing
	// the swarm stay well connected and peers aware of more peers in the
	// swarm. This extension is enabled by default unless explicitly disabled in
	// the session constructor.
	// 
	// This can either be passed in the add_torrent_params::extensions field, or
	// via torrent_handle::add_extension().
	TORRENT_EXPORT boost::shared_ptr<torrent_plugin> create_ut_pex_plugin(torrent_handle const&, void*);
}

#endif // TORRENT_DISABLE_EXTENSIONS

#endif // TORRENT_UT_PEX_EXTENSION_HPP_INCLUDED
