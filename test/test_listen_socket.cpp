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

	aux::listen_socket_t sock(char const* ip, int const port
		, int const original_port, char const* device = "", tp ssl = tp::plaintext)
	{
		aux::listen_socket_t s;
		s.local_endpoint = tcp::endpoint(address::from_string(ip), port);
		s.original_port = original_port;
		s.device = device;
		s.ssl = ssl;
		return s;
	}

	aux::listen_socket_t sock(char const* ip, int const port, tp ssl)
	{ return sock(ip, port, port, "", ssl); }

	aux::listen_socket_t sock(char const* ip, int const port, char const* dev)
	{ return sock(ip, port, port, dev); }

	aux::listen_socket_t sock(char const* ip, int const port)
	{ return sock(ip, port, port); }

} // anonymous namespace

bool operator!=(aux::listen_endpoint_t const& ep, aux::listen_socket_t const& socket)
{
	return !(ep == socket);
}

TORRENT_TEST(listen_socket_endpoint_compare)
{
	TEST_CHECK(ep("4.4.4.4", 6881) != sock("0.0.0.0", 6881));
	TEST_CHECK(ep("4.4.4.4", 6881) == sock("4.4.4.4", 6881));
	TEST_CHECK(ep("4.4.4.4", 6881) != sock("4.4.4.5", 6881));

	TEST_CHECK(ep("4.4.4.4", 6881) != sock("4.4.4.4", 6882));

	TEST_CHECK(ep("4.4.4.6", 6881, "eth1") == sock("4.4.4.6", 6881, "eth1"));
	TEST_CHECK(ep("4.4.4.6", 6881, "eth1") != sock("4.4.4.6", 6881, "enp3s0"));
	TEST_CHECK(ep("4.4.4.6", 6881, "eth1") != sock("4.4.4.6", 6881));

	TEST_CHECK(ep("4.4.4.4", 6881, "eth1") != sock("4.4.4.6", 6881, "eth1"));

	TEST_CHECK(ep("4.4.4.4", 6881) == sock("4.4.4.4", 6883, 6881));

	TEST_CHECK(ep("4.4.4.4", 6881, tp::ssl) == sock("4.4.4.4", 6881, tp::ssl));
	TEST_CHECK(ep("4.4.4.4", 6881, tp::ssl) != sock("4.4.4.4", 6881));

	TEST_CHECK(ep("4.4.4.4", 6882) != sock("4.4.4.4", 6881, 0));
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

