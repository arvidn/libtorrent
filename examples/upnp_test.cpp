/*

Copyright (c) 2010, 2014-2021, Arvid Norberg
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

