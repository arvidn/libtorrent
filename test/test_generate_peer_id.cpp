/*

Copyright (c) 2018-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/generate_peer_id.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/settings_pack.hpp"

TORRENT_TEST(generate_peer_id)
{
	lt::aux::session_settings sett;
	sett.set_str(lt::settings_pack::peer_fingerprint, "abc");
	lt::peer_id const id = lt::aux::generate_peer_id(sett);

	TEST_CHECK(std::equal(id.begin(), id.begin() + 3, "abc"));
	TEST_CHECK(!lt::need_encoding(id.data(), int(id.size())));
}

TORRENT_TEST(generate_peer_id_truncate)
{
	lt::aux::session_settings sett;
	sett.set_str(lt::settings_pack::peer_fingerprint, "abcdefghijklmnopqrstuvwxyz");
	lt::peer_id const id = lt::aux::generate_peer_id(sett);

	TEST_CHECK(std::equal(id.begin(), id.end(), "abcdefghijklmnopqrst"));
}
