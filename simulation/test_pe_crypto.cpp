/*

Copyright (c) 2007, Un Shyam
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

#include <algorithm>
#include <iostream>

#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/session.hpp"

#include "setup_transfer.hpp"
#include "test.hpp"
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"

#if !defined TORRENT_DISABLE_ENCRYPTION

using namespace lt;

char const* pe_policy(int const policy)
{
	if (policy == settings_pack::pe_disabled) return "disabled";
	else if (policy == settings_pack::pe_enabled) return "enabled";
	else if (policy == settings_pack::pe_forced) return "forced";
	return "unknown";
}

void display_pe_settings(lt::settings_pack const& s)
{
	std::printf("out_enc_policy - %s\tin_enc_policy - %s\n"
		, pe_policy(s.get_int(settings_pack::out_enc_policy))
		, pe_policy(s.get_int(settings_pack::in_enc_policy)));

	std::printf("enc_level - %s\t\tprefer_rc4 - %s\n"
		, s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_plaintext ? "plaintext"
		: s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_rc4 ? "rc4"
		: s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_both ? "both" : "unknown"
		, s.get_bool(settings_pack::prefer_rc4) ? "true": "false");
}

void test_transfer(int enc_policy, int level, bool prefer_rc4)
{
	lt::settings_pack default_settings = settings();
	default_settings.set_bool(settings_pack::prefer_rc4, prefer_rc4);
	default_settings.set_int(settings_pack::in_enc_policy, enc_policy);
	default_settings.set_int(settings_pack::out_enc_policy, enc_policy);
	default_settings.set_int(settings_pack::allowed_enc_level, level);
	display_pe_settings(default_settings);

	sim::default_config cfg;
	sim::simulation sim{cfg};

	lt::add_torrent_params default_add_torrent;
	default_add_torrent.flags &= ~lt::torrent_flags::paused;
	default_add_torrent.flags &= ~lt::torrent_flags::auto_managed;
	setup_swarm(2, swarm_test::download, sim, default_settings, default_add_torrent
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_enabled);
			pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_enabled);
			pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_both);
			pack.set_bool(settings_pack::prefer_rc4, false);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 20)
			{
				TEST_ERROR("timeout");
				return true;
			}
			return is_seed(ses);
		});
}

TORRENT_TEST(pe_disabled)
{
	test_transfer(settings_pack::pe_disabled, settings_pack::pe_plaintext, false);
}

TORRENT_TEST(forced_plaintext)
{
	test_transfer(settings_pack::pe_forced, settings_pack::pe_plaintext, false);
}

TORRENT_TEST(forced_rc4)
{
	test_transfer(settings_pack::pe_forced, settings_pack::pe_rc4, true);
}

TORRENT_TEST(forced_both)
{
	test_transfer(settings_pack::pe_forced, settings_pack::pe_both, false);
}

TORRENT_TEST(forced_both_prefer_rc4)
{
	test_transfer(settings_pack::pe_forced, settings_pack::pe_both, true);
}

TORRENT_TEST(enabled_plaintext)
{
	test_transfer(settings_pack::pe_forced, settings_pack::pe_plaintext, false);
}

TORRENT_TEST(enabled_rc4)
{
	test_transfer(settings_pack::pe_enabled, settings_pack::pe_rc4, false);
}

TORRENT_TEST(enabled_both)
{
	test_transfer(settings_pack::pe_enabled, settings_pack::pe_both, false);
}

TORRENT_TEST(enabled_both_prefer_rc4)
{
	test_transfer(settings_pack::pe_enabled, settings_pack::pe_both, true);
}

// make sure that a peer with encryption disabled cannot talk to a peer with
// encryption forced
TORRENT_TEST(disabled_failing)
{
	lt::settings_pack default_settings = settings();
	default_settings.set_bool(settings_pack::prefer_rc4, false);
	default_settings.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	default_settings.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	default_settings.set_int(settings_pack::allowed_enc_level, settings_pack::pe_both);
	display_pe_settings(default_settings);

	sim::default_config cfg;
	sim::simulation sim{cfg};

	lt::add_torrent_params default_add_torrent;
	default_add_torrent.flags &= ~lt::torrent_flags::paused;
	default_add_torrent.flags &= ~lt::torrent_flags::auto_managed;
	setup_swarm(2, swarm_test::download, sim, default_settings, default_add_torrent
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_forced);
			pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_forced);
			pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_both);
			pack.set_bool(settings_pack::prefer_rc4, true);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			// this download should never succeed
			TEST_CHECK(!is_seed(ses));
			return ticks > 120;
		});
}
#else
TORRENT_TEST(disabled)
{
	std::printf("PE test not run because it's disabled\n");
}
#endif

