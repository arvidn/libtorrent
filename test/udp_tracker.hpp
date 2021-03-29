/*

Copyright (c) 2014, 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp" // for EXPORT
#include "libtorrent/address.hpp"

// returns the port the udp tracker is running on
int EXPORT start_udp_tracker(lt::address iface
	= lt::address_v4::any());

// the number of udp tracker announces received
int EXPORT num_udp_announces();

void EXPORT stop_udp_tracker();

