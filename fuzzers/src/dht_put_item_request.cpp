/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>
#include <cstring>
#include <string>
#include <tuple>

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/item.hpp"
#include "libtorrent/kademlia/ed25519.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/span.hpp"
#include "dht_reply_session.hpp"

using namespace lt;
using namespace dht_fuzz;

namespace {

fixture fx;

// fixed, not fuzz-derived: lets the harness produce a genuinely valid
// signature on demand (see below). cached rather than re-derived (a
// SHA-512 + EC scalar multiplication) on every one of the millions of
// calls LLVMFuzzerTestOneInput makes, since the seed never changes
std::tuple<dht::public_key, dht::secret_key> const& fixed_keypair()
{
	static std::tuple<dht::public_key, dht::secret_key> const kp = [] {
		std::array<char, 32> seed{};
		seed.fill(char(0x42));
		return dht::ed25519_create_keypair(seed);
	}();
	return kp;
}

} // namespace

// fuzzes node::incoming_request()'s "put" branch (both immutable and
// mutable) through the public node::incoming(). no traversal or observer
// is needed at all, unlike the get_peers/get_item/sample_infohashes reply
// fuzzers, since this is about *receiving* a put, not processing a reply
// to one of our own queries. put_data_observer::reply() is just
// "{ done(); }" (see dht_get_peers_reply.cpp's comment on why put/
// announce_peer aren't reply-fuzzed), so this is the only place put's real
// logic, token verification, ed25519 signature verification, CAS /
// stale-sequence rejection, and storage, is reachable at all.
//
// the write token is produced by the harness itself via node's public
// generate_token(), the same function the real "get" handler uses,
// instead of simulating a prior "get" round trip; always valid unless the
// corrupt_token bit is set. for mutable puts, a random 64-byte signature
// essentially never verifies against a correct ed25519 implementation, so
// unless corrupt_sig is set, the harness signs the fuzz-controlled value
// itself with a fixed, deterministic keypair via the public
// sign_mutable_item(); without this, the deep path (signature verified,
// item actually stored) would be statistically unreachable by mutation
// alone. the dht_storage_interface in the fixture is one persistent
// global across the whole fuzzing run, so a later run that happens to
// target the same (pk, salt) with a stale or CAS-mismatched sequence
// number naturally exercises that rejection, without needing to
// orchestrate two puts within a single run
extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	if (size < 2)
		return 0;
	span<std::uint8_t const> input(data, static_cast<std::ptrdiff_t>(size));
	std::uint8_t const opcode = input[0];
	bool const do_mutable = (opcode & 1) != 0;
	bool const corrupt_token = (opcode & 2) != 0;
	bool const corrupt_sig = (opcode & 4) != 0;
	input = input.subspan(1);

	// a prefix of the remaining bytes becomes the salt (mutable puts
	// only), the rest is the item's value; deliberately simple slicing,
	// not an attempt at full dict-shape fuzzing, the interesting surface
	// here is seq/signature-validity/CAS, not bencode framing
	std::ptrdiff_t const salt_len = do_mutable ? std::min<std::ptrdiff_t>(input.size() / 2, 32) : 0;
	span<std::uint8_t const> const salt_bytes = input.first(salt_len);
	span<std::uint8_t const> const v_bytes = input.subspan(salt_len);
	std::string const salt(reinterpret_cast<char const*>(salt_bytes.data()),
		static_cast<std::size_t>(salt_bytes.size()));
	std::string const v_content(
		reinterpret_cast<char const*>(v_bytes.data()), static_cast<std::size_t>(v_bytes.size()));
	if (v_content.empty() || v_content.size() > 1000)
		return 0;

	std::int64_t seq_val = 0;
	std::memcpy(&seq_val,
		input.data(),
		std::min<std::size_t>(static_cast<std::size_t>(input.size()), sizeof(seq_val)));

	// sign_mutable_item()/item_target_id() both bdecode this span
	// internally (item.cpp's canonical_string()), so it must already be
	// valid bencode, not raw opaque bytes. wrapping v_content as a
	// bencode string is always valid regardless of its byte content, and
	// is exactly what entry's own bencoding of a["v"] below produces, so
	// the two stay in sync
	std::string const v_buf = std::to_string(v_content.size()) + ":" + v_content;

	entry put;
	put["y"] = "q";
	put["q"] = "put";
	put["t"] = "aa";
	entry& a = put["a"];
	a["id"] = fake_id(0x11).to_string();
	a["v"] = v_content;

	sha1_hash target;
	if (do_mutable)
	{
		auto const [pk, sk] = fixed_keypair();
		target = dht::item_target_id(salt, pk);

		dht::signature sig;
		if (corrupt_sig)
		{
			std::array<char, dht::signature::len> zero{};
			sig = dht::signature(zero.data());
		}
		else
		{
			sig = dht::sign_mutable_item(v_buf, salt, dht::sequence_number(seq_val), pk, sk);
		}

		a["salt"] = salt;
		a["seq"] = seq_val;
		a["k"] = std::string(pk.bytes.data(), pk.bytes.size());
		a["sig"] = std::string(sig.bytes.data(), sig.bytes.size());
	}
	else
	{
		target = dht::item_target_id(v_buf);
	}

	udp::endpoint const addr = fake_endpoint();
	a["token"] = corrupt_token ? std::string("bad-token") : fx.n.generate_token(addr, target);

	std::string buf;
	bencode(std::back_inserter(buf), put);

	bdecode_node decoded;
	error_code ec;
	if (bdecode(buf.data(), buf.data() + buf.size(), decoded, ec) != 0)
		return 0;
	if (decoded.type() != bdecode_node::dict_t)
		return 0;

	dht::msg const m(decoded, addr);
	fx.n.incoming(fx.sock, m);
	return 0;
}
