/*

Copyright (c) 2014-2015, Steven Siloti
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2015-2016, 2018, Alden Torres
Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DIRECT_REQUEST_HPP
#define TORRENT_DIRECT_REQUEST_HPP

#include <libtorrent/kademlia/msg.hpp>
#include <libtorrent/kademlia/traversal_algorithm.hpp>

namespace lt {
namespace dht {

struct direct_traversal : traversal_algorithm
{
	using message_callback = std::function<void(dht::msg const&)>;

	direct_traversal(node& node
		, node_id const& target
		, message_callback cb)
		: traversal_algorithm(node, target)
		, m_cb(std::move(cb))
	{}

	char const* name() const override { return "direct_traversal"; }

	void invoke_cb(msg const& m)
	{
		if (m_cb)
		{
			m_cb(m);
			m_cb = nullptr;
			done();
		}
	}

protected:
	message_callback m_cb;
};

struct direct_observer : observer
{
	direct_observer(std::shared_ptr<traversal_algorithm> algo
		, udp::endpoint const& ep, node_id const& id)
		: observer(std::move(algo), ep, id)
	{}

	void reply(msg const& m) override
	{
		flags |= flag_done;
		static_cast<direct_traversal*>(algorithm())->invoke_cb(m);
	}

	void timeout() override
	{
		if (flags & flag_done) return;
		flags |= flag_done;
		bdecode_node e;
		msg m(e, target_ep());
		static_cast<direct_traversal*>(algorithm())->invoke_cb(m);
	}
};

} // namespace dht
} // namespace lt

#endif //TORRENT_DIRECT_REQUEST_HPP
