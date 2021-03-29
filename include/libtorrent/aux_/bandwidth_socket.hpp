/*

Copyright (c) 2009, 2015, 2017-2018, 2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BANDWIDTH_SOCKET_HPP_INCLUDED
#define TORRENT_BANDWIDTH_SOCKET_HPP_INCLUDED

#include "libtorrent/aux_/export.hpp"

namespace lt::aux {

	struct TORRENT_EXTRA_EXPORT bandwidth_socket
	{
		virtual void assign_bandwidth(int channel, int amount) = 0;
		virtual bool is_disconnecting() const = 0;
		virtual ~bandwidth_socket() {}
	};
}

#endif // TORRENT_BANDWIDTH_SOCKET_HPP_INCLUDED
