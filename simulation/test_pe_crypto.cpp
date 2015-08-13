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

#include "libtorrent/hasher.hpp"
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/random.hpp"

#include "setup_transfer.hpp"
#include "swarm_config.hpp"
#include "test.hpp"

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

using namespace libtorrent;
namespace lt = libtorrent;

char const* pe_policy(boost::uint8_t policy)
{
	using namespace libtorrent;

	if (policy == settings_pack::pe_disabled) return "disabled";
	else if (policy == settings_pack::pe_enabled) return "enabled";
	else if (policy == settings_pack::pe_forced) return "forced";
	return "unknown";
}

void display_settings(libtorrent::settings_pack const& s)
{
	using namespace libtorrent;

	fprintf(stderr, "out_enc_policy - %s\tin_enc_policy - %s\n"
		, pe_policy(s.get_int(settings_pack::out_enc_policy))
		, pe_policy(s.get_int(settings_pack::in_enc_policy)));

	fprintf(stderr, "enc_level - %s\t\tprefer_rc4 - %s\n"
		, s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_plaintext ? "plaintext"
		: s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_rc4 ? "rc4"
		: s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_both ? "both" : "unknown"
		, s.get_bool(settings_pack::prefer_rc4) ? "true": "false");
}

struct test_swarm_config : swarm_config
{
	test_swarm_config(libtorrent::settings_pack::enc_policy policy
		, libtorrent::settings_pack::enc_level level
		, bool prefer_rc4)
		: swarm_config()
		, m_policy(policy)
		, m_level(level)
		, m_prefer_rc4(prefer_rc4)
	{}

	// called for every session that's added
	virtual libtorrent::settings_pack add_session(int idx) override
	{
		settings_pack s = swarm_config::add_session(idx);

		fprintf(stderr, " session %d\n", idx);

		s.set_int(settings_pack::out_enc_policy, settings_pack::pe_enabled);
		s.set_int(settings_pack::in_enc_policy, settings_pack::pe_enabled);
		s.set_int(settings_pack::allowed_enc_level, settings_pack::pe_both);

		if (idx == 1)
		{
			s.set_int(settings_pack::out_enc_policy, m_policy);
			s.set_int(settings_pack::in_enc_policy, m_policy);
			s.set_int(settings_pack::allowed_enc_level, m_level);
			s.set_bool(settings_pack::prefer_rc4, m_prefer_rc4);
		}

		display_settings(s);

		return s;
	}

private:
	libtorrent::settings_pack::enc_policy m_policy;
	libtorrent::settings_pack::enc_level m_level;
	bool m_prefer_rc4;
};

TORRENT_TEST(pe_disabled)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_disabled, settings_pack::pe_both, false);
	setup_swarm(2, cfg);
}

TORRENT_TEST(forced_plaintext)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_forced, settings_pack::pe_both, false);
	setup_swarm(2, cfg);
}

TORRENT_TEST(forced_rc4)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_forced, settings_pack::pe_rc4, false);
	setup_swarm(2, cfg);
}

TORRENT_TEST(forced_both)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_forced, settings_pack::pe_both, false);
	setup_swarm(2, cfg);
}

TORRENT_TEST(forced_both_prefer_rc4)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_forced, settings_pack::pe_both, true);
	setup_swarm(2, cfg);
}

TORRENT_TEST(enabled_plaintext)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_enabled, settings_pack::pe_plaintext, false);
	setup_swarm(2, cfg);
}

TORRENT_TEST(enabled_rc4)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_enabled, settings_pack::pe_rc4, false);
	setup_swarm(2, cfg);
}

TORRENT_TEST(enabled_both)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_enabled, settings_pack::pe_both, false);
	setup_swarm(2, cfg);
}

TORRENT_TEST(enabled_both_prefer_rc4)
{
	using namespace libtorrent;
	test_swarm_config cfg(settings_pack::pe_enabled, settings_pack::pe_both, true);
	setup_swarm(2, cfg);
}
#else
TORRENT_TEST(disabled)
{
	fprintf(stderr, "PE test not run because it's disabled\n");
}
#endif

