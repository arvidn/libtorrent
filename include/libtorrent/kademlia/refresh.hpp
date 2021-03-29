/*

Copyright (c) 2006, Daniel Wallin
Copyright (c) 2006, 2008, 2010, 2013-2014, 2017-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef REFRESH_050324_HPP
#define REFRESH_050324_HPP

#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/get_peers.hpp>

namespace lt {
namespace dht {

class bootstrap : public get_peers
{
public:
	using done_callback = get_peers::nodes_callback;

	bootstrap(node& dht_node, node_id const& target
		, done_callback const& callback);
	char const* name() const override;

	observer_ptr new_observer(udp::endpoint const& ep
		, node_id const& id) override;

protected:

	bool invoke(observer_ptr o) override;

	void done() override;

};

} // namesapce dht
} // namespace lt

#endif // REFRESH_050324_HPP
