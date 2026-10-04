/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/kademlia/get_peers.hpp"
#include "libtorrent/span.hpp"
#include "dht_reply_session.hpp"

using namespace lt;
using namespace dht_fuzz;

namespace {
fixture fx;
}

// fuzzes get_peers_observer::reply() and obfuscated_get_peers_observer::reply(),
// via a real get_peers/obfuscated_get_peers traversal seeded with one
// candidate through the public add_entry() (skipping the routing table
// entirely) and started for real, so its observer is properly registered
// in the traversal's own bookkeeping. the reply (the "r" dict) is
// fuzz-controlled; this exercises reply parsing ("values"/"nodes"/
// "token") in isolation from query construction, which needs the full
// dht_tracker, see dht_node_responses.cpp, which is also where an
// earlier bug in obfuscated_get_peers::invoke() actually lived
extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	span<std::uint8_t const> input(data, static_cast<std::ptrdiff_t>(size));
	auto const obfuscated = take_flag(input);
	if (!obfuscated)
		return 0;

	dht::node_id const target = fake_id(0x42);
	auto const dcb = [](std::vector<tcp::endpoint> const&) {};
	auto const ncb = [](std::vector<std::pair<dht::node_entry, std::string>> const&) {};

	std::shared_ptr<dht::get_peers> ta;
	if (*obfuscated)
		ta = std::make_shared<dht::obfuscated_get_peers>(fx.n, target, dcb, ncb, false);
	else
		ta = std::make_shared<dht::get_peers>(fx.n, target, dcb, ncb, false);

	// the candidate we query; deliberately not target, which would
	// trigger generate_prefix_mask()'s shared_prefix == 160 case inside
	// obfuscated_get_peers::invoke() itself (a real, separate bug this
	// project has already hit, see dht_node_responses.cpp), before this
	// fuzzer's actual target of reply parsing is even reached
	ta->add_entry(fake_id(0x77), fake_endpoint(), dht::observer::flag_initial);
	ta->start();

	deliver_reply(fx, input);
	return 0;
}
