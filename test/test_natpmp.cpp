/*

Copyright (c) 2008-2009, 2011-2012, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
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

#include "libtorrent/natpmp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"
#include <functional>
#include <iostream>
#include <memory>

using namespace lt;

namespace
{
	struct natpmp_callback : aux::portmap_callback
	{
		virtual ~natpmp_callback() = default;

		void on_port_mapping(port_mapping_t const mapping
			, address const& ip, int port
			, portmap_protocol const protocol, error_code const& err
			, portmap_transport, aux::listen_socket_handle const&) override
		{
			std::cout
				<< "mapping: " << mapping
				<< ", port: " << port
				<< ", protocol: " << static_cast<int>(protocol)
				<< ", external-IP: " << print_address(ip)
				<< ", error: \"" << err.message() << "\"\n";
		}
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log_portmap(portmap_transport) const override
		{
			return true;
		}

		virtual void log_portmap(portmap_transport, char const* msg, aux::listen_socket_handle const&) const override
		{
			std::cout << msg << std::endl;
		}
#endif
	};
}

int main(int argc, char* argv[])
{
	io_context ios;
	std::string user_agent = "test agent";

	if (argc < 3 || argc > 4)
	{
		std::cout << "usage: test_natpmp tcp-port udp-port [interface]" << std::endl;
		return 1;
	}

	error_code ec;
	std::vector<ip_route> const routes = lt::enum_routes(ios, ec);
	if (ec)
	{
		std::cerr << "failed to enumerate routes: " << ec.message() << '\n';
		return -1;
	}
	std::vector<ip_interface> const ifs = lt::enum_net_interfaces(ios, ec);
	if (ec)
	{
		std::cerr << "failed to enumerate network interfaces: " << ec.message() << '\n';
		return -1;
	}
	auto const iface = [&]
	{
		if (argc > 3)
			return std::find_if(ifs.begin(), ifs.end(), [&](ip_interface const& ipf)
				{ return ipf.name == string_view(argv[3]); });
		else
			return std::find_if(ifs.begin(), ifs.end(), [&](ip_interface const& face)
				{
				if (!face.interface_address.is_v4()) return false;
				if (face.interface_address.is_loopback()) return false;
				auto const route = std::find_if(routes.begin(), routes.end(), [&](ip_route const& r)
					{ return r.destination.is_unspecified() && string_view(face.name) == r.name; });
				if (route == routes.end()) return false;
				return true;
				});
	}();

	if (iface == ifs.end())
	{
		if (argc < 4)
		{
			std::cerr << "could not find an IPv4 interface to run NAT-PMP test over!\n";
		}
		else
		{
			std::cerr << "could not find interface: \"" << argv[3] << "\"\navailable ones are:\n";
			for (auto const& ipf : ifs)
			{
				std::cerr << ipf.name << '\n';
			}
		}
		return -1;
	}

	natpmp_callback cb;
	auto natpmp_handler = std::make_shared<natpmp>(ios, cb, aux::listen_socket_handle{});
	natpmp_handler->start(*iface);

	deadline_timer timer(ios);

	auto const tcp_map = natpmp_handler->add_mapping(portmap_protocol::tcp
		, atoi(argv[1]), tcp::endpoint({}, aux::numeric_cast<std::uint16_t>(atoi(argv[1]))));
	natpmp_handler->add_mapping(portmap_protocol::udp, atoi(argv[2])
		, tcp::endpoint({}, aux::numeric_cast<std::uint16_t>(atoi(argv[2]))));

	timer.expires_after(seconds(2));
	timer.async_wait([&] (error_code const&) { ios.io_context::stop(); });
	std::cout << "attempting to map ports TCP: " << argv[1]
		<< " UDP: " << argv[2]
		<< " on interface: " << iface->name << std::endl;

	ios.restart();
	ios.run();
	timer.expires_after(seconds(2));
	timer.async_wait([&] (error_code const&) { ios.io_context::stop(); });
	if (tcp_map >= 0)
	{
		std::cout << "removing mapping " << tcp_map << std::endl;
		natpmp_handler->delete_mapping(tcp_map);
	}

	ios.restart();
	ios.run();
	natpmp_handler->close();

	ios.restart();
	ios.run();
	std::cout << "closing" << std::endl;
}
