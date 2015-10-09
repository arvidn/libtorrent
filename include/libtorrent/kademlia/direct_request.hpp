/*

Copyright (c) 2014, Steven Siloti
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

#ifndef TORRENT_DIRECT_REQUEST_HPP
#define TORRENT_DIRECT_REQUEST_HPP

#include <boost/function/function1.hpp>
#include <libtorrent/kademlia/msg.hpp>
#include <libtorrent/kademlia/traversal_algorithm.hpp>

namespace libtorrent { namespace dht
{

struct direct_traversal : traversal_algorithm
{
	typedef boost::function<void(dht::msg const&)> message_callback;

	direct_traversal(node& node
		, node_id target
		, message_callback cb)
		: traversal_algorithm(node, target)
		, m_cb(cb)
	{}

	virtual char const* name() const { return "direct_traversal"; }

	void invoke_cb(msg const& m)
	{
		if (!m_cb.empty())
		{
			m_cb(m);
			m_cb.clear();
			done();
		}
	}

protected:
	message_callback m_cb;
};

struct direct_observer : observer
{
	direct_observer(boost::intrusive_ptr<traversal_algorithm> const& algo
		, udp::endpoint const& ep, node_id const& id)
		: observer(algo, ep, id)
	{}

	virtual void reply(msg const& m)
	{
		flags |= flag_done;
		static_cast<direct_traversal*>(algorithm())->invoke_cb(m);
	}

	virtual void timeout()
	{
		if (flags & flag_done) return;
		flags |= flag_done;
		bdecode_node e;
		msg m(e, target_ep());
		static_cast<direct_traversal*>(algorithm())->invoke_cb(m);
	}
};

}} // namespace libtorrent::dht

#endif //TORRENT_DIRECT_REQUEST_HPP
