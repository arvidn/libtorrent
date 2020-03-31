/*

Copyright (c) 2003-2018, Arvid Norberg, Daniel Wallin
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

#include <string>

// OVERVIEW
//
// The pop_alerts() function on session is the main interface for retrieving
// alerts (warnings, messages and errors from libtorrent). If no alerts have
// been posted by libtorrent pop_alerts() will return an empty list.
//
// By default, only errors are reported. settings_pack::alert_mask can be
// used to specify which kinds of events should be reported. The alert mask is
// a combination of the alert_category_t flags in the alert class.
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
#include "libtorrent/flags.hpp"

namespace libtorrent {

// bitmask type used to define alert categories. Categories can be enabled
// and disabled by the settings_pack::alert_mask setting. Constants are defined
// in the lt::alert_category namespace
using alert_category_t = flags::bitfield_flag<std::uint32_t, struct alert_category_tag>;

namespace alert_category {

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
	constexpr alert_category_t error = 0_bit;

	// Enables alerts when peers send invalid requests, get banned or
	// snubbed.
	constexpr alert_category_t peer = 1_bit;

	// Enables alerts for port mapping events. For NAT-PMP and UPnP.
	constexpr alert_category_t port_mapping = 2_bit;

	// Enables alerts for events related to the storage. File errors and
	// synchronization events for moving the storage, renaming files etc.
	constexpr alert_category_t storage = 3_bit;

	// Enables all tracker events. Includes announcing to trackers,
	// receiving responses, warnings and errors.
	constexpr alert_category_t tracker = 4_bit;

	// Low level alerts for when peers are connected and disconnected.
	constexpr alert_category_t connect = 5_bit;

		// Enables alerts for when a torrent or the session changes state.
	constexpr alert_category_t status = 6_bit;

	// Alerts when a peer is blocked by the ip blocker or port blocker.
	constexpr alert_category_t ip_block = 8_bit;

	// Alerts when some limit is reached that might limit the download
	// or upload rate.
	constexpr alert_category_t performance_warning = 9_bit;

	// Alerts on events in the DHT node. For incoming searches or
	// bootstrapping being done etc.
	constexpr alert_category_t dht = 10_bit;

	// If you enable these alerts, you will receive a stats_alert
	// approximately once every second, for every active torrent.
	// These alerts contain all statistics counters for the interval since
	// the lasts stats alert.
	constexpr alert_category_t stats = 11_bit;

	// Enables debug logging alerts. These are available unless libtorrent
	// was built with logging disabled (``TORRENT_DISABLE_LOGGING``). The
	// alerts being posted are log_alert and are session wide.
	constexpr alert_category_t session_log = 13_bit;

	// Enables debug logging alerts for torrents. These are available
	// unless libtorrent was built with logging disabled
	// (``TORRENT_DISABLE_LOGGING``). The alerts being posted are
	// torrent_log_alert and are torrent wide debug events.
	constexpr alert_category_t torrent_log = 14_bit;

	// Enables debug logging alerts for peers. These are available unless
	// libtorrent was built with logging disabled
	// (``TORRENT_DISABLE_LOGGING``). The alerts being posted are
	// peer_log_alert and low-level peer events and messages.
	constexpr alert_category_t peer_log = 15_bit;

	// enables the incoming_request_alert.
	constexpr alert_category_t incoming_request = 16_bit;

	// enables dht_log_alert, debug logging for the DHT
	constexpr alert_category_t dht_log = 17_bit;

	// enable events from pure dht operations not related to torrents
	constexpr alert_category_t dht_operation = 18_bit;

	// enables port mapping log events. This log is useful
	// for debugging the UPnP or NAT-PMP implementation
	constexpr alert_category_t port_mapping_log = 19_bit;

	// enables verbose logging from the piece picker.
	constexpr alert_category_t picker_log = 20_bit;

	// alerts when files complete downloading
	constexpr alert_category_t file_progress = 21_bit;

	// alerts when pieces complete downloading or fail hash check
	constexpr alert_category_t piece_progress = 22_bit;

	// alerts when we upload blocks to other peers
	constexpr alert_category_t upload = 23_bit;

	// alerts on individual blocks being requested, downloading, finished,
	// rejected, time-out and cancelled. This is likely to post alerts at a
	// high rate.
	constexpr alert_category_t block_progress = 24_bit;

	// The full bitmask, representing all available categories.
	//
	// since the enum is signed, make sure this isn't
	// interpreted as -1. For instance, boost.python
	// does that and fails when assigning it to an
	// unsigned parameter.
	constexpr alert_category_t all = alert_category_t::all();

} // namespace alert_category

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	// The ``alert`` class is the base class that specific messages are derived from.
	// alert types are not copyable, and cannot be constructed by the client. The
	// pointers returned by libtorrent are short lived (the details are described
	// under session_handle::pop_alerts())
	class TORRENT_EXPORT alert
	{
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
	public:

		// hidden
		alert(alert const& rhs) = delete;
		alert& operator=(alert const&) = delete;
		alert(alert&& rhs) noexcept = default;

#if TORRENT_ABI_VERSION == 1
		// only here for backwards compatibility
		enum TORRENT_DEPRECATED_ENUM severity_t { debug, info, warning, critical, fatal, none };

		using category_t = alert_category_t;
#endif

		static constexpr alert_category_t error_notification = 0_bit;
		static constexpr alert_category_t peer_notification = 1_bit;
		static constexpr alert_category_t port_mapping_notification = 2_bit;
		static constexpr alert_category_t storage_notification = 3_bit;
		static constexpr alert_category_t tracker_notification = 4_bit;
		static constexpr alert_category_t connect_notification = 5_bit;
#if TORRENT_ABI_VERSION == 1
		static constexpr alert_category_t TORRENT_DEPRECATED_MEMBER debug_notification = connect_notification;
#endif
		static constexpr alert_category_t status_notification = 6_bit;
#if TORRENT_ABI_VERSION == 1
		static constexpr alert_category_t TORRENT_DEPRECATED_MEMBER progress_notification = 7_bit;
#endif
		static constexpr alert_category_t ip_block_notification = 8_bit;
		static constexpr alert_category_t performance_warning = 9_bit;
		static constexpr alert_category_t dht_notification = 10_bit;
		static constexpr alert_category_t stats_notification = 11_bit;
#if TORRENT_ABI_VERSION == 1
		static constexpr alert_category_t TORRENT_DEPRECATED_MEMBER rss_notification = 12_bit;
#endif
		static constexpr alert_category_t session_log_notification = 13_bit;
		static constexpr alert_category_t torrent_log_notification = 14_bit;
		static constexpr alert_category_t peer_log_notification = 15_bit;
		static constexpr alert_category_t incoming_request_notification = 16_bit;
		static constexpr alert_category_t dht_log_notification = 17_bit;
		static constexpr alert_category_t dht_operation_notification = 18_bit;
		static constexpr alert_category_t port_mapping_log_notification = 19_bit;
		static constexpr alert_category_t picker_log_notification = 20_bit;
		static constexpr alert_category_t file_progress_notification = 21_bit;
		static constexpr alert_category_t piece_progress_notification = 22_bit;
		static constexpr alert_category_t upload_notification = 23_bit;
		static constexpr alert_category_t block_progress_notification = 24_bit;
		static constexpr alert_category_t all_categories = alert_category_t::all();

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
		//				auto* p = static_cast<read_piece_alert*>(a);
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
		virtual int type() const noexcept = 0;

		// returns a string literal describing the type of the alert. It does
		// not include any information that might be bundled with the alert.
		virtual char const* what() const noexcept = 0;

		// generate a string describing the alert and the information bundled
		// with it. This is mainly intended for debug and development use. It is not suitable
		// to use this for applications that may be localized. Instead, handle each alert
		// type individually and extract and render the information from the alert depending
		// on the locale.
		virtual std::string message() const = 0;

		// returns a bitmask specifying which categories this alert belong to.
		virtual alert_category_t category() const noexcept = 0;

#if TORRENT_ABI_VERSION == 1

#include "libtorrent/aux_/disable_warnings_push.hpp"

		// determines whether or not an alert is allowed to be discarded
		// when the alert queue is full. There are a few alerts which may not be discarded,
		// since they would break the user contract, such as save_resume_data_alert.
		TORRENT_DEPRECATED
		bool discardable() const { return discardable_impl(); }

		TORRENT_DEPRECATED
		severity_t severity() const { return warning; }

	protected:

		virtual bool discardable_impl() const { return true; }

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif // TORRENT_ABI_VERSION

	private:
		time_point const m_timestamp;
	};

// When you get an alert, you can use ``alert_cast<>`` to attempt to cast the
// pointer to a specific alert type, in order to query it for more
// information.
//
// .. note::
//   ``alert_cast<>`` can only cast to an exact alert type, not a base class
template <class T> T* alert_cast(alert* a)
{
	static_assert(std::is_base_of<alert, T>::value
		, "alert_cast<> can only be used with alert types (deriving from lt::alert)");

	if (a == nullptr) return nullptr;
	if (a->type() == T::alert_type) return static_cast<T*>(a);
	return nullptr;
}
template <class T> T const* alert_cast(alert const* a)
{
	static_assert(std::is_base_of<alert, T>::value
		, "alert_cast<> can only be used with alert types (deriving from lt::alert)");
	if (a == nullptr) return nullptr;
	if (a->type() == T::alert_type) return static_cast<T const*>(a);
	return nullptr;
}

} // namespace libtorrent

#endif // TORRENT_ALERT_HPP_INCLUDED
