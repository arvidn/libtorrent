/*

Copyright (c) 2016-2017, Steven Siloti
Copyright (c) 2017, 2019-2021, Arvid Norberg
Copyright (c) 2018, Alden Torres
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
#include "libtorrent/string_util.hpp"

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

	ip_interface ifc(char const* ip, char const* device, char const* netmask = nullptr)
	{
		ip_interface ipi;
		ipi.interface_address = make_address(ip);
		if (netmask) ipi.netmask = make_address(netmask);
		std::strncpy(ipi.name, device, sizeof(ipi.name) - 1);
		return ipi;
	}

	ip_interface ifc(char const* ip, char const* device, interface_flags const flags
		, char const* netmask = nullptr)
	{
		ip_interface ipi;
		ipi.interface_address = make_address(ip);
		if (netmask) ipi.netmask = make_address(netmask);
		std::strncpy(ipi.name, device, sizeof(ipi.name) - 1);
		ipi.flags = flags;
		return ipi;
	}

	ip_route rt(char const* ip, char const* device, char const* gateway)
	{
		ip_route ret;
		ret.destination = make_address(ip);
		ret.gateway = make_address(gateway);
		std::strncpy(ret.name, device, sizeof(ret.name) - 1);
		ret.name[sizeof(ret.name) - 1] = '\0';
		ret.mtu = 1500;
		return ret;
	}

	aux::listen_endpoint_t ep(char const* ip, int port
		, tp ssl, aux::listen_socket_flags_t const flags)
	{
		return aux::listen_endpoint_t(make_address(ip), port, std::string{}
			, ssl, flags);
	}

	aux::listen_endpoint_t ep(char const* ip, int port
		, tp ssl = tp::plaintext
		, std::string device = {})
	{
		return aux::listen_endpoint_t(make_address(ip), port, device, ssl
			, aux::listen_socket_t::accept_incoming);
	}

	aux::listen_endpoint_t ep(char const* ip, int port
		, std::string device
		, tp ssl = tp::plaintext)
	{
		return aux::listen_endpoint_t(make_address(ip), port, device, ssl
			, aux::listen_socket_t::accept_incoming);
	}

	aux::listen_endpoint_t ep(char const* ip, int port
		, std::string device
		, aux::listen_socket_flags_t const flags)
	{
		return aux::listen_endpoint_t(make_address(ip), port, device
			, tp::plaintext, flags);
	}

	aux::listen_endpoint_t ep(char const* ip, int port
		, aux::listen_socket_flags_t const flags)
	{
		return aux::listen_endpoint_t(make_address(ip), port, std::string{}
			, tp::plaintext, flags);
	}

	std::shared_ptr<aux::listen_socket_t> sock(char const* ip, int const port
		, int const original_port, char const* device = "")
	{
		auto s = std::make_shared<aux::listen_socket_t>();
		s->local_endpoint = tcp::endpoint(make_address(ip)
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
	test_equal(*sockets.front(), make_address_v4("4.4.4.4"), 6881, "", tp::plaintext);
	test_equal(*sockets.back(), address_v4(), 6881, "", tp::plaintext);
	test_equal(eps.front(), make_address_v4("4.4.4.5"), 6881, "", tp::plaintext);
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
	test_equal(*sockets.front(), make_address_v4("4.4.4.5"), 6881, "", tp::plaintext);
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
	test_equal(*sockets.front(), make_address_v4("10.10.10.10"), 6881, "enp3s0", tp::plaintext);
	test_equal(*sockets.back(), make_address_v4("4.4.4.4"), 6881, "enp3s0", tp::plaintext);
	TEST_EQUAL(eps.size(), 1);
	test_equal(eps.front(), make_address_v4("4.4.4.5"), 6881, "enp3s0", tp::plaintext);
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

TORRENT_TEST(expand_devices)
{
	std::vector<ip_interface> const ifs = {
		ifc("127.0.0.1", "lo", "255.0.0.0")
		, ifc("192.168.1.2", "eth0", "255.255.255.0")
		, ifc("24.172.48.90", "eth1", "255.255.255.0")
		, ifc("::1", "lo", "ffff:ffff:ffff:ffff::")
		, ifc("fe80::d250:99ff:fe0c:9b74", "eth0", "ffff:ffff:ffff:ffff::")
		, ifc("2601:646:c600:a3:d250:99ff:fe0c:9b74", "eth0", "ffff:ffff:ffff:ffff::")
	};

	std::vector<aux::listen_endpoint_t> eps = {
		{
			make_address("127.0.0.1"),
			6881, // port
			"", // device
			aux::transport::plaintext,
			aux::listen_socket_flags_t{} },
		{
			make_address("192.168.1.2"),
			6881, // port
			"", // device
			aux::transport::plaintext,
			aux::listen_socket_flags_t{} }
	};

	expand_devices(ifs, eps);

	TEST_CHECK((eps == std::vector<aux::listen_endpoint_t>{
		{
			make_address("127.0.0.1"),
			6881, // port
			"lo", // device
			aux::transport::plaintext,
			aux::listen_socket_flags_t{},
			make_address("255.0.0.0") },
		{
			make_address("192.168.1.2"),
			6881, // port
			"eth0", // device
			aux::transport::plaintext,
			aux::listen_socket_flags_t{},
			make_address("255.255.255.0") },
		}));
}

TORRENT_TEST(expand_unspecified)
{
	// this causes us to only expand IPv6 addresses on eth0
	std::vector<ip_route> const routes = {
		rt("0.0.0.0", "eth0", "1.2.3.4"),
		rt("::", "eth0", "1234:5678::1"),
	};

	std::vector<ip_interface> const ifs = {
		ifc("127.0.0.1", "lo")
		, ifc("192.168.1.2", "eth0")
		, ifc("24.172.48.90", "eth1")
		, ifc("::1", "lo")
		, ifc("fe80::d250:99ff:fe0c:9b74", "eth0")
		, ifc("2601:646:c600:a3:d250:99ff:fe0c:9b74", "eth0")
	};

	aux::listen_socket_flags_t const global = aux::listen_socket_t::accept_incoming
		| aux::listen_socket_t::was_expanded;
	aux::listen_socket_flags_t const local = aux::listen_socket_t::accept_incoming
		| aux::listen_socket_t::was_expanded
		| aux::listen_socket_t::local_network;

	auto v4_nossl      = ep("0.0.0.0", 6881);
	auto v4_ssl        = ep("0.0.0.0", 6882, tp::ssl);
	auto v4_loopb_nossl= ep("127.0.0.1", 6881, local);
	auto v4_loopb_ssl  = ep("127.0.0.1", 6882, tp::ssl, local);
	auto v4_g1_nossl   = ep("192.168.1.2", 6881, global);
	auto v4_g1_ssl     = ep("192.168.1.2", 6882, tp::ssl, global);
	auto v4_g2_nossl   = ep("24.172.48.90", 6881, global);
	auto v4_g2_ssl     = ep("24.172.48.90", 6882, tp::ssl, global);
	auto v6_unsp_nossl = ep("::", 6883, global);
	auto v6_unsp_ssl   = ep("::", 6884, tp::ssl, global);
	auto v6_ll_nossl   = ep("fe80::d250:99ff:fe0c:9b74", 6883, local);
	auto v6_ll_ssl     = ep("fe80::d250:99ff:fe0c:9b74", 6884, tp::ssl, local);
	auto v6_g_nossl    = ep("2601:646:c600:a3:d250:99ff:fe0c:9b74", 6883, global);
	auto v6_g_ssl      = ep("2601:646:c600:a3:d250:99ff:fe0c:9b74", 6884, tp::ssl, global);
	auto v6_loopb_ssl  = ep("::1", 6884, tp::ssl, local);
	auto v6_loopb_nossl= ep("::1", 6883, local);

	std::vector<aux::listen_endpoint_t> eps = {
		v4_nossl, v4_ssl, v6_unsp_nossl, v6_unsp_ssl
	};

	aux::expand_unspecified_address(ifs, routes, eps);

	TEST_EQUAL(eps.size(), 12);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_g1_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_g1_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_g2_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_g2_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_ll_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_ll_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_loopb_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_loopb_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_loopb_ssl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_loopb_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_unsp_nossl) == 0);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_unsp_ssl) == 0);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_nossl) == 0);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v4_ssl) == 0);

	// test that a user configured endpoint is not duplicated
	auto v6_g_nossl_dev = ep("2601:646:c600:a3:d250:99ff:fe0c:9b74", 6883, "eth0");

	eps.clear();
	eps.push_back(v6_unsp_nossl);
	eps.push_back(v6_g_nossl_dev);

	aux::expand_unspecified_address(ifs, routes, eps);

	TEST_EQUAL(eps.size(), 3);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_ll_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_nossl) == 0);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_loopb_nossl) == 1);
	TEST_CHECK(std::count(eps.begin(), eps.end(), v6_g_nossl_dev) == 1);
}

using eps_t =  std::vector<aux::listen_endpoint_t>;

auto const global = aux::listen_socket_t::accept_incoming
	| aux::listen_socket_t::was_expanded;
auto const local = global | aux::listen_socket_t::local_network;

TORRENT_TEST(expand_unspecified_no_default)
{
	// even though this route isn't a default route, it's a route for a global
	// internet address
	std::vector<ip_route> const routes = {
		rt("128.0.0.0", "eth0", "128.0.0.0"),
	};

	std::vector<ip_interface> const ifs = { ifc("192.168.1.2", "eth0", "255.255.0.0") };
	eps_t eps = { ep("0.0.0.0", 6881) };

	aux::expand_unspecified_address(ifs, routes, eps);

	TEST_CHECK(eps == eps_t{ ep("192.168.1.2", 6881, global) });
}

namespace {

void test_expand_unspecified_if_flags(interface_flags const flags
	, eps_t const& expected)
{
	// even though this route isn't a default route, it's a route for a global
	// internet address
	std::vector<ip_route> const routes = {
		rt("0.0.0.0", "eth99", "0.0.0.0"),
	};

	std::vector<ip_interface> const ifs = { ifc("192.168.1.2", "eth0", flags) };
	eps_t eps = { ep("0.0.0.0", 6881) };
	aux::expand_unspecified_address(ifs, routes, eps);
	TEST_CHECK((eps == expected));
}

void test_expand_unspecified_if_address(char const* address, eps_t const& expected)
{
	std::vector<ip_route> const routes;
	std::vector<ip_interface> const ifs = { ifc(address, "eth0", "255.255.0.0") };
	eps_t eps = { ep("0.0.0.0", 6881) };

	aux::expand_unspecified_address(ifs, routes, eps);

	TEST_CHECK(eps == expected);
}

}

TORRENT_TEST(expand_unspecified_ppp)
{
	test_expand_unspecified_if_flags(if_flags::up | if_flags::pointopoint, eps_t{ ep("192.168.1.2", 6881, global) });
	test_expand_unspecified_if_flags(if_flags::up, eps_t{ ep("192.168.1.2", 6881, local) });
}

TORRENT_TEST(expand_unspecified_down_if)
{
	test_expand_unspecified_if_flags({}, eps_t{});
	test_expand_unspecified_if_flags(if_flags::up, eps_t{ ep("192.168.1.2", 6881, local) });
}

TORRENT_TEST(expand_unspecified_if_loopback)
{
	test_expand_unspecified_if_flags(if_flags::up | if_flags::loopback, eps_t{ ep("192.168.1.2", 6881, local) });
}

TORRENT_TEST(expand_unspecified_global_address)
{
	test_expand_unspecified_if_address("1.2.3.4", eps_t{ ep("1.2.3.4", 6881, global)});
}

TORRENT_TEST(expand_unspecified_link_local)
{
	test_expand_unspecified_if_address("169.254.1.2", eps_t{ ep("169.254.1.2", 6881, local)});
}

TORRENT_TEST(expand_unspecified_loopback)
{
	test_expand_unspecified_if_address("127.1.1.1", eps_t{ ep("127.1.1.1", 6881, local)});
}

namespace {
std::vector<aux::listen_endpoint_t> to_endpoint(listen_interface_t const& iface
	, span<ip_interface const> const ifs)
{
	std::vector<aux::listen_endpoint_t> ret;
	interface_to_endpoints(iface, aux::listen_socket_t::accept_incoming, ifs, ret);
	return ret;
}

using eps = std::vector<aux::listen_endpoint_t>;

listen_interface_t ift(char const* dev, int const port, bool const ssl = false
	, bool const l= false)
{
	return {std::string(dev), port, ssl, l};
}
}
using ls = aux::listen_socket_t;

TORRENT_TEST(interface_to_endpoint)
{
	TEST_CHECK(to_endpoint(ift("10.0.1.1", 6881), {}) == eps{ep("10.0.1.1", 6881)});


	std::vector<ip_interface> const ifs = {
		// this is a global IPv4 address, not a private network
		ifc("185.0.1.2", "eth0")
		, ifc("192.168.2.2", "eth1")
		, ifc("fe80::d250:99ff:fe0c:9b74", "eth0")
		// this is a global IPv6 address, not a private network
		, ifc("2601:646:c600:a3:d250:99ff:fe0c:9b74", "eth1")
	};

	TEST_CHECK((to_endpoint(ift("eth0", 1234), ifs)
		== eps{ep("185.0.1.2", 1234, "eth0", ls::was_expanded | ls::accept_incoming)
		, ep("fe80::d250:99ff:fe0c:9b74", 1234, "eth0", ls::was_expanded | ls::accept_incoming | ls::local_network)}));

	TEST_CHECK((to_endpoint(ift("eth1", 1234), ifs)
		== eps{ep("192.168.2.2", 1234, "eth1", ls::was_expanded | ls::accept_incoming)
		, ep("2601:646:c600:a3:d250:99ff:fe0c:9b74", 1234, "eth1", ls::was_expanded | ls::accept_incoming)}));

	std::vector<ip_interface> const ifs2 = {
		ifc("10.0.1.1", "eth0")
	};

	TEST_CHECK((to_endpoint(ift("eth0", 1234), ifs2)
		== eps{ep("10.0.1.1", 1234, "eth0", ls::was_expanded | ls::accept_incoming)}));
}

