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

using namespace lt;

namespace
{
	using tp = aux::transport;

	void test_equal(aux::listen_socket_t const& s, address addr, int port
		, std::string dev, tp ssl)
	{
		TEST_CHECK(s.ssl == ssl);
		TEST_EQUAL(s.local_endpoint.address(), addr);
		TEST_EQUAL(s.original_port, port);
		TEST_EQUAL(s.device, dev);
	}

	void test_equal(aux::listen_endpoint_t const& e1, address addr, int port
		, std::string dev, tp ssl)
	{
		TEST_CHECK(e1.ssl == ssl);
		TEST_EQUAL(e1.port, port);
		TEST_EQUAL(e1.addr, addr);
		TEST_EQUAL(e1.device, dev);
	}

	ip_interface ifc(char const* ip, char const* device)
	{
		ip_interface ipi;
		ipi.interface_address = address::from_string(ip);
		strncpy(ipi.name, device, sizeof(ipi.name));
		return ipi;
	}

	aux::listen_endpoint_t ep(char const* ip, int port
		, tp ssl = tp::plaintext
		, std::string device = {})
	{
		return aux::listen_endpoint_t(address::from_string(ip), port, device, ssl);
	}

	aux::listen_endpoint_t ep(char const* ip, int port
		, std::string device
		, tp ssl = tp::plaintext)
	{
		return aux::listen_endpoint_t(address::from_string(ip), port, device, ssl);
	}

	std::shared_ptr<aux::listen_socket_t> sock(char const* ip, int const port
		, int const original_port, char const* device = "")
	{
		auto s = std::make_shared<aux::listen_socket_t>();
		s->local_endpoint = tcp::endpoint(address::from_string(ip)
			, aux::numeric_cast<std::uint16_t>(port));
		s->original_port = original_port;
		s->device = device;
		return s;
	}

	std::shared_ptr<aux::listen_socket_t> sock(char const* ip, int const port, char const* dev)
	{ return sock(ip, port, port, dev); }

	std::shared_ptr<aux::listen_socket_t> sock(char const* ip, int const port)
	{ return sock(ip, port, port); }

} // anonymous namespace

TORRENT_TEST(partition_listen_sockets_wildcard2specific)
{
	std::vector<std::shared_ptr<aux::listen_socket_t>> sockets = {
		sock("0.0.0.0", 6881), sock("4.4.4.4", 6881)
	};

	// remove the wildcard socket and replace it with a specific IP
	std::vector<aux::listen_endpoint_t> eps = {
		ep("4.4.4.4", 6881), ep("4.4.4.5", 6881)
	};

	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_EQUAL(eps.size(), 1);
	TEST_EQUAL(std::distance(sockets.begin(), remove_iter), 1);
	TEST_EQUAL(std::distance(remove_iter, sockets.end()), 1);
	test_equal(*sockets.front(), address_v4::from_string("4.4.4.4"), 6881, "", tp::plaintext);
	test_equal(*sockets.back(), address_v4(), 6881, "", tp::plaintext);
	test_equal(eps.front(), address_v4::from_string("4.4.4.5"), 6881, "", tp::plaintext);
}

TORRENT_TEST(partition_listen_sockets_port_change)
{
	std::vector<std::shared_ptr<aux::listen_socket_t>> sockets = {
		sock("4.4.4.4", 6881), sock("4.4.4.5", 6881)
	};

	// change the ports
	std::vector<aux::listen_endpoint_t> eps = {
		ep("4.4.4.4", 6882), ep("4.4.4.5", 6882)
	};
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(sockets.begin() == remove_iter);
	TEST_EQUAL(eps.size(), 2);
}

TORRENT_TEST(partition_listen_sockets_device_bound)
{
	std::vector<std::shared_ptr<aux::listen_socket_t>> sockets = {
		sock("4.4.4.5", 6881), sock("0.0.0.0", 6881)
	};

	// replace the wildcard socket with a pair of device bound sockets
	std::vector<aux::listen_endpoint_t> eps = {
		ep("4.4.4.5", 6881)
		, ep("4.4.4.6", 6881, "eth1")
		, ep("4.4.4.7", 6881, "eth1")
	};

	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_EQUAL(std::distance(sockets.begin(), remove_iter), 1);
	TEST_EQUAL(std::distance(remove_iter, sockets.end()), 1);
	test_equal(*sockets.front(), address_v4::from_string("4.4.4.5"), 6881, "", tp::plaintext);
	test_equal(*sockets.back(), address_v4(), 6881, "", tp::plaintext);
	TEST_EQUAL(eps.size(), 2);
}

TORRENT_TEST(partition_listen_sockets_device_ip_change)
{
	std::vector<std::shared_ptr<aux::listen_socket_t>> sockets = {
		sock("10.10.10.10", 6881, "enp3s0")
		, sock("4.4.4.4", 6881, "enp3s0")
	};

	// change the IP of a device bound socket
	std::vector<aux::listen_endpoint_t> eps = {
		ep("10.10.10.10", 6881, "enp3s0")
		, ep("4.4.4.5", 6881, "enp3s0")
	};
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_EQUAL(std::distance(sockets.begin(), remove_iter), 1);
	TEST_EQUAL(std::distance(remove_iter, sockets.end()), 1);
	test_equal(*sockets.front(), address_v4::from_string("10.10.10.10"), 6881, "enp3s0", tp::plaintext);
	test_equal(*sockets.back(), address_v4::from_string("4.4.4.4"), 6881, "enp3s0", tp::plaintext);
	TEST_EQUAL(eps.size(), 1);
	test_equal(eps.front(), address_v4::from_string("4.4.4.5"), 6881, "enp3s0", tp::plaintext);
}

TORRENT_TEST(partition_listen_sockets_original_port)
{
	std::vector<std::shared_ptr<aux::listen_socket_t>> sockets = {
		sock("10.10.10.10", 6883, 6881), sock("4.4.4.4", 6883, 6881)
	};

	// make sure all sockets are kept when the actual port is different from the original
	std::vector<aux::listen_endpoint_t> eps = {
		ep("10.10.10.10", 6881)
		, ep("4.4.4.4", 6881)
	};

	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(remove_iter == sockets.end());
	TEST_CHECK(eps.empty());
}

TORRENT_TEST(partition_listen_sockets_ssl)
{
	std::vector<std::shared_ptr<aux::listen_socket_t>> sockets = {
		sock("10.10.10.10", 6881), sock("4.4.4.4", 6881)
	};

	// add ssl sockets
	std::vector<aux::listen_endpoint_t> eps = {
		ep("10.10.10.10", 6881)
		, ep("4.4.4.4", 6881)
		, ep("10.10.10.10", 6881, tp::ssl)
		, ep("4.4.4.4", 6881, tp::ssl)
	};

	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(remove_iter == sockets.end());
	TEST_EQUAL(eps.size(), 2);
}

TORRENT_TEST(partition_listen_sockets_op_ports)
{
	std::vector<std::shared_ptr<aux::listen_socket_t>> sockets = {
		sock("10.10.10.10", 6881, 0), sock("4.4.4.4", 6881)
	};

	// replace OS assigned ports with explicit ports
	std::vector<aux::listen_endpoint_t> eps ={
		ep("10.10.10.10", 6882),
		ep("4.4.4.4", 6882),
	};
	auto remove_iter = aux::partition_listen_sockets(eps, sockets);
	TEST_CHECK(remove_iter == sockets.begin());
	TEST_EQUAL(eps.size(), 2);
}

TORRENT_TEST(expand_unspecified)
{
	std::vector<ip_interface> const ifs = {
		ifc("127.0.0.1", "lo")
		, ifc("192.168.1.2", "eth0")
		, ifc("24.172.48.90", "eth1")
		, ifc("::1", "lo")
		, ifc("fe80::d250:99ff:fe0c:9b74", "eth0")
		, ifc( "2601:646:c600:a3:d250:99ff:fe0c:9b74", "eth0")
	};

	auto v4_nossl      = ep("0.0.0.0", 6881);
	auto v4_ssl        = ep("0.0.0.0", 6882, tp::ssl);
	auto v6_unsp_nossl = ep("::", 6883);
	auto v6_unsp_ssl   = ep("::", 6884, tp::ssl);
	auto v6_ll_nossl   = ep("fe80::d250:99ff:fe0c:9b74", 6883);
	auto v6_ll_ssl     = ep("fe80::d250:99ff:fe0c:9b74", 6884, tp::ssl);
	auto v6_g_nossl    = ep("2601:646:c600:a3:d250:99ff:fe0c:9b74", 6883);
	auto v6_g_ssl      = ep("2601:646:c600:a3:d250:99ff:fe0c:9b74", 6884, tp::ssl);

	std::vector<aux::listen_endpoint_t> eps = {
		v4_nossl, v4_ssl, v6_unsp_nossl, v6_unsp_ssl
	};

	aux::expand_unspecified_address(ifs, eps);

	TEST_EQUAL(eps.size(), 6);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_ll_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_ll_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_unsp_nossl) == 0);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_unsp_ssl) == 0);

	// test that a user configured endpoint is not duplicated
	auto v6_g_nossl_dev = ep("2601:646:c600:a3:d250:99ff:fe0c:9b74", 6883, "eth0");

	eps.clear();
	eps.push_back(v6_unsp_nossl);
	eps.push_back(v6_g_nossl_dev);

	aux::expand_unspecified_address(ifs, eps);

	TEST_EQUAL(eps.size(), 2);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_ll_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_nossl) == 0);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_nossl_dev) == 1);
}

