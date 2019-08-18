/*

Copyright (c) 2007-2009, 2016-2019, Arvid Norberg
Copyright (c) 2018, Steven Siloti
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

#include <cstdio>
#include <libtorrent/enum_net.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/broadcast_socket.hpp>

using namespace lt;

int main()
{
	io_context ios;
	error_code ec;

	address def_gw = get_default_gateway(ios, "", false, ec);
	if (ec)
	{
		std::printf("%s\n", ec.message().c_str());
		return 1;
	}

	std::printf("Default gateway: %s\n", def_gw.to_string().c_str());

	std::printf("=========== Routes ===========\n");
	auto const routes = enum_routes(ios, ec);
	if (ec)
	{
		std::printf("%s\n", ec.message().c_str());
		return 1;
	}

	std::printf("%-18s%-18s%-35s%-7sinterface\n", "destination", "network", "gateway", "mtu");

	for (auto const& r : routes)
	{
		std::printf("%-18s%-18s%-35s%-7d%s\n"
			, r.destination.to_string().c_str()
			, r.netmask.to_string().c_str()
			, r.gateway.to_string().c_str()
			, r.mtu
			, r.name);
	}

	std::printf("========= Interfaces =========\n");

	auto const net = enum_net_interfaces(ios, ec);
	if (ec)
	{
		std::printf("%s\n", ec.message().c_str());
		return 1;
	}

	std::printf("%-34s%-45s%-20s%-20s%-34sdescription\n", "address", "netmask", "name", "flags", "default gateway");

	for (auto const& i : net)
	{
		address const iface_def_gw = get_default_gateway(ios, i.name, i.interface_address.is_v6(), ec);
		std::printf("%-34s%-45s%-20s%s%s%-20s%-34s%s %s\n"
			, i.interface_address.to_string().c_str()
			, i.netmask.to_string().c_str()
			, i.name
			, (i.interface_address.is_multicast()?"multicast ":"")
			, (is_local(i.interface_address)?"local ":"")
			, (is_loopback(i.interface_address)?"loopback ":"")
			, iface_def_gw.to_string().c_str()
			, i.friendly_name, i.description);
	}
}
