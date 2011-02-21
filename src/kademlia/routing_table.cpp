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
#include <algorithm>
#include <functional>
#include <numeric>
#include <boost/cstdint.hpp>
#include <boost/bind.hpp>

#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/session_settings.hpp"

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
	, m_lowest_active_bucket(160)
{
	// distribute the refresh times for the buckets in an
	// attempt to even out the network load
	for (int i = 0; i < 160; ++i)
		m_bucket_activity[i] = time_now() - milliseconds(i*5625);
	m_bucket_activity[0] = time_now() - minutes(15);
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
		nodes += i->first.size();
		replacements += i->second.size();
	}
	return boost::make_tuple(nodes, replacements);
}

size_type routing_table::num_global_nodes() const
{
	int first_full = m_lowest_active_bucket;
	int num_nodes = 1; // we are one of the nodes
	for (; first_full < 160
		&& int(m_buckets[first_full].first.size()) < m_bucket_size;
		++first_full)
	{
		num_nodes += m_buckets[first_full].first.size();
	}

	return (2 << (160 - first_full)) * num_nodes;
}

#ifdef TORRENT_DHT_VERBOSE_LOGGING

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

	for (int k = 0; k < 8; ++k)
	{
		for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
			i != end; ++i)
		{
			os << (int(i->first.size()) > (7 - k) ? "|" : " ");
		}
		os << "\n";
	}
	for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
		i != end; ++i)
	{
		os << "+";
	}
	os << "\n";
	for (int k = 0; k < 8; ++k)
	{
		for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
			i != end; ++i)
		{
			os << (int(i->second.size()) > k ? "|" : " ");
		}
		os << "\n";
	}
	os << "-- cached ";
	for (int i = 10; i < 160; ++i)
		os << "-";
	os << "\n\n";

	os << "nodes:\n";
	for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
		i != end; ++i)
	{
		if (i->first.empty()) continue;
		int bucket_index = int(i - m_buckets.begin());
		os << "=== BUCKET = " << bucket_index
			<< " = " << (bucket_index >= m_lowest_active_bucket?"active":"inactive")
			<< " = " << total_seconds(time_now() - m_bucket_activity[bucket_index])
			<< " seconds ago ===== \n";
		for (bucket_t::const_iterator j = i->first.begin()
			, end(i->first.end()); j != end; ++j)
		{
			os << " id: " << j->id
				<< " ip: " << j->ep()
				<< " fails: " << j->fail_count()
				<< " pinged: " << j->pinged()
				<< "\n";
		}
	}
}

#endif

void routing_table::touch_bucket(int bucket)
{
	m_bucket_activity[bucket] = time_now();
}

ptime routing_table::next_refresh(int bucket)
{
	TORRENT_ASSERT(bucket < 160);
	TORRENT_ASSERT(bucket >= 0);
	// lower than or equal to since a refresh of bucket 0 will
	// effectively refresh the lowest active bucket as well
	if (bucket < m_lowest_active_bucket && bucket > 0)
		return time_now() + minutes(15);
	return m_bucket_activity[bucket] + minutes(15);
}

void routing_table::replacement_cache(bucket_t& nodes) const
{
	for (table_t::const_iterator i = m_buckets.begin()
		, end(m_buckets.end()); i != end; ++i)
	{
		std::copy(i->second.begin(), i->second.end()
			, std::back_inserter(nodes));
	}
}

void routing_table::heard_about(node_id const& id, udp::endpoint const& ep)
{
	int bucket_index = distance_exp(m_id, id);
	TORRENT_ASSERT(bucket_index < (int)m_buckets.size());
	TORRENT_ASSERT(bucket_index >= 0);
	bucket_t& b = m_buckets[bucket_index].first;
	bucket_t& rb = m_buckets[bucket_index].second;

	// if the replacement cache is full, we don't
	// need another node. The table is fine the
	// way it is.
	if ((int)rb.size() >= m_bucket_size) return;
	
	// if the node already exists, we don't need it
	if (std::find_if(b.begin(), b.end(), bind(&node_entry::id, _1) == id)
		!= b.end()) return;

	if (std::find_if(rb.begin(), rb.end(), bind(&node_entry::id, _1) == id)
		!= rb.end()) return;

	if (int(b.size()) < m_bucket_size)
	{
		if (bucket_index < m_lowest_active_bucket
			&& bucket_index > 0)
			m_lowest_active_bucket = bucket_index;
		b.push_back(node_entry(id, ep, false));
		return;
	}

	if (int(rb.size()) < m_bucket_size)
		rb.push_back(node_entry(id, ep, false));
}

void routing_table::node_failed(node_id const& id)
{
	int bucket_index = distance_exp(m_id, id);
	TORRENT_ASSERT(bucket_index < (int)m_buckets.size());
	TORRENT_ASSERT(bucket_index >= 0);
	bucket_t& b = m_buckets[bucket_index].first;
	bucket_t& rb = m_buckets[bucket_index].second;

	bucket_t::iterator i = std::find_if(b.begin(), b.end()
		, bind(&node_entry::id, _1) == id);

	if (i == b.end()) return;
	
	// if messages to ourself fails, ignore it
	if (bucket_index == 0) return;

	if (rb.empty())
	{
		i->timed_out();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(table) << " NODE FAILED"
			" id: " << id <<
			" ip: " << i->ep() <<
			" fails: " << i->fail_count() <<
			" pinged: " << i->pinged() <<
			" up-time: " << total_seconds(time_now() - i->first_seen);
#endif

		// if this node has failed too many times, or if this node
		// has never responded at all, remove it
		if (i->fail_count() >= m_settings.max_fail_count || !i->pinged())
		{
			b.erase(i);
			TORRENT_ASSERT(m_lowest_active_bucket <= bucket_index);
			while (m_lowest_active_bucket < 160
				&& m_buckets[m_lowest_active_bucket].first.empty())
			{
				++m_lowest_active_bucket;
			}
		}
		return;
	}

	b.erase(i);

	i = std::find_if(rb.begin(), rb.end(), bind(&node_entry::pinged, _1) == true);
	if (i == rb.end()) i = rb.begin();
	b.push_back(*i);
	rb.erase(i);
}

void routing_table::add_router_node(udp::endpoint router)
{
	m_router_nodes.insert(router);
}

// this function is called every time the node sees
// a sign of a node being alive. This node will either
// be inserted in the k-buckets or be moved to the top
// of its bucket.
// the return value indicates if the table needs a refresh.
// if true, the node should refresh the table (i.e. do a find_node
// on its own id)
bool routing_table::node_seen(node_id const& id, udp::endpoint addr)
{
	if (m_router_nodes.find(addr) != m_router_nodes.end()) return false;
	int bucket_index = distance_exp(m_id, id);
	TORRENT_ASSERT(bucket_index < (int)m_buckets.size());
	TORRENT_ASSERT(bucket_index >= 0);
	bucket_t& b = m_buckets[bucket_index].first;

	bucket_t::iterator i = std::find_if(b.begin(), b.end()
		, bind(&node_entry::id, _1) == id);

	bool ret = need_bootstrap();

	//m_bucket_activity[bucket_index] = time_now();

	if (i != b.end())
	{
		// we already have the node in our bucket
		// just move it to the back since it was
		// the last node we had any contact with
		// in this bucket
		i->set_pinged();
		i->reset_fail_count();
		i->addr = addr.address();
		i->port = addr.port();
//		TORRENT_LOG(table) << "updating node: " << id << " " << addr;
		return ret;
	}

	// if the node was not present in our list
	// we will only insert it if there is room
	// for it, or if some of our nodes have gone
	// offline
	if ((int)b.size() < m_bucket_size)
	{
		if (b.empty()) b.reserve(m_bucket_size);
		b.push_back(node_entry(id, addr, true));
		// if bucket index is 0, the node is ourselves
		// don't updated m_lowest_active_bucket
		if (bucket_index < m_lowest_active_bucket
			&& bucket_index > 0)
			m_lowest_active_bucket = bucket_index;
//		TORRENT_LOG(table) << "inserting node: " << id << " " << addr;
		return ret;
	}

	// if there is no room, we look for nodes that are not 'pinged',
	// i.e. we haven't confirmed that they respond to messages.
	// Then we look for nodes marked as stale
	// in the k-bucket. If we find one, we can replace it.
	
	i = std::find_if(b.begin(), b.end(), bind(&node_entry::pinged, _1) == false);

	if (i != b.end() && !i->pinged())
	{
		// i points to a node that has not been pinged.
		// Replace it with this new one
		b.erase(i);
		b.push_back(node_entry(id, addr, true));
//		TORRENT_LOG(table) << "replacing unpinged node: " << id << " " << addr;
		return ret;
	}

	// A node is considered stale if it has failed at least one
	// time. Here we choose the node that has failed most times.
	// If we don't find one, place this node in the replacement-
	// cache and replace any nodes that will fail in the future
	// with nodes from that cache.

	i = std::max_element(b.begin(), b.end()
		, bind(&node_entry::fail_count, _1)
		< bind(&node_entry::fail_count, _2));

	if (i != b.end() && i->fail_count() > 0)
	{
		// i points to a node that has been marked
		// as stale. Replace it with this new one
		b.erase(i);
		b.push_back(node_entry(id, addr, true));
//		TORRENT_LOG(table) << "replacing stale node: " << id << " " << addr;
		return ret;
	}

	// if we don't have any identified stale nodes in
	// the bucket, and the bucket is full, we have to
	// cache this node and wait until some node fails
	// and then replace it.

	bucket_t& rb = m_buckets[bucket_index].second;

	i = std::find_if(rb.begin(), rb.end()
		, bind(&node_entry::id, _1) == id);

	// if the node is already in the replacement bucket
	// just return.
	if (i != rb.end())
	{
		// make sure we mark this node as pinged
		// and if its address has changed, update
		// that as well
		i->set_pinged();
		i->reset_fail_count();
		i->addr = addr.address();
		i->port = addr.port();
		return ret;
	}
	
	if ((int)rb.size() >= m_bucket_size)
	{
		// if the replacement bucket is full, remove the oldest entry
		// but prefer nodes that haven't been pinged, since they are
		// less reliable than this one, that has been pinged
		i = std::find_if(rb.begin(), rb.end(), bind(&node_entry::pinged, _1) == false);
		rb.erase(i != rb.end() ? i : rb.begin());
	}
	if (rb.empty()) rb.reserve(m_bucket_size);
	rb.push_back(node_entry(id, addr, true));
//	TORRENT_LOG(table) << "inserting node in replacement cache: " << id << " " << addr;
	return ret;
}

bool routing_table::need_bootstrap() const
{
	ptime now = time_now();
	if (now - m_last_bootstrap < seconds(30)) return false;

	for (const_iterator i = begin(); i != end(); ++i)
	{
		if (i->confirmed()) return false;
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

	int bucket_index = distance_exp(m_id, target);
	bucket_t& b = m_buckets[bucket_index].first;

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

	if (int(l.size()) == count)
	{
		TORRENT_ASSERT((options & include_failed)
			|| std::count_if(l.begin(), l.end()
			, !boost::bind(&node_entry::confirmed, _1)) == 0);
		return;
	}

	// if we didn't have enough nodes in that bucket
	// we have to reply with nodes from buckets closer
	// to us. i.e. all the buckets in the range
	// [0, bucket_index) if we are to include ourself
	// or [1, bucket_index) if not.
	bucket_t tmpb;
	for (int i = (options & include_self)?0:1; i < bucket_index; ++i)
	{
		bucket_t& b = m_buckets[i].first;
		if (options & include_failed)
		{
			copy(b.begin(), b.end(), std::back_inserter(tmpb));
		}
		else
		{
			std::remove_copy_if(b.begin(), b.end(), std::back_inserter(tmpb)
				, !bind(&node_entry::confirmed, _1));
		}
	}

	if (count - l.size() < tmpb.size())
	{
		std::random_shuffle(tmpb.begin(), tmpb.end());
		size_t to_copy = count - l.size();
		std::copy(tmpb.begin(), tmpb.begin() + to_copy, std::back_inserter(l));
	}
	else
	{
		std::copy(tmpb.begin(), tmpb.end(), std::back_inserter(l));
	}
		
	TORRENT_ASSERT((int)l.size() <= count);

	// return if we have enough nodes or if the bucket index
	// is the biggest index available (there are no more buckets)
	// to look in.
	if (int(l.size()) == count)
	{
		TORRENT_ASSERT((options & include_failed)
			|| std::count_if(l.begin(), l.end()
				, !boost::bind(&node_entry::confirmed, _1)) == 0);
		return;
	}

	for (size_t i = bucket_index + 1; i < m_buckets.size(); ++i)
	{
		bucket_t& b = m_buckets[i].first;
	
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
		TORRENT_ASSERT((int)l.size() <= count);
		if (int(l.size()) == count)
		{
			TORRENT_ASSERT((options & include_failed)
				|| std::count_if(l.begin(), l.end()
					, !boost::bind(&node_entry::confirmed, _1)) == 0);
			return;
		}
	}
	TORRENT_ASSERT((int)l.size() <= count);

	TORRENT_ASSERT((options & include_failed)
		|| std::count_if(l.begin(), l.end()
			, !boost::bind(&node_entry::confirmed, _1)) == 0);
}

routing_table::iterator routing_table::begin() const
{
	// +1 to avoid ourself
	return iterator(m_buckets.begin() + 1, m_buckets.end());
}

routing_table::iterator routing_table::end() const
{
	return iterator(m_buckets.end(), m_buckets.end());
}

} } // namespace libtorrent::dht

