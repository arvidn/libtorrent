/*

Copyright (c) 2015, Thomas Yuan
Copyright (c) 2016-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PUT_DATA_HPP
#define TORRENT_PUT_DATA_HPP

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/item.hpp>

#include <vector>

namespace lt {
namespace dht {

struct msg;
class node;

struct put_data: traversal_algorithm
{
	using put_callback = std::function<void(item const&, int)>;

	put_data(node& node, put_callback callback);

	char const* name() const override;
	void start() override;

	void set_data(item&& data) { m_data = std::move(data); }
	void set_data(item const& data) = delete;

	void set_targets(std::vector<std::pair<node_entry, std::string>> const& targets);

protected:

	void done() override;
	bool invoke(observer_ptr o) override;

	put_callback m_put_callback;
	item m_data;
	bool m_done = false;
};

struct put_data_observer : traversal_observer
{
	put_data_observer(
		std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id, std::string token)
		: traversal_observer(std::move(algorithm), ep, id)
		, m_token(std::move(token))
	{
	}

	void reply(msg const&) override { done(); }

	std::string m_token;
};

} // namespace dht
} // namespace lt

#endif // TORRENT_PUT_DATA_HPP
