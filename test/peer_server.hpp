/*

Copyright (c) 2013, 2016, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef PEER_SERVER_HPP
#define PEER_SERVER_HPP

#include "test.hpp" // for EXPORT

// returns the port the peer is running on
EXPORT int start_peer();

// the number of incoming connections to this peer
EXPORT int num_peer_hits();

EXPORT void stop_peer();

#endif

