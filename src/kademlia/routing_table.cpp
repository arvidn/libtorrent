/*

Copyright (c) 2006-2018, Arvid Norberg
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

#include <vector>
#include <iterator> // std::distance(), std::next
#include <algorithm> // std::copy, std::remove_copy_if
#include <functional>
#include <numeric>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <cstdint>

#include "libtorrent/config.hpp"

#include <libtorrent/hex.hpp> // to_hex
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/kademlia/dht_settings.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/alert_types.hpp" // for dht_routing_bucket
#include "libtorrent/socket_io.hpp" // for print_endpoint
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/array.hpp"

using namespace std::placeholders;

namespace libtorrent { namespace dht {

namespace {

	template <typename T, typename K>
	void erase_one(T& container, K const& key)
	{
		auto const i = container.find(key);
		TORRENT_ASSERT(i != container.end());
		container.erase(i);
	}

	bool verify_node_address(dht::settings const& settings
		, node_id const& id, address const& addr)
	{
		// only when the node_id pass the verification, add it to routing table.
		return !settings.enforce_node_id || verify_id(id, addr);
	}
}

void ip_set::insert(address const& addr)
{
	if (addr.is_v6())
		m_ip6s.insert(addr.to_v6().to_bytes());
	else
		m_ip4s.insert(addr.to_v4().to_bytes());
}

bool ip_set::exists(address const& addr) const
{
	if (addr.is_v6())
		return m_ip6s.find(addr.to_v6().to_bytes()) != m_ip6s.end();
	else
		return m_ip4s.find(addr.to_v4().to_bytes()) != m_ip4s.end();
}

void ip_set::erase(address const& addr)
{
	if (addr.is_v6())
		erase_one(m_ip6s, addr.to_v6().to_bytes());
	else
		erase_one(m_ip4s, addr.to_v4().to_bytes());
}

bool mostly_verified_nodes(bucket_t const& b)
{
	int const num_verified = static_cast<int>(std::count_if(b.begin(), b.end()
		, [](node_entry const& e) { return e.verified; }));
	if (num_verified == 0 && b.size() > 0) return false;
	return num_verified >= static_cast<int>(b.size()) * 2 / 3;
}

std::uint8_t classify_prefix(int const bucket_idx, bool const last_bucket
	, int const bucket_size, node_id nid)
{
	TORRENT_ASSERT_VAL(bucket_size > 0, bucket_size);
	TORRENT_ASSERT_VAL(bucket_size <= 256, bucket_size);

	std::uint32_t mask = static_cast<std::uint32_t>(bucket_size) - 1;
	// bucket sizes must be even powers of two.
	TORRENT_ASSERT_VAL((mask & static_cast<std::uint32_t>(bucket_size)) == 0, bucket_size);

	int const mask_shift = aux::count_leading_zeros(mask);
	TORRENT_ASSERT_VAL(mask_shift >= 0, mask_shift);
	TORRENT_ASSERT_VAL(mask_shift < 8, mask_shift);
	mask <<= mask_shift;
	TORRENT_ASSERT_VAL(mask > 0, mask);
	TORRENT_ASSERT_VAL(bool((mask & 0x80) != 0), mask);

	// the reason to shift one bit extra (except for the last bucket) is that the
	// first bit *defines* the bucket. That bit will be the same for all entries.
	// We're not interested in that one. However, the last bucket hasn't split
	// yet, so it will contain entries from both "sides", so we need to include
	// the top bit.
	nid <<= bucket_idx + int(!last_bucket);
	std::uint8_t const ret = (nid[0] & mask) >> mask_shift;
	TORRENT_ASSERT_VAL(ret < bucket_size, ret);
	return ret;
}

routing_table::add_node_status_t replace_node_impl(node_entry const& e
	, bucket_t& b, ip_set& ips, int const bucket_index
	, int const bucket_size_limit, bool const last_bucket
#ifndef TORRENT_DISABLE_LOGGING
	, dht_logger* log
#endif
	)
{
	// if the bucket isn't full, we're not replacing anything, and this function
	// should not have been called
	TORRENT_ASSERT(int(b.size()) >= bucket_size_limit);

	bucket_t::iterator j = std::max_element(b.begin(), b.end()
		, [](node_entry const& lhs, node_entry const& rhs)
		{ return lhs.fail_count() < rhs.fail_count(); });
	TORRENT_ASSERT(j != b.end());

	if (j->fail_count() > 0)
	{
		// i points to a node that has been marked
		// as stale. Replace it with this new one
		ips.erase(j->addr());
		*j = e;
		ips.insert(e.addr());
		return routing_table::node_added;
	}

	// then we look for nodes with the same 3 bit prefix (or however
	// many bits prefix the bucket size warrants). If there is no other
	// node with this prefix, remove the duplicate with the highest RTT.
	// as the last replacement strategy, if the node we found matching our
	// bit prefix has higher RTT than the new node, replace it.

	// in order to provide as few lookups as possible before finding
	// the data someone is looking for, make sure there is an affinity
	// towards having a good spread of node IDs in each bucket
	std::uint8_t const to_add_prefix = classify_prefix(bucket_index
		, last_bucket, bucket_size_limit, e.id);

	// nodes organized by their prefix
	aux::array<std::vector<bucket_t::iterator>, 128> nodes_storage;
	auto const nodes = span<std::vector<bucket_t::iterator>>{nodes_storage}.first(bucket_size_limit);

	for (j = b.begin(); j != b.end(); ++j)
	{
		std::uint8_t const prefix = classify_prefix(
			bucket_index, last_bucket, bucket_size_limit, j->id);
		TORRENT_ASSERT(prefix < nodes.size());
		nodes[prefix].push_back(j);
	}

	if (!nodes[to_add_prefix].empty())
	{
		j = *std::max_element(nodes[to_add_prefix].begin(), nodes[to_add_prefix].end()
			, [](bucket_t::iterator lhs, bucket_t::iterator rhs)
			{ return *lhs < *rhs; });

		// only if e is better than the worst node in this prefix slot do we
		// replace it. resetting j means we're not replacing it
		if (!(e < *j)) j = b.end();
	}
	else
	{
		// there is no node in this prefix slot. We definitely want to add it.
		// Now we just need to figure out which one to replace
		std::vector<bucket_t::iterator> replace_candidates;
		for (auto const& n : nodes)
		{
			if (n.size() > 1) replace_candidates.insert(replace_candidates.end(), n.begin(), n.end());
		}

		// since the bucket is full, and there's no node in the prefix-slot
		// we're about to add to, there must be at least one prefix slot that
		// has more than one node.
		TORRENT_ASSERT(!replace_candidates.empty());

		// from these nodes, pick the "worst" one and replace it
		j = *std::max_element(replace_candidates.begin(), replace_candidates.end()
			, [](bucket_t::iterator lhs, bucket_t::iterator rhs)
			{ return *lhs < *rhs; });
	}

	if (j != b.end())
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (log != nullptr && log->should_log(dht_logger::routing_table))
		{
			log->log(dht_logger::routing_table, "replacing node with better one: %s %s [%s %dms %d] vs. [%s %dms %d]"
				, aux::to_hex(e.id).c_str(), print_address(e.addr()).c_str()
				, e.verified ? "verified" : "not-verified", e.rtt
				, classify_prefix(bucket_index, last_bucket, bucket_size_limit, e.id)
				, j->verified ? "verified" : "not-verified", j->rtt
				, classify_prefix(bucket_index, last_bucket, bucket_size_limit, j->id)
				);
		}
#endif
		ips.erase(j->addr());
		*j = e;
		ips.insert(e.addr());
		return routing_table::node_added;
	}
	return routing_table::need_bucket_split;
}

routing_table::routing_table(node_id const& id, udp const proto, int const bucket_size
	, dht::settings const& settings
	, dht_logger* log)
	:
#ifndef TORRENT_DISABLE_LOGGING
	m_log(log),
#endif
	m_settings(settings)
	, m_id(id)
	, m_protocol(proto)
	, m_depth(0)
	, m_last_self_refresh(min_time())
	, m_bucket_size(bucket_size)
{
	// bucket sizes must be a power of 2
	TORRENT_ASSERT_VAL(((bucket_size - 1) & bucket_size) == 0, bucket_size);
	TORRENT_UNUSED(log);
	m_buckets.reserve(30);
}

int routing_table::bucket_limit(int bucket) const
{
	if (!m_settings.extended_routing_table) return m_bucket_size;

	static const aux::array<int, 4> size_exceptions{{{16, 8, 4, 2}}};
	if (bucket < size_exceptions.end_index())
		return m_bucket_size * size_exceptions[bucket];
	return m_bucket_size;
}

void routing_table::status(std::vector<dht_routing_bucket>& s) const
{
	// TODO: This is temporary. For now, only report the largest routing table
	// (of potentially multiple ones, for multi-homed systems)
	// in next major version, break the ABI and support reporting all of them in
	// the dht_stats_alert
	if (s.size() > m_buckets.size()) return;
	s.clear();
	for (auto const& i : m_buckets)
	{
		dht_routing_bucket b;
		b.num_nodes = int(i.live_nodes.size());
		b.num_replacements = int(i.replacements.size());
		s.push_back(b);
	}
}

#if TORRENT_ABI_VERSION == 1
// TODO: 2 use the non deprecated function instead of this one
void routing_table::status(session_status& s) const
{
	int dht_nodes;
	int dht_node_cache;
	int ignore;
	std::tie(dht_nodes, dht_node_cache, ignore) = size();
	s.dht_nodes += dht_nodes;
	s.dht_node_cache += dht_node_cache;
	// TODO: arvidn note
	// when it's across IPv4 and IPv6, adding (dht_global_nodes) would
	// make sense. in the future though, where we may have one DHT node
	// per external interface (which may be multiple of the same address
	// family), then it becomes a bit trickier
	s.dht_global_nodes += num_global_nodes();

	for (auto const& i : m_buckets)
	{
		dht_routing_bucket b;
		b.num_nodes = int(i.live_nodes.size());
		b.num_replacements = int(i.replacements.size());
#if TORRENT_ABI_VERSION == 1
		b.last_active = 0;
#endif
		s.dht_routing_table.push_back(b);
	}
}
#endif

std::tuple<int, int, int> routing_table::size() const
{
	int nodes = 0;
	int replacements = 0;
	int confirmed = 0;
	for (auto const& i : m_buckets)
	{
		nodes += int(i.live_nodes.size());
		confirmed += static_cast<int>(std::count_if(i.live_nodes.begin(), i.live_nodes.end()
			, [](node_entry const& k) { return k.confirmed(); } ));

		replacements += int(i.replacements.size());
	}
	return std::make_tuple(nodes, replacements, confirmed);
}

std::int64_t routing_table::num_global_nodes() const
{
	int deepest_bucket = 0;
	int deepest_size = 0;
	for (auto const& i : m_buckets)
	{
		deepest_size = i.live_nodes.end_index(); // + i.replacements.size();
		if (deepest_size < m_bucket_size) break;
		// this bucket is full
		++deepest_bucket;
	}

	if (deepest_bucket == 0) return 1 + deepest_size;

	if (deepest_size < m_bucket_size / 2) return (std::int64_t(1) << deepest_bucket) * m_bucket_size;
	else return (std::int64_t(2) << deepest_bucket) * deepest_size;
}

int routing_table::depth() const
{
	if (m_depth >= int(m_buckets.size()))
		m_depth = int(m_buckets.size()) - 1;

	if (m_depth < 0) return m_depth;

	// maybe the table is deeper now?
	while (m_depth < int(m_buckets.size()) - 1
		&& int(m_buckets[m_depth + 1].live_nodes.size()) >= m_bucket_size / 2)
	{
		++m_depth;
	}

	// maybe the table is more shallow now?
	while (m_depth > 0
		&& int(m_buckets[m_depth - 1].live_nodes.size()) < m_bucket_size / 2)
	{
		--m_depth;
	}

	return m_depth;
}

node_entry const* routing_table::next_refresh()
{
	// find the node with the least recent 'last_queried' field. if it's too
	// recent, return false. Otherwise return a random target ID that's close to
	// a missing prefix for that bucket

	node_entry* candidate = nullptr;

	// this will have a bias towards pinging nodes close to us first.
	for (auto i = m_buckets.rbegin(), end(m_buckets.rend()); i != end; ++i)
	{
		for (auto& n : i->live_nodes)
		{
			// this shouldn't happen
			TORRENT_ASSERT(m_id != n.id);
			if (n.id == m_id) continue;

			if (n.last_queried == min_time())
			{
				candidate = &n;
				goto out;
			}

			if (candidate == nullptr || n.last_queried < candidate->last_queried)
			{
				candidate = &n;
			}
		}

		if (i == m_buckets.rbegin()
			|| int(i->live_nodes.size()) < bucket_limit(int(std::distance(i, end)) - 1))
		{
			// this bucket isn't full or it can be split
			// check for an unpinged replacement
			// node which may be eligible for the live bucket if confirmed
			auto r = std::find_if(i->replacements.begin(), i->replacements.end()
				, [](node_entry const& e) { return !e.pinged() && e.last_queried == min_time(); });
			if (r != i->replacements.end())
			{
				candidate = &*r;
				goto out;
			}
		}
	}
out:

	// make sure we don't pick the same node again next time we want to refresh
	// the routing table
	if (candidate)
		candidate->last_queried = aux::time_now();

	return candidate;
}

routing_table::table_t::iterator routing_table::find_bucket(node_id const& id)
{
//	TORRENT_ASSERT(id != m_id);

	int num_buckets = int(m_buckets.size());
	if (num_buckets == 0)
	{
		m_buckets.push_back(routing_table_node());
		++num_buckets;
	}

	int bucket_index = std::min(159 - distance_exp(m_id, id), num_buckets - 1);
	TORRENT_ASSERT(bucket_index < int(m_buckets.size()));
	TORRENT_ASSERT(bucket_index >= 0);

	auto i = m_buckets.begin();
	std::advance(i, bucket_index);
	return i;
}

// returns true if the two IPs are "too close" to each other to be allowed in
// the same DHT lookup. If they are, the last one to be found will be ignored
bool compare_ip_cidr(address const& lhs, address const& rhs)
{
	TORRENT_ASSERT(lhs.is_v4() == rhs.is_v4());

	if (lhs.is_v6())
	{
		// if IPv6 addresses is in the same /64, they're too close and we won't
		// trust the second one
		std::uint64_t lhs_ip;
		std::memcpy(&lhs_ip, lhs.to_v6().to_bytes().data(), 8);
		std::uint64_t rhs_ip;
		std::memcpy(&rhs_ip, rhs.to_v6().to_bytes().data(), 8);

		// since the condition we're looking for is all the first  bits being
		// zero, there's no need to byte-swap into host byte order here.
		std::uint64_t const mask = lhs_ip ^ rhs_ip;
		return mask == 0;
	}
	else
	{
		// if IPv4 addresses is in the same /24, they're too close and we won't
		// trust the second one
		std::uint32_t const mask
			= std::uint32_t(lhs.to_v4().to_ulong() ^ rhs.to_v4().to_ulong());
		return mask <= 0x000000ff;
	}
}

std::tuple<node_entry*, routing_table::table_t::iterator, bucket_t*>
routing_table::find_node(udp::endpoint const& ep)
{
	for (auto i = m_buckets.begin() , end(m_buckets.end()); i != end; ++i)
	{
		for (auto j = i->replacements.begin(); j != i->replacements.end(); ++j)
		{
			if (j->addr() != ep.address()) continue;
			if (j->port() != ep.port()) continue;
			return std::make_tuple(&*j, i, &i->replacements);
		}
		for (auto j = i->live_nodes.begin(); j != i->live_nodes.end(); ++j)
		{
			if (j->addr() != ep.address()) continue;
			if (j->port() != ep.port()) continue;
			return std::make_tuple(&*j, i, &i->live_nodes);
		}
	}
	return std::tuple<node_entry*, routing_table::table_t::iterator, bucket_t*>(
		nullptr, m_buckets.end(), nullptr);
}

// TODO: this need to take bucket "prefix" into account. It should be unified
// with add_node_impl()
void routing_table::fill_from_replacements(table_t::iterator bucket)
{
	bucket_t& b = bucket->live_nodes;
	bucket_t& rb = bucket->replacements;
	int const bucket_size = bucket_limit(int(std::distance(m_buckets.begin(), bucket)));

	if (int(b.size()) >= bucket_size) return;

	// sort by RTT first, to find the node with the lowest
	// RTT that is pinged
	std::sort(rb.begin(), rb.end());

	while (int(b.size()) < bucket_size && !rb.empty())
	{
		auto j = std::find_if(rb.begin(), rb.end(), std::bind(&node_entry::pinged, _1));
		if (j == rb.end()) break;
		b.push_back(*j);
		rb.erase(j);
	}
}

void routing_table::prune_empty_bucket()
{
	if (m_buckets.back().live_nodes.empty()
		&& m_buckets.back().replacements.empty())
	{
		m_buckets.erase(m_buckets.end() - 1);
	}
}

void routing_table::remove_node(node_entry* n, bucket_t* b)
{
	std::ptrdiff_t const idx = n - b->data();
	TORRENT_ASSERT(idx >= 0);
	TORRENT_ASSERT(idx < intptr_t(b->size()));
	TORRENT_ASSERT(m_ips.exists(n->addr()));
	m_ips.erase(n->addr());
	b->erase(b->begin() + idx);
}

bool routing_table::add_node(node_entry const& e)
{
	add_node_status_t s = add_node_impl(e);
	if (s == failed_to_add) return false;
	if (s == node_added) return true;

	while (s == need_bucket_split)
	{
		split_bucket();

		// if this assert triggers a lot in the wild, we should probably
		// harden our resistance towards this attack. Perhaps by never
		// splitting a bucket (and discard nodes) if the two buckets above it
		// are empty or close to empty
//		TORRENT_ASSERT(m_buckets.size() <= 50);
		if (m_buckets.size() > 50)
		{
			// this is a sanity check. In the wild, we shouldn't see routing
			// tables deeper than 26 or 27. If we get this deep, there might
			// be a bug in the bucket splitting logic, or there may be someone
			// playing a prank on us, spoofing node IDs.
			s = add_node_impl(e);
			return s == node_added;
		}

		// if the new bucket still has too many nodes in it, we need to keep
		// splitting
		if (int(m_buckets.back().live_nodes.size()) > bucket_limit(int(m_buckets.size()) - 1))
			continue;

		s = add_node_impl(e);

		// we just split the last bucket and tried to insert a new node. If none
		// of the nodes in the split bucket, nor the new node ended up in the new
		// bucket, erase it
		if (m_buckets.back().live_nodes.empty())
		{
			m_buckets.erase(m_buckets.end() - 1);
			// we just split, trying to add the node again should not request
			// another split
			TORRENT_ASSERT(s != need_bucket_split);
		}
		if (s == failed_to_add) return false;
		if (s == node_added) return true;
	}
	return false;
}

bool all_in_same_bucket(span<node_entry const> b, node_id const& id, int const bucket_index)
{
	int const byte_offset = bucket_index / 8;
	int const bit_offset = bucket_index % 8;
	std::uint8_t const mask = 0x80 >> bit_offset;
	int counter[2] = {0, 0};
	int const i =  (id[byte_offset] & mask) ? 1 : 0;
	++counter[i];
	for (auto const& e : b)
	{
		int const idx =  (e.id[byte_offset] & mask) ? 1 : 0;
		++counter[idx];
	}
	return counter[0] == 0 || counter[1] == 0;
}

routing_table::add_node_status_t routing_table::add_node_impl(node_entry e)
{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
//	INVARIANT_CHECK;
#endif

	// don't add if the address isn't the right type
	if (!native_endpoint(e.ep()))
		return failed_to_add;

	// if we already have this (IP,port), don't do anything
	if (m_router_nodes.find(e.ep()) != m_router_nodes.end())
		return failed_to_add;

	// do we already have this IP in the table?
	if (m_ips.exists(e.addr()))
	{
		// This exact IP already exists in the table. A node with the same IP and
		// port but a different ID may be a sign of a malicious node. To be
		// conservative in this case the node is removed.
		// pinged means that we have sent a message to the IP, port and received
		// a response with a correct transaction ID, i.e. it is verified to not
		// be the result of a poisoned routing table

		node_entry * existing;
		routing_table::table_t::iterator existing_bucket;
		bucket_t* bucket;
		std::tie(existing, existing_bucket, bucket) = find_node(e.ep());
		if (existing == nullptr)
		{
			// the node we're trying to add is not a match with an existing node. we
			// should ignore it, unless we allow duplicate IPs in our routing
			// table. There could be a node with the same IP, but with a different
			// port. m_ips just contain IP addresses, whereas the lookup we just
			// performed was for full endpoints (address, port).
			if (m_settings.restrict_routing_ips)
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (m_log != nullptr && m_log->should_log(dht_logger::routing_table))
				{
					m_log->log(dht_logger::routing_table, "ignoring node (duplicate IP): %s %s"
						, aux::to_hex(e.id).c_str(), print_address(e.addr()).c_str());
				}
#endif
				return failed_to_add;
			}
		}
		else if (existing->id == e.id)
		{
			// if the node ID is the same, just update the failcount
			// and be done with it.
			existing->timeout_count = 0;
			if (e.pinged())
			{
				existing->update_rtt(e.rtt);
				existing->last_queried = e.last_queried;
			}
			// if this was a replacement node it may be elligible for
			// promotion to the live bucket
			fill_from_replacements(existing_bucket);
			prune_empty_bucket();
			return node_added;
		}
		else if (existing->id.is_all_zeros())
		{
			// this node's ID was unknown. remove the old entry and
			// replace it with the node's real ID
			remove_node(existing, bucket);
		}
		else if (!e.pinged())
		{
			// this may be a routing table poison attack. If we haven't confirmed
			// that this peer actually exist with this new node ID yet, ignore it.
			// we definitely don't want to replace the existing entry with this one
			if (m_settings.restrict_routing_ips)
				return failed_to_add;
		}
		else
		{
			TORRENT_ASSERT(existing->id != e.id);
			// This is the same IP and port, but with a new node ID.
			// This may indicate a malicious node so remove the entry.
#ifndef TORRENT_DISABLE_LOGGING
			if (m_log != nullptr && m_log->should_log(dht_logger::routing_table))
			{
				m_log->log(dht_logger::routing_table, "evicting node (changed ID): old: %s new: %s %s"
					, aux::to_hex(existing->id).c_str(), aux::to_hex(e.id).c_str(), print_address(e.addr()).c_str());
			}
#endif

			remove_node(existing, bucket);
			fill_from_replacements(existing_bucket);

			// when we detect possible malicious activity in a bucket,
			// schedule the other nodes in the bucket to be pinged soon
			// to clean out any other malicious nodes
			auto const now = aux::time_now();
			for (auto& node : existing_bucket->live_nodes)
			{
				if (node.last_queried + minutes(5) < now)
					node.last_queried = min_time();
			}

			prune_empty_bucket();
			return failed_to_add;
		}
	}

	// don't add ourself
	if (e.id == m_id) return failed_to_add;

	auto const i = find_bucket(e.id);
	bucket_t& b = i->live_nodes;
	bucket_t& rb = i->replacements;
	int const bucket_index = int(std::distance(m_buckets.begin(), i));
	// compare against the max size of the next bucket. Otherwise we may wait too
	// long to split, and lose nodes (in the case where lower-numbered buckets
	// are larger)
	int const bucket_size_limit = bucket_limit(bucket_index);

	bucket_t::iterator j;

	// if the node already exists, we don't need it
	j = std::find_if(b.begin(), b.end()
		, [&e](node_entry const& ne) { return ne.id == e.id; });

	if (j != b.end())
	{
		// a new IP address just claimed this node-ID
		// ignore it
		if (j->addr() != e.addr() || j->port() != e.port())
			return failed_to_add;

		// we already have the node in our bucket
		TORRENT_ASSERT(j->id == e.id && j->ep() == e.ep());
		j->timeout_count = 0;
		j->update_rtt(e.rtt);
		return node_added;
	}

	// if this node exists in the replacement bucket. update it and
	// pull it out from there. We may add it back to the replacement
	// bucket, but we may also replace a node in the main bucket, now
	// that we have an updated RTT
	j = std::find_if(rb.begin(), rb.end()
		, [&e](node_entry const& ne) { return ne.id == e.id; });
	if (j != rb.end())
	{
		// a new IP address just claimed this node-ID
		// ignore it
		if (j->addr() != e.addr() || j->port() != e.port())
			return failed_to_add;

		TORRENT_ASSERT(j->id == e.id && j->ep() == e.ep());
		j->timeout_count = 0;
		j->update_rtt(e.rtt);
		e = *j;
		m_ips.erase(j->addr());
		rb.erase(j);
	}

	if (m_settings.restrict_routing_ips)
	{
		// don't allow multiple entries from IPs very close to each other
		address const& cmp = e.addr();
		j = std::find_if(b.begin(), b.end(), [&](node_entry const& a) { return compare_ip_cidr(a.addr(), cmp); });
		if (j == b.end())
		{
			j = std::find_if(rb.begin(), rb.end(), [&](node_entry const& a) { return compare_ip_cidr(a.addr(), cmp); });
			if (j == rb.end()) goto ip_ok;
		}

		// we already have a node in this bucket with an IP very
		// close to this one. We know that it's not the same, because
		// it claims a different node-ID. Ignore this to avoid attacks
#ifndef TORRENT_DISABLE_LOGGING
		if (m_log != nullptr && m_log->should_log(dht_logger::routing_table))
		{
			m_log->log(dht_logger::routing_table, "ignoring node: %s %s existing node: %s %s"
				, aux::to_hex(e.id).c_str(), print_address(e.addr()).c_str()
				, aux::to_hex(j->id).c_str(), print_address(j->addr()).c_str());
		}
#endif
		return failed_to_add;
	}
ip_ok:

	// if there's room in the main bucket, just insert it
	// if we can split the bucket (i.e. it's the last bucket) use the next
	// bucket's size limit. This makes use split the low-numbered buckets split
	// earlier when we have larger low buckets, to make it less likely that we
	// lose nodes
	if (e.pinged() && int(b.size()) < bucket_size_limit)
	{
		if (b.empty()) b.reserve(bucket_size_limit);
		b.push_back(e);
		m_ips.insert(e.addr());
		return node_added;
	}

	// if there is no room, we look for nodes marked as stale
	// in the k-bucket. If we find one, we can replace it.

	// A node is considered stale if it has failed at least one
	// time. Here we choose the node that has failed most times.
	// If we don't find one, place this node in the replacement-
	// cache and replace any nodes that will fail in the future
	// with nodes from that cache.

	bool const last_bucket = bucket_index + 1 == int(m_buckets.size());

	// only nodes that have been confirmed can split the bucket, and we can only
	// split the last bucket
	// if all nodes in the bucket, including the new node id (e.id) fall in the
	// same bucket, splitting isn't going to do anything.
	bool const can_split = (std::next(i) == m_buckets.end()
		&& m_buckets.size() < 159)
		&& (m_settings.prefer_verified_node_ids == false
			|| (e.verified && mostly_verified_nodes(b)))
		&& e.confirmed()
		&& (i == m_buckets.begin() || std::prev(i)->live_nodes.size() > 1)
		&& !all_in_same_bucket(b, e.id, bucket_index);

	if (can_split) return need_bucket_split;

	if (e.confirmed())
	{
		auto const ret = replace_node_impl(e, b, m_ips, bucket_index, bucket_size_limit, last_bucket
#ifndef TORRENT_DISABLE_LOGGING
			, m_log
#endif
			);
		if (ret != need_bucket_split) return ret;
	}

	// if we can't split, nor replace anything in the live buckets try to insert
	// into the replacement bucket

	// if we don't have any identified stale nodes in
	// the bucket, and the bucket is full, we have to
	// cache this node and wait until some node fails
	// and then replace it.
	j = std::find_if(rb.begin(), rb.end()
		, [&e](node_entry const& ne) { return ne.id == e.id; });

	// if the node is already in the replacement bucket
	// just return.
	if (j != rb.end())
	{
		// if the IP address matches, it's the same node
		// make sure it's marked as pinged
		if (j->ep() == e.ep()) j->set_pinged();
		return node_added;
	}

	if (int(rb.size()) >= m_bucket_size)
	{
		// if the replacement bucket is full, remove the oldest entry
		// but prefer nodes that haven't been pinged, since they are
		// less reliable than this one, that has been pinged
		j = std::find_if(rb.begin(), rb.end()
			, [] (node_entry const& ne) { return !ne.pinged(); });
		if (j == rb.end())
		{
			auto const ret = replace_node_impl(e, rb, m_ips, bucket_index, m_bucket_size, last_bucket
#ifndef TORRENT_DISABLE_LOGGING
				, nullptr
#endif
				);
			return ret == node_added ? node_added : failed_to_add;
		}
		m_ips.erase(j->addr());
		rb.erase(j);
	}

	if (rb.empty()) rb.reserve(m_bucket_size);
	rb.push_back(e);
	m_ips.insert(e.addr());
	return node_added;
}

void routing_table::split_bucket()
{
	INVARIANT_CHECK;

	int const bucket_index = int(m_buckets.size()) - 1;
	int const bucket_size_limit = bucket_limit(bucket_index);
	TORRENT_ASSERT(int(m_buckets.back().live_nodes.size()) >= bucket_limit(bucket_index + 1));

	// this is the last bucket, and it's full already. Split
	// it by adding another bucket
	m_buckets.push_back(routing_table_node());
	bucket_t& new_bucket = m_buckets.back().live_nodes;
	bucket_t& new_replacement_bucket = m_buckets.back().replacements;

	bucket_t& b = m_buckets[bucket_index].live_nodes;
	bucket_t& rb = m_buckets[bucket_index].replacements;

	// move any node whose (160 - distance_exp(m_id, id)) >= (i - m_buckets.begin())
	// to the new bucket
	int const new_bucket_size = bucket_limit(bucket_index + 1);
	for (auto j = b.begin(); j != b.end();)
	{
		int const d = distance_exp(m_id, j->id);
		if (d >= 159 - bucket_index)
		{
			++j;
			continue;
		}
		// this entry belongs in the new bucket
		new_bucket.push_back(*j);
		j = b.erase(j);
	}

	if (int(b.size()) > bucket_size_limit)
	{
		// TODO: 2 move the lowest priority nodes to the replacement bucket
		for (auto i = b.begin() + bucket_size_limit
			, end(b.end()); i != end; ++i)
		{
			rb.push_back(*i);
		}

		b.resize(bucket_size_limit);
	}

	// split the replacement bucket as well. If the live bucket
	// is not full anymore, also move the replacement entries
	// into the main bucket
	for (auto j = rb.begin(); j != rb.end();)
	{
		if (distance_exp(m_id, j->id) >= 159 - bucket_index)
		{
			if (!j->pinged() || int(b.size()) >= bucket_size_limit)
			{
				++j;
				continue;
			}
			b.push_back(*j);
		}
		else
		{
			// this entry belongs in the new bucket
			if (j->pinged() && int(new_bucket.size()) < new_bucket_size)
				new_bucket.push_back(*j);
			else
				new_replacement_bucket.push_back(*j);
		}
		j = rb.erase(j);
	}
}

void routing_table::update_node_id(node_id const& id)
{
	m_id = id;

	m_ips.clear();

	// pull all nodes out of the routing table, effectively emptying it
	table_t old_buckets;
	old_buckets.swap(m_buckets);

	// then add them all back. First add the main nodes, then the replacement
	// nodes
	for (auto const& b : old_buckets)
		for (auto const& n : b.live_nodes)
			add_node(n);

	// now add back the replacement nodes
	for (auto const& b : old_buckets)
		for (auto const& n : b.replacements)
			add_node(n);
}

void routing_table::for_each_node(std::function<void(node_entry const&)> live_cb
	, std::function<void(node_entry const&)> replacements_cb) const
{
	for (auto const& i : m_buckets)
	{
		if (live_cb)
		{
			for (auto const& j : i.live_nodes)
				live_cb(j);
		}
		if (replacements_cb)
		{
			for (auto const& j : i.replacements)
				replacements_cb(j);
		}
	}
}

void routing_table::node_failed(node_id const& nid, udp::endpoint const& ep)
{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
	INVARIANT_CHECK;
#endif

	// if messages to ourself fails, ignore it
	if (nid == m_id) return;

	auto const i = find_bucket(nid);
	bucket_t& b = i->live_nodes;
	bucket_t& rb = i->replacements;

	auto j = std::find_if(b.begin(), b.end()
		, [&nid](node_entry const& ne) { return ne.id == nid; });

	if (j == b.end())
	{
		j = std::find_if(rb.begin(), rb.end()
			, [&nid](node_entry const& ne) { return ne.id == nid; });

		if (j == rb.end()
			|| j->ep() != ep) return;

		j->timed_out();

#ifndef TORRENT_DISABLE_LOGGING
		log_node_failed(nid, *j);
#endif
		return;
	}

	// if the endpoint doesn't match, it's a different node
	// claiming the same ID. The node we have in our routing
	// table is not necessarily stale
	if (j->ep() != ep) return;

	if (rb.empty())
	{
		j->timed_out();

#ifndef TORRENT_DISABLE_LOGGING
		log_node_failed(nid, *j);
#endif

		// if this node has failed too many times, or if this node
		// has never responded at all, remove it
		if (j->fail_count() >= m_settings.max_fail_count || !j->pinged())
		{
			m_ips.erase(j->addr());
			b.erase(j);
		}
		return;
	}

	m_ips.erase(j->addr());
	b.erase(j);

	fill_from_replacements(i);
	prune_empty_bucket();
}

void routing_table::add_router_node(udp::endpoint const& router)
{
	m_router_nodes.insert(router);
}

// we heard from this node, but we don't know if it was spoofed or not (i.e.
// pinged == false)
void routing_table::heard_about(node_id const& id, udp::endpoint const& ep)
{
	if (!verify_node_address(m_settings, id, ep.address())) return;
	add_node(node_entry(id, ep));
}

// this function is called every time the node sees a sign of a node being
// alive. This node will either be inserted in the k-buckets or be moved to the
// top of its bucket. the return value indicates if the table needs a refresh.
// if true, the node should refresh the table (i.e. do a find_node on its own
// id)
bool routing_table::node_seen(node_id const& id, udp::endpoint const& ep, int const rtt)
{
	return verify_node_address(m_settings, id, ep.address()) && add_node(node_entry(id, ep, rtt, true));
}

// fills the vector with the k nodes from our buckets that
// are nearest to the given id.
void routing_table::find_node(node_id const& target
	, std::vector<node_entry>& l, int const options, int count)
{
	l.clear();
	if (count == 0) count = m_bucket_size;

	auto const i = find_bucket(target);
	int const bucket_index = int(std::distance(m_buckets.begin(), i));
	int const bucket_size_limit = bucket_limit(bucket_index);

	l.reserve(aux::numeric_cast<std::size_t>(bucket_size_limit));

	table_t::iterator j = i;

	int unsorted_start_idx = 0;
	for (; j != m_buckets.end() && int(l.size()) < count; ++j)
	{
		bucket_t const& b = j->live_nodes;
		if (options & include_failed)
		{
			std::copy(b.begin(), b.end(), std::back_inserter(l));
		}
		else
		{
			std::remove_copy_if(b.begin(), b.end(), std::back_inserter(l)
				, [](node_entry const& ne) { return !ne.confirmed(); });
		}

		if (int(l.size()) == count) return;

		if (int(l.size()) > count)
		{
			// sort the nodes by how close they are to the target
			std::sort(l.begin() + unsorted_start_idx, l.end()
				, [&target](node_entry const& lhs, node_entry const& rhs)
				{ return compare_ref(lhs.id, rhs.id, target); });

			l.resize(aux::numeric_cast<std::size_t>(count));
			return;
		}
		unsorted_start_idx = int(l.size());
	}

	// if we still don't have enough nodes, copy nodes
	// further away from us

	if (i == m_buckets.begin())
		return;

	j = i;

	unsorted_start_idx = int(l.size());
	do
	{
		--j;
		bucket_t const& b = j->live_nodes;

		if (options & include_failed)
		{
			std::copy(b.begin(), b.end(), std::back_inserter(l));
		}
		else
		{
			std::remove_copy_if(b.begin(), b.end(), std::back_inserter(l)
				, [](node_entry const& ne) { return !ne.confirmed(); });
		}

		if (int(l.size()) == count) return;

		if (int(l.size()) > count)
		{
			// sort the nodes by how close they are to the target
			std::sort(l.begin() + unsorted_start_idx, l.end()
				, [&target](node_entry const& lhs, node_entry const& rhs)
				{ return compare_ref(lhs.id, rhs.id, target); });

			l.resize(aux::numeric_cast<std::size_t>(count));
			return;
		}
		unsorted_start_idx = int(l.size());
	}
	while (j != m_buckets.begin() && int(l.size()) < count);

	TORRENT_ASSERT(int(l.size()) <= count);
}

#if TORRENT_USE_INVARIANT_CHECKS
void routing_table::check_invariant() const
{
	ip_set all_ips;

	for (auto const& i : m_buckets)
	{
		for (auto const& j : i.replacements)
		{
			all_ips.insert(j.addr());
		}
		for (auto const& j : i.live_nodes)
		{
			TORRENT_ASSERT(j.addr().is_v4() == i.live_nodes.begin()->addr().is_v4());
			TORRENT_ASSERT(j.pinged());
			all_ips.insert(j.addr());
		}
	}

	TORRENT_ASSERT(all_ips == m_ips);
}
#endif

bool routing_table::is_full(int const bucket) const
{
	int const num_buckets = int(m_buckets.size());
	if (num_buckets == 0) return false;
	if (bucket >= num_buckets) return false;

	auto i = m_buckets.cbegin();
	std::advance(i, bucket);
	return (int(i->live_nodes.size()) >= bucket_limit(bucket)
		&& int(i->replacements.size()) >= m_bucket_size);
}

#ifndef TORRENT_DISABLE_LOGGING
void routing_table::log_node_failed(node_id const& nid, node_entry const& ne) const
{
	if (m_log != nullptr && m_log->should_log(dht_logger::routing_table))
	{
		m_log->log(dht_logger::routing_table, "NODE FAILED id: %s ip: %s fails: %d pinged: %d up-time: %d"
			, aux::to_hex(nid).c_str(), print_endpoint(ne.ep()).c_str()
			, ne.fail_count()
			, int(ne.pinged())
			, int(total_seconds(aux::time_now() - ne.first_seen)));
	}
}
#endif

} } // namespace libtorrent::dht
