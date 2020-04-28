/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_ALERT_TYPES_HPP_INCLUDED
#define TORRENT_ALERT_TYPES_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/operations.hpp" // for operation_t enum
#include "libtorrent/close_reason.hpp"
#include "libtorrent/piece_block.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_from_native
#include "libtorrent/string_view.hpp"
#include "libtorrent/stack_allocator.hpp"
#include "libtorrent/aux_/noexcept_movable.hpp"
#include "libtorrent/portmap.hpp" // for portmap_transport
#include "libtorrent/aux_/deprecated.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/shared_array.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <bitset>
#include <cstdarg> // for va_list

#if TORRENT_ABI_VERSION == 1
#define PROGRESS_NOTIFICATION | alert::progress_notification
#else
#define PROGRESS_NOTIFICATION
#endif


namespace libtorrent {

#if TORRENT_ABI_VERSION == 1
	TORRENT_DEPRECATED_EXPORT char const* operation_name(int op);
#endif

	// internal
	TORRENT_EXPORT char const* alert_name(int alert_type);

	// user defined alerts should use IDs greater than this
	constexpr int user_alert_id = 10000;

	// this constant represents "max_alert_index" + 1
	constexpr int num_alert_types = 97;

	// internal
	enum alert_priority
	{
		alert_priority_normal = 0,
		alert_priority_high,
		alert_priority_critical
	};

	// struct to hold information about a single DHT routing table bucket
	struct TORRENT_EXPORT dht_routing_bucket
	{
		// the total number of nodes and replacement nodes
		// in the routing table
		int num_nodes;
		int num_replacements;

		// number of seconds since last activity
		int last_active;
	};

TORRENT_VERSION_NAMESPACE_2

	// This is a base class for alerts that are associated with a
	// specific torrent. It contains a handle to the torrent.
	struct TORRENT_EXPORT torrent_alert : alert
	{
		// internal
		torrent_alert(aux::stack_allocator& alloc, torrent_handle const& h);
		torrent_alert(torrent_alert&&) noexcept = default;

#if TORRENT_ABI_VERSION == 1
		static const int TORRENT_DEPRECATED_MEMBER alert_type = 0;
#endif

		// returns the message associated with this alert
		std::string message() const override;

		// The torrent_handle pointing to the torrent this
		// alert is associated with.
		torrent_handle handle;

		char const* torrent_name() const;

	protected:
		std::reference_wrapper<aux::stack_allocator const> m_alloc;
	private:
		aux::allocation_slot m_name_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		std::string TORRENT_DEPRECATED_MEMBER name;
#endif
	};

	// The peer alert is a base class for alerts that refer to a specific peer. It includes all
	// the information to identify the peer. i.e. ``ip`` and ``peer-id``.
	struct TORRENT_EXPORT peer_alert : torrent_alert
	{
		// internal
		peer_alert(aux::stack_allocator& alloc, torrent_handle const& h,
			tcp::endpoint const& i, peer_id const& pi);
		peer_alert(peer_alert&& rhs) noexcept = default;

#if TORRENT_ABI_VERSION == 1
		static const int TORRENT_DEPRECATED_MEMBER alert_type = 1;
#endif

		std::string message() const override;

		// The peer's IP address and port.
		aux::noexcept_movable<tcp::endpoint> endpoint;

		// the peer ID, if known.
		peer_id pid;

#if TORRENT_ABI_VERSION == 1
		// The peer's IP address and port.
		aux::noexcept_movable<tcp::endpoint> TORRENT_DEPRECATED_MEMBER ip;
#endif
	};

	// This is a base class used for alerts that are associated with a
	// specific tracker. It derives from torrent_alert since a tracker
	// is also associated with a specific torrent.
	struct TORRENT_EXPORT tracker_alert : torrent_alert
	{
		// internal
		tracker_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& ep, string_view u);

#if TORRENT_ABI_VERSION == 1
		static const int TORRENT_DEPRECATED_MEMBER alert_type = 2;
#endif

		std::string message() const override;

		// endpoint of the listen interface being announced
		aux::noexcept_movable<tcp::endpoint> local_endpoint;

		// returns a 0-terminated string of the tracker's URL
		char const* tracker_url() const;

	private:
		aux::allocation_slot m_url_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		// The tracker URL
		std::string TORRENT_DEPRECATED_MEMBER url;
#endif
	};

#define TORRENT_DEFINE_ALERT_IMPL(name, seq, prio) \
	name(name&&) noexcept = default; \
	static const int priority = prio; \
	static const int alert_type = seq; \
	virtual int type() const noexcept override { return alert_type; } \
	virtual alert_category_t category() const noexcept override { return static_category; } \
	virtual char const* what() const noexcept override { return alert_name(alert_type); }

#define TORRENT_DEFINE_ALERT(name, seq) \
	TORRENT_DEFINE_ALERT_IMPL(name, seq, alert_priority_normal)

#define TORRENT_DEFINE_ALERT_PRIO(name, seq, prio) \
	TORRENT_DEFINE_ALERT_IMPL(name, seq, prio)

#if TORRENT_ABI_VERSION == 1

#ifdef _MSC_VER
#pragma warning(push, 1)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	// The ``torrent_added_alert`` is posted once every time a torrent is successfully
	// added. It doesn't contain any members of its own, but inherits the torrent handle
	// from its base class.
	// It's posted when the ``status_notification`` bit is set in the alert_mask.
	// deprecated in 1.1.3
	// use add_torrent_alert instead
	struct TORRENT_DEPRECATED_EXPORT torrent_added_alert final : torrent_alert
	{
		// internal
		torrent_added_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(torrent_added_alert, 3)
		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
	};

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif

	// The ``torrent_removed_alert`` is posted whenever a torrent is removed. Since
	// the torrent handle in its base class will always be invalid (since the torrent
	// is already removed) it has the info hash as a member, to identify it.
	// It's posted when the ``status_notification`` bit is set in the alert_mask.
	//
	// Even though the ``handle`` member doesn't point to an existing torrent anymore,
	// it is still useful for comparing to other handles, which may also no
	// longer point to existing torrents, but to the same non-existing torrents.
	//
	// The ``torrent_handle`` acts as a ``weak_ptr``, even though its object no
	// longer exists, it can still compare equal to another weak pointer which
	// points to the same non-existent object.
	struct TORRENT_EXPORT torrent_removed_alert final : torrent_alert
	{
		// internal
		torrent_removed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT_PRIO(torrent_removed_alert, 4, alert_priority_critical)
		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
		sha1_hash info_hash;
	};

	// This alert is posted when the asynchronous read operation initiated by
	// a call to torrent_handle::read_piece() is completed. If the read failed, the torrent
	// is paused and an error state is set and the buffer member of the alert
	// is 0. If successful, ``buffer`` points to a buffer containing all the data
	// of the piece. ``piece`` is the piece index that was read. ``size`` is the
	// number of bytes that was read.
	//
	// If the operation fails, ``error`` will indicate what went wrong.
	struct TORRENT_EXPORT read_piece_alert final : torrent_alert
	{
		// internal
		read_piece_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, piece_index_t p, boost::shared_array<char> d, int s);
		read_piece_alert(aux::stack_allocator& alloc, torrent_handle h
			, piece_index_t p, error_code e);

		TORRENT_DEFINE_ALERT_PRIO(read_piece_alert, 5, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage;
		std::string message() const override;

		error_code const error;
		boost::shared_array<char> const buffer;
		piece_index_t const piece;
		int const size;

#if TORRENT_ABI_VERSION == 1
		error_code TORRENT_DEPRECATED_MEMBER ec;
#endif
	};

	// This is posted whenever an individual file completes its download. i.e.
	// All pieces overlapping this file have passed their hash check.
	struct TORRENT_EXPORT file_completed_alert final : torrent_alert
	{
		// internal
		file_completed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, file_index_t idx);

		TORRENT_DEFINE_ALERT_PRIO(file_completed_alert, 6, alert_priority_normal)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		static constexpr alert_category_t static_category =
			alert_category::file_progress
			PROGRESS_NOTIFICATION
		;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		std::string message() const override;

		// refers to the index of the file that completed.
		file_index_t const index;
	};

	// This is posted as a response to a torrent_handle::rename_file() call, if the rename
	// operation succeeds.
	struct TORRENT_EXPORT file_renamed_alert final : torrent_alert
	{
		// internal
		file_renamed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, string_view n, file_index_t idx);

		TORRENT_DEFINE_ALERT_PRIO(file_renamed_alert, 7, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage;
		std::string message() const override;

		char const* new_name() const;

		// refers to the index of the file that was renamed,
		file_index_t const index;
	private:
		aux::allocation_slot m_name_idx;
#if TORRENT_ABI_VERSION == 1

#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

	public:
		std::string TORRENT_DEPRECATED_MEMBER name;

#if defined __clang__
#pragma clang diagnostic pop
#endif
#endif
	};

	// This is posted as a response to a torrent_handle::rename_file() call, if the rename
	// operation failed.
	struct TORRENT_EXPORT file_rename_failed_alert final : torrent_alert
	{
		// internal
		file_rename_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, file_index_t idx
			, error_code ec);

		TORRENT_DEFINE_ALERT_PRIO(file_rename_failed_alert, 8, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage;

		std::string message() const override;

		// refers to the index of the file that was supposed to be renamed,
		// ``error`` is the error code returned from the filesystem.
		file_index_t const index;
		error_code const error;
	};

	// This alert is generated when a limit is reached that might have a negative impact on
	// upload or download rate performance.
	struct TORRENT_EXPORT performance_alert final : torrent_alert
	{
		enum performance_warning_t
		{

			// This warning means that the number of bytes queued to be written to disk
			// exceeds the max disk byte queue setting (``settings_pack::max_queued_disk_bytes``).
			// This might restrict the download rate, by not queuing up enough write jobs
			// to the disk I/O thread. When this alert is posted, peer connections are
			// temporarily stopped from downloading, until the queued disk bytes have fallen
			// below the limit again. Unless your ``max_queued_disk_bytes`` setting is already
			// high, you might want to increase it to get better performance.
			outstanding_disk_buffer_limit_reached,

			// This is posted when libtorrent would like to send more requests to a peer,
			// but it's limited by ``settings_pack::max_out_request_queue``. The queue length
			// libtorrent is trying to achieve is determined by the download rate and the
			// assumed round-trip-time (``settings_pack::request_queue_time``). The assumed
			// round-trip-time is not limited to just the network RTT, but also the remote disk
			// access time and message handling time. It defaults to 3 seconds. The target number
			// of outstanding requests is set to fill the bandwidth-delay product (assumed RTT
			// times download rate divided by number of bytes per request). When this alert
			// is posted, there is a risk that the number of outstanding requests is too low
			// and limits the download rate. You might want to increase the ``max_out_request_queue``
			// setting.
			outstanding_request_limit_reached,

			// This warning is posted when the amount of TCP/IP overhead is greater than the
			// upload rate limit. When this happens, the TCP/IP overhead is caused by a much
			// faster download rate, triggering TCP ACK packets. These packets eat into the
			// rate limit specified to libtorrent. When the overhead traffic is greater than
			// the rate limit, libtorrent will not be able to send any actual payload, such
			// as piece requests. This means the download rate will suffer, and new requests
			// can be sent again. There will be an equilibrium where the download rate, on
			// average, is about 20 times the upload rate limit. If you want to maximize the
			// download rate, increase the upload rate limit above 5% of your download capacity.
			upload_limit_too_low,

			// This is the same warning as ``upload_limit_too_low`` but referring to the download
			// limit instead of upload. This suggests that your download rate limit is much lower
			// than your upload capacity. Your upload rate will suffer. To maximize upload rate,
			// make sure your download rate limit is above 5% of your upload capacity.
			download_limit_too_low,

			// We're stalled on the disk. We want to write to the socket, and we can write
			// but our send buffer is empty, waiting to be refilled from the disk.
			// This either means the disk is slower than the network connection
			// or that our send buffer watermark is too small, because we can
			// send it all before the disk gets back to us.
			// The number of bytes that we keep outstanding, requested from the disk, is calculated
			// as follows:
			//
			// .. code:: C++
			//
			//    min(512, max(upload_rate * send_buffer_watermark_factor / 100, send_buffer_watermark))
			//
			// If you receive this alert, you might want to either increase your ``send_buffer_watermark``
			// or ``send_buffer_watermark_factor``.
			send_buffer_watermark_too_low,

			// If the half (or more) of all upload slots are set as optimistic unchoke slots, this
			// warning is issued. You probably want more regular (rate based) unchoke slots.
			too_many_optimistic_unchoke_slots,

			// If the disk write queue ever grows larger than half of the cache size, this warning
			// is posted. The disk write queue eats into the total disk cache and leaves very little
			// left for the actual cache. This causes the disk cache to oscillate in evicting large
			// portions of the cache before allowing peers to download any more, onto the disk write
			// queue. Either lower ``max_queued_disk_bytes`` or increase ``cache_size``.
			too_high_disk_queue_limit,

			aio_limit_reached,
#if TORRENT_ABI_VERSION == 1
			bittyrant_with_no_uplimit TORRENT_DEPRECATED_ENUM,
#else
			deprecated_bittyrant_with_no_uplimit,
#endif

			// This is generated if outgoing peer connections are failing because of *address in use*
			// errors, indicating that ``settings_pack::outgoing_ports`` is set and is too small of
			// a range. Consider not using the ``outgoing_ports`` setting at all, or widen the range to
			// include more ports.
			too_few_outgoing_ports,

			too_few_file_descriptors,

			num_warnings
		};

		// internal
		performance_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, performance_warning_t w);

		TORRENT_DEFINE_ALERT(performance_alert, 9)

		static constexpr alert_category_t static_category = alert_category::performance_warning;

		std::string message() const override;

		performance_warning_t const warning_code;
	};

	// Generated whenever a torrent changes its state.
	struct TORRENT_EXPORT state_changed_alert final : torrent_alert
	{
		// internal
		state_changed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, torrent_status::state_t st
			, torrent_status::state_t prev_st);

		TORRENT_DEFINE_ALERT_PRIO(state_changed_alert, 10, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::status;

		std::string message() const override;

		// the new state of the torrent.
		torrent_status::state_t const state;

		// the previous state.
		torrent_status::state_t const prev_state;
	};

	// This alert is generated on tracker time outs, premature disconnects,
	// invalid response or a HTTP response other than "200 OK". From the alert
	// you can get the handle to the torrent the tracker belongs to.
	//
	// The ``times_in_row`` member says how many times in a row this tracker has
	// failed. ``status_code`` is the code returned from the HTTP server. 401
	// means the tracker needs authentication, 404 means not found etc. If the
	// tracker timed out, the code will be set to 0.
	struct TORRENT_EXPORT tracker_error_alert final : tracker_alert
	{
		// internal
		tracker_error_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, int times, string_view u
			, error_code const& e, string_view m);

		TORRENT_DEFINE_ALERT_PRIO(tracker_error_alert, 11, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::tracker | alert_category::error;
		std::string message() const override;

		int const times_in_row;
		error_code const error;

		// the message associated with this error
		char const* error_message() const;

	private:
		aux::allocation_slot m_msg_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		int const TORRENT_DEPRECATED_MEMBER status_code;
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is triggered if the tracker reply contains a warning field.
	// Usually this means that the tracker announce was successful, but the
	// tracker has a message to the client.
	struct TORRENT_EXPORT tracker_warning_alert final : tracker_alert
	{
		// internal
		tracker_warning_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, string_view u, string_view m);

		TORRENT_DEFINE_ALERT(tracker_warning_alert, 12)

		static constexpr alert_category_t static_category = alert_category::tracker | alert_category::error;
		std::string message() const override;

		// the message associated with this warning
		char const* warning_message() const;

	private:
		aux::allocation_slot m_msg_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		// contains the warning message from the tracker.
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is generated when a scrape request succeeds.
	struct TORRENT_EXPORT scrape_reply_alert final : tracker_alert
	{
		// internal
		scrape_reply_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, int incomp, int comp, string_view u);

		TORRENT_DEFINE_ALERT_PRIO(scrape_reply_alert, 13, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::tracker;
		std::string message() const override;

		// the data returned in the scrape response. These numbers
		// may be -1 if the response was malformed.
		int const incomplete;
		int const complete;
	};

	// If a scrape request fails, this alert is generated. This might be due
	// to the tracker timing out, refusing connection or returning an http response
	// code indicating an error.
	struct TORRENT_EXPORT scrape_failed_alert final : tracker_alert
	{
		// internal
		scrape_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, string_view u, error_code const& e);
		scrape_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, string_view u, string_view m);

		TORRENT_DEFINE_ALERT_PRIO(scrape_failed_alert, 14, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::tracker | alert_category::error;
		std::string message() const override;

		// the error itself. This may indicate that the tracker sent an error
		// message (``error::tracker_failure``), in which case it can be
		// retrieved by calling ``error_message()``.
		error_code const error;

		// if the error indicates there is an associated message, this returns
		// that message. Otherwise and empty string.
		char const* error_message() const;

	private:
		aux::allocation_slot m_msg_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		// contains a message describing the error.
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is only for informational purpose. It is generated when a tracker announce
	// succeeds. It is generated regardless what kind of tracker was used, be it UDP, HTTP or
	// the DHT.
	struct TORRENT_EXPORT tracker_reply_alert final : tracker_alert
	{
		// internal
		tracker_reply_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, int np, string_view u);

		TORRENT_DEFINE_ALERT(tracker_reply_alert, 15)

		static constexpr alert_category_t static_category = alert_category::tracker;
		std::string message() const override;

		// tells how many peers the tracker returned in this response. This is
		// not expected to be greater than the ``num_want`` settings. These are not necessarily
		// all new peers, some of them may already be connected.
		int const num_peers;
	};

	// This alert is generated each time the DHT receives peers from a node. ``num_peers``
	// is the number of peers we received in this packet. Typically these packets are
	// received from multiple DHT nodes, and so the alerts are typically generated
	// a few at a time.
	struct TORRENT_EXPORT dht_reply_alert final : tracker_alert
	{
		// internal
		dht_reply_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, int np);

		TORRENT_DEFINE_ALERT(dht_reply_alert, 16)

		static constexpr alert_category_t static_category = alert_category::dht | alert_category::tracker;
		std::string message() const override;

		int const num_peers;
	};

	// This alert is generated each time a tracker announce is sent (or attempted to be sent).
	// There are no extra data members in this alert. The url can be found in the base class
	// however.
	struct TORRENT_EXPORT tracker_announce_alert final : tracker_alert
	{
		// internal
		tracker_announce_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, string_view u, int e);

		TORRENT_DEFINE_ALERT(tracker_announce_alert, 17)

		static constexpr alert_category_t static_category = alert_category::tracker;
		std::string message() const override;

		// specifies what event was sent to the tracker. It is defined as:
		//
		// 0. None
		// 1. Completed
		// 2. Started
		// 3. Stopped
		int const event;
	};

	// This alert is generated when a finished piece fails its hash check. You can get the handle
	// to the torrent which got the failed piece and the index of the piece itself from the alert.
	struct TORRENT_EXPORT hash_failed_alert final : torrent_alert
	{
		// internal
		hash_failed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, piece_index_t index);

		TORRENT_DEFINE_ALERT(hash_failed_alert, 18)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;

		piece_index_t const piece_index;
	};

	// This alert is generated when a peer is banned because it has sent too many corrupt pieces
	// to us. ``ip`` is the endpoint to the peer that was banned.
	struct TORRENT_EXPORT peer_ban_alert final : peer_alert
	{
		// internal
		peer_ban_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		TORRENT_DEFINE_ALERT(peer_ban_alert, 19)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;
	};

	// This alert is generated when a peer is un-snubbed. Essentially when it was snubbed for stalling
	// sending data, and now it started sending data again.
	struct TORRENT_EXPORT peer_unsnubbed_alert final : peer_alert
	{
		// internal
		peer_unsnubbed_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		TORRENT_DEFINE_ALERT(peer_unsnubbed_alert, 20)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;
	};

	// This alert is generated when a peer is snubbed, when it stops sending data when we request
	// it.
	struct TORRENT_EXPORT peer_snubbed_alert final : peer_alert
	{
		// internal
		peer_snubbed_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		TORRENT_DEFINE_ALERT(peer_snubbed_alert, 21)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;
	};

	// This alert is generated when a peer sends invalid data over the peer-peer protocol. The peer
	// will be disconnected, but you get its ip address from the alert, to identify it.
	struct TORRENT_EXPORT peer_error_alert final : peer_alert
	{
		// internal
		peer_error_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& ep, peer_id const& peer_id, operation_t op
			, error_code const& e);

		TORRENT_DEFINE_ALERT(peer_error_alert, 22)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;

		// a 0-terminated string of the low-level operation that failed, or nullptr if
		// there was no low level disk operation.
		operation_t op;

		// tells you what error caused this alert.
		error_code const error;

#if TORRENT_ABI_VERSION == 1
		int const TORRENT_DEPRECATED_MEMBER operation;
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is posted every time an outgoing peer connect attempts succeeds.
	struct TORRENT_EXPORT peer_connect_alert final : peer_alert
	{
		// internal
		peer_connect_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int type);

		TORRENT_DEFINE_ALERT(peer_connect_alert, 23)

		static constexpr alert_category_t static_category = alert_category::connect;
		std::string message() const override;

		int const socket_type;
	};

	// This alert is generated when a peer is disconnected for any reason (other than the ones
	// covered by peer_error_alert ).
	struct TORRENT_EXPORT peer_disconnected_alert final : peer_alert
	{
		// internal
		peer_disconnected_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, operation_t op, int type, error_code const& e
			, close_reason_t r);

		TORRENT_DEFINE_ALERT(peer_disconnected_alert, 24)

		static constexpr alert_category_t static_category = alert_category::connect;
		std::string message() const override;

		// the kind of socket this peer was connected over
		int const socket_type;

		// the operation or level where the error occurred. Specified as an
		// value from the operation_t enum. Defined in operations.hpp.
		operation_t const op;

		// tells you what error caused peer to disconnect.
		error_code const error;

		// the reason the peer disconnected (if specified)
		close_reason_t const reason;

#if TORRENT_ABI_VERSION == 1
		int const TORRENT_DEPRECATED_MEMBER operation;
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This is a debug alert that is generated by an incoming invalid piece request.
	// ``ip`` is the address of the peer and the ``request`` is the actual incoming
	// request from the peer. See peer_request for more info.
	struct TORRENT_EXPORT invalid_request_alert final : peer_alert
	{
		// internal
		invalid_request_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, peer_request const& r
			, bool we_have, bool peer_interested, bool withheld);

		TORRENT_DEFINE_ALERT(invalid_request_alert, 25)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;

		// the request we received from the peer
		peer_request const request;

		// true if we have this piece
		bool const we_have;

		// true if the peer indicated that it was interested to download before
		// sending the request
		bool const peer_interested;

		// if this is true, the peer is not allowed to download this piece because
		// of super-seeding rules.
		bool const withheld;
	};

	// This alert is generated when a torrent switches from being a downloader to a seed.
	// It will only be generated once per torrent. It contains a torrent_handle to the
	// torrent in question.
	struct TORRENT_EXPORT torrent_finished_alert final : torrent_alert
	{
		// internal
		torrent_finished_alert(aux::stack_allocator& alloc,
			torrent_handle h);

		TORRENT_DEFINE_ALERT_PRIO(torrent_finished_alert, 26, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
	};

	// this alert is posted every time a piece completes downloading
	// and passes the hash check. This alert derives from torrent_alert
	// which contains the torrent_handle to the torrent the piece belongs to.
	// Note that being downloaded and passing the hash check may happen before
	// the piece is also fully flushed to disk. So torrent_handle::have_piece()
	// may still return false
	struct TORRENT_EXPORT piece_finished_alert final : torrent_alert
	{
		// internal
		piece_finished_alert(aux::stack_allocator& alloc,
			torrent_handle const& h, piece_index_t piece_num);

		TORRENT_DEFINE_ALERT(piece_finished_alert, 27)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		static constexpr alert_category_t static_category =
			alert_category::piece_progress
			PROGRESS_NOTIFICATION
		;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		std::string message() const override;

		// the index of the piece that finished
		piece_index_t const piece_index;
	};

	// This alert is generated when a peer rejects or ignores a piece request.
	struct TORRENT_EXPORT request_dropped_alert final : peer_alert
	{
		// internal
		request_dropped_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
			, piece_index_t piece_num);

		TORRENT_DEFINE_ALERT(request_dropped_alert, 28)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		static constexpr alert_category_t static_category =
			alert_category::block_progress
			| alert_category::peer
			PROGRESS_NOTIFICATION
		;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		std::string message() const override;

		int const block_index;
		piece_index_t const piece_index;
	};

	// This alert is generated when a block request times out.
	struct TORRENT_EXPORT block_timeout_alert final : peer_alert
	{
		// internal
		block_timeout_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
			, piece_index_t piece_num);

		TORRENT_DEFINE_ALERT(block_timeout_alert, 29)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		static constexpr alert_category_t static_category =
			alert_category::block_progress
			| alert_category::peer
			PROGRESS_NOTIFICATION
		;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		std::string message() const override;

		int const block_index;
		piece_index_t const piece_index;
	};

	// This alert is generated when a block request receives a response.
	struct TORRENT_EXPORT block_finished_alert final : peer_alert
	{
		// internal
		block_finished_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
			, piece_index_t piece_num);

		TORRENT_DEFINE_ALERT(block_finished_alert, 30)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		static constexpr alert_category_t static_category =
			alert_category::block_progress
			PROGRESS_NOTIFICATION
		;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		std::string message() const override;

		int const block_index;
		piece_index_t const piece_index;
	};

	// This alert is generated when a block request is sent to a peer.
	struct TORRENT_EXPORT block_downloading_alert final : peer_alert
	{
		// internal
		block_downloading_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, piece_index_t piece_num);

		TORRENT_DEFINE_ALERT(block_downloading_alert, 31)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		static constexpr alert_category_t static_category =
			alert_category::block_progress
			PROGRESS_NOTIFICATION
		;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		std::string message() const override;

		int const block_index;
		piece_index_t const piece_index;
#if TORRENT_ABI_VERSION == 1
		char const* TORRENT_DEPRECATED_MEMBER peer_speedmsg;
#endif
	};

	// This alert is generated when a block is received that was not requested or
	// whose request timed out.
	struct TORRENT_EXPORT unwanted_block_alert final : peer_alert
	{
		// internal
		unwanted_block_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, piece_index_t piece_num);

		TORRENT_DEFINE_ALERT(unwanted_block_alert, 32)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;

		int const block_index;
		piece_index_t const piece_index;
	};

	// The ``storage_moved_alert`` is generated when all the disk IO has
	// completed and the files have been moved, as an effect of a call to
	// ``torrent_handle::move_storage``. This is useful to synchronize with the
	// actual disk. The ``storage_path()`` member return the new path of the
	// storage.
	struct TORRENT_EXPORT storage_moved_alert final : torrent_alert
	{
		// internal
		storage_moved_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, string_view p);

		TORRENT_DEFINE_ALERT_PRIO(storage_moved_alert, 33, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage;
		std::string message() const override;

		// the path the torrent was moved to
		char const* storage_path() const;

	private:
		aux::allocation_slot m_path_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		std::string TORRENT_DEPRECATED_MEMBER path;
#endif
	};

	// The ``storage_moved_failed_alert`` is generated when an attempt to move the storage,
	// via torrent_handle::move_storage(), fails.
	struct TORRENT_EXPORT storage_moved_failed_alert final : torrent_alert
	{
		// internal
		storage_moved_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& e, string_view file
			, operation_t op);

		TORRENT_DEFINE_ALERT_PRIO(storage_moved_failed_alert, 34, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage;
		std::string message() const override;

		error_code const error;

		// If the error happened for a specific file, this returns its path.
		char const* file_path() const;

		// this indicates what underlying operation caused the error
		operation_t op;
	private:
		aux::allocation_slot m_file_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		char const* TORRENT_DEPRECATED_MEMBER operation;
		// If the error happened for a specific file, ``file`` is its path.
		std::string TORRENT_DEPRECATED_MEMBER file;
#endif
	};

	// This alert is generated when a request to delete the files of a torrent complete.
	//
	// The ``info_hash`` is the info-hash of the torrent that was just deleted. Most of
	// the time the torrent_handle in the ``torrent_alert`` will be invalid by the time
	// this alert arrives, since the torrent is being deleted. The ``info_hash`` member
	// is hence the main way of identifying which torrent just completed the delete.
	//
	// This alert is posted in the ``storage_notification`` category, and that bit
	// needs to be set in the alert_mask.
	struct TORRENT_EXPORT torrent_deleted_alert final : torrent_alert
	{
		// internal
		torrent_deleted_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT_PRIO(torrent_deleted_alert, 35, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage;
		std::string message() const override;

		sha1_hash info_hash;
	};

	// This alert is generated when a request to delete the files of a torrent fails.
	// Just removing a torrent from the session cannot fail
	struct TORRENT_EXPORT torrent_delete_failed_alert final : torrent_alert
	{
		// internal
		torrent_delete_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& e, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT_PRIO(torrent_delete_failed_alert, 36, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage
			| alert_category::error;
		std::string message() const override;

		// tells you why it failed.
		error_code const error;

		// the info hash of the torrent whose files failed to be deleted
		sha1_hash info_hash;

#if TORRENT_ABI_VERSION == 1
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is generated as a response to a ``torrent_handle::save_resume_data`` request.
	// It is generated once the disk IO thread is done writing the state for this torrent.
	struct TORRENT_EXPORT save_resume_data_alert final : torrent_alert
	{
		// internal
		save_resume_data_alert(aux::stack_allocator& alloc
			, add_torrent_params&& params
			, torrent_handle const& h);
		save_resume_data_alert(aux::stack_allocator& alloc
			, add_torrent_params const& params
			, torrent_handle const& h) = delete;

		TORRENT_DEFINE_ALERT_PRIO(save_resume_data_alert, 37, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage;
		std::string message() const override;

		// the ``params`` structure is populated with the fields to be passed to
		// add_torrent() or async_add_torrent() to resume the torrent. To
		// save the state to disk, you may pass it on to write_resume_data().
		add_torrent_params params;

#if TORRENT_ABI_VERSION == 1
		// points to the resume data.
		std::shared_ptr<entry> TORRENT_DEPRECATED_MEMBER resume_data;
#endif
	};

	// This alert is generated instead of ``save_resume_data_alert`` if there was an error
	// generating the resume data. ``error`` describes what went wrong.
	struct TORRENT_EXPORT save_resume_data_failed_alert final : torrent_alert
	{
		// internal
		save_resume_data_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& e);

		TORRENT_DEFINE_ALERT_PRIO(save_resume_data_failed_alert, 38, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::storage
			| alert_category::error;
		std::string message() const override;

		// the error code from the resume_data failure
		error_code const error;

#if TORRENT_ABI_VERSION == 1
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is generated as a response to a ``torrent_handle::pause`` request. It is
	// generated once all disk IO is complete and the files in the torrent have been closed.
	// This is useful for synchronizing with the disk.
	struct TORRENT_EXPORT torrent_paused_alert final : torrent_alert
	{
		// internal
		torrent_paused_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(torrent_paused_alert, 39, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
	};

	// This alert is generated as a response to a torrent_handle::resume() request. It is
	// generated when a torrent goes from a paused state to an active state.
	struct TORRENT_EXPORT torrent_resumed_alert final : torrent_alert
	{
		// internal
		torrent_resumed_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(torrent_resumed_alert, 40, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
	};

	// This alert is posted when a torrent completes checking. i.e. when it transitions
	// out of the ``checking files`` state into a state where it is ready to start downloading
	struct TORRENT_EXPORT torrent_checked_alert final : torrent_alert
	{
		// internal
		torrent_checked_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(torrent_checked_alert, 41, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
	};

	// This alert is generated when a HTTP seed name lookup fails.
	struct TORRENT_EXPORT url_seed_alert final : torrent_alert
	{
		// internal
		url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, string_view u, error_code const& e);
		url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, string_view u, string_view m);

		TORRENT_DEFINE_ALERT(url_seed_alert, 42)

		static constexpr alert_category_t static_category = alert_category::peer | alert_category::error;
		std::string message() const override;

		// the error the web seed encountered. If this is not set, the server
		// sent an error message, call ``error_message()``.
		error_code const error;

		// the URL the error is associated with
		char const* server_url() const;

		// in case the web server sent an error message, this function returns
		// it.
		char const* error_message() const;

	private:
		aux::allocation_slot m_url_idx;
		aux::allocation_slot m_msg_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		// the HTTP seed that failed
		std::string TORRENT_DEPRECATED_MEMBER url;

		// the error message, potentially from the server
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif

	};

	// If the storage fails to read or write files that it needs access to, this alert is
	// generated and the torrent is paused.
	struct TORRENT_EXPORT file_error_alert final : torrent_alert
	{
		// internal
		file_error_alert(aux::stack_allocator& alloc, error_code const& ec
			, string_view file, operation_t op, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(file_error_alert, 43, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::status
			| alert_category::error
			| alert_category::storage;
		std::string message() const override;

		// the error code describing the error.
		error_code const error;

		// indicates which underlying operation caused the error
		operation_t op;

		// the file that experienced the error
		char const* filename() const;

	private:
		aux::allocation_slot m_file_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		char const* TORRENT_DEPRECATED_MEMBER operation;
		// the path to the file that was accessed when the error occurred.
		std::string TORRENT_DEPRECATED_MEMBER file;
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is generated when the metadata has been completely received and the info-hash
	// failed to match it. i.e. the metadata that was received was corrupt. libtorrent will
	// automatically retry to fetch it in this case. This is only relevant when running a
	// torrent-less download, with the metadata extension provided by libtorrent.
	struct TORRENT_EXPORT metadata_failed_alert final : torrent_alert
	{
		// internal
		metadata_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& ec);

		TORRENT_DEFINE_ALERT(metadata_failed_alert, 44)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		// indicates what failed when parsing the metadata. This error is
		// what's returned from lazy_bdecode().
		error_code const error;
	};

	// This alert is generated when the metadata has been completely received and the torrent
	// can start downloading. It is not generated on torrents that are started with metadata, but
	// only those that needs to download it from peers (when utilizing the libtorrent extension).
	//
	// There are no additional data members in this alert.
	//
	// Typically, when receiving this alert, you would want to save the torrent file in order
	// to load it back up again when the session is restarted. Here's an example snippet of
	// code to do that:
	//
	// .. code:: c++
	//
	//	torrent_handle h = alert->handle();
	//	if (h.is_valid()) {
	//		std::shared_ptr<torrent_info const> ti = h.torrent_file();
	//		create_torrent ct(*ti);
	//		entry te = ct.generate();
	//		std::vector<char> buffer;
	//		bencode(std::back_inserter(buffer), te);
	//		FILE* f = fopen((to_hex(ti->info_hash().to_string()) + ".torrent").c_str(), "wb+");
	//		if (f) {
	//			fwrite(&buffer[0], 1, buffer.size(), f);
	//			fclose(f);
	//		}
	//	}
	//
	struct TORRENT_EXPORT metadata_received_alert final : torrent_alert
	{
		// internal
		metadata_received_alert(aux::stack_allocator& alloc
			, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(metadata_received_alert, 45)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
	};

	// This alert is posted when there is an error on a UDP socket. The
	// UDP sockets are used for all uTP, DHT and UDP tracker traffic. They are
	// global to the session.
	struct TORRENT_EXPORT udp_error_alert final : alert
	{
		// internal
		udp_error_alert(
			aux::stack_allocator& alloc
			, udp::endpoint const& ep
			, operation_t op
			, error_code const& ec);

		TORRENT_DEFINE_ALERT(udp_error_alert, 46)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		// the source address associated with the error (if any)
		aux::noexcept_movable<udp::endpoint> endpoint;

		// the operation that failed
		operation_t operation;

		// the error code describing the error
		error_code const error;
	};

	// Whenever libtorrent learns about the machines external IP, this alert is
	// generated. The external IP address can be acquired from the tracker (if it
	// supports that) or from peers that supports the extension protocol.
	// The address can be accessed through the ``external_address`` member.
	struct TORRENT_EXPORT external_ip_alert final : alert
	{
		// internal
		external_ip_alert(aux::stack_allocator& alloc, address const& ip);

		TORRENT_DEFINE_ALERT(external_ip_alert, 47)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;

		// the IP address that is believed to be our external IP
		aux::noexcept_movable<address> external_address;
	};

	enum class socket_type_t : std::uint8_t
	{
		tcp, tcp_ssl, udp, i2p, socks5, utp_ssl
	};

	// This alert is generated when none of the ports, given in the port range, to
	// session can be opened for listening. The ``listen_interface`` member is the
	// interface that failed, ``error`` is the error code describing the failure.
	//
	// In the case an endpoint was created before generating the alert, it is
	// represented by ``address`` and ``port``. The combinations of socket type
	// and operation in which such address and port are not valid are:
	// accept  - i2p
	// accept  - socks5
	// enum_if - tcp
	//
	// libtorrent may sometimes try to listen on port 0, if all other ports failed.
	// Port 0 asks the operating system to pick a port that's free). If that fails
	// you may see a listen_failed_alert with port 0 even if you didn't ask to
	// listen on it.
	struct TORRENT_EXPORT listen_failed_alert final : alert
	{
#if TORRENT_ABI_VERSION == 1
		enum socket_type_t : std::uint8_t
		{
			tcp TORRENT_DEPRECATED_ENUM,
			tcp_ssl TORRENT_DEPRECATED_ENUM,
			udp TORRENT_DEPRECATED_ENUM,
			i2p TORRENT_DEPRECATED_ENUM,
			socks5 TORRENT_DEPRECATED_ENUM,
			utp_ssl TORRENT_DEPRECATED_ENUM
		};
#endif

		// internal
		listen_failed_alert(aux::stack_allocator& alloc, string_view iface
			, lt::address const& listen_addr, int listen_port
			, operation_t op, error_code const& ec, lt::socket_type_t t);

		listen_failed_alert(aux::stack_allocator& alloc, string_view iface
			, tcp::endpoint const& ep, operation_t op, error_code const& ec
			, lt::socket_type_t t);

		listen_failed_alert(aux::stack_allocator& alloc, string_view iface
			, udp::endpoint const& ep, operation_t op, error_code const& ec
			, lt::socket_type_t t);

		listen_failed_alert(aux::stack_allocator& alloc, string_view iface
			, operation_t op, error_code const& ec, lt::socket_type_t t);

		TORRENT_DEFINE_ALERT_PRIO(listen_failed_alert, 48, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::status | alert_category::error;
		std::string message() const override;

		// the network device libtorrent attempted to listen on, or the IP address
		char const* listen_interface() const;

		// the error the system returned
		error_code const error;

		// the underlying operation that failed
		operation_t op;

		// the type of listen socket this alert refers to.
		lt::socket_type_t const socket_type;

		// the address libtorrent attempted to listen on
		// see alert documentation for validity of this value
		aux::noexcept_movable<lt::address> address;

		// the port libtorrent attempted to listen on
		// see alert documentation for validity of this value
		int const port;

	private:
		std::reference_wrapper<aux::stack_allocator const> m_alloc;
		aux::allocation_slot m_interface_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		enum TORRENT_DEPRECATED_ENUM op_t
		{
			parse_addr TORRENT_DEPRECATED_ENUM,
			open TORRENT_DEPRECATED_ENUM,
			bind TORRENT_DEPRECATED_ENUM,
			listen TORRENT_DEPRECATED_ENUM,
			get_socket_name TORRENT_DEPRECATED_ENUM,
			accept TORRENT_DEPRECATED_ENUM,
			enum_if TORRENT_DEPRECATED_ENUM,
			bind_to_device TORRENT_DEPRECATED_ENUM
		};

		// the specific low level operation that failed. See op_t.
		int const TORRENT_DEPRECATED_MEMBER operation;

		// the address and port libtorrent attempted to listen on
		aux::noexcept_movable<tcp::endpoint> TORRENT_DEPRECATED_MEMBER endpoint;

		// the type of listen socket this alert refers to.
		socket_type_t TORRENT_DEPRECATED_MEMBER sock_type;
#endif
	};

	// This alert is posted when the listen port succeeds to be opened on a
	// particular interface. ``address`` and ``port`` is the endpoint that
	// successfully was opened for listening.
	struct TORRENT_EXPORT listen_succeeded_alert final : alert
	{
#if TORRENT_ABI_VERSION == 1
		enum socket_type_t : std::uint8_t
		{
			tcp TORRENT_DEPRECATED_ENUM,
			tcp_ssl TORRENT_DEPRECATED_ENUM,
			udp TORRENT_DEPRECATED_ENUM,
			i2p TORRENT_DEPRECATED_ENUM,
			socks5 TORRENT_DEPRECATED_ENUM,
			utp_ssl TORRENT_DEPRECATED_ENUM
		};
#endif

		// internal
		listen_succeeded_alert(aux::stack_allocator& alloc
			, lt::address const& listen_addr
			, int listen_port
			, lt::socket_type_t t);

		listen_succeeded_alert(aux::stack_allocator& alloc
			, tcp::endpoint const& ep
			, lt::socket_type_t t);

		listen_succeeded_alert(aux::stack_allocator& alloc
			, udp::endpoint const& ep
			, lt::socket_type_t t);

		TORRENT_DEFINE_ALERT_PRIO(listen_succeeded_alert, 49, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;

		// the address libtorrent ended up listening on. This address
		// refers to the local interface.
		aux::noexcept_movable<lt::address> address;

		// the port libtorrent ended up listening on.
		int const port;

		// the type of listen socket this alert refers to.
		lt::socket_type_t const socket_type;

#if TORRENT_ABI_VERSION == 1
		// the endpoint libtorrent ended up listening on. The address
		// refers to the local interface and the port is the listen port.
		aux::noexcept_movable<tcp::endpoint> TORRENT_DEPRECATED_MEMBER endpoint;

		// the type of listen socket this alert refers to.
		socket_type_t TORRENT_DEPRECATED_MEMBER sock_type;
#endif
	};

	// This alert is generated when a NAT router was successfully found but some
	// part of the port mapping request failed. It contains a text message that
	// may help the user figure out what is wrong. This alert is not generated in
	// case it appears the client is not running on a NAT:ed network or if it
	// appears there is no NAT router that can be remote controlled to add port
	// mappings.
	struct TORRENT_EXPORT portmap_error_alert final : alert
	{
		// internal
		portmap_error_alert(aux::stack_allocator& alloc, port_mapping_t i
			, portmap_transport t
			, error_code const& e);

		TORRENT_DEFINE_ALERT(portmap_error_alert, 50)

		static constexpr alert_category_t static_category = alert_category::port_mapping
			| alert_category::error;
		std::string message() const override;

		// refers to the mapping index of the port map that failed, i.e.
		// the index returned from add_mapping().
		port_mapping_t const mapping;

		// UPnP or NAT-PMP
		portmap_transport map_transport;

		// tells you what failed.
		error_code const error;
#if TORRENT_ABI_VERSION == 1
		// is 0 for NAT-PMP and 1 for UPnP.
		int const TORRENT_DEPRECATED_MEMBER map_type;

		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is generated when a NAT router was successfully found and
	// a port was successfully mapped on it. On a NAT:ed network with a NAT-PMP
	// capable router, this is typically generated once when mapping the TCP
	// port and, if DHT is enabled, when the UDP port is mapped.
	struct TORRENT_EXPORT portmap_alert final : alert
	{
		// internal
		portmap_alert(aux::stack_allocator& alloc, port_mapping_t i, int port
			, portmap_transport t, portmap_protocol protocol);

		TORRENT_DEFINE_ALERT(portmap_alert, 51)

		static constexpr alert_category_t static_category = alert_category::port_mapping;
		std::string message() const override;

		// refers to the mapping index of the port map that failed, i.e.
		// the index returned from add_mapping().
		port_mapping_t const mapping;

		// the external port allocated for the mapping.
		int const external_port;

		portmap_protocol const map_protocol;

		portmap_transport const map_transport;

#if TORRENT_ABI_VERSION == 1
		enum TORRENT_DEPRECATED_ENUM protocol_t
		{
			tcp,
			udp
		};

		// the protocol this mapping was for. one of protocol_t enums
		int const TORRENT_DEPRECATED_MEMBER protocol;

		// 0 for NAT-PMP and 1 for UPnP.
		int const TORRENT_DEPRECATED_MEMBER map_type;
#endif
	};

	// This alert is generated to log informational events related to either
	// UPnP or NAT-PMP. They contain a log line and the type (0 = NAT-PMP
	// and 1 = UPnP). Displaying these messages to an end user is only useful
	// for debugging the UPnP or NAT-PMP implementation. This alert is only
	// posted if the alert_category::port_mapping_log flag is enabled in
	// the alert mask.
	struct TORRENT_EXPORT portmap_log_alert final : alert
	{
		// internal
		portmap_log_alert(aux::stack_allocator& alloc, portmap_transport t, const char* m);

		TORRENT_DEFINE_ALERT(portmap_log_alert, 52)

		static constexpr alert_category_t static_category = alert_category::port_mapping_log;
		std::string message() const override;

		portmap_transport const map_transport;

		// the message associated with this log line
		char const* log_message() const;

	private:

		std::reference_wrapper<aux::stack_allocator const> m_alloc;

		aux::allocation_slot m_log_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		int const TORRENT_DEPRECATED_MEMBER map_type;
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif

	};

	// This alert is generated when a fast resume file has been passed to
	// add_torrent() but the files on disk did not match the fast resume file.
	// The error_code explains the reason why the resume file was rejected.
	struct TORRENT_EXPORT fastresume_rejected_alert final : torrent_alert
	{
		// internal
		fastresume_rejected_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& ec, string_view file
			, operation_t op);

		TORRENT_DEFINE_ALERT_PRIO(fastresume_rejected_alert, 53, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::status
			| alert_category::error;
		std::string message() const override;

		error_code error;

		// If the error happened to a specific file, this returns the path to it.
		char const* file_path() const;

		// the underlying operation that failed
		operation_t op;

	private:
		aux::allocation_slot m_path_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		// If the error happened in a disk operation. a 0-terminated string of
		// the name of that operation. ``operation`` is nullptr otherwise.
		char const* TORRENT_DEPRECATED_MEMBER operation;

		// If the error happened to a specific file, ``file`` is the path to it.
		std::string TORRENT_DEPRECATED_MEMBER file;
		std::string TORRENT_DEPRECATED_MEMBER msg;
#endif
	};

	// This alert is posted when an incoming peer connection, or a peer that's about to be added
	// to our peer list, is blocked for some reason. This could be any of:
	//
	// * the IP filter
	// * i2p mixed mode restrictions (a normal peer is not allowed on an i2p swarm)
	// * the port filter
	// * the peer has a low port and ``no_connect_privileged_ports`` is enabled
	// * the protocol of the peer is blocked (uTP/TCP blocking)
	struct TORRENT_EXPORT peer_blocked_alert final : peer_alert
	{
		// internal
		peer_blocked_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& ep, int r);

		TORRENT_DEFINE_ALERT(peer_blocked_alert, 54)

		static constexpr alert_category_t static_category = alert_category::ip_block;
		std::string message() const override;

		enum reason_t
		{
			ip_filter,
			port_filter,
			i2p_mixed,
			privileged_ports,
			utp_disabled,
			tcp_disabled,
			invalid_local_interface
		};

		// the reason for the peer being blocked. Is one of the values from the
		// reason_t enum.
		int const reason;
	};

	// This alert is generated when a DHT node announces to an info-hash on our
	// DHT node. It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_announce_alert final : alert
	{
		// internal
		dht_announce_alert(aux::stack_allocator& alloc, address const& i, int p
			, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT(dht_announce_alert, 55)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;

		aux::noexcept_movable<address> ip;
		int port;
		sha1_hash info_hash;
	};

	// This alert is generated when a DHT node sends a ``get_peers`` message to
	// our DHT node. It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_get_peers_alert final : alert
	{
		// internal
		dht_get_peers_alert(aux::stack_allocator& alloc, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT(dht_get_peers_alert, 56)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;

		sha1_hash info_hash;
	};

	// This alert is posted approximately once every second, and it contains
	// byte counters of most statistics that's tracked for torrents. Each active
	// torrent posts these alerts regularly.
	// This alert has been superseded by calling ``post_torrent_updates()``
	// regularly on the session object. This alert will be removed
	struct TORRENT_EXPORT stats_alert final : torrent_alert
	{
		// internal
		stats_alert(aux::stack_allocator& alloc, torrent_handle const& h, int interval
			, stat const& s);

		TORRENT_DEFINE_ALERT(stats_alert, 57)

		static constexpr alert_category_t static_category = alert_category::stats;
		std::string message() const override;

		enum stats_channel
		{
			upload_payload,
			upload_protocol,
			download_payload,
			download_protocol,
			upload_ip_protocol,
#if TORRENT_ABI_VERSION == 1
			upload_dht_protocol TORRENT_DEPRECATED_ENUM,
			upload_tracker_protocol TORRENT_DEPRECATED_ENUM,
#else
			deprecated1,
			deprecated2,
#endif
			download_ip_protocol,
#if TORRENT_ABI_VERSION == 1
			download_dht_protocol TORRENT_DEPRECATED_ENUM,
			download_tracker_protocol TORRENT_DEPRECATED_ENUM,
#else
			deprecated3,
			deprecated4,
#endif
			num_channels
		};

		// an array of samples. The enum describes what each sample is a
		// measurement of. All of these are raw, and not smoothing is performed.
		std::array<int, num_channels> const transferred;

		// the number of milliseconds during which these stats were collected.
		// This is typically just above 1000, but if CPU is limited, it may be
		// higher than that.
		int const interval;
	};

	// This alert is posted when the disk cache has been flushed for a specific
	// torrent as a result of a call to torrent_handle::flush_cache(). This
	// alert belongs to the ``storage_notification`` category, which must be
	// enabled to let this alert through. The alert is also posted when removing
	// a torrent from the session, once the outstanding cache flush is complete
	// and the torrent does no longer have any files open.
	struct TORRENT_EXPORT cache_flushed_alert final : torrent_alert
	{
		// internal
		cache_flushed_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(cache_flushed_alert, 58, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::storage;
	};

#if TORRENT_ABI_VERSION == 1

#ifdef _MSC_VER
#pragma warning(push, 1)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	// This alert is posted when a bittorrent feature is blocked because of the
	// anonymous mode. For instance, if the tracker proxy is not set up, no
	// trackers will be used, because trackers can only be used through proxies
	// when in anonymous mode.
	struct TORRENT_DEPRECATED_EXPORT anonymous_mode_alert final : torrent_alert
	{
		// internal
		anonymous_mode_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, int k, string_view s);

		TORRENT_DEFINE_ALERT(anonymous_mode_alert, 59)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		enum kind_t
		{
			// means that there's no proxy set up for tracker
			// communication and the tracker will not be contacted.
			// The tracker which this failed for is specified in the ``str`` member.
			tracker_not_anonymous = 0
		};

		// specifies what error this is, see kind_t.
		int kind;
		std::string str;
	};

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // TORRENT_ABI_VERSION

	// This alert is generated when we receive a local service discovery message
	// from a peer for a torrent we're currently participating in.
	struct TORRENT_EXPORT lsd_peer_alert final : peer_alert
	{
		// internal
		lsd_peer_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& i);

		TORRENT_DEFINE_ALERT(lsd_peer_alert, 60)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;
	};

	// This alert is posted whenever a tracker responds with a ``trackerid``.
	// The tracker ID is like a cookie. libtorrent will store the tracker ID
	// for this tracker and repeat it in subsequent announces.
	struct TORRENT_EXPORT trackerid_alert final : tracker_alert
	{
		// internal
		trackerid_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& ep , string_view u, const std::string& id);

		TORRENT_DEFINE_ALERT(trackerid_alert, 61)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;

		// The tracker ID returned by the tracker
		char const* tracker_id() const;

	private:
		aux::allocation_slot m_tracker_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		// The tracker ID returned by the tracker
		std::string TORRENT_DEPRECATED_MEMBER trackerid;
#endif
	};

	// This alert is posted when the initial DHT bootstrap is done.
	struct TORRENT_EXPORT dht_bootstrap_alert final : alert
	{
		// internal
		explicit dht_bootstrap_alert(aux::stack_allocator& alloc);

		TORRENT_DEFINE_ALERT(dht_bootstrap_alert, 62)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;
	};

	// This is posted whenever a torrent is transitioned into the error state.
	struct TORRENT_EXPORT torrent_error_alert final : torrent_alert
	{
		// internal
		torrent_error_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, error_code const& e, string_view f);

		TORRENT_DEFINE_ALERT_PRIO(torrent_error_alert, 64, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::error | alert_category::status;
		std::string message() const override;

		// specifies which error the torrent encountered.
		error_code const error;

		// the filename (or object) the error occurred on.
		char const* filename() const;

	private:
		aux::allocation_slot m_file_idx;
#if TORRENT_ABI_VERSION == 1
	public:
		// the filename (or object) the error occurred on.
		std::string TORRENT_DEPRECATED_MEMBER error_file;
#endif

	};

	// This is always posted for SSL torrents. This is a reminder to the client that
	// the torrent won't work unless torrent_handle::set_ssl_certificate() is called with
	// a valid certificate. Valid certificates MUST be signed by the SSL certificate
	// in the .torrent file.
	struct TORRENT_EXPORT torrent_need_cert_alert final : torrent_alert
	{
		// internal
		torrent_need_cert_alert(aux::stack_allocator& alloc
			, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(torrent_need_cert_alert, 65, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;
#if TORRENT_ABI_VERSION == 1
		error_code const TORRENT_DEPRECATED_MEMBER error;
#endif
	};

	// The incoming connection alert is posted every time we successfully accept
	// an incoming connection, through any mean. The most straight-forward ways
	// of accepting incoming connections are through the TCP listen socket and
	// the UDP listen socket for uTP sockets. However, connections may also be
	// accepted through a Socks5 or i2p listen socket, or via an SSL listen
	// socket.
	struct TORRENT_EXPORT incoming_connection_alert final : alert
	{
		// internal
		incoming_connection_alert(aux::stack_allocator& alloc, int t
			, tcp::endpoint const& i);

		TORRENT_DEFINE_ALERT(incoming_connection_alert, 66)

		static constexpr alert_category_t static_category = alert_category::peer;
		std::string message() const override;

		// tells you what kind of socket the connection was accepted
		// as:
		//
		// 0. none (no socket instantiated)
		// 1. TCP
		// 2. Socks5
		// 3. HTTP
		// 4. uTP
		// 5. i2p
		// 6. SSL/TCP
		// 7. SSL/Socks5
		// 8. HTTPS (SSL/HTTP)
		// 9. SSL/uTP
		//
		int const socket_type;

		// is the IP address and port the connection came from.
		aux::noexcept_movable<tcp::endpoint> endpoint;

#if TORRENT_ABI_VERSION == 1
		// is the IP address and port the connection came from.
		aux::noexcept_movable<tcp::endpoint> TORRENT_DEPRECATED_MEMBER ip;
#endif
	};

	// This alert is always posted when a torrent was attempted to be added
	// and contains the return status of the add operation. The torrent handle of the new
	// torrent can be found as the ``handle`` member in the base class. If adding
	// the torrent failed, ``error`` contains the error code.
	struct TORRENT_EXPORT add_torrent_alert final : torrent_alert
	{
		// internal
		add_torrent_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, add_torrent_params const& p, error_code const& ec);

		TORRENT_DEFINE_ALERT_PRIO(add_torrent_alert, 67, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;

		// a copy of the parameters used when adding the torrent, it can be used
		// to identify which invocation to ``async_add_torrent()`` caused this alert.
		add_torrent_params params;

		// set to the error, if one occurred while adding the torrent.
		error_code error;
	};

	// This alert is only posted when requested by the user, by calling
	// session::post_torrent_updates() on the session. It contains the torrent
	// status of all torrents that changed since last time this message was
	// posted. Its category is ``status_notification``, but it's not subject to
	// filtering, since it's only manually posted anyway.
	struct TORRENT_EXPORT state_update_alert final : alert
	{
		state_update_alert(aux::stack_allocator& alloc
			, std::vector<torrent_status> st);

		TORRENT_DEFINE_ALERT_PRIO(state_update_alert, 68, alert_priority_high)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;

		// contains the torrent status of all torrents that changed since last
		// time this message was posted. Note that you can map a torrent status
		// to a specific torrent via its ``handle`` member. The receiving end is
		// suggested to have all torrents sorted by the torrent_handle or hashed
		// by it, for efficient updates.
		std::vector<torrent_status> status;
	};

#if TORRENT_ABI_VERSION == 1
#ifdef _MSC_VER
#pragma warning(push, 1)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	struct TORRENT_DEPRECATED_EXPORT mmap_cache_alert final : alert
	{
		mmap_cache_alert(aux::stack_allocator& alloc
			, error_code const& ec);
		TORRENT_DEFINE_ALERT(mmap_cache_alert, 69)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		error_code const error;
	};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // TORRENT_ABI_VERSION

	// The session_stats_alert is posted when the user requests session statistics by
	// calling post_session_stats() on the session object. Its category is
	// ``status_notification``, but it is not subject to filtering, since it's only
	// manually posted anyway.
	//
	// the ``message()`` member function returns a string representation of the values that
	// properly match the line returned in ``session_stats_header_alert::message()``.
	//
	// this specific output is parsed by tools/parse_session_stats.py
	// if this is changed, that parser should also be changed
	struct TORRENT_EXPORT session_stats_alert final : alert
	{
		// internal
		session_stats_alert(aux::stack_allocator& alloc, counters const& cnt);

#if TORRENT_ABI_VERSION == 1
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif
		TORRENT_DEFINE_ALERT_PRIO(session_stats_alert, 70, alert_priority_critical)
#if TORRENT_ABI_VERSION == 1
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif

		static constexpr alert_category_t static_category = alert_category::stats;
		std::string message() const override;

		// An array are a mix of *counters* and *gauges*, which meanings can be
		// queries via the session_stats_metrics() function on the session. The
		// mapping from a specific metric to an index into this array is constant
		// for a specific version of libtorrent, but may differ for other
		// versions. The intended usage is to request the mapping, i.e. call
		// session_stats_metrics(), once on startup, and then use that mapping to
		// interpret these values throughout the process' runtime.
		//
		// For more information, see the session-statistics_ section.
		span<std::int64_t const> counters() const;

#if TORRENT_ABI_VERSION == 1
		std::array<std::int64_t, counters::num_counters> const TORRENT_DEPRECATED_MEMBER values;
#else
	private:
		std::reference_wrapper<aux::stack_allocator const> m_alloc;
		aux::allocation_slot m_counters_idx;
#endif
	};

#if TORRENT_ABI_VERSION == 1
	// hidden
	// When a torrent changes its info-hash, this alert is posted. This only
	// happens in very specific cases. For instance, when a torrent is
	// downloaded from a URL, the true info hash is not known immediately. First
	// the .torrent file must be downloaded and parsed.
	//
	// Once this download completes, the ``torrent_update_alert`` is posted to
	// notify the client of the info-hash changing.
#ifdef _MSC_VER
#pragma warning(push, 1)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	struct TORRENT_DEPRECATED_EXPORT torrent_update_alert final : torrent_alert
	{
		// internal
		torrent_update_alert(aux::stack_allocator& alloc, torrent_handle h
			, sha1_hash const& old_hash, sha1_hash const& new_hash);

		TORRENT_DEFINE_ALERT_PRIO(torrent_update_alert, 71, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::status;
		std::string message() const override;

		// ``old_ih`` and ``new_ih`` are the previous and new info-hash for the torrent, respectively.
		sha1_hash old_ih;
		sha1_hash new_ih;
	};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // TORRENT_ABI_VERSION

	// posted when something fails in the DHT. This is not necessarily a fatal
	// error, but it could prevent proper operation
	struct TORRENT_EXPORT dht_error_alert final : alert
	{
		// internal
		dht_error_alert(aux::stack_allocator& alloc, operation_t op
			, error_code const& ec);

		TORRENT_DEFINE_ALERT(dht_error_alert, 73)

		static constexpr alert_category_t static_category = alert_category::error | alert_category::dht;
		std::string message() const override;

		// the error code
		error_code error;

		// the operation that failed
		operation_t op;

#if TORRENT_ABI_VERSION == 1
		enum op_t
		{
			unknown TORRENT_DEPRECATED_ENUM,
			hostname_lookup TORRENT_DEPRECATED_ENUM
		};

		// the operation that failed
		op_t const TORRENT_DEPRECATED_MEMBER operation;
#endif
	};

	// this alert is posted as a response to a call to session::get_item(),
	// specifically the overload for looking up immutable items in the DHT.
	struct TORRENT_EXPORT dht_immutable_item_alert final : alert
	{
		// internal
		dht_immutable_item_alert(aux::stack_allocator& alloc, sha1_hash const& t
			, entry const& i);

		TORRENT_DEFINE_ALERT_PRIO(dht_immutable_item_alert, 74, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::dht;

		std::string message() const override;

		// the target hash of the immutable item. This must
		// match the SHA-1 hash of the bencoded form of ``item``.
		sha1_hash target;

		// the data for this item
		entry item;
	};

	// this alert is posted as a response to a call to session::get_item(),
	// specifically the overload for looking up mutable items in the DHT.
	struct TORRENT_EXPORT dht_mutable_item_alert final : alert
	{
		// internal
		dht_mutable_item_alert(aux::stack_allocator& alloc
			, std::array<char, 32> const& k, std::array<char, 64> const& sig
			, std::int64_t sequence, string_view s, entry const& i, bool a);

		TORRENT_DEFINE_ALERT_PRIO(dht_mutable_item_alert, 75, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;

		// the public key that was looked up
		std::array<char, 32> key;

		// the signature of the data. This is not the signature of the
		// plain encoded form of the item, but it includes the sequence number
		// and possibly the hash as well. See the dht_store document for more
		// information. This is primarily useful for echoing back in a store
		// request.
		std::array<char, 64> signature;

		// the sequence number of this item
		std::int64_t seq;

		// the salt, if any, used to lookup and store this item. If no
		// salt was used, this is an empty string
		std::string salt;

		// the data for this item
		entry item;

		// the last response for mutable data is authoritative.
		bool authoritative;
	};

	// this is posted when a DHT put operation completes. This is useful if the
	// client is waiting for a put to complete before shutting down for instance.
	struct TORRENT_EXPORT dht_put_alert final : alert
	{
		// internal
		dht_put_alert(aux::stack_allocator& alloc, sha1_hash const& t, int n);
		dht_put_alert(aux::stack_allocator& alloc, std::array<char, 32> const& key
			, std::array<char, 64> const& sig
			, std::string s
			, std::int64_t sequence_number
			, int n);

		TORRENT_DEFINE_ALERT(dht_put_alert, 76)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;

		// the target hash the item was stored under if this was an *immutable*
		// item.
		sha1_hash target;

		// if a mutable item was stored, these are the public key, signature,
		// salt and sequence number the item was stored under.
		std::array<char, 32> public_key;
		std::array<char, 64> signature;
		std::string salt;
		std::int64_t seq;

		// DHT put operation usually writes item to k nodes, maybe the node
		// is stale so no response, or the node doesn't support 'put', or the
		// token for write is out of date, etc. num_success is the number of
		// successful responses we got from the puts.
		int num_success;
	};

	// this alert is used to report errors in the i2p SAM connection
	struct TORRENT_EXPORT i2p_alert final : alert
	{
		// internal
		i2p_alert(aux::stack_allocator& alloc, error_code const& ec);

		TORRENT_DEFINE_ALERT(i2p_alert, 77)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		// the error that occurred in the i2p SAM connection
		error_code error;
	};

	// This alert is generated when we send a get_peers request
	// It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_outgoing_get_peers_alert final : alert
	{
		// internal
		dht_outgoing_get_peers_alert(aux::stack_allocator& alloc
			, sha1_hash const& ih, sha1_hash const& obfih
			, udp::endpoint ep);

		TORRENT_DEFINE_ALERT(dht_outgoing_get_peers_alert, 78)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;

		// the info_hash of the torrent we're looking for peers for.
		sha1_hash info_hash;

		// if this was an obfuscated lookup, this is the info-hash target
		// actually sent to the node.
		sha1_hash obfuscated_info_hash;

		// the endpoint we're sending this query to
		aux::noexcept_movable<udp::endpoint> endpoint;

#if TORRENT_ABI_VERSION == 1
		// the endpoint we're sending this query to
		aux::noexcept_movable<udp::endpoint> TORRENT_DEPRECATED_MEMBER ip;
#endif
	};

	// This alert is posted by some session wide event. Its main purpose is
	// trouble shooting and debugging. It's not enabled by the default alert
	// mask and is enabled by the ``alert_category::session_log`` bit.
	// Furthermore, it's by default disabled as a build configuration.
	struct TORRENT_EXPORT log_alert final : alert
	{
		// internal
		log_alert(aux::stack_allocator& alloc, char const* log);
		log_alert(aux::stack_allocator& alloc, char const* fmt, va_list v);

		TORRENT_DEFINE_ALERT(log_alert, 79)

		static constexpr alert_category_t static_category = alert_category::session_log;
		std::string message() const override;

		// returns the log message
		char const* log_message() const;

#if TORRENT_ABI_VERSION == 1
		// returns the log message
		TORRENT_DEPRECATED
		char const* msg() const;
#endif

	private:
		std::reference_wrapper<aux::stack_allocator const> m_alloc;
		aux::allocation_slot m_str_idx;
	};

	// This alert is posted by torrent wide events. It's meant to be used for
	// trouble shooting and debugging. It's not enabled by the default alert
	// mask and is enabled by the ``alert_category::torrent_log`` bit. By
	// default it is disabled as a build configuration.
	struct TORRENT_EXPORT torrent_log_alert final : torrent_alert
	{
		// internal
		torrent_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, char const* fmt, va_list v);

		TORRENT_DEFINE_ALERT(torrent_log_alert, 80)

		static constexpr alert_category_t static_category = alert_category::torrent_log;
		std::string message() const override;

		// returns the log message
		char const* log_message() const;

#if TORRENT_ABI_VERSION == 1
		// returns the log message
		TORRENT_DEPRECATED
		char const* msg() const;
#endif

	private:
		aux::allocation_slot m_str_idx;
	};

	// This alert is posted by events specific to a peer. It's meant to be used
	// for trouble shooting and debugging. It's not enabled by the default alert
	// mask and is enabled by the ``alert_category::peer_log`` bit. By
	// default it is disabled as a build configuration.
	struct TORRENT_EXPORT peer_log_alert final : peer_alert
	{
		// describes whether this log refers to in-flow or out-flow of the
		// peer. The exception is ``info`` which is neither incoming or outgoing.
		enum direction_t
		{
			incoming_message,
			outgoing_message,
			incoming,
			outgoing,
			info
		};

		// internal
		peer_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& i, peer_id const& pi
			, peer_log_alert::direction_t dir
			, char const* event, char const* fmt, va_list v);

		TORRENT_DEFINE_ALERT(peer_log_alert, 81)

		static constexpr alert_category_t static_category = alert_category::peer_log;
		std::string message() const override;

		// string literal indicating the kind of event. For messages, this is the
		// message name.
		char const* event_type;

		direction_t direction;

		// returns the log message
		char const* log_message() const;

#if TORRENT_ABI_VERSION == 1
		// returns the log message
		TORRENT_DEPRECATED
		char const* msg() const;
#endif

	private:
		aux::allocation_slot m_str_idx;
	};

	// posted if the local service discovery socket fails to start properly.
	// it's categorized as ``error_notification``.
	struct TORRENT_EXPORT lsd_error_alert final : alert
	{
		// internal
		lsd_error_alert(aux::stack_allocator& alloc, error_code const& ec);

		TORRENT_DEFINE_ALERT(lsd_error_alert, 82)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		// The error code
		error_code error;
	};

	// holds statistics about a current dht_lookup operation.
	// a DHT lookup is the traversal of nodes, looking up a
	// set of target nodes in the DHT for retrieving and possibly
	// storing information in the DHT
	struct TORRENT_EXPORT dht_lookup
	{
		// string literal indicating which kind of lookup this is
		char const* type;

		// the number of outstanding request to individual nodes
		// this lookup has right now
		int outstanding_requests;

		// the total number of requests that have timed out so far
		// for this lookup
		int timeouts;

		// the total number of responses we have received for this
		// lookup so far for this lookup
		int responses;

		// the branch factor for this lookup. This is the number of
		// nodes we keep outstanding requests to in parallel by default.
		// when nodes time out we may increase this.
		int branch_factor;

		// the number of nodes left that could be queries for this
		// lookup. Many of these are likely to be part of the trail
		// while performing the lookup and would never end up actually
		// being queried.
		int nodes_left;

		// the number of seconds ago the
		// last message was sent that's still
		// outstanding
		int last_sent;

		// the number of outstanding requests
		// that have exceeded the short timeout
		// and are considered timed out in the
		// sense that they increased the branch
		// factor
		int first_timeout;

		// the node-id or info-hash target for this lookup
		sha1_hash target;
	};

	// contains current DHT state. Posted in response to session::post_dht_stats().
	struct TORRENT_EXPORT dht_stats_alert final : alert
	{
		// internal
		dht_stats_alert(aux::stack_allocator& alloc
			, std::vector<dht_routing_bucket> table
			, std::vector<dht_lookup> requests);

		TORRENT_DEFINE_ALERT(dht_stats_alert, 83)

		static constexpr alert_category_t static_category = alert_category::stats;
		std::string message() const override;

		// a vector of the currently running DHT lookups.
		std::vector<dht_lookup> active_requests;

		// contains information about every bucket in the DHT routing
		// table.
		std::vector<dht_routing_bucket> routing_table;
	};

	// posted every time an incoming request from a peer is accepted and queued
	// up for being serviced. This alert is only posted if
	// the alert_category::incoming_request flag is enabled in the alert
	// mask.
	struct TORRENT_EXPORT incoming_request_alert final : peer_alert
	{
		// internal
		incoming_request_alert(aux::stack_allocator& alloc
			, peer_request r, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		static constexpr alert_category_t static_category = alert_category::incoming_request;
		TORRENT_DEFINE_ALERT(incoming_request_alert, 84)

		std::string message() const override;

		// the request this peer sent to us
		peer_request req;
	};

	// debug logging of the DHT when dht_log_notification is set in the alert
	// mask.
	struct TORRENT_EXPORT dht_log_alert final : alert
	{
		enum dht_module_t
		{
			tracker,
			node,
			routing_table,
			rpc_manager,
			traversal
		};

		// internal
		dht_log_alert(aux::stack_allocator& alloc
			, dht_module_t m, char const* fmt, va_list v);

		static constexpr alert_category_t static_category = alert_category::dht_log;
		TORRENT_DEFINE_ALERT(dht_log_alert, 85)

		std::string message() const override;

		// the log message
		char const* log_message() const;

		// the module, or part, of the DHT that produced this log message.
		dht_module_t module;

	private:
		std::reference_wrapper<aux::stack_allocator const> m_alloc;
		aux::allocation_slot m_msg_idx;
	};

	// This alert is posted every time a DHT message is sent or received. It is
	// only posted if the ``alert_category::dht_log`` alert category is
	// enabled. It contains a verbatim copy of the message.
	struct TORRENT_EXPORT dht_pkt_alert final : alert
	{
		enum direction_t
		{ incoming, outgoing };

		// internal
		dht_pkt_alert(aux::stack_allocator& alloc, span<char const> buf
			, dht_pkt_alert::direction_t d, udp::endpoint const& ep);

		static constexpr alert_category_t static_category = alert_category::dht_log;
		TORRENT_DEFINE_ALERT(dht_pkt_alert, 86)

		std::string message() const override;

		// returns a pointer to the packet buffer and size of the packet,
		// respectively. This buffer is only valid for as long as the alert itself
		// is valid, which is owned by libtorrent and reclaimed whenever
		// pop_alerts() is called on the session.
		span<char const> pkt_buf() const;

		// whether this is an incoming or outgoing packet.
		direction_t direction;

		// the DHT node we received this packet from, or sent this packet to
		// (depending on ``direction``).
		aux::noexcept_movable<udp::endpoint> node;

	private:
		std::reference_wrapper<aux::stack_allocator> m_alloc;
		aux::allocation_slot m_msg_idx;
		int const m_size;
#if TORRENT_ABI_VERSION == 1
	public:
		direction_t TORRENT_DEPRECATED_MEMBER dir;
#endif

	};

	// Posted when we receive a response to a DHT get_peers request.
	struct TORRENT_EXPORT dht_get_peers_reply_alert final : alert
	{
		// internal
		dht_get_peers_reply_alert(aux::stack_allocator& alloc
			, sha1_hash const& ih
			, std::vector<tcp::endpoint> const& v);

		static constexpr alert_category_t static_category = alert_category::dht_operation;
		TORRENT_DEFINE_ALERT(dht_get_peers_reply_alert, 87)

		std::string message() const override;

		sha1_hash info_hash;

		int num_peers() const;

#if TORRENT_ABI_VERSION == 1
		TORRENT_DEPRECATED
		void peers(std::vector<tcp::endpoint>& v) const;
#endif
		std::vector<tcp::endpoint> peers() const;

	private:
		std::reference_wrapper<aux::stack_allocator> m_alloc;
		int m_v4_num_peers = 0;
		int m_v6_num_peers = 0;
		aux::allocation_slot m_v4_peers_idx;
		aux::allocation_slot m_v6_peers_idx;
	};

	// This is posted exactly once for every call to session_handle::dht_direct_request.
	// If the request failed, response() will return a default constructed bdecode_node.
	struct TORRENT_EXPORT dht_direct_response_alert final : alert
	{
		// internal
		dht_direct_response_alert(aux::stack_allocator& alloc, void* userdata
			, udp::endpoint const& addr, bdecode_node const& response);

		// for when there was a timeout so we don't have a response
		dht_direct_response_alert(aux::stack_allocator& alloc, void* userdata
			, udp::endpoint const& addr);

		TORRENT_DEFINE_ALERT_PRIO(dht_direct_response_alert, 88, alert_priority_critical)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;

		void const* userdata;
		aux::noexcept_movable<udp::endpoint> endpoint;

		bdecode_node response() const;

	private:
		std::reference_wrapper<aux::stack_allocator> m_alloc;
		aux::allocation_slot m_response_idx;
		int const m_response_size;
#if TORRENT_ABI_VERSION == 1
	public:
		aux::noexcept_movable<udp::endpoint> TORRENT_DEPRECATED_MEMBER addr;
#endif
	};

	// hidden
	using picker_flags_t = flags::bitfield_flag<std::uint32_t, struct picker_flags_tag>;

	// this is posted when one or more blocks are picked by the piece picker,
	// assuming the verbose piece picker logging is enabled (see
	// picker_log_notification).
	struct TORRENT_EXPORT picker_log_alert final : peer_alert
	{
		// internal
		picker_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& ep, peer_id const& peer_id, picker_flags_t flags
			, span<piece_block const> blocks);

		TORRENT_DEFINE_ALERT(picker_log_alert, 89)

		static constexpr alert_category_t static_category = alert_category::picker_log;
		std::string message() const override;

		static constexpr picker_flags_t partial_ratio = 0_bit;
		static constexpr picker_flags_t prioritize_partials = 1_bit;
		static constexpr picker_flags_t rarest_first_partials = 2_bit;
		static constexpr picker_flags_t rarest_first = 3_bit;
		static constexpr picker_flags_t reverse_rarest_first = 4_bit;
		static constexpr picker_flags_t suggested_pieces = 5_bit;
		static constexpr picker_flags_t prio_sequential_pieces = 6_bit;
		static constexpr picker_flags_t sequential_pieces = 7_bit;
		static constexpr picker_flags_t reverse_pieces = 8_bit;
		static constexpr picker_flags_t time_critical = 9_bit;
		static constexpr picker_flags_t random_pieces = 10_bit;
		static constexpr picker_flags_t prefer_contiguous = 11_bit;
		static constexpr picker_flags_t reverse_sequential = 12_bit;
		static constexpr picker_flags_t backup1 = 13_bit;
		static constexpr picker_flags_t backup2 = 14_bit;
		static constexpr picker_flags_t end_game = 15_bit;
		static constexpr picker_flags_t extent_affinity = 16_bit;

		// this is a bitmask of which features were enabled for this particular
		// pick. The bits are defined in the picker_flags_t enum.
		picker_flags_t const picker_flags;

		std::vector<piece_block> blocks() const;

	private:
		aux::allocation_slot m_array_idx;
		int const m_num_blocks;
	};

	// this alert is posted when the session encounters a serious error,
	// potentially fatal
	struct TORRENT_EXPORT session_error_alert final : alert
	{
		// internal
		session_error_alert(aux::stack_allocator& alloc, error_code err
			, string_view error_str);

		TORRENT_DEFINE_ALERT(session_error_alert, 90)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		// The error code, if one is associated with this error
		error_code const error;

	private:
		std::reference_wrapper<aux::stack_allocator> m_alloc;
		aux::allocation_slot m_msg_idx;
	};

	// posted in response to a call to session::dht_live_nodes(). It contains the
	// live nodes from the DHT routing table of one of the DHT nodes running
	// locally.
	struct TORRENT_EXPORT dht_live_nodes_alert final : alert
	{
		// internal
		dht_live_nodes_alert(aux::stack_allocator& alloc
			, sha1_hash const& nid
			, std::vector<std::pair<sha1_hash, udp::endpoint>> const& nodes);

		TORRENT_DEFINE_ALERT(dht_live_nodes_alert, 91)

		static constexpr alert_category_t static_category = alert_category::dht;
		std::string message() const override;

		// the local DHT node's node-ID this routing table belongs to
		sha1_hash node_id;

		// the number of nodes in the routing table and the actual nodes.
		int num_nodes() const;
		std::vector<std::pair<sha1_hash, udp::endpoint>> nodes() const;

	private:
		std::reference_wrapper<aux::stack_allocator> m_alloc;
		int m_v4_num_nodes = 0;
		int m_v6_num_nodes = 0;
		aux::allocation_slot m_v4_nodes_idx;
		aux::allocation_slot m_v6_nodes_idx;
	};

	// The session_stats_header alert is posted the first time
	// post_session_stats() is called
	//
	// the ``message()`` member function returns a string representation of the
	// header that properly match the stats values string returned in
	// ``session_stats_alert::message()``.
	//
	// this specific output is parsed by tools/parse_session_stats.py
	// if this is changed, that parser should also be changed
	struct TORRENT_EXPORT session_stats_header_alert final : alert
	{
		// internal
		explicit session_stats_header_alert(aux::stack_allocator& alloc);
		TORRENT_DEFINE_ALERT(session_stats_header_alert, 92)

		static constexpr alert_category_t static_category = alert_category::stats;
		std::string message() const override;
	};

	// posted as a response to a call to session::dht_sample_infohashes() with
	// the information from the DHT response message.
	struct TORRENT_EXPORT dht_sample_infohashes_alert final : alert
	{
		// internal
		dht_sample_infohashes_alert(aux::stack_allocator& alloc
			, udp::endpoint const& endp
			, time_duration interval
			, int num
			, std::vector<sha1_hash> const& samples
			, std::vector<std::pair<sha1_hash, udp::endpoint>> const& nodes);

		static constexpr alert_category_t static_category = alert_category::dht_operation;
		TORRENT_DEFINE_ALERT(dht_sample_infohashes_alert, 93)

		std::string message() const override;

		// the node the request was sent to (and this response was received from)
		aux::noexcept_movable<udp::endpoint> endpoint;

		// the interval to wait before making another request to this node
		time_duration const interval;

		// This field indicates how many info-hash keys are currently in the node's storage.
		// If the value is larger than the number of returned samples it indicates that the
		// indexer may obtain additional samples after waiting out the interval.
		int const num_infohashes;

		// returns the number of info-hashes returned by the node, as well as the
		// actual info-hashes. ``num_samples()`` is more efficient than
		// ``samples().size()``.
		int num_samples() const;
		std::vector<sha1_hash> samples() const;

		// The total number of nodes returned by ``nodes()``.
		int num_nodes() const;

		// This is the set of more DHT nodes returned by the request.
		//
		// The information is included so that indexing nodes can perform a key
		// space traversal with a single RPC per node by adjusting the target
		// value for each RPC.
		std::vector<std::pair<sha1_hash, udp::endpoint>> nodes() const;

	private:
		std::reference_wrapper<aux::stack_allocator> m_alloc;
		int const m_num_samples;
		aux::allocation_slot m_samples_idx;
		int m_v4_num_nodes = 0;
		int m_v6_num_nodes = 0;
		aux::allocation_slot m_v4_nodes_idx;
		aux::allocation_slot m_v6_nodes_idx;
	};

	// This alert is posted when a block intended to be sent to a peer is placed in the
	// send buffer. Note that if the connection is closed before the send buffer is sent,
	// the alert may be posted without the bytes having been sent to the peer.
	// It belongs to the ``upload_notification`` category.
	struct TORRENT_EXPORT block_uploaded_alert final : peer_alert
	{
		// internal
		block_uploaded_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
			, piece_index_t piece_num);

		TORRENT_DEFINE_ALERT(block_uploaded_alert, 94)

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		static constexpr alert_category_t static_category =
			alert_category::upload
			PROGRESS_NOTIFICATION
		;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		std::string message() const override;

		int const block_index;
		piece_index_t const piece_index;
	};

	// this alert is posted to indicate to the client that some alerts were
	// dropped. Dropped meaning that the alert failed to be delivered to the
	// client. The most common cause of such failure is that the internal alert
	// queue grew too big (controlled by alert_queue_size).
	struct TORRENT_EXPORT alerts_dropped_alert final : alert
	{
		// internal
		explicit alerts_dropped_alert(aux::stack_allocator& alloc
			, std::bitset<num_alert_types> const&);
		TORRENT_DEFINE_ALERT_PRIO(alerts_dropped_alert, 95, alert_priority_critical + 1)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		// a bitmask indicating which alerts were dropped. Each bit represents the
		// alert type ID, where bit 0 represents whether any alert of type 0 has
		// been dropped, and so on.
		std::bitset<num_alert_types> dropped_alerts;
	};

	// this alert is posted with SOCKS5 related errors, when a SOCKS5 proxy is
	// configured. It's enabled with the error_notification alert category.
	struct TORRENT_EXPORT socks5_alert final : alert
	{
		// internal
		explicit socks5_alert(aux::stack_allocator& alloc
			, tcp::endpoint const& ep, operation_t operation, error_code const& ec);
		TORRENT_DEFINE_ALERT(socks5_alert, 96)

		static constexpr alert_category_t static_category = alert_category::error;
		std::string message() const override;

		// the error
		error_code error;

		// the operation that failed
		operation_t op;

		// the endpoint configured as the proxy
		aux::noexcept_movable<tcp::endpoint> ip;
	};

TORRENT_VERSION_NAMESPACE_2_END

#undef TORRENT_DEFINE_ALERT_IMPL
#undef TORRENT_DEFINE_ALERT
#undef TORRENT_DEFINE_ALERT_PRIO
#undef PROGRESS_NOTIFICATION

}

#endif
