/*

Copyright (c) 2010, 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RESOLVER_HPP_INCLUDE
#define TORRENT_RESOLVER_HPP_INCLUDE

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/tcp.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <unordered_map>
#include <vector>
#include <map>

#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/address.hpp"

namespace lt::aux {

struct TORRENT_EXTRA_EXPORT resolver final : resolver_interface
{
	explicit resolver(io_context& ios);

	void async_resolve(std::string const& host, resolver_flags flags
		, callback_t h) override;

	void abort() override;

	void set_cache_timeout(seconds timeout) override;

private:

	void on_lookup(error_code const& ec, tcp::resolver::results_type ips
		, std::string const& hostname);

	void callback(resolver_interface::callback_t h
		, error_code const& ec, std::vector<address> const& ips);

	struct dns_cache_entry
	{
		time_point last_seen;
		std::vector<address> addresses;
	};

	std::unordered_map<std::string, dns_cache_entry> m_cache;
	io_context& m_ios;

	// all lookups in this resolver are aborted on shutdown.
	tcp::resolver m_resolver;

	// lookups in this resolver are not aborted on shutdown
	tcp::resolver m_critical_resolver;

	// max number of cached entries
	int m_max_size;

	// timeout of cache entries
	time_duration m_timeout;

	// the callbacks to call when a host resolution completes. This allows to
	// attach more callbacks if the same host is looked up mutliple times
	std::multimap<std::string, resolver_interface::callback_t> m_callbacks;
};

}

#endif
