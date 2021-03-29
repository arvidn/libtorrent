/*

Copyright (c) 2015-2017, Steven Siloti
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_IP_HELPERS_HPP_INCLUDED
#define TORRENT_IP_HELPERS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/export.hpp"

namespace lt::aux {

	TORRENT_EXTRA_EXPORT bool is_global(address const& a);
	TORRENT_EXTRA_EXPORT bool is_local(address const& a);
	TORRENT_EXTRA_EXPORT bool is_link_local(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_teredo(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_ip_address(std::string const& host);

	// internal
	template <typename Endpoint>
	bool is_v4(Endpoint const& ep)
	{
		return ep.protocol() == Endpoint::protocol_type::v4();
	}
	template <typename Endpoint>
	bool is_v6(Endpoint const& ep)
	{
		return ep.protocol() == Endpoint::protocol_type::v6();
	}

	TORRENT_EXTRA_EXPORT address ensure_v6(address const& a);

}

#endif
