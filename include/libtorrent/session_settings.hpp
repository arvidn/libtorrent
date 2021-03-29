/*

Copyright (c) 2006-2007, 2010, 2013-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SESSION_SETTINGS_HPP_INCLUDED
#define TORRENT_SESSION_SETTINGS_HPP_INCLUDED

#if TORRENT_ABI_VERSION == 1

#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/kademlia/dht_settings.hpp"

#include <string>

namespace lt {

#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

	using dht_settings = dht::dht_settings;

#include "libtorrent/aux_/disable_warnings_pop.hpp"

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
