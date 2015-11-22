/*

Copyright (c) 2013, Steven Siloti
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

#include <libtorrent/config.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/kademlia/get_item.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>

#if TORRENT_USE_ASSERTS
#include <libtorrent/bencode.hpp>
#endif

namespace libtorrent { namespace dht
{

void get_item::got_data(bdecode_node const& v,
	char const* pk,
	boost::uint64_t seq,
	char const* sig)
{
	// we received data!
	// if no data_callback, we needn't care about the data we get.
	// only put_immutable_item no data_callback
	if (!m_data_callback) return;

	// for get_immutable_item
	if (m_immutable)
	{
		// If m_data isn't empty, we should have post alert.
		if (!m_data.empty()) return;

		sha1_hash incoming_target = item_target_id(v.data_section());
		if (incoming_target != m_target) return;

		m_data.assign(v);

		// There can only be one true immutable item with a given id
		// Now that we've got it and the user doesn't want to do a put
		// there's no point in continuing to query other nodes
		m_data_callback(m_data, true);
		done();

		return;
	}

	// immutalbe data should has been handled before this line, only mutable
	// data can reach here, which means pk and sig must be valid.
	if (!pk || !sig) return;

	std::pair<char const*, int> salt(m_salt.c_str(), int(m_salt.size()));
	sha1_hash incoming_target = item_target_id(salt, pk);
	if (incoming_target != m_target) return;

	// this is mutable data. If it passes the signature
	// check, remember it. Just keep the version with
	// the highest sequence number.
	if (m_data.empty() || m_data.seq() < seq)
	{
		if (!m_data.assign(v, salt, seq, pk, sig))
			return;

		// for get_item, we should call callback when we get data,
		// even if the date is not authoritative, we can update later.
		// so caller can get response ASAP without waitting transaction
		// time-out (15 seconds).
		// for put_item, the callback function will do nothing
		// if the data is non-authoritative.
		m_data_callback(m_data, false);
	}
}

get_item::get_item(
	node& dht_node
	, node_id target
	, data_callback const& dcallback
	, nodes_callback const& ncallback)
	: find_data(dht_node, target, ncallback)
	, m_data_callback(dcallback)
	, m_immutable(true)
{
}

get_item::get_item(
	node& dht_node
	, char const* pk
	, std::string const& salt
	, data_callback const& dcallback
	, nodes_callback const& ncallback)
	: find_data(dht_node, item_target_id(
		std::make_pair(salt.c_str(), int(salt.size())), pk)
		, ncallback)
	, m_data_callback(dcallback)
	, m_data(pk, salt)
	, m_immutable(false)
{
}

char const* get_item::name() const { return "get"; }

observer_ptr get_item::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) get_item_observer(this, ep, id));
#if TORRENT_USE_ASSERTS
	o->m_in_constructor = false;
#endif
	return o;
}

bool get_item::invoke(observer_ptr o)
{
	if (m_done)
	{
		m_invoke_count = -1;
		return false;
	}

	entry e;
	e["y"] = "q";
	entry& a = e["a"];

	e["q"] = "get";
	a["target"] = m_target.to_string();

	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

void get_item::done()
{
	// no data_callback for immutable item put
	if (!m_data_callback) return find_data::done();

	if (m_data.is_mutable() || m_data.empty())
	{
		// for mutable data, now we have authoritative data since
		// we've heard from everyone, to be sure we got the
		// latest version of the data (i.e. highest sequence number)
		m_data_callback(m_data, true);

#if TORRENT_USE_ASSERTS
		if (m_data.is_mutable())
		{
			TORRENT_ASSERT(m_target
				== item_target_id(std::pair<char const*, int>(m_data.salt().c_str()
					, m_data.salt().size())
					, m_data.pk().data()));
		}
#endif
	}

	find_data::done();
}

void get_item_observer::reply(msg const& m)
{
	char const* pk = NULL;
	char const* sig = NULL;
	boost::uint64_t seq = 0;

	bdecode_node r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%p] missing response dict"
			, static_cast<void*>(algorithm()));
#endif
		timeout();
		return;
	}

	bdecode_node k = r.dict_find_string("k");
	if (k && k.string_length() == item_pk_len)
		pk = k.string_ptr();

	bdecode_node s = r.dict_find_string("sig");
	if (s && s.string_length() == item_sig_len)
		sig = s.string_ptr();

	bdecode_node q = r.dict_find_int("seq");
	if (q)
		seq = q.int_value();
	else if (pk && sig)
	{
		timeout();
		return;
	}

	bdecode_node v = r.dict_find("v");
	if (v)
	{
		static_cast<get_item*>(algorithm())->got_data(v, pk, seq, sig);
	}

	find_data_observer::reply(m);
}

} } // namespace libtorrent::dht
