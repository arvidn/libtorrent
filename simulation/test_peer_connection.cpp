/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "settings.hpp"
#include "fake_peer.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"
#include "simulator/queue.hpp"

// a dummy SHA-1 hash: `fill` repeated to fill all 20 bytes
lt::sha1_hash dummy_sha1(char const fill = 'a')
{
	return lt::sha1_hash(std::string(20, fill).c_str());
}

// a dummy SHA-256 hash: `fill` repeated to fill all 32 bytes
lt::sha256_hash dummy_sha256(char const fill = 'a')
{
	return lt::sha256_hash(std::string(32, fill).c_str());
}

// Shared core for every test in this file: creates a session, adds
// `params` to it, connects a single fake_peer presenting `connect_ih` in
// its handshake, invokes peer_fun(fake_peer&) once the connection is up,
// and forwards every subsequent alert to test(alert const*). `connect_ih`
// is independent of what's in `params` so callers can exercise a
// mismatched or not-yet-resolved info-hash (e.g. a magnet add).
template <typename PeerFun, typename TestFun>
void run_fake_peer_session(lt::add_torrent_params params,
	lt::sha1_hash const& connect_ih,
	PeerFun&& peer_fun,
	TestFun&& test)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto ios = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(lt::settings_pack::alert_mask, lt::alert_category::all & ~lt::alert_category::stats);
	if (!(params.flags & lt::torrent_flags::seed_mode))
		sp.disk_io_constructor = lt::disabled_disk_io_constructor;

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	auto peer = std::make_unique<fake_peer>(sim, "60.0.0.1");

	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	ses->async_add_torrent(std::move(params));

	bool connected = false;
	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (!connected && lt::alert_cast<lt::add_torrent_alert>(a))
		{
			connected = true;
			peer->connect_to(ep("50.0.0.1", 6881), connect_ih);
			peer_fun(*peer.get());
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

// a torrent with valid metadata from the start (the common case: most
// protocol edge cases don't care how the torrent got its metadata)
template <typename PeerFun, typename TestFun>
void test_peer(lt::torrent_flags_t const flags, PeerFun&& peer_fun, TestFun&& test)
{
	lt::add_torrent_params params = (flags & lt::torrent_flags::seed_mode)
		? ::create_torrent(0, true)
		: ::create_torrent(0, false);
	int const num_pieces = params.ti->num_pieces();
	lt::sha1_hash const info_hash = params.ti->info_hash();
	params.flags |= flags;

	run_fake_peer_session(
		std::move(params), info_hash, [&](fake_peer& p) { peer_fun(p, num_pieces); }, test);
}

// a torrent added as a magnet (info-hash only, no metadata); peer_fun is
// responsible for delivering matching metadata via BEP 9 first, if the
// case needs the torrent to actually acquire it. Useful for edge cases
// around the acquire-metadata transition (torrent::set_metadata()),
// which a torrent that already has valid metadata never exercises.
template <typename PeerFun, typename TestFun>
void test_peer_magnet(lt::info_hash_t const& hashes, PeerFun&& peer_fun, TestFun&& test)
{
	lt::add_torrent_params params;
	params.info_hashes = hashes;
	params.save_path = ".";

	run_fake_peer_session(std::move(params), hashes.get_best(), peer_fun, test);
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
			bitfield[lt::aux::random(num_pieces)] = true;
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
			bitfield[lt::aux::random(num_pieces)] = true;
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

// verify that a peer_disconnected_alert is posted even when the connection
// is torn down before being associated with a torrent (i.e. before the
// handshake completes successfully). Here the peer sends an info-hash that
// doesn't match any torrent in the session.
TORRENT_TEST(disconnect_unknown_info_hash)
{
	peer_disconnects d;
	run_fake_peer_session(::create_torrent(0, false), dummy_sha1('0'), [](fake_peer&) {}, d);
	TEST_CHECK(d.alerts == vec{lt::errors::invalid_info_hash});
}

// A peer's claims made during the BT handshake, in particular the
// v2-upgrade bit (reserved byte 7, bit 0x10), don't necessarily match what
// the torrent's metadata turns out to describe. A magnet link's info-hash
// alone doesn't establish whether the metadata will describe a hybrid
// (v1 + v2) torrent, so such a claim is recorded speculatively (see
// bt_peer_connection.cpp's handshake handling and torrent::set_metadata()
// in src/torrent.cpp) and must be corrected once metadata resolves the
// question, or v2 hash-exchange code (hash_picker,
// torrent::get_hashes()/add_hashes()) can be reached for a torrent with no
// per-file merkle trees at all.

struct built_torrent
{
	lt::info_hash_t hashes;
	std::vector<char> metadata; // raw bencoded info-dict, for set_metadata()
};

// a single-file, single-piece torrent, either v1-only or genuinely hybrid
built_torrent build_torrent(bool const hybrid)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test-torrent/file.txt", 16 * 1024);
	lt::create_torrent t(std::move(fs), 16 * 1024);

	for (lt::piece_index_t i : t.piece_range())
		t.set_hash(i, dummy_sha1());

	if (hybrid)
	{
		for (lt::file_index_t const f : t.file_range())
			for (lt::piece_index_t::diff_type i : t.file_piece_range(f))
				t.set_hash2(f, i, dummy_sha256());
	}

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), t.generate());
	lt::add_torrent_params const atp = lt::load_torrent_buffer(buf);

	built_torrent ret;
	ret.hashes = atp.ti->info_hashes();
	auto const info = atp.ti->info_section();
	ret.metadata.assign(info.begin(), info.end());
	return ret;
}

// BEP 78 hash_request / hashes / hash_reject payload: a 32-byte file root
// followed by 4 big-endian int32 fields (base, index, count,
// proof_layers). The content doesn't matter for these cases, since the
// checks under test run before any of it is parsed.
std::vector<char> hash_msg_payload() { return std::vector<char>(32 + 4 * 4, '\0'); }

// reserved bytes advertising BEP 10 (extended protocol) plus the
// v2-upgrade bit (byte 7, bit 0x10)
std::array<char, 8> const v2_claim_reserved{0, 0, 0, 0, 0, 0x10, 0, 0x10};
// reserved bytes advertising only BEP 10 (extended protocol)
std::array<char, 8> const no_v2_claim_reserved{0, 0, 0, 0, 0, 0x10, 0, 0};

// runs one edge case: what torrent the peer connects to, what it claims and
// sends, and what libtorrent is expected to do about it. New cases can be
// added here without adding new plumbing.
void run_metadata_edge_case(bool const hybrid_torrent,
	std::array<char, 8> const& reserved,
	int const follow_up_msg // -1 = none, else a raw BT message type (e.g. 21/22/23)
	,
	bool const expect_metadata,
	bool const expect_peer_error)
{
	built_torrent const bt = build_torrent(hybrid_torrent);
	// did the torrent acquire metadata, and was that accompanied by a peer
	// error (e.g. the peer got disconnected once metadata resolved a claim
	// it made in the handshake to be false)?
	bool metadata_received = false;
	bool peer_error = false;
	test_peer_magnet(
		bt.hashes,
		[&](fake_peer& p) {
			p.set_reserved_bits(reserved);

			// d1:md11:ut_metadatai2ee13:metadata_sizei<N>ee
			std::string const extended_handshake = "d1:md11:ut_metadatai2ee13:metadata_sizei"
				+ std::to_string(bt.metadata.size()) + "ee";
			p.send_extended(0, extended_handshake);

			// d8:msg_typei1e5:piecei0e10:total_sizei<N>ee<metadata>
			std::string metadata_piece = "d8:msg_typei1e5:piecei0e10:total_sizei"
				+ std::to_string(bt.metadata.size()) + "ee";
			metadata_piece.insert(metadata_piece.end(), bt.metadata.begin(), bt.metadata.end());
			p.send_extended(2, metadata_piece);

			if (follow_up_msg >= 0)
				p.send_message(std::uint8_t(follow_up_msg), hash_msg_payload());
		},
		[&](lt::alert const* a) {
			if (lt::alert_cast<lt::metadata_received_alert>(a))
				metadata_received = true;
			if (lt::alert_cast<lt::peer_error_alert>(a))
				peer_error = true;
		});

	TEST_EQUAL(metadata_received, expect_metadata);
	TEST_EQUAL(peer_error, expect_peer_error);
}

// v1-only torrent, peer falsely claims v2 support: the claim is recorded
// speculatively (see above) and must not survive metadata resolving to
// v1-only.

// maybe_send_hash_request() constructs a hash_picker for this file once
// metadata arrives; must not crash despite no known v2 hashes for a
// v1-only torrent, no crafted hash-exchange message required
TORRENT_TEST(v1_only_false_v2_claim_survives_metadata)
{
	run_metadata_edge_case(false, v2_claim_reserved, -1, true, false);
}

TORRENT_TEST(v1_only_false_v2_claim_rejects_hash_request)
{
	run_metadata_edge_case(false, v2_claim_reserved, 21, true, true);
}

TORRENT_TEST(v1_only_false_v2_claim_rejects_unsolicited_hashes)
{
	run_metadata_edge_case(false, v2_claim_reserved, 22, true, true);
}

TORRENT_TEST(v1_only_false_v2_claim_rejects_hash_reject)
{
	run_metadata_edge_case(false, v2_claim_reserved, 23, true, true);
}

// v1-only torrent, peer makes no v2 claim: hash messages are rejected for
// any peer that never claimed v2 support.

TORRENT_TEST(v1_only_honest_claim_survives_metadata)
{
	run_metadata_edge_case(false, no_v2_claim_reserved, -1, true, false);
}

TORRENT_TEST(v1_only_honest_claim_rejects_hash_request)
{
	run_metadata_edge_case(false, no_v2_claim_reserved, 21, true, true);
}

// hybrid torrent, peer honestly claims v2 support: a legitimate v2 peer on
// a torrent that actually has v2 hashes must not be penalized.

TORRENT_TEST(hybrid_honest_v2_claim_survives_metadata)
{
	run_metadata_edge_case(true, v2_claim_reserved, -1, true, false);
}

TORRENT_TEST(hybrid_honest_v2_claim_hash_request_not_rejected)
{
	// an unrecognized file-root gets a hash_reject reply, not a disconnect
	run_metadata_edge_case(true, v2_claim_reserved, 21, true, false);
}
