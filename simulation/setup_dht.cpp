/*

Copyright (c) 2014-2015, Arvid Norberg
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

#include "libtorrent/session_settings.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "setup_transfer.hpp"
#include <boost/bind.hpp>
#include <memory> // for unique_ptr
#include <random>
#include "libtorrent/socket_io.hpp" // print_endpoint
#include "libtorrent/random.hpp"
#include "libtorrent/crc32c.hpp"
#include "libtorrent/alert_types.hpp" // for dht_routing_bucket

#include "setup_dht.hpp"

namespace lt = libtorrent;
using namespace sim;
using namespace libtorrent;

namespace {

	lt::time_point start_time;

	// this is the IP address assigned to node 'idx'
	asio::ip::address addr_from_int(int idx)
	{
		return asio::ip::address_v4(lt::random());
	}

	// this is the node ID assigned to node 'idx'
	dht::node_id id_from_addr(lt::address const& addr)
	{
		return dht::generate_id(addr);
	}

} // anonymous namespace

struct dht_node final : lt::dht::udp_socket_interface
{
	enum flags_t
	{
		add_dead_nodes = 1
	};

	dht_node(sim::simulation& sim, lt::dht_settings const& sett, lt::counters& cnt
		, int idx, std::uint32_t flags)
		: m_io_service(sim, addr_from_int(idx))
#if LIBSIMULATOR_USE_MOVE
		, m_socket(m_io_service)
		, m_dht(this, sett, id_from_addr(m_io_service.get_ips().front())
			, nullptr, cnt)
#else
		, m_socket(new asio::ip::udp::socket(m_io_service))
		, m_dht(new lt::dht::node(this, sett, id_from_addr(m_io_service.get_ips().front())
			, nullptr, cnt))
#endif
		, m_add_dead_nodes(flags & add_dead_nodes)
	{
		error_code ec;
		sock().open(asio::ip::udp::v4());
		sock().bind(asio::ip::udp::endpoint(lt::address_v4::any(), 6881));
		sock().non_blocking(true);

		sock().async_receive_from(asio::mutable_buffers_1(m_buffer, sizeof(m_buffer))
			, m_ep, boost::bind(&dht_node::on_read, this, _1, _2));
	}

#if LIBSIMULATOR_USE_MOVE
	// This type is not copyable, because the socket and the dht node is not
	// copyable.
	dht_node(dht_node const&) = delete;
	dht_node& operator=(dht_node const&) = delete;

	// it's also not movable, because it passes in its this-pointer to the async
	// receive function, which pins this object down. However, std::vector cannot
	// hold non-movable and non-copyable types. Instead, pretend that it's
	// movable and make sure it never needs to be moved (for instance, by
	// reserving space in the vector before emplacing any nodes).
	dht_node(dht_node&& n) noexcept
		: m_socket(std::move(n.m_socket))
		, m_dht(this, n.m_dht.settings(), n.m_dht.nid()
			, n.m_dht.observer(), n.m_dht.stats_counters())
	{
		assert(false && "dht_node is not movable");
		throw std::runtime_error("dht_node is not movable");
	}
	dht_node& operator=(dht_node&&)
		noexcept
	{
		assert(false && "dht_node is not movable");
		throw std::runtime_error("dht_node is not movable");
	}
#endif

	void on_read(lt::error_code const& ec, std::size_t bytes_transferred)
	{
		if (ec) return;

		using libtorrent::entry;
		using libtorrent::bdecode;

		int pos;
		error_code err;

		// since the simulation is single threaded, we can get away with just
		// allocating a single of these
		static bdecode_node msg;
		int const ret = bdecode(m_buffer, m_buffer + bytes_transferred, msg, err, &pos, 10, 500);
		if (ret != 0) return;

		if (msg.type() != bdecode_node::dict_t) return;

		libtorrent::dht::msg m(msg, m_ep);
		dht().incoming(m);

		sock().async_receive_from(asio::mutable_buffers_1(m_buffer, sizeof(m_buffer))
			, m_ep, boost::bind(&dht_node::on_read, this, _1, _2));
	}

	bool has_quota() override { return true; }
	bool send_packet(entry& e, udp::endpoint const& addr, int flags) override
	{
		// since the simulaton is single threaded, we can get away with allocating
		// just a single send buffer
		static std::vector<char> send_buf;

		send_buf.clear();
		bencode(std::back_inserter(send_buf), e);
		error_code ec;

		sock().send_to(boost::asio::const_buffers_1(send_buf.data(), int(send_buf.size())), addr);
		return true;
	}

	// the node_id and IP address of this node
	std::pair<dht::node_id, lt::udp::endpoint> node_info() const
	{
		return std::make_pair(dht().nid(), lt::udp::endpoint(m_io_service.get_ips().front(), 6881));
	}

	void bootstrap(std::vector<std::pair<dht::node_id, lt::udp::endpoint>> const& nodes)
	{
		// we don't want to tell every node about every other node. That's way too
		// expensive. instead. pick a random subset of nodes proportionate to the
		// bucket it would fall into

		dht::node_id id = dht().nid();

		// the number of slots left per bucket
		std::array<int, 160> nodes_per_bucket;
		nodes_per_bucket.fill(8);

		// when we use the larger routing table, the low buckets are larger
		nodes_per_bucket[0] = 128;
		nodes_per_bucket[1] = 64;
		nodes_per_bucket[2] = 32;
		nodes_per_bucket[3] = 16;

		for (auto const& n : nodes)
		{
			if (n.first == id) continue;
			int const bucket = 159 - dht::distance_exp(id, n.first);

/*			printf("%s ^ %s = %s %d\n"
				, to_hex(id.to_string()).c_str()
				, to_hex(n.first.to_string()).c_str()
				, to_hex(dht::distance(id, n.first).to_string()).c_str()
				, bucket);
*/
			// there are no more slots in this bucket, just move ont
			if (nodes_per_bucket[bucket] == 0) continue;
			--nodes_per_bucket[bucket];
			bool const added = dht().m_table.node_seen(n.first, n.second, (lt::random() % 300) + 10);
			TORRENT_ASSERT(added);
			if (m_add_dead_nodes)
			{
				// generate a random node ID that would fall in `bucket`
				dht::node_id const mask = dht::generate_prefix_mask(bucket + 1);
				dht::node_id target = dht::generate_random_id() & ~mask;
				target |= id & mask;
				dht().m_table.node_seen(target, rand_udp_ep(), (lt::random() % 300) + 10);
			}
		}
/*
		for (int i = 0; i < 40; ++i)
		{
			printf("%d ", nodes_per_bucket[i]);
		}
		printf("\n");
*/
//#error add invalid IPs as well, to simulate churn
	}

	void stop()
	{
		sock().close();
	}

#if LIBSIMULATOR_USE_MOVE
	lt::dht::node& dht() { return m_dht; }
	lt::dht::node const& dht() const { return m_dht; }
#else
	lt::dht::node& dht() { return *m_dht; }
	lt::dht::node const& dht() const { return *m_dht; }
#endif

private:
	asio::io_service m_io_service;
#if LIBSIMULATOR_USE_MOVE
	lt::udp::socket m_socket;
	lt::udp::socket& sock() { return m_socket; }
	lt::dht::node m_dht;
#else
	std::shared_ptr<lt::udp::socket> m_socket;
	lt::udp::socket& sock() { return *m_socket; }
	std::shared_ptr<lt::dht::node> m_dht;
#endif
	lt::udp::endpoint m_ep;
	bool m_add_dead_nodes;
	char m_buffer[1300];
};

dht_network::dht_network(sim::simulation& sim, int num_nodes)
{
	m_sett.ignore_dark_internet = false;
	m_sett.restrict_routing_ips = false;
	m_nodes.reserve(num_nodes);

// TODO: how can we introduce churn among peers?

	std::vector<std::pair<dht::node_id, lt::udp::endpoint>> all_nodes;
	all_nodes.reserve(num_nodes);

	for (int i = 0; i < num_nodes; ++i)
	{
		// node 0 is the one we log
		m_nodes.emplace_back(sim, m_sett, m_cnt, i, 0/*, dht_node::add_dead_nodes*/);
		all_nodes.push_back(m_nodes.back().node_info());
	}

	int cnt = 0;
	for (auto& n : m_nodes)
	{
		n.bootstrap(all_nodes);
		if (++cnt == 50)
		{
			// every now and then, shuffle all_nodes to make the
			// routing tables more randomly distributed
			std::random_shuffle(all_nodes.begin(), all_nodes.end());
			cnt = 0;
		}
	}
}

dht_network::~dht_network() {}

void print_routing_table(std::vector<lt::dht_routing_bucket> const& rt)
{
	int bucket = 0;
	for (std::vector<lt::dht_routing_bucket>::const_iterator i = rt.begin()
		, end(rt.end()); i != end; ++i, ++bucket)
	{
		char const* progress_bar =
			"################################"
			"################################"
			"################################"
			"################################";
		char const* short_progress_bar = "--------";
		printf("%3d [%3d, %d] %s%s\n"
			, bucket, i->num_nodes, i->num_replacements
			, progress_bar + (128 - i->num_nodes)
			, short_progress_bar + (8 - (std::min)(8, i->num_replacements)));
	}
}

std::vector<lt::udp::endpoint> dht_network::router_nodes() const
{
	int idx = 0;
	std::vector<lt::udp::endpoint> ret;
	ret.reserve(8);
	for (auto const& n : m_nodes)
	{
		if (idx >= 8) break;
		++idx;
		ret.push_back(n.node_info().second);
	}
	return ret;
}

void dht_network::stop()
{
	for (auto& n : m_nodes) n.stop();
}


