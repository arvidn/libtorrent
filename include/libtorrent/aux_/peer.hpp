/*

Copyright (c) 2003-2004, 2006, 2012, 2014-2016, 2019-2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_HPP_INCLUDED
#define TORRENT_PEER_HPP_INCLUDED

#include <string>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/address.hpp"

namespace lt::aux {

	struct TORRENT_EXTRA_EXPORT peer_entry
	{
		std::string hostname;
		peer_id pid;
		std::uint16_t port;

		bool operator==(const peer_entry& p) const
		{ return pid == p.pid; }

		bool operator<(const peer_entry& p) const
		{ return pid < p.pid; }
	};

	struct ipv4_peer_entry
	{
		address_v4::bytes_type ip;
		std::uint16_t port;
	};

	struct ipv6_peer_entry
	{
		address_v6::bytes_type ip;
		std::uint16_t port;
	};

}

#endif // TORRENT_PEER_HPP_INCLUDED
