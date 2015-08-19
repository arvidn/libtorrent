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

	std::pair<char const*, int> salt(m_salt.c_str(), int(m_salt.size()));

	sha1_hash incoming_target;
	if (pk)
		incoming_target = item_target_id(salt, pk);
	else
		incoming_target = item_target_id(v.data_section());

	if (incoming_target != m_target) return;

	if (pk && sig)
	{
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
			// we can just ignore the return value here since for mutable
			// data, we always need the transaction done.
			m_data_callback(m_data, false);
		}
	}
	else if (m_data.empty())
	{
		// this is the first time we receive data,
		// and it's immutable

		m_data.assign(v);
		bool put_requested = m_data_callback(m_data, true);

		// if we intend to put, we need to keep going
		// until we find the closest nodes, since those
		// are the ones we're putting to
		if (put_requested)
		{
#if TORRENT_USE_ASSERTS
			std::vector<char> buffer;
			bencode(std::back_inserter(buffer), m_data.value());
			TORRENT_ASSERT(m_target == hasher(&buffer[0], buffer.size()).final());
#endif

			// this function is called when we're done, passing
			// in all relevant nodes we received data from close
			// to the target.
			m_nodes_callback = boost::bind(&get_item::put, this, _1);
		}
		else
		{
			// There can only be one true immutable item with a given id
			// Now that we've got it and the user doesn't want to do a put
			// there's no point in continuing to query other nodes
			abort();
		}
	}
}

get_item::get_item(
	node& dht_node
	, node_id target
	, data_callback const& dcallback)
	: find_data(dht_node, target, nodes_callback())
	, m_data_callback(dcallback)
{
}

get_item::get_item(
	node& dht_node
	, char const* pk
	, std::string const& salt
	, data_callback const& dcallback)
	: find_data(dht_node, item_target_id(
		std::make_pair(salt.c_str(), int(salt.size())), pk)
		, nodes_callback())
	, m_data_callback(dcallback)
	, m_data(pk, salt)
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
	if (m_data.is_mutable() || m_data.empty())
	{
		// for mutable data, now we have authoritative data since
		// we've heard from everyone, to be sure we got the
		// latest version of the data (i.e. highest sequence number)
		bool put_requested = m_data_callback(m_data, true);
		if (put_requested)
		{
#if TORRENT_USE_ASSERTS
			if (m_data.is_mutable())
			{
				TORRENT_ASSERT(m_target
					== item_target_id(std::pair<char const*, int>(m_data.salt().c_str()
						, m_data.salt().size())
					, m_data.pk().data()));
			}
			else
			{
				std::vector<char> buffer;
				bencode(std::back_inserter(buffer), m_data.value());
				TORRENT_ASSERT(m_target == hasher(&buffer[0], buffer.size()).final());
			}
#endif

			// this function is called when we're done, passing
			// in all relevant nodes we received data from close
			// to the target.
			m_nodes_callback = boost::bind(&get_item::put, this, _1);
		}
	}
	find_data::done();
}

// this function sends a put message to the nodes
// closest to the target. Those nodes are passed in
// as the v argument
void get_item::put(std::vector<std::pair<node_entry, std::string> > const& v)
{
#ifndef TORRENT_DISABLE_LOGGING
	// TODO: 3 it would be nice to not have to spend so much time rendering
	// the bencoded dict if logging is disabled
	get_node().observer()->log(dht_logger::traversal, "[%p] sending put "
		"[ seq: %" PRId64 " nodes: %d ]"
		, static_cast<void*>(this), (m_data.is_mutable() ? m_data.seq() : -1)
		, int(v.size()));
#endif

	// create a dummy traversal_algorithm
	boost::intrusive_ptr<traversal_algorithm> algo(
		new traversal_algorithm(m_node, (node_id::min)()));

	// store on the first k nodes
	for (std::vector<std::pair<node_entry, std::string> >::const_iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_node().observer()->log(dht_logger::traversal, "[%p] put-distance: %d"
			, static_cast<void*>(this), 160 - distance_exp(m_target, i->first.id));
#endif

		void* ptr = m_node.m_rpc.allocate_observer();
		if (ptr == 0) return;

		// TODO: 3 we don't support CAS errors here! we need a custom observer
		observer_ptr o(new (ptr) announce_observer(algo, i->first.ep(), i->first.id));
#if TORRENT_USE_ASSERTS
		o->m_in_constructor = false;
#endif
		entry e;
		e["y"] = "q";
		e["q"] = "put";
		entry& a = e["a"];
		a["v"] = m_data.value();
		a["token"] = i->second;
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
		m_node.m_rpc.invoke(e, i->first.ep(), o);
	}
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
		return;

	bdecode_node v = r.dict_find("v");
	if (v)
	{
		static_cast<get_item*>(algorithm())->got_data(v, pk, seq, sig);
	}

	find_data_observer::reply(m);
}

} } // namespace libtorrent::dht
