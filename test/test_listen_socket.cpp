/*

Copyright (c) 2016, Steven Siloti
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
#include "libtorrent/aux_/session_impl.hpp"

using namespace libtorrent;

namespace
{
	void test_equal(listen_socket_t const& s, address addr, int port, std::string dev, bool ssl)
	{
		TEST_EQUAL(s.ssl, ssl);
		TEST_EQUAL(s.local_endpoint.address(), addr);
		TEST_EQUAL(s.original_port, port);
		TEST_EQUAL(s.device, dev);
	}

	void test_equal(aux::listen_endpoint_t const& e1, address addr, int port, std::string dev, bool ssl)
	{
		TEST_EQUAL(e1.ssl, ssl);
		TEST_EQUAL(e1.port, port);
		TEST_EQUAL(e1.addr, addr);
		TEST_EQUAL(e1.device, dev);
	}
}

TORRENT_TEST(partition_listen_sockets_wildcard2specific)
{
	std::list<listen_socket_t> sockets;
	listen_socket_t s;
	s.local_endpoint = tcp::endpoint(tcp::v4(), 6881);
	s.original_port = 6881;
	sockets.push_back(s);
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.4"), 6881);
	sockets.push_back(s);

	// remove the wildcard socket and replace it with a specific IP
	std::vector<aux::listen_endpoint_t> eps;
	eps.emplace_back(address_v4::from_string("4.4.4.4"), 6881, "", false);
	eps.emplace_back(address_v4::from_string("4.4.4.5"), 6881, "", false);
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_EQUAL(eps.size(), 1);
	TEST_EQUAL(std::distance(sockets.begin(), remove_iter), 1);
	TEST_EQUAL(std::distance(remove_iter, sockets.end()), 1);
	test_equal(sockets.front(), address_v4::from_string("4.4.4.4"), 6881, "", false);
	test_equal(sockets.back(), address_v4(), 6881, "", false);
	test_equal(eps.front(), address_v4::from_string("4.4.4.5"), 6881, "", false);
}

TORRENT_TEST(partition_listen_sockets_port_change)
{
	std::list<listen_socket_t> sockets;
	listen_socket_t s;
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.4"), 6881);
	s.original_port = 6881;
	sockets.push_back(s);
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.5"), 6881);
	sockets.push_back(s);

	// change the ports
	std::vector<aux::listen_endpoint_t> eps;
	eps.emplace_back(address_v4::from_string("4.4.4.4"), 6882, "", false);
	eps.emplace_back(address_v4::from_string("4.4.4.5"), 6882, "", false);
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(sockets.begin() == remove_iter);
	TEST_EQUAL(eps.size(), 2);
}

TORRENT_TEST(partition_listen_sockets_device_bound)
{
	std::list<listen_socket_t> sockets;
	listen_socket_t s;
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.5"), 6881);
	s.original_port = 6881;
	sockets.push_back(s);
	s.local_endpoint = tcp::endpoint(tcp::v4(), 6881);
	sockets.push_back(s);


	// replace the wildcard socket with a pair of device bound sockets
	std::vector<aux::listen_endpoint_t> eps;
	eps.emplace_back(address_v4::from_string("4.4.4.5"), 6881, "", false);
	eps.emplace_back(address_v4::from_string("4.4.4.6"), 6881, "eth1", false);
	eps.emplace_back(address_v4::from_string("4.4.4.7"), 6881, "eth1", false);
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_EQUAL(std::distance(sockets.begin(), remove_iter), 1);
	TEST_EQUAL(std::distance(remove_iter, sockets.end()), 1);
	test_equal(sockets.front(), address_v4::from_string("4.4.4.5"), 6881, "", false);
	test_equal(sockets.back(), address_v4(), 6881, "", false);
	TEST_EQUAL(eps.size(), 2);
}

TORRENT_TEST(partition_listen_sockets_device_ip_change)
{
	std::list<listen_socket_t> sockets;
	listen_socket_t s;
	s.local_endpoint = tcp::endpoint(address_v4::from_string("10.10.10.10"), 6881);
	s.device = "enp3s0";
	s.original_port = 6881;
	sockets.push_back(s);
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.4"), 6881);
	sockets.push_back(s);

	// change the IP of a device bound socket
	std::vector<aux::listen_endpoint_t> eps;
	eps.emplace_back(address_v4::from_string("10.10.10.10"), 6881, "enp3s0", false);
	eps.emplace_back(address_v4::from_string("4.4.4.5"), 6881, "enp3s0", false);
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_EQUAL(std::distance(sockets.begin(), remove_iter), 1);
	TEST_EQUAL(std::distance(remove_iter, sockets.end()), 1);
	test_equal(sockets.front(), address_v4::from_string("10.10.10.10"), 6881, "enp3s0", false);
	test_equal(sockets.back(), address_v4::from_string("4.4.4.4"), 6881, "enp3s0", false);
	TEST_EQUAL(eps.size(), 1);
	test_equal(eps.front(), address_v4::from_string("4.4.4.5"), 6881, "enp3s0", false);
}

TORRENT_TEST(partition_listen_sockets_original_port)
{
	std::list<listen_socket_t> sockets;
	listen_socket_t s;
	s.local_endpoint = tcp::endpoint(address_v4::from_string("10.10.10.10"), 6883);
	s.original_port = 6881;
	sockets.push_back(s);
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.4"), 6883);
	sockets.push_back(s);

	// make sure all sockets are kept when the actual port is different from the original
	std::vector<aux::listen_endpoint_t> eps;
	eps.emplace_back(address_v4::from_string("10.10.10.10"), 6881, "", false);
	eps.emplace_back(address_v4::from_string("4.4.4.4"), 6881, "", false);
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(remove_iter == sockets.end());
	TEST_CHECK(eps.empty());
}

TORRENT_TEST(partition_listen_sockets_ssl)
{
	std::list<listen_socket_t> sockets;
	listen_socket_t s;
	s.local_endpoint = tcp::endpoint(address_v4::from_string("10.10.10.10"), 6881);
	s.original_port = 6881;
	sockets.push_back(s);
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.4"), 6881);
	sockets.push_back(s);

	// add ssl sockets
	std::vector<aux::listen_endpoint_t> eps;
	eps.emplace_back(address_v4::from_string("10.10.10.10"), 6881, "", false);
	eps.emplace_back(address_v4::from_string("4.4.4.4"), 6881, "", false);
	eps.emplace_back(address_v4::from_string("10.10.10.10"), 6881, "", true);
	eps.emplace_back(address_v4::from_string("4.4.4.4"), 6881, "", true);
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(remove_iter == sockets.end());
	TEST_EQUAL(eps.size(), 2);
}

TORRENT_TEST(partition_listen_sockets_op_ports)
{
	std::list<listen_socket_t> sockets;
	listen_socket_t s;
	s.local_endpoint = tcp::endpoint(address_v4::from_string("10.10.10.10"), 6881);
	s.original_port = 0;
	sockets.push_back(s);
	s.local_endpoint = tcp::endpoint(address_v4::from_string("4.4.4.4"), 6881);
	sockets.push_back(s);

	// replace OS assigned ports with explicit ports
	std::vector<aux::listen_endpoint_t> eps;
	eps.emplace_back(address_v4::from_string("10.10.10.10"), 6882, "", false);
	eps.emplace_back(address_v4::from_string("4.4.4.4"), 6882, "", false);
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(remove_iter == sockets.begin());
	TEST_EQUAL(eps.size(), 2);
}

