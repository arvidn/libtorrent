/*

Copyright (c) 2006, Arvid Norberg & Daniel Wallin
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

#include "libtorrent/pch.hpp"

#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node.hpp>

#include <libtorrent/io.hpp>

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DECLARE_LOG(traversal);
#endif

refresh::refresh(
	node_impl& node
	, node_id target
	, done_callback const& callback)
	: find_data(node, target, find_data::data_callback(), callback)
{
}

char const* refresh::name() const
{
	return "refresh";
}

observer_ptr refresh::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) find_data_observer(this, ep, id));
#ifdef TORRENT_DEBUG
	o->m_in_constructor = false;
#endif
	return o;
}

bool refresh::invoke(observer_ptr o)
{
	entry e;
	e["y"] = "q";
	e["q"] = "find_node";
	entry& a = e["a"];
	a["target"] = target().to_string();
	m_node.m_rpc.invoke(e, o->target_ep(), o);
	return true;
}

} } // namespace libtorrent::dht

