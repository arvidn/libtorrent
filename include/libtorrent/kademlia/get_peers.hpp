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

namespace libtorrent { namespace dht
{

struct get_peers : find_data
{
	typedef boost::function<void(std::vector<tcp::endpoint> const&)> data_callback;

	void got_peers(std::vector<tcp::endpoint> const& peers);

	get_peers(node& dht_node, node_id target
		, data_callback const& dcallback
		, nodes_callback const& ncallback
		, bool noseeds);

	virtual char const* name() const;

protected:
	virtual bool invoke(observer_ptr o);
	virtual observer_ptr new_observer(void* ptr, udp::endpoint const& ep, node_id const& id);

	data_callback m_data_callback;
	bool m_noseeds;
};

struct obfuscated_get_peers : get_peers
{
	typedef get_peers::nodes_callback done_callback;

	obfuscated_get_peers(node& dht_node, node_id target
		, data_callback const& dcallback
		, nodes_callback const& ncallback
		, bool noseeds);

	virtual char const* name() const;

protected:

	virtual observer_ptr new_observer(void* ptr, udp::endpoint const& ep,
		node_id const& id);
	virtual bool invoke(observer_ptr o);
	virtual void done();
private:
	// when set to false, we no longer obfuscate
	// the target hash, and send regular get_peers
	bool m_obfuscated;
};

struct get_peers_observer : find_data_observer
{
	get_peers_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id)
		: find_data_observer(algorithm, ep, id)
	{}

	virtual void reply(msg const&);
};

struct obfuscated_get_peers_observer : traversal_observer
{
	obfuscated_get_peers_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id)
		: traversal_observer(algorithm, ep, id)
	{}
	virtual void reply(msg const&);
};

} } // namespace libtorrent::dht

#endif // LIBTORRENT_GET_PEERS_HPP
