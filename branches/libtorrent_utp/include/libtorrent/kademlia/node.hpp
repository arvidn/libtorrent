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
#include <libtorrent/kademlia/find_data.hpp>

#include <libtorrent/io.hpp>
#include <libtorrent/session_settings.hpp>
#include <libtorrent/assert.hpp>
#include <libtorrent/thread.hpp>

#include <boost/cstdint.hpp>
#include <boost/optional.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/ref.hpp>
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

class traversal_algorithm;

struct key_desc_t
{
	char const* name;
	int type;
	int size;
	int flags;

	enum { optional = 1}; 
};

bool TORRENT_EXPORT verify_message(lazy_entry const* msg, key_desc_t const desc[], lazy_entry const* ret[]
	, int size , char* error, int error_size);

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

// this is the entry for a torrent that has been published
// in the DHT.
struct search_torrent_entry
{
	search_torrent_entry(): total_tag_points(0), total_name_points(0) {}

	// the tags of the torrent. The key of
	// this entry is the sha-1 hash of one of
	// these tags. The counter is the number of
	// times a tag has been included in a publish
	// call. The counters are periodically
	// decremented by a factor, so that the
	// popularity ratio between the tags is
	// maintained. The decrement is rounded down.
	std::map<std::string, int> tags;

	// this is the sum of all values in the tags
	// map. It is only an optimization to avoid
	// recalculating it constantly
	int total_tag_points;
	
	// the name of the torrent
	std::map<std::string, int> name;
	int total_name_points;

	// increase the popularity counters for this torrent
	void publish(std::string const& name, char const* in_tags[], int num_tags);

	// return a score of how well this torrent matches
	// the given set of tags. Each word in the string
	// (separated by a space) is considered a tag.
	// tags with 2 letters or fewer are ignored
	int match(char const* tags[], int num_tags) const;

	// this is called once every hour, and will
	// decrement the popularity counters of the
	// tags. Returns true if this entry should
	// be deleted
	bool tick();
	
	void get_name(std::string& t) const;
	void get_tags(std::string& t) const;
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
	announce_observer(boost::intrusive_ptr<traversal_algorithm> const& algo)
		: observer(algo)
	{}

	void reply(msg const&) { m_done = true; }
};

struct count_peers
{
	int& count;
	count_peers(int& c): count(c) {}
	void operator()(std::pair<libtorrent::dht::node_id
		, libtorrent::dht::torrent_entry> const& t)
	{
		count += t.second.peers.size();
	}
};
	
class node_impl : boost::noncopyable
{
typedef std::map<node_id, torrent_entry> table_t;
typedef std::map<std::pair<node_id, sha1_hash>, search_torrent_entry> search_table_t;
public:
	node_impl(libtorrent::aux::session_impl& ses
		, bool (*f)(void*, entry const&, udp::endpoint const&, int)
		, dht_settings const& settings, boost::optional<node_id> nid
		, void* userdata);

	virtual ~node_impl() {}

	void refresh(node_id const& id, find_data::nodes_callback const& f);
	void bootstrap(std::vector<udp::endpoint> const& nodes
		, find_data::nodes_callback const& f);
	void add_router_node(udp::endpoint router);
		
	void unreachable(udp::endpoint const& ep);
	void incoming(msg const& m);

	int num_torrents() const { return m_map.size(); }
	int num_peers() const
	{
		int ret = 0;
		std::for_each(m_map.begin(), m_map.end(), count_peers(ret));
		return ret;
	}

	void refresh();
	void refresh_bucket(int bucket);
	int bucket_size(int bucket);

	typedef routing_table::iterator iterator;
	
	iterator begin() const { return m_table.begin(); }
	iterator end() const { return m_table.end(); }

	node_id const& nid() const { return m_id; }

	boost::tuple<int, int> size() const{ return m_table.size(); }
	size_type num_global_nodes() const
	{ return m_table.num_global_nodes(); }

	int data_size() const { return int(m_map.size()); }

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	void print_state(std::ostream& os) const
	{ m_table.print_state(os); }
#endif

	void announce(sha1_hash const& info_hash, int listen_port
		, boost::function<void(std::vector<tcp::endpoint> const&)> f);

	bool verify_token(std::string const& token, char const* info_hash
		, udp::endpoint const& addr);

	std::string generate_token(udp::endpoint const& addr, char const* info_hash);
	
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
	bool lookup_peers(sha1_hash const& info_hash, entry& reply) const;
	bool lookup_torrents(sha1_hash const& target, entry& reply
		, char* tags) const;

	dht_settings const& m_settings;
	
	// the maximum number of peers to send in a get_peers
	// reply. Ordinary trackers usually limit this to 50.
	// 50 => 6 * 50 = 250 bytes + packet overhead
	int m_max_peers_reply;

private:
	typedef libtorrent::mutex mutex_t;
	mutex_t m_mutex;

	// this list must be destructed after the rpc manager
	// since it might have references to it
	std::set<traversal_algorithm*> m_running_requests;

	void incoming_request(msg const& h, entry& e);

	node_id m_id;

public:
	routing_table m_table;
	rpc_manager m_rpc;

private:
	table_t m_map;
	search_table_t m_search_map;
	
	ptime m_last_tracker_tick;

	// secret random numbers used to create write tokens
	int m_secret[2];

	libtorrent::aux::session_impl& m_ses;
	bool (*m_send)(void*, entry const&, udp::endpoint const&, int);
	void* m_userdata;
};


} } // namespace libtorrent::dht

#endif // NODE_HPP

