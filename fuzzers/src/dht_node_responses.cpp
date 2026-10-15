/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/span.hpp"
#include "dht_reply_session.hpp"

using namespace lt;

namespace {

// fixed at compile time rather than derived from fuzz input, so a crafted
// candidate node id can coincide with it by construction (either via a
// seeded corpus entry or a mutation that copies one input region into
// another), instead of needing a 160-bit preimage
dht::node_id const g_target = dht_fuzz::fake_id(0x42);

// endpoints our own node has sent an outgoing query to, and that query's
// 2-byte transaction id, taken from the real bytes about to go over the
// wire (send_fun below) rather than guessed, since rpc_manager only
// accepts a reply whose tid *and* source endpoint both match an
// outstanding request
struct pending_query
{
	udp::endpoint ep;
	std::array<char, 2> tid;
};
std::vector<pending_query> g_pending;

dht_fuzz::null_dht_observer o;
aux::session_settings sett;
dht::dht_state state;
std::unique_ptr<dht::dht_storage_interface> dht_storage(dht::dht_default_storage_constructor(sett));
counters cnt;
io_context ios;

// held by shared_ptr (rather than as a plain object) because
// dht_tracker::start() calls self() (enable_shared_from_this), which
// throws std::bad_weak_ptr unless the object is already shared_ptr-owned
auto dht_node = std::make_shared<dht::dht_tracker>(
	&o,
	ios,
	[](aux::listen_socket_handle const&,
		udp::endpoint const& ep,
		span<char const> buf,
		error_code&,
		aux::udp_send_flags_t) {
		bdecode_node msg;
		error_code ec;
		if (bdecode(buf.data(), buf.data() + buf.size(), msg, ec) != 0)
			return;
		if (msg.type() != bdecode_node::dict_t)
			return;
		if (msg.dict_find_string_value("y") != "q")
			return;
		string_view const tid = msg.dict_find_string_value("t");
		if (tid.size() != 2)
			return;
		if (g_pending.size() >= 32)
			g_pending.erase(g_pending.begin());
		pending_query pq;
		pq.ep = ep;
		std::memcpy(pq.tid.data(), tid.data(), 2);
		g_pending.push_back(pq);
	},
	sett,
	cnt,
	*dht_storage,
	std::move(state));

auto listen_socket = std::make_shared<aux::listen_socket_t>();
aux::listen_socket_handle s(listen_socket);
std::once_flag once_flag;

// a fake peer address, distinct per raw incoming packet or add_node call,
// the same way the original dht_node fuzzer varied its sender
error_code g_addr_ec;
address_v4 g_next_addr = make_address_v4("2.2.2.2", g_addr_ec);

udp::endpoint next_fake_endpoint()
{
	udp::endpoint const ep(g_next_addr, 6881);
	g_next_addr = address_v4(aux::plus_one(g_next_addr.to_bytes()));
	return ep;
}

// a minimal "r" reply (just "id") to the given pending query. delivering
// this is what promotes a peer from heard-about to pinged in the routing
// table (rpc_manager::incoming() calls routing_table::node_seen() for any
// matched reply), which is required before find_node() will ever return
// it as a traversal candidate. heard_about() alone (from an incoming
// *query*, or from a response's "nodes" field) only reaches the
// replacement cache, never the live bucket
std::string build_ping_reply(pending_query const& pq, span<std::uint8_t const> data)
{
	std::array<char, 20> const id = dht_fuzz::take_padded<20>(data);

	entry e;
	e["y"] = "r";
	e["t"] = std::string(pq.tid.data(), pq.tid.size());
	e["r"]["id"] = std::string(id.data(), id.size());

	std::string msg;
	bencode(std::back_inserter(msg), e);
	return msg;
}

// a "r" reply with a single compact "nodes" entry taken from fuzz input,
// to the given pending query. any response's "nodes" field feeds
// routing_table::heard_about() for each listed candidate, which is how a
// candidate's id gets to be fuzz-controlled, including, by construction
// (a seeded corpus entry, or a mutation copying one input region into
// another) rather than a 160-bit preimage search, equal to g_target, which
// is what's needed to reach generate_prefix_mask()'s shared_prefix == 160
// case the next time this candidate is queried
std::string build_node_response(pending_query const& pq, span<std::uint8_t const> data)
{
	std::array<char, 20> const node_id_bytes = dht_fuzz::take_padded<20>(data);
	std::array<char, 4> const ipv4 = dht_fuzz::take_padded<4>(data);
	std::array<char, 2> const port = dht_fuzz::take_padded<2>(data);

	std::string nodes;
	nodes.append(node_id_bytes.data(), node_id_bytes.size());
	nodes.append(ipv4.data(), ipv4.size());
	nodes.append(port.data(), port.size());

	entry e;
	e["y"] = "r";
	e["t"] = std::string(pq.tid.data(), pq.tid.size());
	entry& r = e["r"];
	r["id"] = std::string(node_id_bytes.data(), node_id_bytes.size());
	r["nodes"] = nodes;

	std::string msg;
	bencode(std::back_inserter(msg), e);
	return msg;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	std::call_once(once_flag, [] {
		// without this, node::get_peers() builds a plain (unobfuscated)
		// get_peers traversal, whose invoke() never calls
		// generate_prefix_mask() at all, only obfuscated_get_peers does
		sett.set_bool(settings_pack::dht_privacy_lookups, true);
		dht_node->new_socket(s);
		// arms the per-node connection_timer (dht_tracker::connection_timeout,
		// private, so unreachable directly) and sets m_running, both needed
		// for rpc_manager::tick() to ever run via that timer; with an empty
		// dht_state, bootstrap() has no seed nodes to query and is a no-op
		dht_node->start([](std::vector<std::pair<dht::node_entry, std::string>> const&) {});
	});

	span<std::uint8_t const> input(data, static_cast<std::ptrdiff_t>(size));
	if (input.empty())
		return 0;
	std::uint8_t const opcode = input[0] % 5;
	input = input.subspan(1);

	switch (opcode)
	{
		case 0:
			// raw incoming packet from a fake, ever-advancing peer address,
			// exercising the request-parsing / incoming-request side
			dht_node->incoming_packet(s,
				next_fake_endpoint(),
				span<char const>(reinterpret_cast<char const*>(input.data()), input.size()));
			break;
		case 1:
			dht_node->get_peers(g_target, [](std::vector<tcp::endpoint> const&) {});
			break;
		case 2:
			dht_node->announce(g_target, 6881, {}, [](std::vector<tcp::endpoint> const&) {});
			break;
		case 3:
			// pings a fresh fake address, so a later case 4 has a pending
			// query to answer (see build_ping_reply/build_node_response)
			dht_node->add_node(next_fake_endpoint());
			break;
		case 4: {
			if (g_pending.empty() || input.empty())
				return 0;
			bool const as_ping_reply = (input[0] & 1) != 0;
			pending_query const pq = g_pending[input[0] % g_pending.size()];
			input = input.subspan(1);
			std::string const msg =
				as_ping_reply ? build_ping_reply(pq, input) : build_node_response(pq, input);
			dht_node->incoming_packet(
				s, pq.ep, span<char const>(msg.data(), static_cast<std::ptrdiff_t>(msg.size())));
			break;
		}
	}

	// runs any timer callback whose deadline has already passed, in
	// particular dht_tracker::connection_timeout(), which expires queries
	// that never got a matching reply; without this, every query sent
	// above that outlives its reply leaks into rpc_manager's transaction
	// table forever, since nothing else ever drives this io_context
	ios.poll();
	return 0;
}
