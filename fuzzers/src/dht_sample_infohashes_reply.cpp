/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/kademlia/sample_infohashes.hpp"
#include "libtorrent/span.hpp"
#include "dht_reply_session.hpp"

using namespace lt;
using namespace dht_fuzz;

namespace {
fixture fx;
}

// fuzzes sample_infohashes_observer::reply(), via a real sample_infohashes
// traversal seeded with one candidate through add_entry() and started for
// real. the reply (the "r" dict) is fuzz-controlled; this
// exercises reply parsing ("nodes"/"samples"/"num"/"interval") in
// isolation from query construction, which needs the full dht_tracker
extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	auto const dcb = [](sha1_hash,
						 time_duration,
						 int,
						 std::vector<sha1_hash>,
						 std::vector<std::pair<sha1_hash, udp::endpoint>>) {};

	auto ta = std::make_shared<dht::sample_infohashes>(fx.n, fake_id(0x42), dcb);
	ta->add_entry(fake_id(0x77), fake_endpoint(), dht::observer::flag_initial);
	ta->start();

	deliver_reply(fx, span<std::uint8_t const>(data, static_cast<std::ptrdiff_t>(size)));
	return 0;
}
