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

#include <stdio.h>
#include <libtorrent/enum_net.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/broadcast_socket.hpp>

using namespace libtorrent;

int main()
{
	io_service ios;
	error_code ec;
	address local = guess_local_address(ios);
	printf("Local address: %s\n", local.to_string(ec).c_str());

	address def_gw = get_default_gateway(ios, ec);
	if (ec)
	{
		fprintf(stderr, "%s\n", ec.message().c_str());
		return 1;
	}

	printf("Default gateway: %s\n", def_gw.to_string(ec).c_str());

	printf("=========== Routes ===========\n");
	std::vector<ip_route> routes = enum_routes(ios, ec);
	if (ec)
	{
		printf("%s\n", ec.message().c_str());
		return 1;
	}

	printf("%-18s%-18s%-35s%-7sinterface\n", "destination", "network", "gateway", "mtu");

	for (std::vector<ip_route>::const_iterator i = routes.begin()
		, end(routes.end()); i != end; ++i)
	{
		printf("%-18s%-18s%-35s%-7d%s\n"
			, i->destination.to_string(ec).c_str()
			, i->netmask.to_string(ec).c_str()
			, i->gateway.to_string(ec).c_str()
			, i->mtu
			, i->name);
	}

	printf("========= Interfaces =========\n");

	std::vector<ip_interface> const& net = enum_net_interfaces(ios, ec);
	if (ec)
	{
		printf("%s\n", ec.message().c_str());
		return 1;
	}

	printf("%-30s%-45s%-20s%-8sflags\n", "address", "netmask", "name", "mtu");

	for (std::vector<ip_interface>::const_iterator i = net.begin()
		, end(net.end()); i != end; ++i)
	{
		printf("%-30s%-45s%-20s%-8d%s%s%s\n"
			, i->interface_address.to_string(ec).c_str()
			, i->netmask.to_string(ec).c_str()
			, i->name
			, i->mtu
			, (is_multicast(i->interface_address)?"multicast ":"")
			, (is_local(i->interface_address)?"local ":"")
			, (is_loopback(i->interface_address)?"loopback ":"")
			);
	}
}

