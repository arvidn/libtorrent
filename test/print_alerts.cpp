/*

Copyright (c) 2016-2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "print_alerts.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "print_alerts.hpp"

void print_alerts(lt::session* ses, lt::time_point start_time)
{
	using namespace lt;

	if (ses == nullptr) return;

	std::vector<lt::alert*> alerts;
	ses->pop_alerts(&alerts);

	for (std::vector<lt::alert*>::iterator i = alerts.begin()
		, end(alerts.end()); i != end; ++i)
	{
		alert* a = *i;
#ifndef TORRENT_DISABLE_LOGGING
		if (peer_log_alert* pla = alert_cast<peer_log_alert>(a))
		{
			// in order to keep down the amount of logging, just log actual peer
			// messages
			if (pla->direction != peer_log_alert::incoming_message
				&& pla->direction != peer_log_alert::outgoing_message)
			{
				continue;
			}
		}
#endif
		lt::time_duration d = a->timestamp() - start_time;
		std::uint32_t millis = std::uint32_t(lt::duration_cast<lt::milliseconds>(d).count());
		std::printf("%4u.%03u: %-25s %s\n", millis / 1000, millis % 1000
			, a->what()
			, a->message().c_str());
	}

}

