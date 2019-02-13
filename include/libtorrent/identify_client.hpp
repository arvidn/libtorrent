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

#ifndef TORRENT_IDENTIFY_CLIENT_HPP_INCLUDED
#define TORRENT_IDENTIFY_CLIENT_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/optional.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/fingerprint.hpp"

// TODO: hide this declaration when deprecated functions are disabled, and
// remove its internal use
namespace libtorrent {

namespace aux {

	TORRENT_EXTRA_EXPORT
	std::string identify_client_impl(const peer_id& p);

}

	// these functions don't really need to be public. This mechanism of
	// advertising client software and version is also out-dated.

	// This function can can be used to extract a string describing a client
	// version from its peer-id. It will recognize most clients that have this
	// kind of identification in the peer-id.
	TORRENT_DEPRECATED_EXPORT
	std::string identify_client(const peer_id& p);

#if TORRENT_ABI_VERSION == 1

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#pragma warning(push, 1)
#pragma warning(disable: 4996)
#endif
	// Returns an optional fingerprint if any can be identified from the peer
	// id. This can be used to automate the identification of clients. It will
	// not be able to identify peers with non- standard encodings. Only Azureus
	// style, Shadow's style and Mainline style.
	TORRENT_DEPRECATED_EXPORT
	boost::optional<fingerprint>
		client_fingerprint(peer_id const& p);

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // TORRENT_ABI_VERSION

}

#endif // TORRENT_IDENTIFY_CLIENT_HPP_INCLUDED
