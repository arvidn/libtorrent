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

#ifndef NODE_HPP
#define NODE_HPP

#include <algorithm>
#include <map>
#include <set>

#include <libtorrent/config.hpp>
#include <libtorrent/kademlia/dht_storage.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/msg.hpp>
#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/put_data.hpp>
#include <libtorrent/kademlia/item.hpp>

#include <libtorrent/io.hpp>
#include <libtorrent/socket.hpp> // for udp::endpoint
#include <libtorrent/session_settings.hpp>
#include <libtorrent/assert.hpp>
#include <libtorrent/thread.hpp>
#include <libtorrent/bloom_filter.hpp>

#include <boost/cstdint.hpp>
#include <boost/ref.hpp>

#include "libtorrent/socket.hpp"

namespace libtorrent {
	class alert_manager;
	struct alert_dispatcher;
	class alert;
	struct counters;
	struct dht_routing_bucket;
}

namespace libtorrent { namespace dht
{

struct traversal_algorithm;
struct dht_observer;

void TORRENT_EXTRA_EXPORT write_nodes_entry(entry& r, nodes_t const& nodes);

struct null_type {};

class announce_observer : public observer
{
public:
	announce_observer(boost::intrusive_ptr<traversal_algorithm> const& algo
		, udp::endpoint const& ep, node_id const& id)
		: observer(algo, ep, id)
	{}

	void reply(msg const&) { flags |= flag_done; }
};

struct udp_socket_interface
{
	virtual bool has_quota() = 0;
	virtual bool send_packet(entry& e, udp::endpoint const& addr, int flags) = 0;
protected:
	~udp_socket_interface() {}
};

class TORRENT_EXTRA_EXPORT node : boost::noncopyable
{
public:
	node(udp_socket_interface* sock
		, libtorrent::dht_settings const& settings, node_id nid
		, dht_observer* observer, counters& cnt
		, dht_storage_constructor_type storage_constructor = dht_default_storage_constructor);

	~node();

	void update_node_id();

	void tick();
	void bootstrap(std::vector<udp::endpoint> const& nodes
		, find_data::nodes_callback const& f);
	void add_router_node(udp::endpoint router);

	void unreachable(udp::endpoint const& ep);
	void incoming(msg const& m);

#ifndef TORRENT_NO_DEPRECATE
	int num_torrents() const { return m_storage->num_torrents(); }
	int num_peers() const { return m_storage->num_peers(); }
#endif

	int bucket_size(int bucket);

	node_id const& nid() const { return m_id; }

	boost::tuple<int, int, int> size() const { return m_table.size(); }
	boost::int64_t num_global_nodes() const
	{ return m_table.num_global_nodes(); }

#ifndef TORRENT_NO_DEPRECATE
	int data_size() const { return int(m_storage->num_torrents()); }
#endif

#if defined TORRENT_DEBUG
	void print_state(std::ostream& os) const
	{ m_table.print_state(os); }
#endif

	enum flags_t { flag_seed = 1, flag_implied_port = 2 };
	void get_peers(sha1_hash const& info_hash
		, boost::function<void(std::vector<tcp::endpoint> const&)> dcallback
		, boost::function<void(std::vector<std::pair<node_entry, std::string> > const&)> ncallback
		, bool noseeds);
	void announce(sha1_hash const& info_hash, int listen_port, int flags
		, boost::function<void(std::vector<tcp::endpoint> const&)> f);

	void direct_request(udp::endpoint ep, entry& e
		, boost::function<void(msg const&)> f);

	void get_item(sha1_hash const& target, boost::function<void(item const&)> f);
	void get_item(char const* pk, std::string const& salt, boost::function<void(item const&, bool)> f);

	void put_item(sha1_hash const& target, entry const& data, boost::function<void(int)> f);
	void put_item(char const* pk, std::string const& salt
		, boost::function<void(item const&, int)> f
		, boost::function<void(item&)> data_cb);

	bool verify_token(std::string const& token, char const* info_hash
		, udp::endpoint const& addr) const;

	std::string generate_token(udp::endpoint const& addr, char const* info_hash);

	// the returned time is the delay until connection_timeout()
	// should be called again the next time
	time_duration connection_timeout();

	// generates a new secret number used to generate write tokens
	void new_write_key();

	// pings the given node, and adds it to
	// the routing table if it respons and if the
	// bucket is not full.
	void add_node(udp::endpoint node);

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

	void status(std::vector<dht_routing_bucket>& table
		, std::vector<dht_lookup>& requests);

	void update_stats_counters(counters& c) const;

#ifndef TORRENT_NO_DEPRECATE
	void status(libtorrent::session_status& s);
#endif

	libtorrent::dht_settings const& settings() const { return m_settings; }
	counters& stats_counters() const { return m_counters; }

	dht_observer* observer() const { return m_observer; }
private:

	void send_single_refresh(udp::endpoint const& ep, int bucket
		, node_id const& id = node_id());
	void lookup_peers(sha1_hash const& info_hash, entry& reply
		, bool noseed, bool scrape) const;
	bool lookup_torrents(sha1_hash const& target, entry& reply
		, char* tags) const;

	libtorrent::dht_settings const& m_settings;

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
	dht_observer* m_observer;

	time_point m_last_tracker_tick;

	// the last time we issued a bootstrap or a refresh on our own ID, to expand
	// the routing table buckets close to us.
	time_point m_last_self_refresh;

	// secret random numbers used to create write tokens
	int m_secret[2];

	udp_socket_interface* m_sock;
	counters& m_counters;

	boost::scoped_ptr<dht_storage_interface> m_storage;
};

} } // namespace libtorrent::dht

#endif // NODE_HPP
