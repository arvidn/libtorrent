/*

Copyright (c) 2003-2016, Arvid Norberg, Daniel Wallin
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

#ifndef TORRENT_ALERT_HPP_INCLUDED
#define TORRENT_ALERT_HPP_INCLUDED

#include <memory>
#include <deque>
#include <string>
#include <vector>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/enum.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_binary_params.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

// OVERVIEW
//
// The pop_alerts() function on session is the main interface for retrieving
// alerts (warnings, messages and errors from libtorrent). If no alerts have
// been posted by libtorrent pop_alerts() will return an empty list.
// 
// By default, only errors are reported. settings_pack::alert_mask can be
// used to specify which kinds of events should be reported. The alert mask is
// comprised by bits from the category_t enum.
// 
// Every alert belongs to one or more category. There is a cost associated with
// posting alerts. Only alerts that belong to an enabled category are
// posted. Setting the alert bitmask to 0 will disable all alerts (except those
// that are non-discardable). Alerts that are responses to API calls such as
// save_resume_data() and post_session_stats() are non-discardable and will be
// posted even if their category is disabled.
// 
// There are other alert base classes that some alerts derive from, all the
// alerts that are generated for a specific torrent are derived from
// torrent_alert, and tracker events derive from tracker_alert.
//
// Alerts returned by pop_alerts() are only valid until the next call to
// pop_alerts(). You may not copy an alert object to access it after the next
// call to pop_alerts(). Internal members of alerts also become invalid once
// pop_alerts() is called again.

#include "libtorrent/time.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent {

	// The ``alert`` class is the base class that specific messages are derived from.
	// alert types are not copyable, and cannot be constructed by the client. The
	// pointers returned by libtorrent are short lived (the details are described
	// under session_handle::pop_alerts())
	class TORRENT_EXPORT alert
	{
	public:

#ifndef TORRENT_NO_DEPRECATE
		// only here for backwards compatibility
		enum severity_t { debug, info, warning, critical, fatal, none };
#endif

		// these are bits for the alert_mask used by the session. See
		// settings_pack::alert_mask.
		enum category_t
		{
			// Enables alerts that report an error. This includes:
			// 
			// * tracker errors
			// * tracker warnings
			// * file errors
			// * resume data failures
			// * web seed errors
			// * .torrent files errors
			// * listen socket errors
			// * port mapping errors
			error_notification            = 0x1,

			// Enables alerts when peers send invalid requests, get banned or
			// snubbed.
			peer_notification             = 0x2,

			// Enables alerts for port mapping events. For NAT-PMP and UPnP.
			port_mapping_notification     = 0x4,

			// Enables alerts for events related to the storage. File errors and
			// synchronization events for moving the storage, renaming files etc.
			storage_notification          = 0x8,

			// Enables all tracker events. Includes announcing to trackers,
			// receiving responses, warnings and errors.
			tracker_notification          = 0x10,

			// Low level alerts for when peers are connected and disconnected.
			debug_notification            = 0x20,

			// Enables alerts for when a torrent or the session changes state.
			status_notification           = 0x40,

			// Alerts for when blocks are requested and completed. Also when
			// pieces are completed.
			progress_notification         = 0x80,

			// Alerts when a peer is blocked by the ip blocker or port blocker.
			ip_block_notification         = 0x100,

			// Alerts when some limit is reached that might limit the download
			// or upload rate.
			performance_warning           = 0x200,

			// Alerts on events in the DHT node. For incoming searches or
			// bootstrapping being done etc.
			dht_notification              = 0x400,

			// If you enable these alerts, you will receive a stats_alert
			// approximately once every second, for every active torrent.
			// These alerts contain all statistics counters for the interval since
			// the lasts stats alert.
			stats_notification            = 0x800,

#ifndef TORRENT_NO_DEPRECATE
			// Alerts on RSS related events, like feeds being updated, feed error
			// conditions and successful RSS feed updates. Enabling this categoty
			// will make you receive rss_alert alerts.
			rss_notification              = 0x1000,
#endif

			// Enables debug logging alerts. These are available unless libtorrent
			// was built with logging disabled (``TORRENT_DISABLE_LOGGING``). The
			// alerts being posted are log_alert and are session wide.
			session_log_notification      = 0x2000,

			// Enables debug logging alerts for torrents. These are available
			// unless libtorrent was built with logging disabled
			// (``TORRENT_DISABLE_LOGGING``). The alerts being posted are
			// torrent_log_alert and are torrent wide debug events.
			torrent_log_notification      = 0x4000,

			// Enables debug logging alerts for peers. These are available unless
			// libtorrent was built with logging disabled
			// (``TORRENT_DISABLE_LOGGING``). The alerts being posted are
			// peer_log_alert and low-level peer events and messages.
			peer_log_notification         = 0x8000,

			// enables the incoming_request_alert.
			incoming_request_notification = 0x10000,

			// enables dht_log_alert, debug logging for the DHT
			dht_log_notification          = 0x20000,

			// enable events from pure dht operations not related to torrents
			dht_operation_notification    = 0x40000,

			// enables port mapping log events. This log is useful
			// for debugging the UPnP or NAT-PMP implementation
			port_mapping_log_notification = 0x80000,

			// enables verbose logging from the piece picker.
			picker_log_notification       = 0x100000,

			// The full bitmask, representing all available categories.
			//
			// since the enum is signed, make sure this isn't
			// interpreted as -1. For instance, boost.python
			// does that and fails when assigning it to an
			// unsigned parameter.
			all_categories = 0x7fffffff
		};

		// hidden
		alert();
		// hidden
		virtual ~alert();

		// a timestamp is automatically created in the constructor
		time_point timestamp() const;

		// returns an integer that is unique to this alert type. It can be
		// compared against a specific alert by querying a static constant called ``alert_type``
		// in the alert. It can be used to determine the run-time type of an alert* in
		// order to cast to that alert type and access specific members.
		// 
		// e.g:
		//
		// .. code:: c++
		//
		//	std::vector<alert*> alerts;
		//	ses.pop_alerts(&alerts);
		//	for (alert* i : alerts) {
		//		switch (a->type()) {
		// 
		//			case read_piece_alert::alert_type:
		//			{
		//				read_piece_alert* p = (read_piece_alert*)a;
		//				if (p->ec) {
		//					// read_piece failed
		//					break;
		//				}
		//				// use p
		//				break;
		//			}
		//			case file_renamed_alert::alert_type:
		//			{
		//				// etc...
		//			}
		//		}
		//	}
		virtual int type() const = 0;

		// returns a string literal describing the type of the alert. It does
		// not include any information that might be bundled with the alert.
		virtual char const* what() const = 0;

		// generate a string describing the alert and the information bundled
		// with it. This is mainly intended for debug and development use. It is not suitable
		// to use this for applications that may be localized. Instead, handle each alert
		// type individually and extract and render the information from the alert depending
		// on the locale.
		virtual std::string message() const = 0;

		// returns a bitmask specifying which categories this alert belong to.
		virtual int category() const = 0;

#ifndef TORRENT_NO_DEPRECATE

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

		// determines whether or not an alert is allowed to be discarded
		// when the alert queue is full. There are a few alerts which may not be discared,
		// since they would break the user contract, such as save_resume_data_alert.
		TORRENT_DEPRECATED
		bool discardable() const { return discardable_impl(); }

		TORRENT_DEPRECATED
		severity_t severity() const { return warning; }

		// returns a pointer to a copy of the alert.
		TORRENT_DEPRECATED
		std::auto_ptr<alert> clone() const { return clone_impl(); }

	protected:

		virtual bool discardable_impl() const { return true; }

		virtual std::auto_ptr<alert> clone_impl() const = 0;

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif // TORRENT_NO_DEPRECATE

	protected:
		// the alert is not copyable (but for backwards compatibility reasons it
		// retains the ability to clone itself, for now).
#if __cplusplus >= 201103L
		alert(alert const& rhs) = default;
#endif

	private:
		// explicitly disallow assignment and copyconstruction
		alert& operator=(alert const&);

		time_point m_timestamp;
	};

// When you get an alert, you can use ``alert_cast<>`` to attempt to cast the pointer to a
// more specific alert type, in order to query it for more information.
template <class T> T* alert_cast(alert* a)
{
	if (a == 0) return 0;
	if (a->type() == T::alert_type) return static_cast<T*>(a);
	return 0;
}
template <class T> T const* alert_cast(alert const* a)
{
	if (a == 0) return 0;
	if (a->type() == T::alert_type) return static_cast<T const*>(a);
	return 0;
}

} // namespace libtorrent

#endif // TORRENT_ALERT_HPP_INCLUDED

