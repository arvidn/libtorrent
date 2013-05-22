/*

Copyright (c) 2012, Arvid Norberg
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

#ifndef TORRENT_ALERT_HANDLER_HPP_INCLUDED
#define TORRENT_ALERT_HANDLER_HPP_INCLUDED

#include "libtorrent/alert_types.hpp" // for num_alert_types
#include <vector>

namespace libtorrent
{

	struct alert_observer;
	struct alert_handler;

	// block until the specified alert is posted to
	// the alert_handler, return a copy of the alert
	// keep in mind that this has to be called from a
	// different thread than the one calling dispatch_alerts(),
	// otherwise you'll have a deadlock.
	TORRENT_EXPORT std::auto_ptr<alert> wait_for_alert(alert_handler& h, int type);

struct TORRENT_EXPORT alert_handler
{
	// TODO 2: move the responsibility of picking which
	// alert types to subscribe to to the observer
	// TODO 3: make subscriptions automatically enable
	// the corresponding category of alerts in the session somehow
	// TODO 3: perhaps this class could hold a reference to session and
	// make dispatch_alerts() not take any arguments.
	void subscribe(alert_observer* o, int flags = 0, ...);
	void dispatch_alerts(std::deque<alert*>& alerts) const;
	void unsubscribe(alert_observer* o);

private:

	void subscribe_impl(int const* type_list, int num_types, alert_observer* o, int flags);

	std::vector<alert_observer*> m_observers[num_alert_types];
};


}

#endif // TORRENT_ALERT_HANDLER_HPP_INCLUDED

