/*

Copyright (c) 2003-2013, Arvid Norberg, Daniel Wallin
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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/preprocessor/repetition/enum.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_params.hpp>
#include <boost/preprocessor/repetition/enum_shifted_binary_params.hpp>

// OVERVIEW
//
// The pop_alert() function on session is the interface for retrieving
// alerts, warnings, messages and errors from libtorrent. If no alerts have
// been posted by libtorrent pop_alert() will return a default initialized
// ``std::auto_ptr`` object. If there is an alert in libtorrent's queue, the alert
// from the front of the queue is popped and returned.
// You can then use the alert object and query
// 
// By default, only errors are reported. set_alert_mask() can be
// used to specify which kinds of events should be reported. The alert mask
// is a bitmask with the following bits:
// 
// +--------------------------------+---------------------------------------------------------------------+
// | ``error_notification``         | Enables alerts that report an error. This includes:                 |
// |                                |                                                                     |
// |                                | * tracker errors                                                    |
// |                                | * tracker warnings                                                  |
// |                                | * file errors                                                       |
// |                                | * resume data failures                                              |
// |                                | * web seed errors                                                   |
// |                                | * .torrent files errors                                             |
// |                                | * listen socket errors                                              |
// |                                | * port mapping errors                                               |
// +--------------------------------+---------------------------------------------------------------------+
// | ``peer_notification``          | Enables alerts when peers send invalid requests, get banned or      |
// |                                | snubbed.                                                            |
// +--------------------------------+---------------------------------------------------------------------+
// | ``port_mapping_notification``  | Enables alerts for port mapping events. For NAT-PMP and UPnP.       |
// +--------------------------------+---------------------------------------------------------------------+
// | ``storage_notification``       | Enables alerts for events related to the storage. File errors and   |
// |                                | synchronization events for moving the storage, renaming files etc.  |
// +--------------------------------+---------------------------------------------------------------------+
// | ``tracker_notification``       | Enables all tracker events. Includes announcing to trackers,        |
// |                                | receiving responses, warnings and errors.                           |
// +--------------------------------+---------------------------------------------------------------------+
// | ``debug_notification``         | Low level alerts for when peers are connected and disconnected.     |
// +--------------------------------+---------------------------------------------------------------------+
// | ``status_notification``        | Enables alerts for when a torrent or the session changes state.     |
// +--------------------------------+---------------------------------------------------------------------+
// | ``progress_notification``      | Alerts for when blocks are requested and completed. Also when       |
// |                                | pieces are completed.                                               |
// +--------------------------------+---------------------------------------------------------------------+
// | ``ip_block_notification``      | Alerts when a peer is blocked by the ip blocker or port blocker.    |
// +--------------------------------+---------------------------------------------------------------------+
// | ``performance_warning``        | Alerts when some limit is reached that might limit the download     |
// |                                | or upload rate.                                                     |
// +--------------------------------+---------------------------------------------------------------------+
// | ``stats_notification``         | If you enable these alerts, you will receive a stats_alert          |
// |                                | approximately once every second, for every active torrent.          |
// |                                | These alerts contain all statistics counters for the interval since |
// |                                | the lasts stats alert.                                              |
// +--------------------------------+---------------------------------------------------------------------+
// | ``dht_notification``           | Alerts on events in the DHT node. For incoming searches or          |
// |                                | bootstrapping being done etc.                                       |
// +--------------------------------+---------------------------------------------------------------------+
// | ``rss_notification``           | Alerts on RSS related events, like feeds being updated, feed error  |
// |                                | conditions and successful RSS feed updates. Enabling this categoty  |
// |                                | will make you receive rss_alert alerts.                             |
// +--------------------------------+---------------------------------------------------------------------+
// | ``all_categories``             | The full bitmask, representing all available categories.            |
// +--------------------------------+---------------------------------------------------------------------+
// 
// Every alert belongs to one or more category. There is a small cost involved in posting alerts. Only
// alerts that belong to an enabled category are posted. Setting the alert bitmask to 0 will disable
// all alerts
// 
// There's another alert base class that some alerts derive from, all the
// alerts that are generated for a specific torrent are derived from::
// 
//	struct torrent_alert: alert
//	{
//		// ...
//		torrent_handle handle;
//	};
// 
// There's also a base class for all alerts referring to tracker events::
// 
//	struct tracker_alert: torrent_alert
//	{
//		// ...
//		std::string url;
//	};
//

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/ptime.hpp"
#include "libtorrent/config.hpp"

#ifndef BOOST_NO_TYPEID
#include <typeinfo>
#endif

#ifndef TORRENT_MAX_ALERT_TYPES
#define TORRENT_MAX_ALERT_TYPES 15
#endif

namespace libtorrent {

	// The ``alert`` class is the base class that specific messages are derived from.
	class TORRENT_EXPORT alert
	{
	public:

#ifndef TORRENT_NO_DEPRECATE
		// only here for backwards compatibility
		enum severity_t { debug, info, warning, critical, fatal, none };
#endif

		enum category_t
		{
			error_notification = 0x1,
			peer_notification = 0x2,
			port_mapping_notification = 0x4,
			storage_notification = 0x8,
			tracker_notification = 0x10,
			debug_notification = 0x20,
			status_notification = 0x40,
			progress_notification = 0x80,
			ip_block_notification = 0x100,
			performance_warning = 0x200,
			dht_notification = 0x400,
			stats_notification = 0x800,
			rss_notification = 0x1000,

			// since the enum is signed, make sure this isn't
			// interpreted as -1. For instance, boost.python
			// does that and fails when assigning it to an
			// unsigned parameter.
			all_categories = 0x7fffffff
		};

		alert();
		virtual ~alert();

		// a timestamp is automatically created in the constructor
		ptime timestamp() const;

		// returns an integer that is unique to this alert type. It can be
		// compared against a specific alert by querying a static constant called ``alert_type``
		// in the alert. It can be used to determine the run-time type of an alert* in
		// order to cast to that alert type and access specific members.
		// 
		// e.g::
		//
		//	std::auto_ptr<alert> a = ses.pop_alert();
		//	switch (a->type())
		//	{
		//		case read_piece_alert::alert_type:
		//		{
		//			read_piece_alert* p = (read_piece_alert*)a.get();
		//			if (p->ec) {
		//				// read_piece failed
		//				break;
		//			}
		//			// use p
		//			break;
		//		}
		//		case file_renamed_alert::alert_type:
		//		{
		//			// etc...
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

		// determines whether or not an alert is allowed to be discarded
		// when the alert queue is full. There are a few alerts which may not be discared,
		// since they would break the user contract, such as save_resume_data_alert.
		virtual bool discardable() const { return true; }

#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED_PREFIX
		severity_t severity() const TORRENT_DEPRECATED { return warning; }
#endif

		// returns a pointer to a copy of the alert.
		virtual std::auto_ptr<alert> clone() const = 0;

	private:
		ptime m_timestamp;
	};

	struct TORRENT_EXPORT unhandled_alert : std::exception
	{
		unhandled_alert() {}
	};

#ifndef BOOST_NO_TYPEID

	namespace detail {

		struct void_;

		template<class Handler
			, BOOST_PP_ENUM_PARAMS(TORRENT_MAX_ALERT_TYPES, class T)>
		void handle_alert_dispatch(
			const std::auto_ptr<alert>& alert_, const Handler& handler
			, const std::type_info& typeid_
			, T0*, BOOST_PP_ENUM_SHIFTED_BINARY_PARAMS(TORRENT_MAX_ALERT_TYPES, T, *p))
		{
			if (typeid_ == typeid(T0))
				handler(*static_cast<T0*>(alert_.get()));
			else
				handle_alert_dispatch(alert_, handler, typeid_
					, BOOST_PP_ENUM_SHIFTED_PARAMS(
					TORRENT_MAX_ALERT_TYPES, p), (void_*)0);
		}

		template<class Handler>
		void handle_alert_dispatch(
			const std::auto_ptr<alert>&
			, const Handler&
			, const std::type_info&
			, BOOST_PP_ENUM_PARAMS(TORRENT_MAX_ALERT_TYPES, void_* BOOST_PP_INTERCEPT))
		{
			throw unhandled_alert();
		}

	} // namespace detail

	template<BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(
		TORRENT_MAX_ALERT_TYPES, class T, detail::void_)>
	struct TORRENT_EXPORT handle_alert
	{
		template<class Handler>
		handle_alert(const std::auto_ptr<alert>& alert_
			, const Handler& handler)
		{
			#define ALERT_POINTER_TYPE(z, n, text) (BOOST_PP_CAT(T, n)*)0

			detail::handle_alert_dispatch(alert_, handler, typeid(*alert_)
				, BOOST_PP_ENUM(TORRENT_MAX_ALERT_TYPES, ALERT_POINTER_TYPE, _));

			#undef ALERT_POINTER_TYPE
		}
	};

#endif // BOOST_NO_TYPEID

// When you get an alert, you can use ``alert_cast<>`` to attempt to cast the pointer to a
// more specific alert type, in order to query it for more information.
//
// You can also use a `alert dispatcher`_ mechanism that's available in libtorrent.

template <class T>
T* alert_cast(alert* a)
{
	if (a == 0) return 0;
	if (a->type() == T::alert_type) return static_cast<T*>(a);
	return 0;
}
template <class T>
T const* alert_cast(alert const* a)
{
	if (a == 0) return 0;
	if (a->type() == T::alert_type) return static_cast<T const*>(a);
	return 0;
}

} // namespace libtorrent

#endif // TORRENT_ALERT_HPP_INCLUDED

