/*

Copyright (c) 2008, Arvid Norberg
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
#include "test_utils.hpp"
#include "swarm_config.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/extensions/metadata_transfer.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"

#include <boost/tuple/tuple.hpp>

using namespace libtorrent;

enum flags_t
{
	clear_files = 1,

	// disconnect immediately after receiving the metadata (to test that
	// edge case, it caused a crash once)
	disconnect = 2,

	// force encryption (to make sure the plugin uses the peer_connection
	// API in a compatible way)
	full_encryption = 4,

	// have the downloader connect to the seeder
	// (instead of the other way around)
	reverse = 8,

	// only use uTP
	utp = 16,

	// upload-only mode
	upload_only = 32
};

struct test_swarm_config : swarm_config
{
	test_swarm_config(int flags
		, boost::shared_ptr<torrent_plugin> (*plugin)(torrent_handle const&, void*))
		: swarm_config()
		, m_flags(flags)
		, m_plugin(plugin)
		, m_metadata_alerts(0)
	{}

	// called for every session that's added
	virtual libtorrent::settings_pack add_session(int idx) override
	{
		settings_pack s = swarm_config::add_session(idx);

		fprintf(stderr, " session %d\n", idx);

		fprintf(stderr, "\n==== test transfer: %s%s%s%s%s%s ====\n\n"
			, (m_flags & clear_files) ? "clear-files " : ""
			, (m_flags & disconnect) ? "disconnect " : ""
			, (m_flags & full_encryption) ? "encryption " : ""
			, (m_flags & reverse) ? "reverse " : ""
			, (m_flags & utp) ? "utp " : ""
			, (m_flags & upload_only) ? "upload_only " : "");

		s.set_int(settings_pack::out_enc_policy, settings_pack::pe_forced);
		s.set_int(settings_pack::in_enc_policy, settings_pack::pe_forced);
		s.set_bool(settings_pack::prefer_rc4, m_flags & full_encryption);

		if (m_flags & utp)
		{
			s.set_bool(settings_pack::enable_incoming_utp, true);
			s.set_bool(settings_pack::enable_outgoing_utp, true);
			s.set_bool(settings_pack::enable_incoming_tcp, false);
			s.set_bool(settings_pack::enable_outgoing_tcp, false);
		}
		else
		{
			s.set_bool(settings_pack::enable_incoming_utp, false);
			s.set_bool(settings_pack::enable_outgoing_utp, false);
			s.set_bool(settings_pack::enable_incoming_tcp, true);
			s.set_bool(settings_pack::enable_outgoing_tcp, true);
		}

		return s;
	}

	virtual libtorrent::add_torrent_params add_torrent(int idx) override
	{
		add_torrent_params p = swarm_config::add_torrent(idx);

		if (m_flags & reverse)
		{
			p.save_path = save_path(1 - idx);
		}

		if (idx == 1)
		{
			// this is the guy who should download the metadata.
			p.info_hash = p.ti->info_hash();
			p.ti.reset();
		}

		return p;
	}

	void on_session_added(int idx, session& ses) override
	{
		ses.add_extension(m_plugin);
	}

	bool on_alert(libtorrent::alert const* alert
		, int session_idx
		, std::vector<libtorrent::torrent_handle> const& handles
		, libtorrent::session& ses) override
	{
		if (alert_cast<metadata_received_alert>(alert))
		{
			m_metadata_alerts += 1;
		}

		// make sure this function can be called on
		// torrents without metadata
		if ((m_flags & disconnect) == 0)
		{
			handles[session_idx].status();
		}

		if ((m_flags & disconnect)
			&& session_idx == 1
			&& alert_cast<metadata_received_alert>(alert))
		{
			ses.remove_torrent(handles[session_idx]);
			return true;
		}
		return false;
	}

	virtual void on_torrent_added(int idx, torrent_handle h) override
	{
		if (idx == 0) return;

		if (m_flags & upload_only)
		{
			h.set_upload_mode(true);
		}
	}

	virtual void on_exit(std::vector<torrent_handle> const& torrents) override
	{
		TEST_EQUAL(m_metadata_alerts, 1);
		// in this case we should have completed without downloading anything
		// because the downloader had upload only set
		if (m_flags & upload_only) return;

		swarm_config::on_exit(torrents);
	}

private:
	int m_flags;
	boost::shared_ptr<torrent_plugin> (*m_plugin)(torrent_handle const&, void*);
	int m_metadata_alerts;
};

TORRENT_TEST(ut_metadata_encryption_reverse)
{
	test_swarm_config cfg(full_encryption | reverse, &create_ut_metadata_plugin);
	setup_swarm(2, cfg);
}

TORRENT_TEST(ut_metadata_encryption_utp)
{
	test_swarm_config cfg(full_encryption | utp, &create_ut_metadata_plugin);
	setup_swarm(2, cfg);
}

TORRENT_TEST(ut_metadata_reverse)
{
	test_swarm_config cfg(reverse, &create_ut_metadata_plugin);
	setup_swarm(2, cfg);
}

TORRENT_TEST(ut_metadata_upload_only)
{
	test_swarm_config cfg(upload_only, &create_ut_metadata_plugin);
	setup_swarm(2, cfg);
}

#ifndef TORRENT_NO_DEPRECATE

// TODO: add more flags combinations?

TORRENT_TEST(metadata_encryption_reverse)
{
	test_swarm_config cfg(full_encryption | reverse, &create_metadata_plugin);
	setup_swarm(2, cfg);
}

TORRENT_TEST(metadata_encryption_utp)
{
	test_swarm_config cfg(full_encryption | utp, &create_metadata_plugin);
	setup_swarm(2, cfg);
}

TORRENT_TEST(metadata_reverse)
{
	test_swarm_config cfg(reverse, &create_metadata_plugin);
	setup_swarm(2, cfg);
}

TORRENT_TEST(metadata_upload_only)
{
	test_swarm_config cfg(upload_only, &create_metadata_plugin);
	setup_swarm(2, cfg);
}

#endif

