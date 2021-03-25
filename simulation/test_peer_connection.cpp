/*

Copyright (c) 2021, Arvid Norberg
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

#include <functional>

#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "settings.hpp"
#include "fake_peer.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"
#include "simulator/queue.hpp"

template <typename PeerFun, typename TestFun>
void test_peer(lt::torrent_flags_t const flags
	, PeerFun&& peer_fun
	, TestFun&& test)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto ios = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(lt::settings_pack::alert_mask, lt::alert_category::all & ~lt::alert_category::stats);
	if (!(flags & lt::torrent_flags::seed_mode))
		sp.disk_io_constructor = lt::disabled_disk_io_constructor;

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	auto peer = std::make_unique<fake_peer>(sim, "60.0.0.1");

	// add torrent
	lt::add_torrent_params params
		= (flags & lt::torrent_flags::seed_mode)
		? ::create_torrent(0, true) : ::create_torrent(0, false);
	int const num_pieces = params.ti->num_pieces();
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.flags |= flags;
	lt::sha1_hash const info_hash = params.ti->info_hash();
	ses->async_add_torrent(std::move(params));

	lt::torrent_handle h;
	bool connected = false;
	print_alerts(*ses, [&](lt::session& ses, lt::alert const* a) {
		if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			h = at->handle;

			TORRENT_ASSERT(!connected);
			peer->connect_to(ep("50.0.0.1", 6881), info_hash);
			peer_fun(*peer.get(), num_pieces);
			connected = true;
		}
		if (connected)
			test(a);
	});

	// set up a timer to fire later, to shut down
	sim::timer t2(sim, lt::seconds(700)
		, [&](boost::system::error_code const&)
	{
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

struct peer_errors
{
	void operator()(lt::alert const* a)
	{
		auto* pe = lt::alert_cast<lt::peer_error_alert>(a);
		if (!pe) return;
		alerts.push_back(pe->error);
	}

	std::vector<lt::error_code> alerts;
};

struct peer_disconnects
{
	void operator()(lt::alert const* a)
	{
		// when we're expecting an orderly disconnect, make sure we don't also
		// get a peer-error.
		TEST_CHECK(lt::alert_cast<lt::peer_error_alert>(a) == nullptr);

		auto* pd = lt::alert_cast<lt::peer_disconnected_alert>(a);
		if (!pd) return;
		alerts.push_back(pd->error);
	}

	std::vector<lt::error_code> alerts;
};

struct invalid_requests
{
	void operator()(lt::alert const* a)
	{
		// we don't expect a peer error
		TEST_CHECK(lt::alert_cast<lt::peer_error_alert>(a) == nullptr);

		auto* ir = lt::alert_cast<lt::invalid_request_alert>(a);
		if (!ir) return;
		alerts.push_back(ir->request);
	}

	std::vector<lt::peer_request> alerts;
};

using vec = std::vector<lt::error_code>;
using reqs = std::vector<lt::peer_request>;

TORRENT_TEST(alternate_have_all_have_none)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int)
		{
			p.send_have_all();
			p.send_have_none();
			p.send_have_all();
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(alternate_have_all_have_none_seed)
{
	peer_disconnects d;
	test_peer(lt::torrent_flags::seed_mode, [](fake_peer& p, int)
		{
			p.send_have_all();
			p.send_have_none();
			p.send_have_all();
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::upload_upload_connection});
}

TORRENT_TEST(bitfield_and_have_none)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, false);
			bitfield[lt::random(num_pieces)] = true;
			p.send_bitfield(bitfield);
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(bitfield_and_have_all)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, false);
			bitfield[lt::random(num_pieces)] = true;
			p.send_bitfield(bitfield);
			p.send_have_all();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(full_bitfield_and_have_all)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, true);
			p.send_bitfield(bitfield);
			p.send_have_all();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(full_bitfield_and_have_none)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, true);
			p.send_bitfield(bitfield);
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(invalid_request)
{
	invalid_requests e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_interested();
			p.send_request(1_piece, 0);
		}
		, e);
	TEST_CHECK((e.alerts == reqs{lt::peer_request{1_piece, 0, lt::default_block_size}}));
}

TORRENT_TEST(large_message)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_large_message();
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::packet_too_large});
}

TORRENT_TEST(have_all_invalid_msg)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_have_all();
			p.send_invalid_message();
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_message});
}

TORRENT_TEST(invalid_message)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_invalid_message();
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_message});
}

TORRENT_TEST(short_bitfield)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces - 1, true);
			p.send_bitfield(bitfield);
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_bitfield_size});
}

TORRENT_TEST(long_bitfield)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces + 9, true);
			p.send_bitfield(bitfield);
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_bitfield_size});
}
