/*

Copyright (c) 2013-2016, Arvid Norberg
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

#include "libtorrent/resolver.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/aux_/time.hpp"

namespace lt {
LIBTORRENT_VERSION_NAMESPACE {
	resolver::resolver(io_service& ios)
		: m_ios(ios)
		, m_resolver(ios)
		, m_critical_resolver(ios)
		, m_max_size(700)
		, m_timeout(seconds(1200))
	{}

	void resolver::on_lookup(error_code const& ec, tcp::resolver::iterator i
		, resolver_interface::callback_t h, std::string hostname)
	{
		COMPLETE_ASYNC("resolver::on_lookup");
		if (ec)
		{
			std::vector<address> empty;
			h(ec, empty);
			return;
		}

		dns_cache_entry& ce = m_cache[hostname];
		time_point now = aux::time_now();
		ce.last_seen = now;
		ce.addresses.clear();
		while (i != tcp::resolver::iterator())
		{
			ce.addresses.push_back(i->endpoint().address());
			++i;
		}

		h(ec, ce.addresses);

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

	void resolver::async_resolve(std::string const& host, int const flags
		, resolver_interface::callback_t const& h)
	{
		auto const i = m_cache.find(host);
		if (i != m_cache.end())
		{
			// keep cache entries valid for m_timeout seconds
			if ((flags & resolver_interface::prefer_cache)
				|| i->second.last_seen + m_timeout >= aux::time_now())
			{
				error_code ec;
				m_ios.post(std::bind(h, ec, i->second.addresses));
				return;
			}
		}

		// special handling for raw IP addresses. There's no need to get in line
		// behind actual lookups if we can just resolve it immediately.
		error_code ec;
		address const ip = address::from_string(host, ec);
		if (!ec)
		{
			std::vector<address> addresses;
			addresses.push_back(ip);
			m_ios.post(std::bind(h, ec, addresses));
			return;
		}

		// the port is ignored
		tcp::resolver::query const q(host, "80");

		using namespace std::placeholders;
		ADD_OUTSTANDING_ASYNC("resolver::on_lookup");
		if (flags & resolver_interface::abort_on_shutdown)
		{
			m_resolver.async_resolve(q, std::bind(&resolver::on_lookup, this, _1, _2
				, h, host));
		}
		else
		{
			m_critical_resolver.async_resolve(q, std::bind(&resolver::on_lookup, this, _1, _2
				, h, host));
		}
	}

	void resolver::abort()
	{
		m_resolver.cancel();
	}
}}
