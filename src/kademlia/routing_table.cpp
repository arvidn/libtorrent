/*

Copyright (c) 2006-2016, Arvid Norberg
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
#include <iterator> // std::distance()
#include <algorithm> // std::copy, std::remove_copy_if
#include <functional>
#include <numeric>

#include "libtorrent/config.hpp"

#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/broadcast_socket.hpp" // for cidr_distance
#include "libtorrent/session_status.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/alert_types.hpp" // for dht_routing_bucket
#include "libtorrent/socket_io.hpp" // for print_endpoint
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/address.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/cstdint.hpp>
#include <boost/bind.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

using boost::uint8_t;

#if BOOST_VERSION <= 104700
namespace boost {
size_t hash_value(libtorrent::address_v4::bytes_type ip)
{
	return boost::hash_value(*reinterpret_cast<boost::uint32_t*>(&ip[0]));
}
}
#endif

namespace libtorrent { namespace dht
{
namespace
{
	template <typename T, typename K>
	void erase_one(T& container, K const& key)
	{
		typename T::iterator i = container.find(key);
		TORRENT_ASSERT(i != container.end());
		container.erase(i);
	}

	bool verify_node_address(dht_settings const& settings
		, node_id const& id, address const& addr)
	{
		// only when the node_id pass the verification, add it to routing table.
		if (settings.enforce_node_id && !verify_id(id, addr)) return false;

		return true;
	}
}

routing_table::routing_table(node_id const& id, int bucket_size
	, dht_settings const& settings
	, dht_logger* log)
	:
#ifndef TORRENT_DISABLE_LOGGING
	m_log(log),
#endif
	m_settings(settings)
	, m_id(id)
	, m_depth(0)
	, m_last_self_refresh(min_time())
	, m_bucket_size(bucket_size)
{
	TORRENT_UNUSED(log);
	m_buckets.reserve(30);
}

int routing_table::bucket_limit(int bucket) const
{
	if (!m_settings.extended_routing_table) return m_bucket_size;

	static const int size_exceptions[] = {16, 8, 4, 2};
	if (bucket < int(sizeof(size_exceptions)/sizeof(size_exceptions[0])))
		return m_bucket_size * size_exceptions[bucket];
	return m_bucket_size;
}

void routing_table::status(std::vector<dht_routing_bucket>& s) const
{
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		dht_routing_bucket b;
		b.num_nodes = i->live_nodes.size();
		b.num_replacements = i->replacements.size();
		s.push_back(b);
	}
}

#ifndef TORRENT_NO_DEPRECATE
// TODO: 2 use the non deprecated function instead of this one
void routing_table::status(session_status& s) const
{
	int ignore;
	boost::tie(s.dht_nodes, s.dht_node_cache, ignore) = size();
	s.dht_global_nodes = num_global_nodes();

	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		dht_routing_bucket b;
		b.num_nodes = i->live_nodes.size();
		b.num_replacements = i->replacements.size();
#ifndef TORRENT_NO_DEPRECATE
		b.last_active = 0;
#endif
		s.dht_routing_table.push_back(b);
	}
}
#endif

boost::tuple<int, int, int> routing_table::size() const
{
	int nodes = 0;
	int replacements = 0;
	int confirmed = 0;
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		nodes += i->live_nodes.size();
		for (bucket_t::const_iterator k = i->live_nodes.begin()
			, end2(i->live_nodes.end()); k != end2; ++k)
		{
			if (k->confirmed()) ++confirmed;
		}

		replacements += i->replacements.size();
	}
	return boost::make_tuple(nodes, replacements, confirmed);
}

boost::int64_t routing_table::num_global_nodes() const
{
	int deepest_bucket = 0;
	int deepest_size = 0;
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		deepest_size = i->live_nodes.size(); // + i->replacements.size();
		if (deepest_size < m_bucket_size) break;
		// this bucket is full
		++deepest_bucket;
	}

	if (deepest_bucket == 0) return 1 + deepest_size;

	if (deepest_size < m_bucket_size / 2) return (boost::int64_t(1) << deepest_bucket) * m_bucket_size;
	else return (boost::int64_t(2) << deepest_bucket) * deepest_size;
}

int routing_table::depth() const
{
	if (m_depth >= int(m_buckets.size()))
		m_depth = m_buckets.size() - 1;

	if (m_depth < 0) return m_depth;

	// maybe the table is deeper now?
	while (m_depth < int(m_buckets.size())-1
		&& int(m_buckets[m_depth+1].live_nodes.size()) >= m_bucket_size / 2)
	{
		++m_depth;
	}

	// maybe the table is more shallow now?
	while (m_depth > 0
		&& int(m_buckets[m_depth-1].live_nodes.size()) < m_bucket_size / 2)
	{
		--m_depth;
	}

	return m_depth;
}

#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM

void routing_table::print_state(std::ostream& os) const
{
	std::vector<char> buf(2048);
	int cursor = 0;

	cursor += snprintf(&buf[cursor], buf.size() - cursor
		, "kademlia routing table state\n"
		"bucket_size: %d\n"
		"global node count: %" PRId64 "\n"
		"node_id: %s\n\n"
		"number of nodes per bucket:\n"
		, m_bucket_size
		, num_global_nodes()
		, to_hex(m_id.to_string()).c_str());
	if (cursor > buf.size() - 500) buf.resize(buf.size() * 3 / 2);

	int idx = 0;

	for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
		i != end; ++i, ++idx)
	{
		cursor += snprintf(&buf[cursor], buf.size() - cursor
			, "%2d: ", idx);
		for (int k = 0; k < int(i->live_nodes.size()); ++k)
			cursor += snprintf(&buf[cursor], buf.size() - cursor, "#");
		for (int k = 0; k < int(i->replacements.size()); ++k)
			cursor += snprintf(&buf[cursor], buf.size() - cursor, "-");
		cursor += snprintf(&buf[cursor], buf.size() - cursor, "\n");

		if (cursor > buf.size() - 500) buf.resize(buf.size() * 3 / 2);
	}

	time_point now = aux::time_now();

	cursor += snprintf(&buf[cursor], buf.size() - cursor
		, "\nnodes:");

	int bucket_index = 0;
	for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
		i != end; ++i, ++bucket_index)
	{
		cursor += snprintf(&buf[cursor], buf.size() - cursor
			, "\n=== BUCKET == %d == %d|%d ==== \n"
			, bucket_index, int(i->live_nodes.size())
			, int(i->replacements.size()));
		if (cursor > buf.size() - 500) buf.resize(buf.size() * 3 / 2);

		int id_shift;
		// the last bucket is special, since it hasn't been split yet, it
		// includes that top bit as well
		if (bucket_index + 1 == m_buckets.size())
			id_shift = bucket_index;
		else
			id_shift = bucket_index + 1;

		for (bucket_t::const_iterator j = i->live_nodes.begin()
			, end2(i->live_nodes.end()); j != end2; ++j)
		{
			int bucket_size_limit = bucket_limit(bucket_index);
			boost::uint32_t top_mask = bucket_size_limit - 1;
			int mask_shift = 0;
			TORRENT_ASSERT_VAL(bucket_size_limit > 0, bucket_size_limit);
			while ((top_mask & 0x80) == 0)
			{
				top_mask <<= 1;
				++mask_shift;
			}
			top_mask = (0xff << mask_shift) & 0xff;

			node_id id = j->id;
			id <<= id_shift;

			cursor += snprintf(&buf[cursor], buf.size() - cursor
				, " prefix: %2x id: %s"
				, ((id[0] & top_mask) >> mask_shift)
				, to_hex(j->id.to_string()).c_str());

			if (j->rtt == 0xffff)
			{
				cursor += snprintf(&buf[cursor], buf.size() - cursor
					, " rtt:     ");
			}
			else
			{
				cursor += snprintf(&buf[cursor], buf.size() - cursor
					, " rtt: %4d", j->rtt);
			}

			cursor += snprintf(&buf[cursor], buf.size() - cursor
				, " fail: %4d ping: %d dist: %3d"
				, j->fail_count()
				, j->pinged()
				, distance_exp(m_id, j->id));

			if (j->last_queried == min_time())
			{
				cursor += snprintf(&buf[cursor], buf.size() - cursor
					, " query:    ");
			}
			else
			{
				cursor += snprintf(&buf[cursor], buf.size() - cursor
					, " query: %3d", int(total_seconds(now - j->last_queried)));
			}

			cursor += snprintf(&buf[cursor], buf.size() - cursor
				, " ip: %s\n", print_endpoint(j->ep()).c_str());
			if (cursor > buf.size() - 500) buf.resize(buf.size() * 3 / 2);
		}
	}

	cursor += snprintf(&buf[cursor], buf.size() - cursor
		, "\nnode spread per bucket:\n");
	bucket_index = 0;
	for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
		i != end; ++i, ++bucket_index)
	{
		int bucket_size_limit = bucket_limit(bucket_index);

		// mask out the first 3 bits, or more depending
		// on the bucket_size_limit
		// we have all the lower bits set in (bucket_size_limit-1)
		// but we want the left-most bits to be set. Shift it
		// until the MSB is set
		boost::uint32_t top_mask = bucket_size_limit - 1;
		int mask_shift = 0;
		TORRENT_ASSERT_VAL(bucket_size_limit > 0, bucket_size_limit);
		while ((top_mask & 0x80) == 0)
		{
			top_mask <<= 1;
			++mask_shift;
		}
		top_mask = (0xff << mask_shift) & 0xff;
		bucket_size_limit = (top_mask >> mask_shift) + 1;
		TORRENT_ASSERT_VAL(bucket_size_limit <= 256, bucket_size_limit);
		bool sub_buckets[256];
		memset(sub_buckets, 0, sizeof(sub_buckets));

		int id_shift;
		// the last bucket is special, since it hasn't been split yet, it
		// includes that top bit as well
		if (bucket_index + 1 == m_buckets.size())
			id_shift = bucket_index;
		else
			id_shift = bucket_index + 1;

		for (bucket_t::const_iterator j = i->live_nodes.begin()
			, end2(i->live_nodes.end()); j != end2; ++j)
		{
			node_id id = j->id;
			id <<= id_shift;
			int b = (id[0] & top_mask) >> mask_shift;
			TORRENT_ASSERT(b >= 0 && b < int(sizeof(sub_buckets)/sizeof(sub_buckets[0])));
			sub_buckets[b] = true;
		}

		cursor += snprintf(&buf[cursor], buf.size() - cursor
			, "%2d mask: %2x: [", bucket_index, (top_mask >> mask_shift));

		for (int j = 0; j < bucket_size_limit; ++j)
		{
			cursor += snprintf(&buf[cursor], buf.size() - cursor
				, (sub_buckets[j] ? "X" : " "));
		}
		cursor += snprintf(&buf[cursor], buf.size() - cursor
			, "]\n");
		if (cursor > buf.size() - 500) buf.resize(buf.size() * 3 / 2);
	}
	buf[cursor] = '\0';
	os << &buf[0];
}

#endif

node_entry const* routing_table::next_refresh()
{
	// find the node with the least recent 'last_queried' field. if it's too
	// recent, return false. Otherwise return a random target ID that's close to
	// a missing prefix for that bucket

	node_entry* candidate = NULL;

	// this will have a bias towards pinging nodes close to us first.
	int idx = m_buckets.size() - 1;
	for (table_t::reverse_iterator i = m_buckets.rbegin()
		, end(m_buckets.rend()); i != end; ++i, --idx)
	{
		for (bucket_t::iterator j = i->live_nodes.begin()
			, end2(i->live_nodes.end()); j != end2; ++j)
		{
			// this shouldn't happen
			TORRENT_ASSERT(m_id != j->id);
			if (j->id == m_id) continue;

			if (j->last_queried == min_time())
			{
				candidate = &*j;
				goto out;
			}

			if (candidate == NULL || j->last_queried < candidate->last_queried)
			{
				candidate = &*j;
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

void routing_table::replacement_cache(bucket_t& nodes) const
{
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		std::copy(i->replacements.begin(), i->replacements.end()
			, std::back_inserter(nodes));
	}
}

routing_table::table_t::iterator routing_table::find_bucket(node_id const& id)
{
//	TORRENT_ASSERT(id != m_id);

	int num_buckets = m_buckets.size();
	if (num_buckets == 0)
	{
		m_buckets.push_back(routing_table_node());
		++num_buckets;
	}

	int bucket_index = (std::min)(159 - distance_exp(m_id, id), num_buckets - 1);
	TORRENT_ASSERT(bucket_index < int(m_buckets.size()));
	TORRENT_ASSERT(bucket_index >= 0);

	table_t::iterator i = m_buckets.begin();
	std::advance(i, bucket_index);
	return i;
}

namespace {

bool compare_ip_cidr(node_entry const& lhs, node_entry const& rhs)
{
	TORRENT_ASSERT(lhs.addr().is_v4() == rhs.addr().is_v4());
	// the number of bits in the IPs that may match. If
	// more bits that this matches, something suspicious is
	// going on and we shouldn't add the second one to our
	// routing table
	int cutoff = rhs.addr().is_v4() ? 8 : 64;
	int dist = cidr_distance(lhs.addr(), rhs.addr());
	return dist <= cutoff;
}

} // anonymous namespace

node_entry* routing_table::find_node(udp::endpoint const& ep
	, routing_table::table_t::iterator* bucket)
{
	for (table_t::iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		for (bucket_t::iterator j = i->replacements.begin();
			j != i->replacements.end(); ++j)
		{
			if (j->addr() != ep.address()) continue;
			if (j->port() != ep.port()) continue;
			*bucket = i;
			return &*j;
		}
		for (bucket_t::iterator j = i->live_nodes.begin();
			j != i->live_nodes.end(); ++j)
		{
			if (j->addr() != ep.address()) continue;
			if (j->port() != ep.port()) continue;
			*bucket = i;
			return &*j;
		}
	}
	*bucket = m_buckets.end();
	return 0;
}

void routing_table::remove_node(node_entry* n
	, routing_table::table_t::iterator bucket)
{
	INVARIANT_CHECK;

	if (!bucket->replacements.empty()
		&& n >= &bucket->replacements[0]
		&& n < &bucket->replacements[0] + bucket->replacements.size())
	{
		int idx = n - &bucket->replacements[0];
		TORRENT_ASSERT(m_ips.count(n->a) > 0);
		erase_one(m_ips, n->a);
		bucket->replacements.erase(bucket->replacements.begin() + idx);
	}

	if (!bucket->live_nodes.empty()
		&& n >= &bucket->live_nodes[0]
		&& n < &bucket->live_nodes[0] + bucket->live_nodes.size())
	{
		int idx = n - &bucket->live_nodes[0];
		TORRENT_ASSERT(m_ips.count(n->a) > 0);
		erase_one(m_ips, n->a);
		bucket->live_nodes.erase(bucket->live_nodes.begin() + idx);
	}
}

bool routing_table::add_node(node_entry e)
{
	add_node_status_t s = add_node_impl(e);
	if (s == failed_to_add) return false;
	if (s == node_added) return true;

	while (s == need_bucket_split)
	{
		split_bucket();

		// if this assert triggers a lot in the wild, we should probably
		// harden our resistence towards this attack. Perhaps by never
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
			if (s == node_added) return true;
			return false;
		}

		// if the new bucket still has too many nodes in it, we need to keep
		// splitting
		if (m_buckets.back().live_nodes.size() > bucket_limit(m_buckets.size()-1))
			continue;

		s = add_node_impl(e);
		if (s == failed_to_add) return false;
		if (s == node_added) return true;
	}
	return false;
}

routing_table::add_node_status_t routing_table::add_node_impl(node_entry e)
{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
//	INVARIANT_CHECK;
#endif

	// if we already have this (IP,port), don't do anything
	if (m_router_nodes.find(e.ep()) != m_router_nodes.end())
		return failed_to_add;

	// don't add ourself
	if (e.id == m_id) return failed_to_add;

	// do we already have this IP in the table?
	if (m_ips.count(e.addr().to_v4().to_bytes()) > 0)
	{
		// this exact IP already exists in the table. It might be the case
		// that the node changed IP. If pinged is true, and the port also
		// matches then we assume it's in fact the same node, and just update
		// the routing table
		// pinged means that we have sent a message to the IP, port and received
		// a response with a correct transaction ID, i.e. it is verified to not
		// be the result of a poisoned routing table

		table_t::iterator existing_bucket;
		node_entry* existing = find_node(e.ep(), &existing_bucket);
		if (existing == 0)
		{
			// the node we're trying to add is not a match with an existing node. we
			// should ignore it, unless we allow duplicate IPs in our routing
			// table. There could be a node with the same IP, but with a different
			// port. m_ips just contain IP addresses, whereas the lookup we just
			// performed was for full endpoints (address, port).
			if (m_settings.restrict_routing_ips)
			{
#ifndef TORRENT_DISABLE_LOGGING
				char hex_id[41];
				to_hex(reinterpret_cast<char const*>(&e.id[0]), 20, hex_id);
				m_log->log(dht_logger::routing_table, "ignoring node (duplicate IP): %s %s"
					, hex_id, print_address(e.addr()).c_str());
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
			return node_added;
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
			// this is the same IP and port, but with
			// a new node ID. remove the old entry and
			// replace it with this new ID
			remove_node(existing, existing_bucket);
		}
	}

	table_t::iterator i = find_bucket(e.id);
	bucket_t& b = i->live_nodes;
	bucket_t& rb = i->replacements;
	int const bucket_index = std::distance(m_buckets.begin(), i);
	// compare against the max size of the next bucket. Otherwise we may wait too
	// long to split, and lose nodes (in the case where lower-numbered buckets
	// are larger)
	int const bucket_size_limit = bucket_limit(bucket_index);
	int const next_bucket_size_limit = bucket_limit(bucket_index + 1);

	bucket_t::iterator j;

	// if the node already exists, we don't need it
	j = std::find_if(b.begin(), b.end()
		, boost::bind(&node_entry::id, _1) == e.id);

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
	j = std::find_if(rb.begin(), rb.end(), boost::bind(&node_entry::id, _1) == e.id);
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
		erase_one(m_ips, j->addr().to_v4().to_bytes());
		rb.erase(j);
	}

	if (m_settings.restrict_routing_ips)
	{
		// don't allow multiple entries from IPs very close to each other
		// TODO: 3 the call to compare_ip_cidr here is expensive. peel off some
		// layers of abstraction here to make it quicker. Look at xoring and using _builtin_ctz()
		j = std::find_if(b.begin(), b.end(), boost::bind(&compare_ip_cidr, _1, e));
		if (j == b.end())
		{
			j = std::find_if(rb.begin(), rb.end(), boost::bind(&compare_ip_cidr, _1, e));
			if (j == rb.end()) goto ip_ok;
		}

		// we already have a node in this bucket with an IP very
		// close to this one. We know that it's not the same, because
		// it claims a different node-ID. Ignore this to avoid attacks
#ifndef TORRENT_DISABLE_LOGGING
		char hex_id1[41];
		to_hex(e.id.data(), 20, hex_id1);
		char hex_id2[41];
		to_hex(j->id.data(), 20, hex_id2);
		m_log->log(dht_logger::routing_table, "ignoring node: %s %s existing node: %s %s"
			, hex_id1, print_address(e.addr()).c_str()
			, hex_id2, print_address(j->addr()).c_str());
#endif
		return failed_to_add;
	}
ip_ok:

	// can we split the bucket?
	// only nodes that haven't failed can split the bucket, and we can only
	// split the last bucket
	bool const can_split = (boost::next(i) == m_buckets.end()
		&& m_buckets.size() < 159)
		&& e.fail_count() == 0
		&& (i == m_buckets.begin() || boost::prior(i)->live_nodes.size() > 1);

	// if there's room in the main bucket, just insert it
	// if we can split the bucket (i.e. it's the last bucket) use the next
	// bucket's size limit. This makes use split the low-numbered buckets split
	// earlier when we have larger low buckets, to make it less likely that we
	// lose nodes
	if (int(b.size()) < (can_split ? next_bucket_size_limit : bucket_size_limit))
	{
		if (b.empty()) b.reserve(bucket_size_limit);
		b.push_back(e);
		m_ips.insert(e.addr().to_v4().to_bytes());
		return node_added;
	}

	// if there is no room, we look for nodes that are not 'pinged',
	// i.e. we haven't confirmed that they respond to messages.
	// Then we look for nodes marked as stale
	// in the k-bucket. If we find one, we can replace it.
	// then we look for nodes with the same 3 bit prefix (or however
	// many bits prefix the bucket size warrants). If there is no other
	// node with this prefix, remove the duplicate with the highest RTT.
	// as the last replacement strategy, if the node we found matching our
	// bit prefix has higher RTT than the new node, replace it.

	if (e.pinged() && e.fail_count() == 0)
	{
		// if the node we're trying to insert is considered pinged,
		// we may replace other nodes that aren't pinged

		j = std::find_if(b.begin(), b.end(), boost::bind(&node_entry::pinged, _1) == false);

		if (j != b.end() && !j->pinged())
		{
			// j points to a node that has not been pinged.
			// Replace it with this new one
			erase_one(m_ips, j->addr().to_v4().to_bytes());
			*j = e;
			m_ips.insert(e.addr().to_v4().to_bytes());
			return node_added;
		}

		// A node is considered stale if it has failed at least one
		// time. Here we choose the node that has failed most times.
		// If we don't find one, place this node in the replacement-
		// cache and replace any nodes that will fail in the future
		// with nodes from that cache.

		j = std::max_element(b.begin(), b.end()
			, boost::bind(&node_entry::fail_count, _1)
			< boost::bind(&node_entry::fail_count, _2));
		TORRENT_ASSERT(j != b.end());

		if (j->fail_count() > 0)
		{
			// i points to a node that has been marked
			// as stale. Replace it with this new one
			erase_one(m_ips, j->addr().to_v4().to_bytes());
			*j = e;
			m_ips.insert(e.addr().to_v4().to_bytes());
			return node_added;
		}

		// in order to provide as few lookups as possible before finding
		// the data someone is looking for, make sure there is an affinity
		// towards having a good spread of node IDs in each bucket

		boost::uint32_t mask = bucket_size_limit - 1;
		int mask_shift = 0;
		TORRENT_ASSERT_VAL(mask > 0, mask);
		while ((mask & 0x80) == 0)
		{
			mask <<= 1;
			++mask_shift;
		}

		// in case bucket_size_limit is not an even power of 2
		mask = (0xff << mask_shift) & 0xff;

		// pick out all nodes that have the same prefix as the new node
		std::vector<bucket_t::iterator> nodes;
		bool force_replace = false;

		{
			node_id id = e.id;
			// the last bucket is special, since it hasn't been split yet, it
			// includes that top bit as well
			if (bucket_index + 1 == m_buckets.size())
				id <<= bucket_index;
			else
				id <<= bucket_index + 1;

			for (j = b.begin(); j != b.end(); ++j)
			{
				if (!matching_prefix(*j, mask, id[0] & mask, bucket_index)) continue;
				nodes.push_back(j);
			}
		}

		if (!nodes.empty())
		{
			j = *std::max_element(nodes.begin(), nodes.end()
				, boost::bind(&node_entry::rtt, boost::bind(&bucket_t::iterator::operator*, _1))
				< boost::bind(&node_entry::rtt, boost::bind(&bucket_t::iterator::operator*, _2)));
		}
		else
		{
			// there is no node in this prefix-slot, there may be some
			// nodes sharing a prefix. Find all nodes that do not
			// have a unique prefix

			// find node entries with duplicate prefixes in O(1)
			std::vector<bucket_t::iterator> prefix(1 << (8 - mask_shift), b.end());
			TORRENT_ASSERT(int(prefix.size()) >= bucket_size_limit);

			// the begin iterator from this object is used as a placeholder
			// for an occupied slot whose node has already been added to the
			// duplicate nodes list.
			bucket_t placeholder;

			nodes.reserve(b.size());
			for (j = b.begin(); j != b.end(); ++j)
			{
				node_id id = j->id;
				id <<= bucket_index + 1;
				int this_prefix = (id[0] & mask) >> mask_shift;
				TORRENT_ASSERT(this_prefix >= 0);
				TORRENT_ASSERT(this_prefix < int(prefix.size()));
				if (prefix[this_prefix] != b.end())
				{
					// there's already a node with this prefix. Remember both
					// duplicates.
					nodes.push_back(j);

					if (prefix[this_prefix] != placeholder.begin())
					{
						nodes.push_back(prefix[this_prefix]);
						prefix[this_prefix] = placeholder.begin();
					}
				}
			}

			if (!nodes.empty())
			{
				// from these nodes, pick the one with the highest RTT
				// and replace it

				std::vector<bucket_t::iterator>::iterator k = std::max_element(nodes.begin(), nodes.end()
					, boost::bind(&node_entry::rtt, boost::bind(&bucket_t::iterator::operator*, _1))
					< boost::bind(&node_entry::rtt, boost::bind(&bucket_t::iterator::operator*, _2)));

				// in this case, we would really rather replace the node even if
				// the new node has higher RTT, becase it fills a new prefix that we otherwise
				// don't have.
				force_replace = true;
				j = *k;
			}
			else
			{
				j = std::max_element(b.begin(), b.end()
					, boost::bind(&node_entry::rtt, _1)
					< boost::bind(&node_entry::rtt, _2));
			}
		}

		if (j != b.end() && (force_replace || j->rtt > e.rtt))
		{
			erase_one(m_ips, j->addr().to_v4().to_bytes());
			*j = e;
			m_ips.insert(e.addr().to_v4().to_bytes());
#ifndef TORRENT_DISABLE_LOGGING
			if (m_log)
			{
				char hex_id[41];
				to_hex(e.id.data(), sha1_hash::size, hex_id);
				m_log->log(dht_logger::routing_table, "replacing node with higher RTT: %s %s"
					, hex_id, print_address(e.addr()).c_str());
			}
#endif
			return node_added;
		}
		// in order to keep lookup times small, prefer nodes with low RTTs

	}

	// if we can't split, try to insert into the replacement bucket

	if (!can_split)
	{
		// if we don't have any identified stale nodes in
		// the bucket, and the bucket is full, we have to
		// cache this node and wait until some node fails
		// and then replace it.

		j = std::find_if(rb.begin(), rb.end()
			, boost::bind(&node_entry::id, _1) == e.id);

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
			j = std::find_if(rb.begin(), rb.end(), boost::bind(&node_entry::pinged, _1) == false);
			if (j == rb.end()) j = rb.begin();
			erase_one(m_ips, j->addr().to_v4().to_bytes());
			rb.erase(j);
		}

		if (rb.empty()) rb.reserve(m_bucket_size);
		rb.push_back(e);
		m_ips.insert(e.addr().to_v4().to_bytes());
		return node_added;
	}

	return need_bucket_split;
}

void routing_table::split_bucket()
{
	INVARIANT_CHECK;

	int const bucket_index = m_buckets.size()-1;
	int const bucket_size_limit = bucket_limit(bucket_index);
	TORRENT_ASSERT(int(m_buckets.back().live_nodes.size()) >= bucket_limit(bucket_index + 1));

	// this is the last bucket, and it's full already. Split
	// it by adding another bucket
	m_buckets.push_back(routing_table_node());
	bucket_t& new_bucket = m_buckets.back().live_nodes;
	bucket_t& new_replacement_bucket = m_buckets.back().replacements;

	bucket_t& b = m_buckets[bucket_index].live_nodes;
	bucket_t& rb = m_buckets[bucket_index].replacements;

	// move any node whose (160 - distane_exp(m_id, id)) >= (i - m_buckets.begin())
	// to the new bucket
	int const new_bucket_size = bucket_limit(bucket_index + 1);
	for (bucket_t::iterator j = b.begin(); j != b.end();)
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

	if (b.size() > bucket_size_limit)
	{
		// TODO: 2 move the lowest priority nodes to the replacement bucket
		for (bucket_t::iterator i = b.begin() + bucket_size_limit
			, end(b.end()); i != end; ++i)
		{
			rb.push_back(*i);
		}

		b.resize(bucket_size_limit);
	}

	// split the replacement bucket as well. If the live bucket
	// is not full anymore, also move the replacement entries
	// into the main bucket
	for (bucket_t::iterator j = rb.begin(); j != rb.end();)
	{
		if (distance_exp(m_id, j->id) >= 159 - bucket_index)
		{
			if (int(b.size()) >= bucket_size_limit)
			{
				++j;
				continue;
			}
			b.push_back(*j);
		}
		else
		{
			// this entry belongs in the new bucket
			if (int(new_bucket.size()) < new_bucket_size)
				new_bucket.push_back(*j);
			else
				new_replacement_bucket.push_back(*j);
		}
		j = rb.erase(j);
	}
}

void routing_table::update_node_id(node_id id)
{
	m_id = id;

	m_ips.clear();

	// pull all nodes out of the routing table, effectively emptying it
	table_t old_buckets;
	old_buckets.swap(m_buckets);

	// then add them all back. First add the main nodes, then the replacement
	// nodes
	for (int i = 0; i < old_buckets.size(); ++i)
	{
		bucket_t const& bucket = old_buckets[i].live_nodes;
		for (int j = 0; j < bucket.size(); ++j)
		{
			add_node(bucket[j]);
		}
	}

	// now add back the replacement nodes
	for (int i = 0; i < old_buckets.size(); ++i)
	{
		bucket_t const& bucket = old_buckets[i].replacements;
		for (int j = 0; j < bucket.size(); ++j)
		{
			add_node(bucket[j]);
		}
	}
}

void routing_table::for_each_node(
	void (*fun1)(void*, node_entry const&)
	, void (*fun2)(void*, node_entry const&)
	, void* userdata) const
{
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		if (fun1)
		{
			for (bucket_t::const_iterator j = i->live_nodes.begin()
				, end2(i->live_nodes.end()); j != end2; ++j)
				fun1(userdata, *j);
		}
		if (fun2)
		{
			for (bucket_t::const_iterator j = i->replacements.begin()
				, end2(i->replacements.end()); j != end2; ++j)
				fun2(userdata, *j);
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

	table_t::iterator i = find_bucket(nid);
	bucket_t& b = i->live_nodes;
	bucket_t& rb = i->replacements;

	bucket_t::iterator j = std::find_if(b.begin(), b.end()
		, boost::bind(&node_entry::id, _1) == nid);

	if (j == b.end())
	{
		j = std::find_if(rb.begin(), rb.end()
			, boost::bind(&node_entry::id, _1) == nid);

		if (j == rb.end()
			|| j->ep() != ep) return;

		j->timed_out();

#ifndef TORRENT_DISABLE_LOGGING
		char hex_id[41];
		to_hex(nid.data(), 20, hex_id);
		m_log->log(dht_logger::routing_table, "NODE FAILED id: %s ip: %s fails: %d pinged: %d up-time: %d"
			, hex_id, print_endpoint(j->ep()).c_str()
			, int(j->fail_count())
			, int(j->pinged())
			, int(total_seconds(aux::time_now() - j->first_seen)));
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
		char hex_id[41];
		to_hex(nid.data(), 20, hex_id);
		m_log->log(dht_logger::routing_table, "NODE FAILED id: %s ip: %s fails: %d pinged: %d up-time: %d"
			, hex_id, print_endpoint(j->ep()).c_str()
			, int(j->fail_count())
			, int(j->pinged())
			, int(total_seconds(aux::time_now() - j->first_seen)));
#endif

		// if this node has failed too many times, or if this node
		// has never responded at all, remove it
		if (j->fail_count() >= m_settings.max_fail_count || !j->pinged())
		{
			erase_one(m_ips, j->addr().to_v4().to_bytes());
			b.erase(j);
		}
		return;
	}

	erase_one(m_ips, j->a);
	b.erase(j);

	// sort by RTT first, to find the node with the lowest
	// RTT that is pinged
	std::sort(rb.begin(), rb.end()
		, boost::bind(&node_entry::rtt, _1) < boost::bind(&node_entry::rtt, _2));

	j = std::find_if(rb.begin(), rb.end(), boost::bind(&node_entry::pinged, _1));
	if (j == rb.end()) j = rb.begin();
	b.push_back(*j);
	rb.erase(j);
}

void routing_table::add_router_node(udp::endpoint router)
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
bool routing_table::node_seen(node_id const& id, udp::endpoint ep, int rtt)
{
	if (!verify_node_address(m_settings, id, ep.address())) return false;
	return add_node(node_entry(id, ep, rtt, true));
}

// fills the vector with the k nodes from our buckets that
// are nearest to the given id.
void routing_table::find_node(node_id const& target
	, std::vector<node_entry>& l, int options, int count)
{
	l.clear();
	if (count == 0) count = m_bucket_size;

	table_t::iterator i = find_bucket(target);
	int bucket_index = std::distance(m_buckets.begin(), i);
	int bucket_size_limit = bucket_limit(bucket_index);

	l.reserve(bucket_size_limit);

	table_t::iterator j = i;

	int unsorted_start_idx = 0;
	for (; j != m_buckets.end() && int(l.size()) < count; ++j)
	{
		bucket_t& b = j->live_nodes;
		if (options & include_failed)
		{
			copy(b.begin(), b.end()
				, std::back_inserter(l));
		}
		else
		{
			std::remove_copy_if(b.begin(), b.end()
				, std::back_inserter(l)
				, !boost::bind(&node_entry::confirmed, _1));
		}

		if (int(l.size()) == count) return;

		if (int(l.size()) > count)
		{
			// sort the nodes by how close they are to the target
			std::sort(l.begin() + unsorted_start_idx, l.end(), boost::bind(&compare_ref
				, boost::bind(&node_entry::id, _1)
				, boost::bind(&node_entry::id, _2), target));

			l.resize(count);
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
		bucket_t& b = j->live_nodes;

		if (options & include_failed)
		{
			std::copy(b.begin(), b.end(), std::back_inserter(l));
		}
		else
		{
			std::remove_copy_if(b.begin(), b.end(), std::back_inserter(l)
				, !boost::bind(&node_entry::confirmed, _1));
		}

		if (int(l.size()) == count) return;

		if (int(l.size()) > count)
		{
			// sort the nodes by how close they are to the target
			std::sort(l.begin() + unsorted_start_idx, l.end(), boost::bind(&compare_ref
				, boost::bind(&node_entry::id, _1)
				, boost::bind(&node_entry::id, _2), target));

			l.resize(count);
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
	boost::unordered_multiset<address_v4::bytes_type> all_ips;

	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		for (bucket_t::const_iterator j = i->replacements.begin();
			j != i->replacements.end(); ++j)
		{
			all_ips.insert(j->addr().to_v4().to_bytes());
		}
		for (bucket_t::const_iterator j = i->live_nodes.begin();
			j != i->live_nodes.end(); ++j)
		{
			all_ips.insert(j->addr().to_v4().to_bytes());
		}
	}

	TORRENT_ASSERT(all_ips == m_ips);
}
#endif

bool routing_table::is_full(int bucket) const
{
	int num_buckets = m_buckets.size();
	if (num_buckets == 0) return false;
	if (bucket >= num_buckets) return false;

	table_t::const_iterator i = m_buckets.begin();
	std::advance(i, bucket);
	return (i->live_nodes.size() >= bucket_limit(bucket)
		&& i->replacements.size() >= m_bucket_size);
}

} } // namespace libtorrent::dht

