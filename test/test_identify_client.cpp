/*

Copyright (c) 2015, 2017, 2019, Arvid Norberg
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

