/*

Copyright (c) 2006, 2009, 2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_ADDRESS_HPP_INCLUDED
#define TORRENT_ADDRESS_HPP_INCLUDED

#include <boost/version.hpp>

#include "libtorrent/config.hpp"
#include "libtorrent/string_view.hpp"

#if defined TORRENT_BUILD_SIMULATOR
#include "simulator/simulator.hpp"
#else
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/network_v4.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // SIMULATOR

namespace lt {

#if defined TORRENT_BUILD_SIMULATOR
	using sim::asio::ip::address;
	using sim::asio::ip::address_v4;
	using sim::asio::ip::address_v6;
#else
	using boost::asio::ip::address;
	using boost::asio::ip::address_v4;
	using boost::asio::ip::address_v6;
#endif // SIMULATOR

	using boost::asio::ip::network_v4;
	using boost::asio::ip::make_network_v4;
	using boost::asio::ip::v4_mapped;

#if defined TORRENT_BUILD_SIMULATOR
	using sim::asio::ip::make_address;
	using sim::asio::ip::make_address_v4;
	using sim::asio::ip::make_address_v6;
#else
	using boost::asio::ip::make_address;
	using boost::asio::ip::make_address_v4;
	using boost::asio::ip::make_address_v6;
#endif

}

#endif
