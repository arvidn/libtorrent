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

#ifndef ROUTING_TABLE_HPP
#define ROUTING_TABLE_HPP

#include <vector>
#include <deque>
#include <boost/cstdint.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/iterator/iterator_facade.hpp>
#include <boost/iterator/iterator_categories.hpp>
#include <boost/utility.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/array.hpp>
#include <set>

#include <libtorrent/kademlia/logging.hpp>

#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/node_entry.hpp>
#include <libtorrent/session_settings.hpp>

namespace pt = boost::posix_time;

namespace libtorrent { namespace dht
{

using asio::ip::udp;

//TORRENT_DECLARE_LOG(table);
	
typedef std::deque<node_entry> bucket_t;

// differences in the implementation from the description in
// the paper:
//
// * The routing table tree is not allocated dynamically, there
// 	are always 160 buckets.
// * Nodes are not marked as being stale, they keep a counter
// 	that tells how many times in a row they have failed. When
// 	a new node is to be inserted, the node that has failed
// 	the most times is replaced. If none of the nodes in the
// 	bucket has failed, then it is put in the replacement
// 	cache (just like in the paper).

class routing_table;

namespace aux
{

	// Iterates over a flattened routing_table structure.
	class routing_table_iterator
	: public boost::iterator_facade<
		routing_table_iterator
		, node_entry const
		, boost::forward_traversal_tag
		>
	{
	public:
		routing_table_iterator()
		{
		}

	private:
		friend class libtorrent::dht::routing_table;
		friend class boost::iterator_core_access;

		typedef boost::array<std::pair<bucket_t, bucket_t>, 160>::const_iterator
			bucket_iterator_t;

		routing_table_iterator(
			bucket_iterator_t begin
			, bucket_iterator_t end)
			: m_bucket_iterator(begin)
			, m_bucket_end(end)
			, m_iterator(begin != end ? begin->first.begin() : bucket_t::iterator())
		{
			if (m_bucket_iterator == m_bucket_end) return;
			while (m_iterator == m_bucket_iterator->first.end())
			{
				if (++m_bucket_iterator == m_bucket_end)
					break;
				m_iterator = m_bucket_iterator->first.begin();
			}
		}

		bool equal(routing_table_iterator const& other) const
		{
			return m_bucket_iterator == other.m_bucket_iterator
				&& (m_bucket_iterator == m_bucket_end
					|| m_iterator == other.m_iterator);
		}

		void increment()
		{
			assert(m_bucket_iterator != m_bucket_end);
			++m_iterator;
			while (m_iterator == m_bucket_iterator->first.end())
			{
				if (++m_bucket_iterator == m_bucket_end)
					break;
				m_iterator = m_bucket_iterator->first.begin();
			}
		}

		node_entry const& dereference() const
		{
			assert(m_bucket_iterator != m_bucket_end);
			return *m_iterator;
		}

		bucket_iterator_t m_bucket_iterator;
		bucket_iterator_t m_bucket_end;
		bucket_t::const_iterator m_iterator;
	};

} // namespace aux

class routing_table
{
public:
	typedef aux::routing_table_iterator iterator;
	typedef iterator const_iterator;

	routing_table(node_id const& id, int bucket_size
		, dht_settings const& settings);

	void node_failed(node_id const& id);
	
	// adds an endpoint that will never be added to
	// the routing table
	void add_router_node(udp::endpoint router);

	// iterates over the router nodes added
	typedef std::set<udp::endpoint>::const_iterator router_iterator;
	router_iterator router_begin() const { return m_router_nodes.begin(); }
	router_iterator router_end() const { return m_router_nodes.end(); }

	// this function is called every time the node sees
	// a sign of a node being alive. This node will either
	// be inserted in the k-buckets or be moved to the top
	// of its bucket.
	bool node_seen(node_id const& id, udp::endpoint addr);
	
	// returns time when the given bucket needs another refresh.
	// if the given bucket is empty but there are nodes
	// in a bucket closer to us, or if the bucket is non-empty and
	// the time from the last activity is more than 15 minutes
	boost::posix_time::ptime next_refresh(int bucket);

	// fills the vector with the count nodes from our buckets that
	// are nearest to the given id.
	void find_node(node_id const& id, std::vector<node_entry>& l
		, bool include_self, int count = 0);
	
	// returns true if the given node would be placed in a bucket
	// that is not full. If the node already exists in the table
	// this function returns false
	bool need_node(node_id const& id);
	
	// this will set the given bucket's latest activity
	// to the current time
	void touch_bucket(int bucket);
	
	int bucket_size(int bucket)
	{
		assert(bucket >= 0 && bucket < 160);
		return (int)m_buckets[bucket].first.size();
	}
	int bucket_size() const { return m_bucket_size; }

	iterator begin() const;
	iterator end() const;

	boost::tuple<int, int> size() const;
	
	// returns true if there are no working nodes
	// in the routing table
	bool need_bootstrap() const;
	
	void replacement_cache(bucket_t& nodes) const;

	// used for debug and monitoring purposes. This will print out
	// the state of the routing table to the given stream
	void print_state(std::ostream& os) const;
	
private:

	// constant called k in paper
	int m_bucket_size;
	
	dht_settings const& m_settings;

	// 160 (k-bucket, replacement cache) pairs
	typedef boost::array<std::pair<bucket_t, bucket_t>, 160> table_t;
	table_t m_buckets;
	// timestamps of the last activity in each bucket
	typedef boost::array<boost::posix_time::ptime, 160> table_activity_t;
	table_activity_t m_bucket_activity;
	node_id m_id; // our own node id
	
	// this is a set of all the endpoints that have
	// been identified as router nodes. They will
	// be used in searches, but they will never
	// be added to the routing table.
	std::set<udp::endpoint> m_router_nodes;
	
	// this is the lowest bucket index with nodes in it
	int m_lowest_active_bucket;
};

} } // namespace libtorrent::dht

#endif // ROUTING_TABLE_HPP

