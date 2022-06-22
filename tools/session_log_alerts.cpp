/*

Copyright (c) 2017, Alden Torres
Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2018, Eugene Shalygin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"

#include <cstdio>
#include <cinttypes>
#include <cstdlib>

using namespace lt;

int main(int /*argc*/, char* /*argv*/[])
{
	std::printf("press Ctrl+C, kill the process or wait for 1000 alerts\n");

	settings_pack sett;
	sett.set_int(settings_pack::alert_mask, 0x7fffffff);
	session s(sett);

	int count = 0;
	while (count <= 1000)
	{
		s.wait_for_alert(seconds(5));

		std::vector<alert*> alerts;
		s.pop_alerts(&alerts);
		for (auto const a : alerts)
		{
			if (a->type() == log_alert::alert_type)
			{
				std::printf("log_alert - %s\n", a->message().c_str());
				count++;
			}
		}
	}
	std::printf("\n");

	return 0;
}
