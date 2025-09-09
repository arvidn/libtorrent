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

#include <cstdio>
#include <string>
#include <libtorrent/enum_net.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/broadcast_socket.hpp>

using namespace lt;

namespace {

std::string operator "" _s(char const* str, size_t len) { return std::string(str, len); }

std::string safe_to_string(address const& addr)
{
	try {
		return addr.to_string();
	} catch (...) {
		return "invalid-address";
	}
}

std::string print_flags(interface_flags const f)
{
	return
		((f & if_flags::up) ? "UP "_s : ""_s)
		+ ((f & if_flags::broadcast) ? "BROADCAST "_s : ""_s)
		+ ((f & if_flags::loopback) ? "LOOP "_s : ""_s)
		+ ((f & if_flags::pointopoint) ? "PPP "_s : ""_s)
		+ ((f & if_flags::running) ? "RUN "_s : ""_s)
		+ ((f & if_flags::noarp) ? "NOARP "_s : ""_s)
		+ ((f & if_flags::promisc) ? "PROMISC "_s : ""_s)
		+ ((f & if_flags::allmulti) ? "ALLMULTI "_s : ""_s)
		+ ((f & if_flags::master) ? "MASTER "_s : ""_s)
		+ ((f & if_flags::slave) ? "SLAVE "_s : ""_s)
		+ ((f & if_flags::multicast) ? "MULTICAST "_s : ""_s)
		+ ((f & if_flags::dynamic) ? "SYN "_s : ""_s)
		+ ((f & if_flags::lower_up) ? "LWR_UP "_s : ""_s)
		+ ((f & if_flags::dormant) ? "DORMANT "_s : ""_s)
		;
}

char const* print_state(if_state const s)
{
	switch (s)
	{
		case if_state::up: return "up";
		case if_state::dormant: return "dormant";
		case if_state::lowerlayerdown: return "lowerlayerdown";
		case if_state::down: return "down";
		case if_state::notpresent: return "notpresent";
		case if_state::testing: return "testing";
		case if_state::unknown: return "unknown";
	};
	return "unknown";
}

}

int main()
{
	io_service ios;
	error_code ec;

	std::printf("=========== Routes ===========\n");
	auto const routes = enum_routes(ios, ec);
	if (ec)
	{
		std::printf("enum_routes: %s\n", ec.message().c_str());
		return 1;
	}

	std::printf("%-45s%-45s%-35s%-7s%-18s%s\n", "destination", "network", "gateway", "mtu", "source-hint", "interface");

	for (auto const& r : routes)
	{
		std::printf("%-45s%-45s%-35s%-7d%-18s%s\n"
			, safe_to_string(r.destination).c_str()
			, safe_to_string(r.netmask).c_str()
			, r.gateway.is_unspecified() ? "-" : safe_to_string(r.gateway).c_str()
			, r.mtu
			, r.source_hint.is_unspecified() ? "-" : safe_to_string(r.source_hint).c_str()
			, r.name);
	}

	std::printf("========= Interfaces =========\n");

	auto const net = enum_net_interfaces(ios, ec);
	if (ec)
	{
		std::printf("enum_ifs: %s\n", ec.message().c_str());
		return 1;
	}

	std::printf("%-34s%-45s%-20s%-20s%-15s%-20s%s\n", "address", "netmask", "name", "gateway", "state", "flags", "description");

	for (auto const& i : net)
	{
		boost::optional<address> const gateway = get_gateway(i, routes);
		std::printf("%-34s%-45s%-20s%-20s%-15s%-20s%s %s\n"
			, safe_to_string(i.interface_address).c_str()
			, safe_to_string(i.netmask).c_str()
			, i.name
			, gateway ? safe_to_string(*gateway).c_str() : "-"
			, print_state(i.state)
			, print_flags(i.flags).c_str()
			, i.friendly_name
			, i.description);
	}
}
