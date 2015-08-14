/*

Copyright (c) 2008-2015, Arvid Norberg
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

#include "setup_swarm.hpp"
#include "swarm_config.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/extensions/lt_trackers.hpp"
#include "test.hpp"

enum flags
{
	no_metadata = 1
};

struct test_swarm_config : swarm_config
{
	test_swarm_config(int flags)
		: swarm_config()
		, m_flags(flags)
	{}

	void on_session_added(int idx, session& ses) override
	{
		ses.add_extension(create_lt_trackers_plugin);
	}

	virtual libtorrent::add_torrent_params add_torrent(int idx) override
	{
		add_torrent_params p = swarm_config::add_torrent(idx);

		if (m_flags & no_metadata)
		{
			p.info_hash = sha1_hash("aaaaaaaaaaaaaaaaaaaa");
			p.ti.reset();
		}

		// make sure neither peer has any content
		// TODO: it would be more efficient to not create the content in the first
		// place
		p.save_path = save_path(1);

		if (idx == 1)
		{
			p.trackers.push_back("http://test.non-existent.com/announce");
		}

		return p;
	}

	bool on_alert(libtorrent::alert const* alert
		, int session_idx
		, std::vector<libtorrent::torrent_handle> const& handles
		, libtorrent::session& ses) override
	{

		if ((m_flags & no_metadata) == 0)
		{
			if (handles[0].trackers().size() == 1
				&& handles[1].trackers().size() == 1)
				return true;
		}
		return false;
	}

	virtual void on_exit(std::vector<torrent_handle> const& torrents) override
	{
		TEST_CHECK(torrents.size() > 0);

		// a peer that does not have metadata should not exchange trackers, since
		// it may be a private torrent
		if (m_flags & no_metadata)
		{
			TEST_EQUAL(torrents[0].trackers().size(), 0);
			TEST_EQUAL(torrents[1].trackers().size(), 1);
		}
		else
		{
			TEST_EQUAL(torrents[0].trackers().size(), 1);
			TEST_EQUAL(torrents[1].trackers().size(), 1);
		}
	}

private:
	int m_flags;
};

TORRENT_TEST(plain)
{
	test_swarm_config cfg(0);
	setup_swarm(2, cfg);
}

TORRENT_TEST(no_metadata)
{
	test_swarm_config cfg(no_metadata);
	setup_swarm(2, cfg);
}

