/*

Copyright (c) 2016, Arvid Norberg
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

#include "print_alerts.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "print_alerts.hpp"

void print_alerts(libtorrent::session* ses, libtorrent::time_point start_time)
{
	using namespace libtorrent;
	namespace lt = libtorrent;

	if (ses == NULL) return;

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
		boost::uint32_t millis = lt::duration_cast<lt::milliseconds>(d).count();
		printf("%4d.%03d: %-25s %s\n", millis / 1000, millis % 1000
			, a->what()
			, a->message().c_str());
	}

}

