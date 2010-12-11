/*

Copyright (c) 2006, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include <vector>
#include <iterator> // std::distance()
#include <algorithm>
#include <functional>
#include <numeric>
#include <boost/cstdint.hpp>
#include <boost/bind.hpp>

#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/time.hpp"

using boost::uint8_t;

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(table)
#endif

routing_table::routing_table(node_id const& id, int bucket_size
	, dht_settings const& settings)
	: m_bucket_size(bucket_size)
	, m_settings(settings)
	, m_id(id)
	, m_last_bootstrap(min_time())
{
}

void routing_table::status(session_status& s) const
{
	boost::tie(s.dht_nodes, s.dht_node_cache) = size();
	s.dht_global_nodes = num_global_nodes();
}

boost::tuple<int, int> routing_table::size() const
{
	int nodes = 0;
	int replacements = 0;
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		nodes += i->live_nodes.size();
		replacements += i->replacements.size();
	}
	return boost::make_tuple(nodes, replacements);
}

size_type routing_table::num_global_nodes() const
{
	int num_nodes = 1; // we are one of the nodes
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		num_nodes += i->live_nodes.size();
	}

	return (2 << m_buckets.size()) * num_nodes;
}

#if (defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG) && TORRENT_USE_IOSTREAM

void routing_table::print_state(std::ostream& os) const
{
	os << "kademlia routing table state\n"
		<< "bucket_size: " << m_bucket_size << "\n"
		<< "global node count: " << num_global_nodes() << "\n"
		<< "node_id: " << m_id << "\n\n";

	os << "number of nodes per bucket:\n-- live ";
	for (int i = 8; i < 160; ++i)
		os << "-";
	os << "\n";

	for (int k = 0; k < m_bucket_size; ++k)
	{
		for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
			i != end; ++i)
		{
			os << (int(i->live_nodes.size()) > (m_bucket_size - 1 - k) ? "|" : " ");
		}
		os << "\n";
	}
	for (int i = 0; i < 160; ++i) os << "+";
	os << "\n";

	for (int k = 0; k < m_bucket_size; ++k)
	{
		for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
			i != end; ++i)
		{
			os << (int(i->replacements.size()) > k ? "|" : " ");
		}
		os << "\n";
	}
	os << "-- cached ";
	for (int i = 10; i < 160; ++i)
		os << "-";
	os << "\n\n";

	os << "nodes:\n";
	int bucket_index = 0;
	for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
		i != end; ++i, ++bucket_index)
	{
//		if (i->live_nodes.empty()) continue;
		os << "=== BUCKET = " << bucket_index
			<< " = " << total_seconds(time_now() - i->last_active)
			<< " seconds ago ===== \n";
		for (bucket_t::const_iterator j = i->live_nodes.begin()
			, end(i->live_nodes.end()); j != end; ++j)
		{
			os << " id: " << j->id
				<< " ip: " << j->ep()
				<< " fails: " << j->fail_count()
				<< " pinged: " << j->pinged()
				<< " dist: " << distance_exp(m_id, j->id)
				<< "\n";
		}
	}
}

#endif

void routing_table::touch_bucket(node_id const& target)
{
	table_t::iterator i = find_bucket(target);
	i->last_active = time_now();
}

bool routing_table::need_refresh(node_id& target) const
{
	if (m_buckets.empty()) return false;

	table_t::const_iterator i = std::min_element(m_buckets.begin(), m_buckets.end()
		, boost::bind(&routing_table_node::last_active, _1)
			< boost::bind(&routing_table_node::last_active, _2));

	if (time_now() - i->last_active < minutes(15)) return false;

	// generate a random node_id within the given bucket
	target = generate_id(address());
	int num_bits = std::distance(m_buckets.begin(), i) + 1;
	node_id mask(0);
	for (int i = 0; i < num_bits; ++i) mask[i/8] |= 0x80 >> (i&7);

	// target = (target & ~mask) | (root & mask)
	node_id root = m_id;
	root &= mask;
	target &= ~mask;
	target |= root;

	// make sure this is in another subtree than m_id
	// clear the (num_bits - 1) bit and then set it to the
	// inverse of m_id's corresponding bit.
	target[(num_bits - 1) / 8] &= ~(0x80 >> ((num_bits - 1) % 8));
	target[(num_bits - 1) / 8] |=
		(~(m_id[(num_bits - 1) / 8])) & (0x80 >> ((num_bits - 1) % 8));

	TORRENT_ASSERT(distance_exp(m_id, target) == 160 - num_bits);
	return true;
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
	TORRENT_ASSERT(bucket_index < m_buckets.size());
	TORRENT_ASSERT(bucket_index >= 0);

	table_t::iterator i = m_buckets.begin();
	std::advance(i, bucket_index);
	return i;
}

bool routing_table::add_node(node_entry const& e)
{
	if (m_router_nodes.find(e.ep()) != m_router_nodes.end()) return false;

	bool ret = need_bootstrap();

	// don't add ourself
	if (e.id == m_id) return ret;

	table_t::iterator i = find_bucket(e.id);
	bucket_t& b = i->live_nodes;
	bucket_t& rb = i->replacements;

	// if the replacement cache is full, we don't
	// need another node. The table is fine the
	// way it is.
	if ((int)rb.size() >= m_bucket_size) return ret;
	
	// if the node already exists, we don't need it
	bucket_t::iterator j = std::find_if(b.begin(), b.end()
		, boost::bind(&node_entry::id, _1) == e.id);

	if (j != b.end())
	{
		// we already have the node in our bucket
		// just move it to the back since it was
		// the last node we had any contact with
		// in this bucket
		*j = e;
//		TORRENT_LOG(table) << "updating node: " << i->id << " " << i->addr;
		return ret;
	}

	if (std::find_if(rb.begin(), rb.end(), boost::bind(&node_entry::id, _1) == e.id)
		!= rb.end()) return ret;

	// if the node was not present in our list
	// we will only insert it if there is room
	// for it, or if some of our nodes have gone
	// offline
	if (b.size() < m_bucket_size)
	{
		if (b.empty()) b.reserve(m_bucket_size);
		b.push_back(e);
//		TORRENT_LOG(table) << "inserting node: " << e.id << " " << e.addr;
		return ret;
	}

	// if there is no room, we look for nodes that are not 'pinged',
	// i.e. we haven't confirmed that they respond to messages.
	// Then we look for nodes marked as stale
	// in the k-bucket. If we find one, we can replace it.

	// can we split the bucket?
	bool can_split = false;

	if (e.pinged() && e.fail_count() == 0)
	{
		// only nodes that are pinged and haven't failed
		// can split the bucket, and we can only split
		// the last bucket
		can_split = (boost::next(i) == m_buckets.end() && m_buckets.size() < 160);

		// if the node we're trying to insert is considered pinged,
		// we may replace other nodes that aren't pinged

		j = std::find_if(b.begin(), b.end(), boost::bind(&node_entry::pinged, _1) == false);

		if (j != b.end() && !j->pinged())
		{
			// j points to a node that has not been pinged.
			// Replace it with this new one
			b.erase(j);
			b.push_back(e);
//			TORRENT_LOG(table) << "replacing unpinged node: " << e.id << " " << e.addr;
			return ret;
		}

		// A node is considered stale if it has failed at least one
		// time. Here we choose the node that has failed most times.
		// If we don't find one, place this node in the replacement-
		// cache and replace any nodes that will fail in the future
		// with nodes from that cache.

		j = std::max_element(b.begin(), b.end()
				, boost::bind(&node_entry::fail_count, _1)
				< boost::bind(&node_entry::fail_count, _2));

		if (j != b.end() && j->fail_count() > 0)
		{
			// i points to a node that has been marked
			// as stale. Replace it with this new one
			b.erase(j);
			b.push_back(e);
//			TORRENT_LOG(table) << "replacing stale node: " << e.id << " " << e.addr;
			return ret;
		}
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
			// make sure we mark this node as pinged
			// and if its address has changed, update
			// that as well
			*j = e;
			return ret;
		}

		if ((int)rb.size() >= m_bucket_size)
		{
			// if the replacement bucket is full, remove the oldest entry
			// but prefer nodes that haven't been pinged, since they are
			// less reliable than this one, that has been pinged
			j = std::find_if(rb.begin(), rb.end(), boost::bind(&node_entry::pinged, _1) == false);
			rb.erase(j != rb.end() ? j : rb.begin());
		}

		if (rb.empty()) rb.reserve(m_bucket_size);
		rb.push_back(e);
//		TORRENT_LOG(table) << "inserting node in replacement cache: " << e.id << " " << e.addr;
		return ret;
	}

	// this is the last bucket, and it's full already. Split
	// it by adding another bucket
	m_buckets.push_back(routing_table_node());
	bucket_t& new_bucket = m_buckets.back().live_nodes;
	bucket_t& new_replacement_bucket = m_buckets.back().replacements;

	// move any node whose (160 - distane_exp(m_id, id)) >= (i - m_buckets.begin())
	// to the new bucket
	int bucket_index = std::distance(m_buckets.begin(), i);
	for (bucket_t::iterator j = b.begin(); j != b.end();)
	{
		if (distance_exp(m_id, j->id) >= 159 - bucket_index)
		{
			++j;
			continue;
		}
		// this entry belongs in the new bucket
		new_bucket.push_back(*j);
		j = b.erase(j);
	}
	for (bucket_t::iterator j = rb.begin(); j != rb.end();)
	{
		if (distance_exp(m_id, j->id) >= 159 - bucket_index)
		{
			++j;
			continue;
		}
		// this entry belongs in the new bucket
		new_replacement_bucket.push_back(*j);
		j = rb.erase(j);
	}

	// now insert the new node in the appropriate bucket
	if (distance_exp(m_id, e.id) >= 159 - bucket_index)
	{
		if (b.size() < m_bucket_size)
			b.push_back(e);
		else if (rb.size() < m_bucket_size)
			rb.push_back(e);
	}
	else
	{
		if (new_bucket.size() < m_bucket_size)
			new_bucket.push_back(e);
		else if (new_replacement_bucket.size() < m_bucket_size)
			new_replacement_bucket.push_back(e);
	}
	return ret;
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
				, end(i->live_nodes.end()); j != end; ++j)
				fun1(userdata, *j);
		}
		if (fun2)
		{
			for (bucket_t::const_iterator j = i->replacements.begin()
				, end(i->replacements.end()); j != end; ++j)
				fun2(userdata, *j);
		}
	}
}

void routing_table::node_failed(node_id const& id)
{
	// if messages to ourself fails, ignore it
	if (id == m_id) return;

	table_t::iterator i = find_bucket(id);
	bucket_t& b = i->live_nodes;
	bucket_t& rb = i->replacements;

	bucket_t::iterator j = std::find_if(b.begin(), b.end()
		, boost::bind(&node_entry::id, _1) == id);

	if (j == b.end()) return;
	
	if (rb.empty())
	{
		j->timed_out();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(table) << " NODE FAILED"
			" id: " << id <<
			" ip: " << j->ep() <<
			" fails: " << j->fail_count() <<
			" pinged: " << j->pinged() <<
			" up-time: " << total_seconds(time_now() - j->first_seen);
#endif

		// if this node has failed too many times, or if this node
		// has never responded at all, remove it
		if (j->fail_count() >= m_settings.max_fail_count || !j->pinged())
			b.erase(j);
		return;
	}

	b.erase(j);

	j = std::find_if(rb.begin(), rb.end(), boost::bind(&node_entry::pinged, _1) == true);
	if (j == rb.end()) j = rb.begin();
	b.push_back(*j);
	rb.erase(j);
}

void routing_table::add_router_node(udp::endpoint router)
{
	m_router_nodes.insert(router);
}

// we heard from this node, but we don't know if it
// was spoofed or not (i.e. pinged == false)
void routing_table::heard_about(node_id const& id, udp::endpoint const& ep)
{
	add_node(node_entry(id, ep, false));
}

// this function is called every time the node sees
// a sign of a node being alive. This node will either
// be inserted in the k-buckets or be moved to the top
// of its bucket.
// the return value indicates if the table needs a refresh.
// if true, the node should refresh the table (i.e. do a find_node
// on its own id)
bool routing_table::node_seen(node_id const& id, udp::endpoint ep)
{
	return add_node(node_entry(id, ep, true));
}

bool routing_table::need_bootstrap() const
{
	ptime now = time_now();
	if (now - m_last_bootstrap < seconds(30)) return false;

	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		for (bucket_t::const_iterator j = i->live_nodes.begin()
			, end(i->live_nodes.end()); j != end; ++j)
		{
			if (j->confirmed()) return false;
		}
	}
	m_last_bootstrap = now;
	return true;
}

template <class SrcIter, class DstIter, class Pred>
DstIter copy_if_n(SrcIter begin, SrcIter end, DstIter target, size_t n, Pred p)
{
	for (; n > 0 && begin != end; ++begin)
	{
		if (!p(*begin)) continue;
		*target = *begin;
		--n;
		++target;
	}
	return target;
}

template <class SrcIter, class DstIter>
DstIter copy_n(SrcIter begin, SrcIter end, DstIter target, size_t n)
{
	for (; n > 0 && begin != end; ++begin)
	{
		*target = *begin;
		--n;
		++target;
	}
	return target;
}

// fills the vector with the k nodes from our buckets that
// are nearest to the given id.
void routing_table::find_node(node_id const& target
	, std::vector<node_entry>& l, int options, int count)
{
	l.clear();
	if (count == 0) count = m_bucket_size;
	l.reserve(count);

	table_t::iterator i = find_bucket(target);
	bucket_t& b = i->live_nodes;

	// copy all nodes that hasn't failed into the target
	// vector.
	if (options & include_failed)
	{
		copy_n(b.begin(), b.end(), std::back_inserter(l)
			, (std::min)(size_t(count), b.size()));
	}
	else
	{
		copy_if_n(b.begin(), b.end(), std::back_inserter(l)
			, (std::min)(size_t(count), b.size())
			, boost::bind(&node_entry::confirmed, _1));
	}
	TORRENT_ASSERT((int)l.size() <= count);

	if (int(l.size()) >= count) return;

	// if we didn't have enough nodes in that bucket
	// we have to reply with nodes from buckets closer
	// to us.
	table_t::iterator j = i;
	++j;

	for (; j != m_buckets.end() && l.size() < count; ++j)
	{
		bucket_t& b = j->live_nodes;
		size_t to_copy = (std::min)(count - l.size(), b.size());
		if (options & include_failed)
		{
			copy(b.begin(), b.begin() + to_copy
				, std::back_inserter(l));
		}
		else
		{
			std::remove_copy_if(b.begin(), b.begin() + to_copy
				, std::back_inserter(l)
				, !boost::bind(&node_entry::confirmed, _1));
		}
	}

	if (int(l.size()) >= count) return;

	// if we still don't have enough nodes, copy nodes
	// further away from us

	if (i == m_buckets.begin()) return;
	j = i;

	do
	{
		--j;
		bucket_t& b = j->live_nodes;
	
		size_t to_copy = (std::min)(count - l.size(), b.size());
		if (options & include_failed)
		{
			copy_n(b.begin(), b.end(), std::back_inserter(l), to_copy);
		}
		else
		{
			copy_if_n(b.begin(), b.end(), std::back_inserter(l)
				, to_copy, boost::bind(&node_entry::confirmed, _1));
		}
	}
	while (j != m_buckets.begin() && l.size() < count);
}
/*
routing_table::iterator routing_table::begin() const
{
	// +1 to avoid ourself
	return iterator(m_buckets.begin() + 1, m_buckets.end());
}

routing_table::iterator routing_table::end() const
{
	return iterator(m_buckets.end(), m_buckets.end());
}
*/
} } // namespace libtorrent::dht

