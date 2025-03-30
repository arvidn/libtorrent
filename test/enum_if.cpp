/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2007-2008, 2016-2017, 2019-2021, Arvid Norberg
Copyright (c) 2018, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdio>
#include <string>
#include "libtorrent/aux_/enum_net.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"

using namespace lt;

namespace {

std::string operator ""_s(char const* str, size_t len) { return std::string(str, len); }

std::string print_flags(aux::interface_flags const f)
{
	namespace if_flags = aux::if_flags;

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

char const* print_state(aux::if_state const s)
{
	using if_state = aux::if_state;

	switch (s)
	{
		case if_state::up: return "up";
		case if_state::dormant: return "dormant";
		case if_state::lowerlayerdown: return "lowerlayerdown";
		case if_state::down: return "down";
		case if_state::notpresent: return "notpresent";
		case if_state::testing: return "testing";
		case if_state::unknown: return "unknown";
	}
	return "unknown";
}

}

int main()
{
	io_context ios;
	error_code ec;

	std::printf("=========== Routes ===========\n");
	auto const routes = aux::enum_routes(ios, ec);
	if (ec)
	{
		std::printf("enum_routes: %s\n", ec.message().c_str());
		return 1;
	}

	std::printf("%-45s%-45s%-35s%-7s%-18s%s\n", "destination", "network", "gateway", "mtu", "source-hint", "interface");

	for (auto const& r : routes)
	{
		std::printf("%-45s%-45s%-35s%-7d%-18s%s\n"
			, r.destination.to_string().c_str()
			, r.netmask.to_string().c_str()
			, r.gateway.is_unspecified() ? "-" : r.gateway.to_string().c_str()
			, r.mtu
			, r.source_hint.is_unspecified() ? "-" : r.source_hint.to_string().c_str()
			, r.name);
	}

	std::printf("========= Interfaces =========\n");

	auto const net = aux::enum_net_interfaces(ios, ec);
	if (ec)
	{
		std::printf("enum_ifs: %s\n", ec.message().c_str());
		return 1;
	}

	std::printf("%-34s%-45s%-20s%-20s%-15s%-20s%s\n", "address", "netmask", "name", "gateway", "state", "flags", "description");

	for (auto const& i : net)
	{
		std::optional<address> const gateway = get_gateway(i, routes);
		std::printf("%-34s%-45s%-20s%-20s%-15s%-20s%s %s\n"
			, i.interface_address.to_string().c_str()
			, i.netmask.to_string().c_str()
			, i.name
			, gateway ? gateway->to_string().c_str() : "-"
			, print_state(i.state)
			, print_flags(i.flags).c_str()
			, i.friendly_name
			, i.description);
	}
}
