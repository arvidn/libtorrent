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

#ifndef NODE_HPP
#define NODE_HPP

#include <map>
#include <set>
#include <mutex>
#include <cstdint>

#include <libtorrent/config.hpp>
#include <libtorrent/kademlia/dht_storage.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/item.hpp>

#include <libtorrent/socket.hpp> // for udp::endpoint
#include <libtorrent/string_view.hpp>

namespace libtorrent {
	struct counters;
	struct dht_routing_bucket;
	struct dht_settings;
}

namespace libtorrent { namespace dht
{

struct traversal_algorithm;
struct dht_observer;
struct msg;

TORRENT_EXTRA_EXPORT entry write_nodes_entry(std::vector<node_entry> const& nodes);

class announce_observer : public observer
{
public:
	announce_observer(std::shared_ptr<traversal_algorithm> const& algo
		, udp::endpoint const& ep, node_id const& id)
		: observer(algo, ep, id)
	{}

	void reply(msg const&) { flags |= flag_done; }
};

struct udp_socket_interface
{
	virtual bool has_quota() = 0;
	virtual bool send_packet(entry& e, udp::endpoint const& addr) = 0;
protected:
	~udp_socket_interface() {}
};

class TORRENT_EXTRA_EXPORT node : boost::noncopyable
{
public:
	node(udp proto, udp_socket_interface* sock
		, libtorrent::dht_settings const& settings
		, node_id const& nid
		, dht_observer* observer, counters& cnt
		, std::map<std::string, node*> const& nodes
		, dht_storage_interface& storage);

	~node();

	void update_node_id();

	void tick();
	void bootstrap(std::vector<udp::endpoint> const& nodes
		, find_data::nodes_callback const& f);
	void add_router_node(udp::endpoint const& router);

	void unreachable(udp::endpoint const& ep);
	void incoming(msg const& m);

#ifndef TORRENT_NO_DEPRECATE
	int num_torrents() const { return int(m_storage.num_torrents()); }
	int num_peers() const { return int(m_storage.num_peers()); }
#endif

	int bucket_size(int bucket);

	node_id const& nid() const { return m_id; }

	std::tuple<int, int, int> size() const { return m_table.size(); }
	std::int64_t num_global_nodes() const
	{ return m_table.num_global_nodes(); }

#ifndef TORRENT_NO_DEPRECATE
	int data_size() const { return int(m_storage.num_torrents()); }
#endif

	enum flags_t { flag_seed = 1, flag_implied_port = 2 };
	void get_peers(sha1_hash const& info_hash
		, std::function<void(std::vector<tcp::endpoint> const&)> dcallback
		, std::function<void(std::vector<std::pair<node_entry, std::string>> const&)> ncallback
		, bool noseeds);
	void announce(sha1_hash const& info_hash, int listen_port, int flags
		, std::function<void(std::vector<tcp::endpoint> const&)> f);

	void direct_request(udp::endpoint const& ep, entry& e
		, std::function<void(msg const&)> f);

	void get_item(sha1_hash const& target, std::function<void(item const&)> f);
	void get_item(public_key const& pk, std::string const& salt, std::function<void(item const&, bool)> f);

	void put_item(sha1_hash const& target, entry const& data, std::function<void(int)> f);
	void put_item(public_key const& pk, std::string const& salt
		, std::function<void(item const&, int)> f
		, std::function<void(item&)> data_cb);

	bool verify_token(string_view token, sha1_hash const& info_hash
		, udp::endpoint const& addr) const;

	std::string generate_token(udp::endpoint const& addr, sha1_hash const& info_hash);

	// the returned time is the delay until connection_timeout()
	// should be called again the next time
	time_duration connection_timeout();

	// generates a new secret number used to generate write tokens
	void new_write_key();

	// pings the given node, and adds it to
	// the routing table if it response and if the
	// bucket is not full.
	void add_node(udp::endpoint const& node);

	void replacement_cache(bucket_t& nodes) const
	{ m_table.replacement_cache(nodes); }

	int branch_factor() const { return m_settings.search_branching; }

	void add_traversal_algorithm(traversal_algorithm* a)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_running_requests.insert(a);
	}

	void remove_traversal_algorithm(traversal_algorithm* a)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_running_requests.erase(a);
	}

	void status(std::vector<dht_routing_bucket>& table
		, std::vector<dht_lookup>& requests);

	std::tuple<int, int, int> get_stats_counters() const;

#ifndef TORRENT_NO_DEPRECATE
	void status(libtorrent::session_status& s);
#endif

	libtorrent::dht_settings const& settings() const { return m_settings; }
	counters& stats_counters() const { return m_counters; }

	dht_observer* observer() const { return m_observer; }

	udp protocol() const { return m_protocol.protocol; }
	char const* protocol_family_name() const { return m_protocol.family_name; }
	char const* protocol_nodes_key() const { return m_protocol.nodes_key; }

	bool native_address(udp::endpoint const& ep) const
	{ return ep.protocol().family() == m_protocol.protocol.family(); }
	bool native_address(tcp::endpoint const& ep) const
	{ return ep.protocol().family() == m_protocol.protocol.family(); }
	bool native_address(address const& addr) const
	{
		return (addr.is_v4() && m_protocol.protocol == m_protocol.protocol.v4())
			|| (addr.is_v6() && m_protocol.protocol == m_protocol.protocol.v6());
	}

private:
	void send_single_refresh(udp::endpoint const& ep, int bucket
		, node_id const& id = node_id());
	bool lookup_peers(sha1_hash const& info_hash, entry& reply
		, bool noseed, bool scrape, address const& requester) const;

	libtorrent::dht_settings const& m_settings;

	std::mutex m_mutex;

	// this list must be destructed after the rpc manager
	// since it might have references to it
	std::set<traversal_algorithm*> m_running_requests;

	void incoming_request(msg const& h, entry& e);

	void write_nodes_entries(sha1_hash const& info_hash
		, bdecode_node const& want, entry& r);

	node_id m_id;

public:
	routing_table m_table;
	rpc_manager m_rpc;

private:
	struct protocol_descriptor
	{
		udp protocol;
		char const* family_name;
		char const* nodes_key;
	};

	static protocol_descriptor const& map_protocol_to_descriptor(udp protocol);

	std::map<std::string, node*> const& m_nodes;

	dht_observer* m_observer;

	protocol_descriptor const& m_protocol;

	time_point m_last_tracker_tick;

	// the last time we issued a bootstrap or a refresh on our own ID, to expand
	// the routing table buckets close to us.
	time_point m_last_self_refresh;

	// secret random numbers used to create write tokens
	int m_secret[2];

	udp_socket_interface* m_sock;
	counters& m_counters;

	dht_storage_interface& m_storage;
};

} } // namespace libtorrent::dht

#endif // NODE_HPP
