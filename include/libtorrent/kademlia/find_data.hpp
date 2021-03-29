/*

Copyright (c) 2006, Daniel Wallin
Copyright (c) 2006-2010, 2013-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef FIND_DATA_050323_HPP
#define FIND_DATA_050323_HPP

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include <vector>
#include <map>

namespace lt {
namespace dht {

class node;

// -------- find data -----------

struct find_data : traversal_algorithm
{
	using nodes_callback = std::function<void(std::vector<std::pair<node_entry, std::string>> const&)>;

	find_data(node& dht_node, node_id const& target
		, nodes_callback ncallback);

	void got_write_token(node_id const& n, std::string write_token);

	void start() override;

	char const* name() const override;

protected:

	void done() override;
	observer_ptr new_observer(udp::endpoint const& ep
		, node_id const& id) override;

	nodes_callback m_nodes_callback;
	std::map<node_id, std::string> m_write_tokens;
	bool m_done;
};

struct find_data_observer : traversal_observer
{
	find_data_observer(
		std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id)
		: traversal_observer(std::move(algorithm), ep, id)
	{}

	void reply(msg const&) override;
};

} // namespace dht
} // namespace lt

#endif // FIND_DATA_050323_HPP
