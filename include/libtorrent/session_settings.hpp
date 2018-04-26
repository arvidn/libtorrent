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

#ifndef TORRENT_SESSION_SETTINGS_HPP_INCLUDED
#define TORRENT_SESSION_SETTINGS_HPP_INCLUDED

#if TORRENT_ABI_VERSION == 1

#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/kademlia/dht_settings.hpp"

#include <string>

namespace libtorrent {

	using dht_settings = dht::dht_settings;

	using aux::proxy_settings;

	// The ``pe_settings`` structure is used to control the settings related
	// to peer protocol encryption.
	struct TORRENT_DEPRECATED_EXPORT pe_settings
	{
		// initializes the encryption settings with the default values
		pe_settings()
			: out_enc_policy(enabled)
			, in_enc_policy(enabled)
			, allowed_enc_level(both)
			, prefer_rc4(false)
		{}

		// the encoding policy options for use with pe_settings::out_enc_policy
		// and pe_settings::in_enc_policy.
		enum enc_policy
		{
			// Only encrypted connections are allowed. Incoming connections that
			// are not encrypted are closed and if the encrypted outgoing
			// connection fails, a non-encrypted retry will not be made.
			forced,

			// encrypted connections are enabled, but non-encrypted connections
			// are allowed. An incoming non-encrypted connection will be accepted,
			// and if an outgoing encrypted connection fails, a non- encrypted
			// connection will be tried.
			enabled,

			// only non-encrypted connections are allowed.
			disabled
		};

		// the encryption levels, to be used with pe_settings::allowed_enc_level.
		enum enc_level
		{
			// use only plaintext encryption
			plaintext = 1,
			// use only rc4 encryption
			rc4 = 2,
			// allow both
			both = 3
		};

		// control the settings for incoming
		// and outgoing connections respectively.
		// see enc_policy enum for the available options.
		std::uint8_t out_enc_policy;
		std::uint8_t in_enc_policy;

		// determines the encryption level of the
		// connections.  This setting will adjust which encryption scheme is
		// offered to the other peer, as well as which encryption scheme is
		// selected by the client. See enc_level enum for options.
		std::uint8_t allowed_enc_level;

		// if the allowed encryption level is both, setting this to
		// true will prefer rc4 if both methods are offered, plaintext
		// otherwise
		bool prefer_rc4;
	};
}

#endif // TORRENT_ABI_VERSION
#endif
