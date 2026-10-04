/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_FUZZERS_DHT_REPLY_SESSION_HPP
#define LIBTORRENT_FUZZERS_DHT_REPLY_SESSION_HPP

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/kademlia/dht_storage.hpp"
#include "libtorrent/kademlia/msg.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/span.hpp"

namespace dht_fuzz {

struct null_dht_observer : lt::dht::dht_observer
{
	void set_external_address(
		lt::aux::listen_socket_handle const&, lt::address const&, lt::address const&) override
	{}
	int get_listen_port(lt::aux::transport, lt::aux::listen_socket_handle const&) const override
	{
		return 6881;
	}
	void get_peers(lt::sha1_hash const&) override {}
	void outgoing_get_peers(
		lt::sha1_hash const&, lt::sha1_hash const&, lt::udp::endpoint const&) override
	{}
	void announce(lt::sha1_hash const&, lt::address const&, int) override {}
	bool on_dht_request(lt::string_view, lt::dht::msg const&, lt::entry&) override { return false; }
	lt::ip_filter const* get_dht_ip_filter() const override { return nullptr; }
#ifndef TORRENT_DISABLE_LOGGING
	void log(lt::dht::dht_logger::module_t, char const*, ...) override {}
	bool should_log(module_t) const override { return true; }
	void log_packet(lt::dht::dht_logger::message_direction_t,
		lt::span<char const>,
		lt::udp::endpoint const&) override
	{}
#endif
};

// stands in for the real UDP layer: instead of actually sending anything,
// it records the transaction id and destination of the one query a test
// run makes, so the harness can answer it through node::incoming() as if
// the reply came back over the wire, with real tid and real rpc_manager
// bookkeeping (m_transactions, flag_queried, m_was_sent), just no socket
struct capturing_socket_manager : lt::dht::socket_manager
{
	bool has_quota() override { return true; }
	bool send_packet(
		lt::aux::listen_socket_handle const&, lt::entry& e, lt::udp::endpoint const& addr) override
	{
		lt::entry const* t = e.find_key("t");
		if (t != nullptr && t->type() == lt::entry::string_t && t->string().size() == 2)
		{
			tid = t->string();
			ep = addr;
			captured = true;
		}
		return true;
	}

	bool captured = false;
	std::string tid;
	lt::udp::endpoint ep;
};

// a minimal real dht::node plus just enough of rpc_manager's surroundings
// (via capturing_socket_manager) to run one real traversal_algorithm
// end to end: add_entry() seeds a candidate directly, skipping the routing
// table and node_seen()/heard_about() bootstrapping dht_node_responses.cpp
// needs; start() then runs the real add_requests()/invoke()/new_observer()
// chain, so the resulting observer is properly registered in the
// traversal's own bookkeeping (m_results, m_invoke_count), unlike
// constructing an observer directly, which left those invariants unmet and
// crashed in observer::~observer() and traversal_algorithm::finished()
struct fixture
{
	null_dht_observer obs;
	lt::aux::session_settings sett;
	capturing_socket_manager sock_man;
	lt::counters cnt;
	std::unique_ptr<lt::dht::dht_storage_interface> storage =
		lt::dht::dht_default_storage_constructor(sett);
	std::shared_ptr<lt::aux::listen_socket_t> listen_socket =
		std::make_shared<lt::aux::listen_socket_t>();
	lt::aux::listen_socket_handle sock{listen_socket};
	lt::dht::node n{sock,
		&sock_man,
		sett,
		lt::dht::node_id(),
		&obs,
		cnt,
		[](lt::dht::node_id const&, lt::string_view) -> lt::dht::node* { return nullptr; },
		*storage};

	// dht_tracker normally does this (new_socket()'s update_storage_node_ids())
	// whenever a node is created; storage's put_*_item() asserts it's been
	// done at least once (it's used for distance-based eviction)
	fixture() { storage->update_node_ids({n.nid()}); }
};

inline lt::udp::endpoint fake_endpoint()
{
	// parsed once rather than on every call, since this runs in the
	// per-input hot path of four separate fuzzer binaries
	static lt::udp::endpoint const ep = [] {
		lt::error_code ec;
		return lt::udp::endpoint(lt::make_address_v4("3.3.3.3", ec), 6881);
	}();
	return ep;
}

inline lt::dht::node_id fake_id(char fill = 0x11)
{
	std::array<char, 20> id{};
	id.fill(fill);
	return lt::dht::node_id(id.data());
}

// pulls up to n bytes off the front of data, returning what was taken and
// advancing data past it (clamped, since a short reply still needs to
// produce something rather than read out of bounds)
inline lt::span<std::uint8_t const> take(lt::span<std::uint8_t const>& data, std::ptrdiff_t const n)
{
	lt::span<std::uint8_t const> const chunk = data.first(std::min(data.size(), n));
	data = data.subspan(chunk.size());
	return chunk;
}

// same as take(), but zero-pads the result up to exactly N bytes instead
// of returning a possibly-shorter chunk; for fixed-width wire fields (a
// 20-byte node id, a 4-byte IPv4 address, ...) where a short result would
// either under-fill a hardcoded bencode length prefix or desync a
// fixed-stride compact record, rather than just producing a smaller field
template <std::size_t N>
std::array<char, N> take_padded(lt::span<std::uint8_t const>& data)
{
	std::array<char, N> out{};
	lt::span<std::uint8_t const> const chunk = take(data, static_cast<std::ptrdiff_t>(N));
	std::copy(chunk.begin(), chunk.end(), out.begin());
	return out;
}

// pops a single selector bit (bit 0 of the leading byte) off the front of
// data, for fuzzers that use one fuzz-controlled byte to choose between
// two traversal/message shapes before fuzzing the rest; nullopt if data
// is empty, the same way an empty reply/query payload is dropped elsewhere
inline std::optional<bool> take_flag(lt::span<std::uint8_t const>& data)
{
	if (data.empty())
		return std::nullopt;
	bool const flag = (data[0] & 1) != 0;
	data = data.subspan(1);
	return flag;
}

// wraps fuzz-controlled bytes as the "r" dict of a reply to the query
// fx.sock_man captured, and delivers it through node::incoming(), the
// real path a reply takes, so rpc_manager matches it by tid *and* source
// endpoint exactly like it would for a real packet. data is expected to
// be a complete bencoded dict (the "r" value); anything else, or no
// captured query to reply to, is dropped (returns false) the same way a
// malformed or unsolicited packet would be
inline bool deliver_reply(fixture& fx, lt::span<std::uint8_t const> data)
{
	// node::connection_timeout() is what expires an outstanding query that
	// never gets a matching reply, normally driven by dht_tracker's own
	// 1-second timer, which this bare node never has.
	// without this, every run whose reply fails to parse/match (the common
	// case for mutated fuzz bytes) leaks its observer into rpc_manager's
	// transaction table forever. aux::time_now() is real wall-clock time,
	// so this only actually prunes anything once 15 real seconds have
	// passed; cheap either way since rpc_manager::tick() short-circuits
	// on an empty transaction table.
	fx.n.connection_timeout();

	if (!fx.sock_man.captured)
		return false;

	std::string msg = "d1:r";
	msg.append(reinterpret_cast<char const*>(data.data()), static_cast<std::size_t>(data.size()));
	msg += "1:t2:";
	msg += fx.sock_man.tid;
	msg += "1:y1:re";

	lt::bdecode_node decoded;
	lt::error_code ec;
	if (lt::bdecode(msg.data(), msg.data() + msg.size(), decoded, ec) != 0)
		return false;
	if (decoded.type() != lt::bdecode_node::dict_t)
		return false;

	lt::dht::msg const m(decoded, fx.sock_man.ep);
	fx.n.incoming(fx.sock, m);
	return true;
}

} // namespace dht_fuzz

#endif
