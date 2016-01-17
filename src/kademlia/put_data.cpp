/*

Copyright (c) 2006-2016, Arvid Norberg & Daniel Wallin
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

#include <libtorrent/kademlia/put_data.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/io.hpp>

namespace libtorrent { namespace dht
{

put_data::put_data(node& dht_node, put_callback const& callback)
	: traversal_algorithm(dht_node, (node_id::min)())
	, m_put_callback(callback)
	, m_done(false)
{
}

char const* put_data::name() const { return "put_data"; }

void put_data::start()
{
	// router nodes must not be added to puts
	init();
	bool is_done = add_requests();
	if (is_done) done();
}

void put_data::set_targets(std::vector<std::pair<node_entry, std::string> > const& targets)
{
	for (std::vector<std::pair<node_entry, std::string> >::const_iterator i = targets.begin()
		, end(targets.end()); i != end; ++i)
	{
		void* ptr = m_node.m_rpc.allocate_observer();
		if (ptr == 0) return;

		observer_ptr o(new (ptr) put_data_observer(this, i->first.ep()
			, i->first.id, i->second));

	#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
		o->m_in_constructor = false;
	#endif
		m_results.push_back(o);
	}
}

void put_data::done()
{
	m_done = true;

#ifndef TORRENT_DISABLE_LOGGING
	get_node().observer()->log(dht_logger::traversal, "[%p] %s DONE, response %d, timeout %d"
		, static_cast<void*>(this), name(), m_responses, m_timeouts);
#endif

	m_put_callback(m_data, m_responses);
	traversal_algorithm::done();
}

bool put_data::invoke(observer_ptr o)
{
	if (m_done)
	{
		m_invoke_count = -1;
		return false;
	}

	// TODO: what if o is not an isntance of put_data_observer? This need to be
	// redesigned for better type saftey.
	put_data_observer* po = static_cast<put_data_observer*>(o.get());

	entry e;
	e["y"] = "q";
	e["q"] = "put";
	entry& a = e["a"];
	a["v"] = m_data.value();
	a["token"] = po->m_token;
	if (m_data.is_mutable())
	{
		a["k"] = std::string(m_data.pk().data(), item_pk_len);
		a["seq"] = m_data.seq();
		a["sig"] = std::string(m_data.sig().data(), item_sig_len);
		if (!m_data.salt().empty())
		{
			a["salt"] = m_data.salt();
		}
	}

	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

} } // namespace libtorrent::dht

