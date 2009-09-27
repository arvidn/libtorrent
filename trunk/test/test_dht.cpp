/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/session.hpp"

#include "test.hpp"

int test_main()
{
	using namespace libtorrent;

	int dht_port = 48199;
	session ses(fingerprint("LT", 0, 1, 0, 0), std::make_pair(dht_port, 49000));

	// DHT should be running on port 48199 now

	io_service ios;
	error_code ec;
	datagram_socket sock(ios);

	sock.open(udp::v4(), ec);
	TEST_CHECK(!ec);
	if (ec) std::cout << ec.message() << std::endl;

	char const ping_msg[] = "d1:ad2:id20:00000000000000000001e1:q4:ping1:t2:101:y1:qe";

	// ping
	sock.send_to(asio::buffer(ping_msg, sizeof(ping_msg) - 1)
		, udp::endpoint(address::from_string("127.0.0.1"), dht_port), 0, ec);
	TEST_CHECK(!ec);
	if (ec) std::cout << ec.message() << std::endl;

	char inbuf[1600];
	udp::endpoint ep;
	int size = sock.receive_from(asio::buffer(inbuf, sizeof(inbuf)), ep, 0, ec);
	TEST_CHECK(!ec);
	if (ec) std::cout << ec.message() << std::endl;

	lazy_entry pong;
	int ret = lazy_bdecode(inbuf, inbuf + size, pong);
	TEST_CHECK(ret == 0);

	if (ret != 0) return 1;

	TEST_CHECK(pong.type() == lazy_entry::dict_t);

	if (pong.type() != lazy_entry::dict_t) return 1;

	lazy_entry const* t = pong.dict_find_string("t");
	TEST_CHECK(t);
	if (t) TEST_CHECK(t->string_value() == "10");

	lazy_entry const* y = pong.dict_find_string("y");
	TEST_CHECK(y);
	if (y) TEST_CHECK(y->string_value() == "r");

	return 0;
}

