/*

Copyright (c) 2017, Arvid Norberg, Alden Torres
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

#ifndef TORRENT_SAMPLE_INFOHASHES_HPP
#define TORRENT_SAMPLE_INFOHASHES_HPP

#include <vector>

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/time.hpp>

namespace libtorrent { namespace dht
{

class sample_infohashes final : public traversal_algorithm
{
public:

	using data_callback = std::function<void(time_duration
		, int, std::vector<sha1_hash>
		, std::vector<std::pair<sha1_hash, udp::endpoint>>)>;

	sample_infohashes(node& dht_node
		, node_id const& target
		, data_callback const& dcallback);

	char const* name() const override;

	void got_samples(time_duration interval
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

}} // namespace libtorrent::dht

#endif // TORRENT_SAMPLE_INFOHASHES_HPP
