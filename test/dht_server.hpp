/*

Copyright (c) 2013, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp" // for EXPORT

// returns the port the DHT is running on
int EXPORT start_dht();

// the number of DHT messages received
int EXPORT num_dht_hits();

void EXPORT stop_dht();

