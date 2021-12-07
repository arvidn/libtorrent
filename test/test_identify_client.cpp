/*

Copyright (c) 2015, 2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/identify_client.hpp"

using namespace lt;

TORRENT_TEST(identify_client)
{
	TEST_EQUAL(aux::identify_client_impl(peer_id("-AZ123B-............")), "Azureus 1.2.3.11");
	TEST_EQUAL(aux::identify_client_impl(peer_id("-AZ1230-............")), "Azureus 1.2.3");
	TEST_EQUAL(aux::identify_client_impl(peer_id("S123--..............")), "Shadow 1.2.3");
	TEST_EQUAL(aux::identify_client_impl(peer_id("S\x1\x2\x3....\0...........")), "Shadow 1.2.3");
	TEST_EQUAL(aux::identify_client_impl(peer_id("M1-2-3--............")), "Mainline 1.2.3");
	TEST_EQUAL(aux::identify_client_impl(peer_id("\0\0\0\0\0\0\0\0\0\0\0\0........")), "Generic");
	TEST_EQUAL(aux::identify_client_impl(peer_id("-xx1230-............")), "xx 1.2.3");
}

