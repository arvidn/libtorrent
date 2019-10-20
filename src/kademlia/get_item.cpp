/*

Copyright (c) 2013, Steven Siloti
Copyright (c) 2015, Thomas
Copyright (c) 2013-2019, Arvid Norberg
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2017, Pavel Pimenov
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
#include <libtorrent/bdecode.hpp>
#include <libtorrent/kademlia/get_item.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/performance_counters.hpp>

namespace libtorrent { namespace dht {

void get_item::got_data(bdecode_node const& v,
	public_key const& pk,
	sequence_number const seq,
	signature const& sig)
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
		if (incoming_target != target()) return;

		m_data.assign(v);

		// There can only be one true immutable item with a given id
		// Now that we've got it and the user doesn't want to do a put
		// there's no point in continuing to query other nodes
		m_data_callback(m_data, true);
		done();

		return;
	}

	// immutable data should have been handled before this line, only mutable
	// data can reach here, which means pk, sig and seq must be valid.

	std::string const salt_copy(m_data.salt());
	sha1_hash const incoming_target = item_target_id(salt_copy, pk);
	if (incoming_target != target()) return;

	// this is mutable data. If it passes the signature
	// check, remember it. Just keep the version with
	// the highest sequence number.
	if (m_data.empty() || m_data.seq() < seq)
	{
		if (!m_data.assign(v, salt_copy, seq, pk, sig))
			return;

		// for get_item, we should call callback when we get data,
		// even if the date is not authoritative, we can update later.
		// so caller can get response ASAP without waiting transaction
		// time-out (15 seconds).
		// for put_item, the callback function will do nothing
		// if the data is non-authoritative.
		m_data_callback(m_data, false);
	}
}

get_item::get_item(
	node& dht_node
	, node_id const& target
	, data_callback dcallback
	, nodes_callback ncallback)
	: find_data(dht_node, target, std::move(ncallback))
	, m_data_callback(std::move(dcallback))
	, m_immutable(true)
{
}

get_item::get_item(
	node& dht_node
	, public_key const& pk
	, span<char const> salt
	, data_callback dcallback
	, nodes_callback ncallback)
	: find_data(dht_node, item_target_id(salt, pk), std::move(ncallback))
	, m_data_callback(std::move(dcallback))
	, m_data(pk, salt)
	, m_immutable(false)
{
}

char const* get_item::name() const { return "get"; }

observer_ptr get_item::new_observer(udp::endpoint const& ep
	, node_id const& id)
{
	auto o = m_node.m_rpc.allocate_observer<get_item_observer>(self(), ep, id);
#if TORRENT_USE_ASSERTS
	if (o) o->m_in_constructor = false;
#endif
	return o;
}

bool get_item::invoke(observer_ptr o)
{
	if (m_done) return false;

	entry e;
	e["y"] = "q";
	entry& a = e["a"];

	e["q"] = "get";
	a["target"] = target().to_string();

	m_node.stats_counters().inc_stats_counter(counters::dht_get_out);

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
			TORRENT_ASSERT(target() == item_target_id(m_data.salt(), m_data.pk()));
		}
#endif
	}

	find_data::done();
}

void get_item_observer::reply(msg const& m)
{
	public_key pk{};
	signature sig{};
	sequence_number seq{0};

	bdecode_node const r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%p] missing response dict"
			, static_cast<void*>(algorithm()));
#endif
		timeout();
		return;
	}

	bdecode_node const k = r.dict_find_string("k");
	if (k && k.string_length() == public_key::len)
		std::memcpy(pk.bytes.data(), k.string_ptr(), public_key::len);

	bdecode_node const s = r.dict_find_string("sig");
	if (s && s.string_length() == signature::len)
		std::memcpy(sig.bytes.data(), s.string_ptr(), signature::len);

	bdecode_node const q = r.dict_find_int("seq");
	if (q)
	{
		seq = sequence_number(q.int_value());
	}
	else if (k && s)
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
