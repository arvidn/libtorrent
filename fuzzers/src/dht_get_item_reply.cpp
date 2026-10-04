/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>

#include "libtorrent/kademlia/get_item.hpp"
#include "libtorrent/span.hpp"
#include "dht_reply_session.hpp"

using namespace lt;
using namespace dht_fuzz;

namespace {
fixture fx;
}

// fuzzes get_item_observer::reply(), for both the immutable (target hash)
// and mutable (public key + salt) traversal shapes, via a real get_item
// traversal seeded with one candidate through add_entry() and started for
// real. the reply (the "r" dict) is fuzz-controlled; this
// exercises reply parsing ("v"/"k"/"sig"/"seq", and the resulting
// mutable-item signature verification inside got_data()) in isolation
// from query construction, which needs the full dht_tracker
extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	span<std::uint8_t const> input(data, static_cast<std::ptrdiff_t>(size));
	auto const mutable_item = take_flag(input);
	if (!mutable_item)
		return 0;

	auto const dcb = [](dht::item const&, bool) {};
	auto const ncb = [](std::vector<std::pair<dht::node_entry, std::string>> const&) {};

	std::shared_ptr<dht::get_item> ta;
	if (*mutable_item)
	{
		std::array<char, dht::public_key::len> pk_bytes{};
		pk_bytes.fill(char(0x55));
		dht::public_key const pk(pk_bytes.data());
		ta = std::make_shared<dht::get_item>(fx.n, pk, "salt", dcb, ncb);
	}
	else
	{
		ta = std::make_shared<dht::get_item>(fx.n, fake_id(0x42), dcb, ncb);
	}

	// the candidate we query; unrelated to the traversal's own target
	// (a plain hash for immutable, or derived from pk+salt for mutable)
	ta->add_entry(fake_id(0x77), fake_endpoint(), dht::observer::flag_initial);
	ta->start();

	deliver_reply(fx, input);
	return 0;
}
