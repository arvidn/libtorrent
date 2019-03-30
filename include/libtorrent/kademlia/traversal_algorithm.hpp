/*

Copyright (c) 2006-2018, Arvid Norberg & Daniel Wallin
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

#ifndef TRAVERSAL_ALGORITHM_050324_HPP
#define TRAVERSAL_ALGORITHM_050324_HPP

#include <vector>
#include <set>
#include <memory>

#include <libtorrent/fwd.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/address.hpp>
#include <libtorrent/flags.hpp>
#include <libtorrent/bdecode.hpp>

namespace libtorrent {

namespace dht {

class node;
struct node_endpoint;

using traversal_flags_t = libtorrent::flags::bitfield_flag<std::uint8_t, struct traversal_flags_tag>;

// this class may not be instantiated as a stack object
struct TORRENT_EXTRA_EXPORT traversal_algorithm
	: std::enable_shared_from_this<traversal_algorithm>
{
	void traverse(node_id const& id, udp::endpoint const& addr);
	void finished(observer_ptr o);

	static constexpr traversal_flags_t prevent_request = 0_bit;
	static constexpr traversal_flags_t short_timeout = 1_bit;

	void failed(observer_ptr o, traversal_flags_t flags = {});
	virtual ~traversal_algorithm();
	void status(dht_lookup& l);

	virtual char const* name() const;
	virtual void start();

	node_id const& target() const { return m_target; }

	void resort_result(observer*);
	void add_entry(node_id const& id, udp::endpoint const& addr, observer_flags_t flags);

	traversal_algorithm(node& dht_node, node_id const& target);
	traversal_algorithm(traversal_algorithm const&) = delete;
	traversal_algorithm& operator=(traversal_algorithm const&) = delete;
	int invoke_count() const { TORRENT_ASSERT(m_invoke_count >= 0); return m_invoke_count; }
	int branch_factor() const { TORRENT_ASSERT(m_branch_factor >= 0); return m_branch_factor; }

	node& get_node() const { return m_node; }

#ifndef TORRENT_DISABLE_LOGGING
	std::uint32_t id() const { return m_id; }
#endif

protected:

	std::shared_ptr<traversal_algorithm> self()
	{ return shared_from_this(); }

	// returns true if we're done
	bool add_requests();

	void add_router_entries();
	void init();

	virtual void done();
	// should construct an algorithm dependent
	// observer in ptr.
	virtual observer_ptr new_observer(udp::endpoint const& ep
		, node_id const& id);

	virtual bool invoke(observer_ptr) { return false; }

	int num_responses() const { return m_responses; }
	int num_timeouts() const { return m_timeouts; }

	node& m_node;

	// this vector is sorted by node-id distance from our node id. Closer nodes
	// are earlier in the vector. However, not the entire vector is necessarily
	// sorted, the tail of the vector may contain nodes out-of-order. This is
	// used when bootstrapping. The ``m_sorted_results`` member indicates how
	// many of the first elements are sorted.
	std::vector<observer_ptr> m_results;

	int num_sorted_results() const { return m_sorted_results; }

private:

	node_id const m_target;
	std::int8_t m_invoke_count = 0;
	std::int8_t m_branch_factor = 3;
	// the number of elements at the beginning of m_results that are sorted by
	// node_id.
	std::int8_t m_sorted_results = 0;
	std::int16_t m_responses = 0;
	std::int16_t m_timeouts = 0;

	// set to true when done() is called, and will prevent adding new results, as
	// they would never be serviced and the whole traversal algorithm would stall
	// and leak
	bool m_done = false;

#ifndef TORRENT_DISABLE_LOGGING
	// this is a unique ID for this specific traversal_algorithm instance,
	// just used for logging
	std::uint32_t m_id;
#endif

	// the IP addresses of the nodes in m_results
	std::set<std::uint32_t> m_peer4_prefixes;
	std::set<std::uint64_t> m_peer6_prefixes;
#ifndef TORRENT_DISABLE_LOGGING
	void log_timeout(observer_ptr const& o, char const* prefix) const;
#endif
};

void look_for_nodes(char const* nodes_key, udp const& protocol
	, bdecode_node const& r, std::function<void(node_endpoint const&)> f);

struct traversal_observer : observer
{
	traversal_observer(
		std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id)
		: observer(std::move(algorithm), ep, id)
	{}

	// parses out "nodes" and keeps traversing
	void reply(msg const&) override;
};

} // namespace dht
} // namespace libtorrent

#endif // TRAVERSAL_ALGORITHM_050324_HPP
