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

#ifndef NODE_HPP
#define NODE_HPP

#include <algorithm>
#include <map>
#include <set>

#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include <libtorrent/io.hpp>
#include <libtorrent/session_settings.hpp>
#include <libtorrent/assert.hpp>

#include <boost/cstdint.hpp>
#include <boost/optional.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/ref.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/optional.hpp>

#include "libtorrent/socket.hpp"

namespace libtorrent {
	
	namespace aux { struct session_impl; }
	struct session_status;

}

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(node);
#endif

struct traversal_algorithm;

// this is the entry for every peer
// the timestamp is there to make it possible
// to remove stale peers
struct peer_entry
{
	tcp::endpoint addr;
	ptime added;
};

// this is a group. It contains a set of group members
struct torrent_entry
{
	std::set<peer_entry> peers;
};

inline bool operator<(peer_entry const& lhs, peer_entry const& rhs)
{
	return lhs.addr.address() == rhs.addr.address()
		? lhs.addr.port() < rhs.addr.port()
		: lhs.addr.address() < rhs.addr.address();
}

struct null_type {};

class announce_observer : public observer
{
public:
	announce_observer(boost::pool<>& allocator
		, sha1_hash const& info_hash
		, int listen_port
		, std::string const& write_token)
		: observer(allocator)
		, m_info_hash(info_hash)
		, m_listen_port(listen_port)
		, m_token(write_token)
	{}

	void send(msg& m)
	{
		m.port = m_listen_port;
		m.info_hash = m_info_hash;
		m.write_token = m_token;
	}

	void timeout() {}
	void reply(msg const&) {}
	void abort() {}

private:
	sha1_hash m_info_hash;
	int m_listen_port;
	std::string m_token;
};

class node_impl : boost::noncopyable
{
typedef std::map<node_id, torrent_entry> table_t;
public:
	node_impl(libtorrent::aux::session_impl& ses, boost::function<void(msg const&)> const& f
		, dht_settings const& settings, boost::optional<node_id> nid);

	virtual ~node_impl() {}

	void refresh(node_id const& id, boost::function0<void> f);
	void bootstrap(std::vector<udp::endpoint> const& nodes
		, boost::function0<void> f);
	void find_node(node_id const& id, boost::function<
	void(std::vector<node_entry> const&)> f);
	void add_router_node(udp::endpoint router);
		
	void unreachable(udp::endpoint const& ep);
	void incoming(msg const& m);

	void refresh();
	void refresh_bucket(int bucket);
	int bucket_size(int bucket);

	typedef routing_table::iterator iterator;
	
	iterator begin() const { return m_table.begin(); }
	iterator end() const { return m_table.end(); }

	typedef table_t::iterator data_iterator;

	node_id const& nid() const { return m_id; }

	boost::tuple<int, int> size() const{ return m_table.size(); }
	size_type num_global_nodes() const
	{ return m_table.num_global_nodes(); }

	data_iterator begin_data() { return m_map.begin(); }
	data_iterator end_data() { return m_map.end(); }
	int data_size() const { return int(m_map.size()); }

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	void print_state(std::ostream& os) const
	{ m_table.print_state(os); }
#endif

	void announce(sha1_hash const& info_hash, int listen_port
		, boost::function<void(std::vector<tcp::endpoint> const&)> f);

	bool verify_token(msg const& m);
	std::string generate_token(msg const& m);
	
	// the returned time is the delay until connection_timeout()
	// should be called again the next time
	time_duration connection_timeout();
	time_duration refresh_timeout();

	// generates a new secret number used to generate write tokens
	void new_write_key();

	// pings the given node, and adds it to
	// the routing table if it respons and if the
	// bucket is not full.
	void add_node(udp::endpoint node);

	void replacement_cache(bucket_t& nodes) const
	{ m_table.replacement_cache(nodes); }

	int branch_factor() const { return m_settings.search_branching; }

	void add_traversal_algorithm(traversal_algorithm* a)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_running_requests.insert(a);
	}

	void remove_traversal_algorithm(traversal_algorithm* a)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_running_requests.erase(a);
	}

	void status(libtorrent::session_status& s);

protected:
	// is called when a find data request is received. Should
	// return false if the data is not stored on this node. If
	// the data is stored, it should be serialized into 'data'.
	bool on_find(msg const& m, std::vector<tcp::endpoint>& peers) const;

	// this is called when a store request is received. The data
	// is store-parameters and the data to be stored.
	void on_announce(msg const& m, msg& reply);

	dht_settings const& m_settings;
	
	// the maximum number of peers to send in a get_peers
	// reply. Ordinary trackers usually limit this to 50.
	// 50 => 6 * 50 = 250 bytes + packet overhead
	int m_max_peers_reply;

private:
	typedef boost::mutex mutex_t;
	mutex_t m_mutex;

	// this list must be destructed after the rpc manager
	// since it might have references to it
	std::set<traversal_algorithm*> m_running_requests;

	void incoming_request(msg const& h);

	node_id m_id;

public:
	routing_table m_table;
	rpc_manager m_rpc;

private:
	table_t m_map;
	
	ptime m_last_tracker_tick;

	// secret random numbers used to create write tokens
	int m_secret[2];

	libtorrent::aux::session_impl& m_ses;
};


} } // namespace libtorrent::dht

#endif // NODE_HPP

