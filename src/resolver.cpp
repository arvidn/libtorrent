/*

Copyright (c) 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016-2018, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/aux_/time.hpp"

namespace lt::aux {

	resolver::resolver(io_context& ios)
		: m_ios(ios)
		, m_resolver(ios)
		, m_critical_resolver(ios)
		, m_max_size(700)
		, m_timeout(seconds(1200))
	{}

	void resolver::callback(resolver_interface::callback_t h
		, error_code const& ec, std::vector<address> const& ips)
	{
		try {
			h(ec, ips);
		} catch (std::exception&) {
			TORRENT_ASSERT_FAIL();
		}
	}

	void resolver::on_lookup(error_code const& ec, tcp::resolver::results_type ips
		, std::string const& hostname)
	{
		COMPLETE_ASYNC("resolver::on_lookup");
		if (ec)
		{
			auto const range = m_callbacks.equal_range(hostname);
			for (auto c = range.first; c != range.second; ++c)
				callback(std::move(c->second), ec, {});
			m_callbacks.erase(range.first, range.second);
			return;
		}

		dns_cache_entry& ce = m_cache[hostname];
		ce.last_seen = time_now();
		ce.addresses.clear();
		for (auto i : ips)
			ce.addresses.push_back(i.endpoint().address());

		auto const range = m_callbacks.equal_range(hostname);
		for (auto c = range.first; c != range.second; ++c)
			callback(std::move(c->second), ec, ce.addresses);
		m_callbacks.erase(range.first, range.second);

		// if m_cache grows too big, weed out the
		// oldest entries
		if (int(m_cache.size()) > m_max_size)
		{
			auto oldest = m_cache.begin();
			for (auto k = m_cache.begin(); k != m_cache.end(); ++k)
			{
				if (k->second.last_seen < oldest->second.last_seen)
					oldest = k;
			}

			// remove the oldest entry
			m_cache.erase(oldest);
		}
	}

	void resolver::async_resolve(std::string const& host, resolver_flags const flags
		, resolver_interface::callback_t h)
	{
		// special handling for raw IP addresses. There's no need to get in line
		// behind actual lookups if we can just resolve it immediately.
		error_code ec;
		address const ip = make_address(host, ec);
		if (!ec)
		{
			post(m_ios, [=]{ callback(h, ec, std::vector<address>{ip}); });
			return;
		}
		ec.clear();

		auto const i = m_cache.find(host);
		if (i != m_cache.end())
		{
			// keep cache entries valid for m_timeout seconds
			if ((flags & resolver_interface::cache_only)
				|| i->second.last_seen + m_timeout >= time_now())
			{
				std::vector<address> ips = i->second.addresses;
				post(m_ios, [=] { callback(h, ec, ips); });
				return;
			}
		}

		if (flags & resolver_interface::cache_only)
		{
			// we did not find a cache entry, fail the lookup
			post(m_ios, [=] {
				callback(h, boost::asio::error::host_not_found, std::vector<address>{});
			});
			return;
		}

		auto iter = m_callbacks.find(host);
		bool const done = (iter != m_callbacks.end());

		m_callbacks.insert(iter, {host, std::move(h)});

		// if there is an existing outtanding lookup, our callback will be
		// called once it completes. We're done here.
		if (done) return;

		// the port is ignored
		using namespace std::placeholders;
		ADD_OUTSTANDING_ASYNC("resolver::on_lookup");
		if (flags & resolver_interface::abort_on_shutdown)
		{
			m_resolver.async_resolve(host, "80", std::bind(&resolver::on_lookup, this, _1, _2
				, host));
		}
		else
		{
			m_critical_resolver.async_resolve(host, "80", std::bind(&resolver::on_lookup, this, _1, _2
				, host));
		}
	}

	void resolver::abort()
	{
		m_resolver.cancel();
	}

	void resolver::set_cache_timeout(seconds const timeout)
	{
		if (timeout >= seconds(0))
			m_timeout = timeout;
		else
			m_timeout = seconds(0);
	}
}
