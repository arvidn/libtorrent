/*

Copyright (c) 2010, 2014-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdlib>
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"

namespace {
void print_alert(lt::alert const* a)
{
	using namespace lt;

	if (alert_cast<portmap_error_alert>(a))
	{
		std::printf("%s","\x1b[32m");
	}
	else if (alert_cast<portmap_alert>(a))
	{
		std::printf("%s","\x1b[33m");
	}

	std::printf("%s\n", a->message().c_str());
	std::printf("%s", "\x1b[0m");
}
} // anonymous namespace

int main(int argc, char*[])
{
	using namespace lt;

	if (argc != 1)
	{
		fputs("usage: ./upnp_test\n", stderr);
		return 1;
	}

	settings_pack p;
	p.set_int(settings_pack::alert_mask, alert_category::port_mapping);
	lt::session s(p);

	for (;;)
	{
		alert const* a = s.wait_for_alert(seconds(5));
		if (a == nullptr)
		{
			p.set_bool(settings_pack::enable_upnp, false);
			p.set_bool(settings_pack::enable_natpmp, false);
			s.apply_settings(p);
			break;
		}
		std::vector<alert*> alerts;
		s.pop_alerts(&alerts);
		for (std::vector<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			print_alert(*i);
		}
	}

	std::printf("\x1b[1m\n\n===================== done mapping. Now deleting mappings ========================\n\n\n\x1b[0m");

	for (;;)
	{
		alert const* a = s.wait_for_alert(seconds(5));
		if (a == nullptr) break;
		std::vector<alert*> alerts;
		s.pop_alerts(&alerts);
		for (std::vector<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			print_alert(*i);
		}
	}


	return 0;
}

