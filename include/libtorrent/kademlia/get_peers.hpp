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

#ifndef LIBTORRENT_GET_PEERS_HPP
#define LIBTORRENT_GET_PEERS_HPP

#include <libtorrent/kademlia/find_data.hpp>

namespace libtorrent { namespace dht {

struct get_peers : find_data
{
	using data_callback = std::function<void(std::vector<tcp::endpoint> const&)>;

	void got_peers(std::vector<tcp::endpoint> const& peers);

	get_peers(node& dht_node, node_id const& target
		, data_callback const& dcallback
		, nodes_callback const& ncallback
		, bool noseeds);

	char const* name() const override;

protected:
	bool invoke(observer_ptr o) override;
	observer_ptr new_observer(udp::endpoint const& ep
		, node_id const& id) override;

	data_callback m_data_callback;
	bool m_noseeds;
};

struct obfuscated_get_peers : get_peers
{
	obfuscated_get_peers(node& dht_node, node_id const& target
		, data_callback const& dcallback
		, nodes_callback const& ncallback
		, bool noseeds);

	char const* name() const override;

protected:

	observer_ptr new_observer(udp::endpoint const& ep,
		node_id const& id) override;
	bool invoke(observer_ptr o) override;
	void done() override;
private:
	// when set to false, we no longer obfuscate
	// the target hash, and send regular get_peers
	bool m_obfuscated;
};

struct get_peers_observer : find_data_observer
{
	get_peers_observer(
		std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id)
		: find_data_observer(std::move(algorithm), ep, id)
	{}

	void reply(msg const&) override;
#ifndef TORRENT_DISABLE_LOGGING
private:
	void log_peers(msg const& m, bdecode_node const& r, int size) const;
#endif
};

struct obfuscated_get_peers_observer : traversal_observer
{
	obfuscated_get_peers_observer(
		std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id)
		: traversal_observer(std::move(algorithm), ep, id)
	{}
	void reply(msg const&) override;
};

} } // namespace libtorrent::dht

#endif // LIBTORRENT_GET_PEERS_HPP
