/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
Copyright (c) 2020, Fonic
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SAMPLE_INFOHASHES_HPP
#define TORRENT_SAMPLE_INFOHASHES_HPP

#include <vector>

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/time.hpp>

namespace lt {
namespace dht {

class sample_infohashes final : public traversal_algorithm
{
public:

	using data_callback = std::function<void(sha1_hash
		, time_duration
		, int, std::vector<sha1_hash>
		, std::vector<std::pair<sha1_hash, udp::endpoint>>)>;

	sample_infohashes(node& dht_node
		, node_id const& target
		, data_callback dcallback);

	char const* name() const override;

	void got_samples(sha1_hash const& nid
		, time_duration interval
		, int num, std::vector<sha1_hash> samples
		, std::vector<std::pair<sha1_hash, udp::endpoint>> nodes);

protected:

	data_callback m_data_callback;
};

class sample_infohashes_observer final : public traversal_observer
{
public:

	sample_infohashes_observer(std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id);

	void reply(msg const&) override;
};

} // namespace dht
} // namespace lt

#endif // TORRENT_SAMPLE_INFOHASHES_HPP
