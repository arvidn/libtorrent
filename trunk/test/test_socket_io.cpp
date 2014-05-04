/*

Copyright (c) 2014, Arvid Norberg
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
#include "setup_transfer.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/socket.hpp"

#include <string>

using namespace libtorrent;
using namespace libtorrent::detail;

int test_main()
{
	// test address_to_bytes
	TEST_EQUAL(address_to_bytes(address_v4::from_string("10.11.12.13")), "\x0a\x0b\x0c\x0d");
	TEST_EQUAL(address_to_bytes(address_v4::from_string("16.5.127.1")), "\x10\x05\x7f\x01");

	// test endpoint_to_bytes
	TEST_EQUAL(endpoint_to_bytes(udp::endpoint(address_v4::from_string("10.11.12.13"), 8080)), "\x0a\x0b\x0c\x0d\x1f\x90");
	TEST_EQUAL(endpoint_to_bytes(udp::endpoint(address_v4::from_string("16.5.127.1"), 12345)), "\x10\x05\x7f\x01\x30\x39");

	std::string buf;
	std::back_insert_iterator<std::string> out1(buf);
	write_address(address_v4::from_string("16.5.128.1"), out1);
	TEST_EQUAL(buf, "\x10\x05\x80\x01");
	std::string::iterator in = buf.begin();
	address addr4 = read_v4_address(in);
	TEST_EQUAL(addr4, address_v4::from_string("16.5.128.1"));

	buf.clear();
	std::back_insert_iterator<std::string> out2(buf);
	write_endpoint(udp::endpoint(address_v4::from_string("16.5.128.1"), 1337), out2);
	TEST_EQUAL(buf, "\x10\x05\x80\x01\x05\x39");
	in = buf.begin();
	udp::endpoint ep4 = read_v4_endpoint<udp::endpoint>(in);
	TEST_EQUAL(ep4, udp::endpoint(address_v4::from_string("16.5.128.1"), 1337));

#if TORRENT_USE_IPV6
	buf.clear();
	std::back_insert_iterator<std::string> out3(buf);
	write_address(address_v6::from_string("1000::ffff"), out3);
	TEST_CHECK(std::equal(buf.begin(), buf.end(), "\x10\0\0\0\0\0\0\0\0\0\0\0\0\0\xff\xff"));
	in = buf.begin();
	address addr6 = read_v6_address(in); 
	TEST_EQUAL(addr6, address_v6::from_string("1000::ffff"));

	buf.clear();
	std::back_insert_iterator<std::string> out4(buf);
	write_endpoint(udp::endpoint(address_v6::from_string("1000::ffff"), 1337), out4);
	TEST_CHECK(std::equal(buf.begin(), buf.end(), "\x10\0\0\0\0\0\0\0\0\0\0\0\0\0\xff\xff\x05\x39"));
	TEST_EQUAL(buf.size(), 18);
	in = buf.begin();
	udp::endpoint ep6 = read_v6_endpoint<udp::endpoint>(in); 
	TEST_EQUAL(ep6, udp::endpoint(address_v6::from_string("1000::ffff"), 1337));
#endif

	char const eplist[] = "l6:\x10\x05\x80\x01\x05\x39" "18:\x10\0\0\0\0\0\0\0\0\0\0\0\0\0\xff\xff\x05\x39" "e";
	lazy_entry e;
	error_code ec;
	lazy_bdecode(eplist, eplist + sizeof(eplist)-1, e, ec);
	TEST_CHECK(!ec);
	std::vector<udp::endpoint> list;
	read_endpoint_list<udp::endpoint>(&e, list);

	TEST_EQUAL(list.size(), 2);
	TEST_EQUAL(list[0], udp::endpoint(address_v4::from_string("16.5.128.1"), 1337));
	TEST_EQUAL(list[1], udp::endpoint(address_v6::from_string("1000::ffff"), 1337));

	entry e2 = bdecode(eplist, eplist + sizeof(eplist)-1);
	list.clear();
	read_endpoint_list<udp::endpoint>(&e2, list);

	TEST_EQUAL(list.size(), 2);
	TEST_EQUAL(list[0], udp::endpoint(address_v4::from_string("16.5.128.1"), 1337));
	TEST_EQUAL(list[1], udp::endpoint(address_v6::from_string("1000::ffff"), 1337));

	return 0;
}

