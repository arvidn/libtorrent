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

#ifndef ROUTING_TABLE_HPP
#define ROUTING_TABLE_HPP

#include <vector>
#include <boost/cstdint.hpp>

#include <boost/utility.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/array.hpp>
#include <boost/noncopyable.hpp>
#include <set>

#include <libtorrent/kademlia/logging.hpp>

#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/node_entry.hpp>
#include <libtorrent/session_settings.hpp>
#include <libtorrent/size_type.hpp>
#include <libtorrent/assert.hpp>
#include <libtorrent/ptime.hpp>

namespace libtorrent
{
	struct session_status;
}

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(table);
#endif

	
typedef std::vector<node_entry> bucket_t;

struct routing_table_node
{
	bucket_t replacements;
	bucket_t live_nodes;
};

// differences in the implementation from the description in
// the paper:
//
// * Nodes are not marked as being stale, they keep a counter
// 	that tells how many times in a row they have failed. When
// 	a new node is to be inserted, the node that has failed
// 	the most times is replaced. If none of the nodes in the
// 	bucket has failed, then it is put in the replacement
// 	cache (just like in the paper).

class TORRENT_EXTRA_EXPORT routing_table : boost::noncopyable
{
public:
	typedef std::vector<routing_table_node> table_t;

	routing_table(node_id const& id, int bucket_size
		, dht_settings const& settings);

	void status(session_status& s) const;

	void node_failed(node_id const& id, udp::endpoint const& ep);
	
	// adds an endpoint that will never be added to
	// the routing table
	void add_router_node(udp::endpoint router);

	// iterates over the router nodes added
	typedef std::set<udp::endpoint>::const_iterator router_iterator;
	router_iterator router_begin() const { return m_router_nodes.begin(); }
	router_iterator router_end() const { return m_router_nodes.end(); }

	enum add_node_status_t {
		failed_to_add = 0,
		node_added,
		need_bucket_split
	};
	add_node_status_t add_node_impl(node_entry e);

	bool add_node(node_entry e);

	// this function is called every time the node sees
	// a sign of a node being alive. This node will either
	// be inserted in the k-buckets or be moved to the top
	// of its bucket.
	bool node_seen(node_id const& id, udp::endpoint ep, int rtt);

	// this may add a node to the routing table and mark it as
	// not pinged. If the bucket the node falls into is full,
	// the node will be ignored.
	void heard_about(node_id const& id, udp::endpoint const& ep);
	
	node_entry const* next_refresh();

	enum
	{
		// nodes that have not been pinged are considered failed by this flag
		include_failed = 1
	};

	// fills the vector with the count nodes from our buckets that
	// are nearest to the given id.
	void find_node(node_id const& id, std::vector<node_entry>& l
		, int options, int count = 0);
	void remove_node(node_entry* n
		, table_t::iterator bucket) ;
	
	int bucket_size(int bucket) const
	{
		int num_buckets = m_buckets.size();
		if (num_buckets == 0) return 0;
		if (bucket < num_buckets) bucket = num_buckets - 1;
		table_t::const_iterator i = m_buckets.begin();
		std::advance(i, bucket);
		return (int)i->live_nodes.size();
	}

	void for_each_node(void (*)(void*, node_entry const&)
		, void (*)(void*, node_entry const&), void* userdata) const;

	int bucket_size() const { return m_bucket_size; }

	// returns the number of nodes in the main buckets, number of nodes in the
	// replacement buckets and the number of nodes in the main buckets that have
	// been pinged and confirmed up
	boost::tuple<int, int, int> size() const;

	size_type num_global_nodes() const;

	// the number of bits down we have full buckets
	// i.e. essentially the number of full buckets
	// we have
	int depth() const;
	
	int num_active_buckets() const { return m_buckets.size(); }
	
	void replacement_cache(bucket_t& nodes) const;

#if defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG
	// used for debug and monitoring purposes. This will print out
	// the state of the routing table to the given stream
	void print_state(std::ostream& os) const;
#endif

	int bucket_limit(int bucket) const;

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
#endif

private:

	table_t::iterator find_bucket(node_id const& id);

	void split_bucket();

	// return a pointer the node_entry with the given endpoint
	// or 0 if we don't have such a node. Both the address and the
	// port has to match
	node_entry* find_node(udp::endpoint const& ep
		, routing_table::table_t::iterator* bucket);

	dht_settings const& m_settings;

	// constant called k in paper
	int m_bucket_size;
	
	// (k-bucket, replacement cache) pairs
	// the first entry is the bucket the furthest
	// away from our own ID. Each time the bucket
	// closest to us (m_buckets.back()) has more than
	// bucket size nodes in it, another bucket is
	// added to the end and it's split up between them
	table_t m_buckets;

	node_id m_id; // our own node id

	// the last seen depth (i.e. levels in the routing table)
	// it's mutable because it's updated by depth(), which is const
	mutable int m_depth;

	// the last time we refreshed our own bucket
	// refreshed every 15 minutes
	mutable ptime m_last_self_refresh;
	
	// this is a set of all the endpoints that have
	// been identified as router nodes. They will
	// be used in searches, but they will never
	// be added to the routing table.
	std::set<udp::endpoint> m_router_nodes;

	// these are all the IPs that are in the routing
	// table. It's used to only allow a single entry
	// per IP in the whole table. Currently only for
	// IPv4
	std::multiset<address_v4::bytes_type> m_ips;
};

} } // namespace libtorrent::dht

#endif // ROUTING_TABLE_HPP

