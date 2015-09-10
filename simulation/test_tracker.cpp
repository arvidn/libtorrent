/*

Copyright (c) 2010, Arvid Norberg
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
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "swarm_config.hpp"
#include "simulator/simulator.hpp"
#include "simulator/http_server.hpp"
#include "libtorrent/alert_types.hpp"

using namespace libtorrent;
using namespace sim;
namespace lt = libtorrent;

// seconds
const int duration = 10000;

struct test_swarm_config : swarm_config
{
	test_swarm_config(int num_torrents, std::vector<std::string>* announces=NULL)
		: swarm_config()
		, m_announces(announces)
		, m_num_torrents(num_torrents)
	{}

	void on_session_added(int idx, session& ses) override
	{
	}

	virtual libtorrent::add_torrent_params add_torrent(int idx) override
	{
		add_torrent_params p = swarm_config::add_torrent(idx);

		// add the tracker to the last torrent (if there's only one, it will be a
		// seed from the start, if there are more than one, it will be a torrent
		// that downloads the torrent and turns into a seed)
		if (m_num_torrents - 1 == idx)
		{
			p.trackers.push_back("http://2.2.2.2:8080/announce");
		}

		return p;
	}

	bool on_alert(libtorrent::alert const* alert
		, int session_idx
		, std::vector<libtorrent::torrent_handle> const& handles
		, libtorrent::session& ses) override
	{
		if (m_announces == NULL) return false;

		char type = 0;
		if (lt::alert_cast<lt::tracker_announce_alert>(alert))
		{
			type = 'A';
		}
		else if (lt::alert_cast<lt::tracker_error_alert>(alert))
		{
			type = 'E';
		}
		else if (lt::alert_cast<lt::tracker_warning_alert>(alert))
		{
			type = 'W';
		}
		else if (lt::alert_cast<lt::tracker_reply_alert>(alert))
		{
			type = 'R';
		}
		else
		{
			return false;
		}

		char msg[500];
		snprintf(msg, sizeof(msg), "%c: %d %s", type
			, int(duration_cast<chrono::seconds>(alert->timestamp().time_since_epoch()).count())
			, alert->message().c_str());
		m_announces->push_back(msg);

		return false;
	}

	virtual void on_exit(std::vector<torrent_handle> const& torrents) override
	{
	}

	virtual bool tick(int t) override { return t > duration; }

private:
	std::vector<std::string>* m_announces;
	int m_num_torrents;
};

void test_interval(int interval)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	lt::time_point start = lt::clock_type::now();

	sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
	// listen on port 8080
	sim::http_server http(web_server, 8080);

	// the timestamps (in seconds) of all announces
	std::vector<int> announces;

	http.register_handler("/announce"
	, [&announces,interval,start](std::string method, std::string req)
	{
		boost::uint32_t seconds = chrono::duration_cast<lt::seconds>(
			lt::clock_type::now() - start).count();
		announces.push_back(seconds);

		char response[500];
		int size = snprintf(response, sizeof(response), "d8:intervali%de5:peers0:e", interval);
		return sim::send_response(200, "OK", size) + response;
	});

	std::vector<std::string> announce_alerts;
	test_swarm_config cfg(1, &announce_alerts);
	setup_swarm(1, sim, cfg);

	int counter = 0;
	for (int i = 0; i < int(announces.size()); ++i)
	{
		TEST_EQUAL(announces[i], counter);
		counter += interval;
		if (counter > duration + 1) counter = duration + 1;
	}

	// TODO: verify that announce_alerts seem right as well
}

void test_completed()
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	lt::time_point start = lt::clock_type::now();

	sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
	// listen on port 8080
	sim::http_server http(web_server, 8080);

	// the timestamps (in seconds) of all announces
	std::vector<std::string> announces;

	const int interval = 500;

	http.register_handler("/announce"
	, [&announces,interval,start](std::string method, std::string req)
	{
		TEST_EQUAL(method, "GET");
		announces.push_back(req);

		char response[500];
		int size = snprintf(response, sizeof(response), "d8:intervali%de5:peers0:e", interval);
		return sim::send_response(200, "OK", size) + response;
	});

	test_swarm_config cfg(2);
	setup_swarm(2, sim, cfg);

	// the first announce should be event=started, the second should be
	// event=completed, then all but the last should have no event and the last
	// be event=stopped.
	for (int i = 0; i < int(announces.size()); ++i)
	{
		std::string const& str = announces[i];
		const bool has_start = str.find("&event=started")
			!= std::string::npos;
		const bool has_completed = str.find("&event=completed")
			!= std::string::npos;
		const bool has_stopped = str.find("&event=stopped")
			!= std::string::npos;

		// we there can only be one event
		const bool has_event = str.find("&event=") != std::string::npos;

		fprintf(stderr, "- %s\n", str.c_str());

		TEST_EQUAL(int(has_start) + int(has_completed) + int(has_stopped)
			, int(has_event));

		switch (i)
		{
			case 0:
				TEST_CHECK(has_start);
				break;
			case 1:
				TEST_CHECK(has_completed);
				break;
			default:
				if (i == int(announces.size()) - 1)
				{
					TEST_CHECK(has_stopped);
				}
				else
				{
					TEST_CHECK(!has_event);
				}
				break;
		}
	}
}

TORRENT_TEST(event_completed)
{
	test_completed();
}

TORRENT_TEST(announce_interval_440)
{
	test_interval(440);
}

TORRENT_TEST(announce_interval_1800)
{
	test_interval(1800);
}

TORRENT_TEST(announce_interval_1200)
{
	test_interval(3600);
}

// TODO: test with different queuing settings
// TODO: test when a torrent transitions from downloading to finished and
// finished to seeding
// TODO: test that left, downloaded and uploaded are reported correctly

// TODO: test scrape

