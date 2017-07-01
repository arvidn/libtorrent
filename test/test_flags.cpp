/*

Copyright (c) 2017, Arvid Norberg
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

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp" // for announce_entry
#include "libtorrent/announce_entry.hpp"
#include "settings.hpp"

using namespace libtorrent;
namespace lt = libtorrent;

void test_add_and_get_flags(boost::uint64_t flags)
{
	session ses(settings());
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.ti = boost::shared_ptr<torrent_info>(
		new torrent_info("../test_torrents/base.torrent", ec));
	TEST_CHECK(!ec);
	p.flags = flags;
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.get_flags() & flags, flags);
}

void test_set_after_add(boost::uint64_t flags)
{
	session ses(settings());
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.ti = boost::shared_ptr<torrent_info>(
		new torrent_info("../test_torrents/base.torrent", ec));
	TEST_CHECK(!ec);
	p.flags = 0xffffffffffffffff & ~flags;
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.get_flags() & flags, 0);
	h.set_flags(flags);
	TEST_EQUAL(h.get_flags() & flags, flags);
}

void test_unset_after_add(boost::uint64_t flags)
{
	session ses(settings());
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.ti = boost::shared_ptr<torrent_info>(
		new torrent_info("../test_torrents/base.torrent", ec));
	TEST_CHECK(!ec);
	p.flags = flags;
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.get_flags() & flags, flags);
	h.set_flags(flags, 0);
	TEST_EQUAL(h.get_flags() & flags, 0);
}

TORRENT_TEST(flag_seed_mode)
{
	// seed-mode (can't be set after adding)
	test_add_and_get_flags(add_torrent_params::flag_seed_mode);
	test_unset_after_add(add_torrent_params::flag_seed_mode);
}

TORRENT_TEST(flag_upload_mode)
{
	// upload-mode
	test_add_and_get_flags(add_torrent_params::flag_upload_mode);
	test_set_after_add(add_torrent_params::flag_upload_mode);
	test_unset_after_add(add_torrent_params::flag_upload_mode);
}

TORRENT_TEST(flag_share_mode)
{
	// share-mode
	test_add_and_get_flags(add_torrent_params::flag_share_mode);
	test_set_after_add(add_torrent_params::flag_share_mode);
	test_unset_after_add(add_torrent_params::flag_share_mode);
}

TORRENT_TEST(flag_apply_ip_filter)
{
	// apply-ip-filter
	test_add_and_get_flags(add_torrent_params::flag_apply_ip_filter);
	test_set_after_add(add_torrent_params::flag_apply_ip_filter);
	test_unset_after_add(add_torrent_params::flag_apply_ip_filter);
}

TORRENT_TEST(flag_paused)
{
	// paused
	test_add_and_get_flags(add_torrent_params::flag_paused);
	// TODO: change to a different test setup. currently always paused.
	//test_set_after_add(add_torrent_params::flag_paused);
	//test_unset_after_add(add_torrent_params::flag_paused);
}

TORRENT_TEST(flag_auto_managed)
{
	// auto-managed
	test_add_and_get_flags(add_torrent_params::flag_auto_managed);
	test_set_after_add(add_torrent_params::flag_auto_managed);
	test_unset_after_add(add_torrent_params::flag_auto_managed);
}

TORRENT_TEST(flag_super_seeding)
{
	// super-seeding
	test_add_and_get_flags(add_torrent_params::flag_super_seeding);
	test_set_after_add(add_torrent_params::flag_super_seeding);
	test_unset_after_add(add_torrent_params::flag_super_seeding);
}

TORRENT_TEST(flag_sequential_download)
{
	// sequential-download
	test_add_and_get_flags(add_torrent_params::flag_sequential_download);
	test_set_after_add(add_torrent_params::flag_sequential_download);
	test_unset_after_add(add_torrent_params::flag_sequential_download);
}

TORRENT_TEST(flag_pinned)
{
	// pinned
	test_add_and_get_flags(add_torrent_params::flag_pinned);
	test_set_after_add(add_torrent_params::flag_pinned);
	test_unset_after_add(add_torrent_params::flag_pinned);
}

TORRENT_TEST(flag_stop_when_ready)
{
	// stop-when-ready
	test_add_and_get_flags(add_torrent_params::flag_stop_when_ready);
	// setting stop-when-ready when already stopped has no effect.
	// TODO: change to a different test setup. currently always paused.
	//test_set_after_add(add_torrent_params::flag_stop_when_ready);
	test_unset_after_add(add_torrent_params::flag_stop_when_ready);
}
