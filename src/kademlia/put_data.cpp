/*

Copyright (c) 2006, Daniel Wallin
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016-2020, Arvid Norberg
Copyright (c) 2016-2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <libtorrent/kademlia/put_data.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/aux_/io_bytes.hpp>
#include <libtorrent/performance_counters.hpp>

namespace lt { namespace dht {

put_data::put_data(node& dht_node, put_callback callback)
	: traversal_algorithm(dht_node, {})
	, m_put_callback(std::move(callback))
{}

char const* put_data::name() const { return "put_data"; }

void put_data::start()
{
	// router nodes must not be added to puts
	init();
	bool const is_done = add_requests();
	if (is_done) done();
}

void put_data::set_targets(std::vector<std::pair<node_entry, std::string>> const& targets)
{
	for (auto const& p : targets)
	{
		auto o = m_node.m_rpc.allocate_observer<put_data_observer>(self(), p.first.ep()
			, p.first.id, p.second);
		if (!o) return;

#if TORRENT_USE_ASSERTS
		o->m_in_constructor = false;
#endif
		m_results.push_back(std::move(o));
	}
}

void put_data::done()
{
	m_done = true;

#ifndef TORRENT_DISABLE_LOGGING
	get_node().observer()->log(dht_logger::traversal, "[%u] %s DONE, response %d, timeout %d"
		, id(), name(), num_responses(), num_timeouts());
#endif

	m_put_callback(m_data, num_responses());
	traversal_algorithm::done();
}

bool put_data::invoke(observer_ptr o)
{
	if (m_done) return false;

	// TODO: what if o is not an instance of put_data_observer? This need to be
	// redesigned for better type safety.
	auto* po = static_cast<put_data_observer*>(o.get());

	entry e;
	e["y"] = "q";
	e["q"] = "put";
	entry& a = e["a"];
	a["v"] = m_data.value();
	a["token"] = po->m_token;
	if (m_data.is_mutable())
	{
		a["k"] = m_data.pk().bytes;
		a["seq"] = m_data.seq().value;
		a["sig"] = m_data.sig().bytes;
		if (!m_data.salt().empty())
		{
			a["salt"] = m_data.salt();
		}
	}

	m_node.stats_counters().inc_stats_counter(counters::dht_put_out);

	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

} } // namespace lt::dht
