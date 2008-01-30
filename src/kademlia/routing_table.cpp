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

#include <vector>
#include <deque>
#include <algorithm>
#include <functional>
#include <numeric>
#include <boost/cstdint.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/session_settings.hpp"

using boost::bind;
using boost::uint8_t;

using boost::posix_time::second_clock;
using boost::posix_time::minutes;
using boost::posix_time::seconds;
using boost::posix_time::hours;

namespace pt = boost::posix_time;

namespace libtorrent { namespace dht
{

using asio::ip::udp;
typedef asio::ip::address_v4 address;

routing_table::routing_table(node_id const& id, int bucket_size
	, dht_settings const& settings)
	: m_bucket_size(bucket_size)
	, m_settings(settings)
	, m_id(id)
	, m_lowest_active_bucket(160)
{
	// distribute the refresh times for the buckets in an
	// attempt do even out the network load
	for (int i = 0; i < 160; ++i)
		m_bucket_activity[i] = second_clock::universal_time() - seconds(15*60 - i*5);
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

void routing_table::print_state(std::ostream& os) const
{
	os << "kademlia routing table state\n"
		<< "bucket_size: " << m_bucket_size << "\n"
		<< "node_id: " << m_id << "\n\n";

	os << "number of nodes per bucket:\n"
		"live\n";
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
	os << "cached\n-----------\n";

	os << "nodes:\n";
	for (table_t::const_iterator i = m_buckets.begin(), end(m_buckets.end());
		i != end; ++i)
	{
		int bucket_index = int(i - m_buckets.begin());
		os << "bucket " << bucket_index << " "
			<< to_simple_string(m_bucket_activity[bucket_index])
			<< " " << (bucket_index >= m_lowest_active_bucket?"active":"inactive")
			<< "\n";
		for (bucket_t::const_iterator j = i->first.begin()
			, end(i->first.end()); j != end; ++j)
		{
			os << "ip: " << j->addr << " 	fails: " << j->fail_count
				<< " 	id: " << j->id << "\n";
		}
	}
}

void routing_table::touch_bucket(int bucket)
{
	m_bucket_activity[bucket] = second_clock::universal_time();
}

boost::posix_time::ptime routing_table::next_refresh(int bucket)
{
	assert(bucket < 160);
	assert(bucket >= 0);
	// lower than or equal to since a refresh of bucket 0 will
	// effectively refresh the lowest active bucket as well
	if (bucket <= m_lowest_active_bucket && bucket > 0)
		return second_clock::universal_time() + minutes(15);
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

bool routing_table::need_node(node_id const& id)
{
	int bucket_index = distance_exp(m_id, id);
	assert(bucket_index < (int)m_buckets.size());
	assert(bucket_index >= 0);
	bucket_t& b = m_buckets[bucket_index].first;
	bucket_t& rb = m_buckets[bucket_index].second;

	// if the replacement cache is full, we don't
	// need another node. The table is fine the
	// way it is.
	if ((int)rb.size() >= m_bucket_size) return false;
	
	// if the node already exists, we don't need it
	if (std::find_if(b.begin(), b.end(), bind(std::equal_to<node_id>()
		, bind(&node_entry::id, _1), id)) != b.end()) return false;

	if (std::find_if(rb.begin(), rb.end(), bind(std::equal_to<node_id>()
		, bind(&node_entry::id, _1), id)) != rb.end()) return false;

	return true;
}

void routing_table::node_failed(node_id const& id)
{
	int bucket_index = distance_exp(m_id, id);
	assert(bucket_index < (int)m_buckets.size());
	assert(bucket_index >= 0);
	bucket_t& b = m_buckets[bucket_index].first;
	bucket_t& rb = m_buckets[bucket_index].second;

	bucket_t::iterator i = std::find_if(b.begin(), b.end()
		, bind(std::equal_to<node_id>()
		, bind(&node_entry::id, _1), id));

	if (i == b.end()) return;
	
	// if messages to ourself fails, ignore it
	if (bucket_index == 0) return;

	if (rb.empty())
	{
		++i->fail_count;
		if (i->fail_count >= m_settings.max_fail_count)
		{
			b.erase(i);
			assert(m_lowest_active_bucket <= bucket_index);
			while (m_buckets[m_lowest_active_bucket].first.empty()
				&& m_lowest_active_bucket < 160)
			{
				++m_lowest_active_bucket;
			}
		}
		return;
	}

	b.erase(i);
	b.push_back(rb.back());
	rb.erase(rb.end() - 1);
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
	assert(bucket_index < (int)m_buckets.size());
	assert(bucket_index >= 0);
	bucket_t& b = m_buckets[bucket_index].first;

	bucket_t::iterator i = std::find_if(b.begin(), b.end()
		, bind(std::equal_to<node_id>()
		, bind(&node_entry::id, _1), id));

	bool ret = need_bootstrap();

	m_bucket_activity[bucket_index] = second_clock::universal_time();

	if (i != b.end())
	{
		// TODO: what do we do if we see a node with
		// the same id as a node at a different address?
//		assert(i->addr == addr);

		// we already have the node in our bucket
		// just move it to the back since it was
		// the last node we had any contact with
		// in this bucket
		b.erase(i);
		b.push_back(node_entry(id, addr));
//		TORRENT_LOG(table) << "replacing node: " << id << " " << addr;
		return ret;
	}

	// if the node was not present in our list
	// we will only insert it if there is room
	// for it, or if some of our nodes have gone
	// offline
	if ((int)b.size() < m_bucket_size)
	{
		b.push_back(node_entry(id, addr));
		// if bucket index is 0, the node is ourselves
		// don't updated m_lowest_active_bucket
		if (bucket_index < m_lowest_active_bucket
			&& bucket_index > 0)
			m_lowest_active_bucket = bucket_index;
//		TORRENT_LOG(table) << "inserting node: " << id << " " << addr;
		return ret;
	}

	// if there is no room, we look for nodes marked as stale
	// in the k-bucket. If we find one, we can replace it.
	// A node is considered stale if it has failed at least one
	// time. Here we choose the node that has failed most times.
	// If we don't find one, place this node in the replacement-
	// cache and replace any nodes that will fail in the future
	// with nodes from that cache.

	i = std::max_element(b.begin(), b.end()
		, bind(std::less<int>()
			, bind(&node_entry::fail_count, _1)
			, bind(&node_entry::fail_count, _2)));

	if (i != b.end() && i->fail_count > 0)
	{
		// i points to a node that has been marked
		// as stale. Replace it with this new one
		b.erase(i);
		b.push_back(node_entry(id, addr));
//		TORRENT_LOG(table) << "replacing stale node: " << id << " " << addr;
		return ret;
	}

	// if we don't have any identified stale nodes in
	// the bucket, and the bucket is full, we have to
	// cache this node and wait until some node fails
	// and then replace it.

	bucket_t& rb = m_buckets[bucket_index].second;

	i = std::find_if(rb.begin(), rb.end()
		, bind(std::equal_to<node_id>()
		, bind(&node_entry::id, _1), id));

	// if the node is already in the replacement bucket
	// just return.
	if (i != rb.end()) return ret;
	
	if ((int)rb.size() > m_bucket_size) rb.erase(rb.begin());
	rb.push_back(node_entry(id, addr));
//	TORRENT_LOG(table) << "inserting node in replacement cache: " << id << " " << addr;
	return ret;
}

bool routing_table::need_bootstrap() const
{
	for (const_iterator i = begin(); i != end(); ++i)
	{
		if (i->fail_count == 0) return false;
	}
	return true;
}

// fills the vector with the k nodes from our buckets that
// are nearest to the given id.
void routing_table::find_node(node_id const& target
	, std::vector<node_entry>& l, bool include_self, int count)
{
	l.clear();
	if (count == 0) count = m_bucket_size;
	l.reserve(count);

	int bucket_index = distance_exp(m_id, target);
	bucket_t& b = m_buckets[bucket_index].first;

	// copy all nodes that hasn't failed into the target
	// vector.
	std::remove_copy_if(b.begin(), b.end(), std::back_inserter(l)
		, bind(&node_entry::fail_count, _1));
	assert((int)l.size() <= count);

	if ((int)l.size() == count)
	{
		assert(std::count_if(l.begin(), l.end()
			, boost::bind(std::not_equal_to<int>()
				, boost::bind(&node_entry::fail_count, _1), 0)) == 0);
		return;
	}

	// if we didn't have enough nodes in that bucket
	// we have to reply with nodes from buckets closer
	// to us. i.e. all the buckets in the range
	// [0, bucket_index) if we are to include ourself
	// or [1, bucket_index) if not.
	bucket_t tmpb;
	for (int i = include_self?0:1; i < count; ++i)
	{
		bucket_t& b = m_buckets[i].first;
		std::remove_copy_if(b.begin(), b.end(), std::back_inserter(tmpb)
			, bind(&node_entry::fail_count, _1));
	}

	std::random_shuffle(tmpb.begin(), tmpb.end());
	size_t to_copy = (std::min)(m_bucket_size - l.size()
		, tmpb.size());
	std::copy(tmpb.begin(), tmpb.begin() + to_copy
		, std::back_inserter(l));
		
	assert((int)l.size() <= m_bucket_size);

	// return if we have enough nodes or if the bucket index
	// is the biggest index available (there are no more buckets)
	// to look in.
	if ((int)l.size() == count
		|| bucket_index == (int)m_buckets.size() - 1)
	{
		assert(std::count_if(l.begin(), l.end()
			, boost::bind(std::not_equal_to<int>()
				, boost::bind(&node_entry::fail_count, _1), 0)) == 0);
		return;
	}

	for (size_t i = bucket_index + 1; i < m_buckets.size(); ++i)
	{
		bucket_t& b = m_buckets[i].first;
	
		std::remove_copy_if(b.begin(), b.end(), std::back_inserter(l)
			, bind(&node_entry::fail_count, _1));
		if ((int)l.size() >= count)
		{
			l.erase(l.begin() + count, l.end());
			assert(std::count_if(l.begin(), l.end()
				, boost::bind(std::not_equal_to<int>()
					, boost::bind(&node_entry::fail_count, _1), 0)) == 0);
			return;
		}
	}
	assert((int)l.size() == count
		|| std::distance(l.begin(), l.end()) < m_bucket_size);
	assert((int)l.size() <= count);

	assert(std::count_if(l.begin(), l.end()
		, boost::bind(std::not_equal_to<int>()
			, boost::bind(&node_entry::fail_count, _1), 0)) == 0);
}

routing_table::iterator routing_table::begin() const
{
	return iterator(m_buckets.begin(), m_buckets.end());
}

routing_table::iterator routing_table::end() const
{
	return iterator(m_buckets.end(), m_buckets.end());
}

} } // namespace libtorrent::dht

