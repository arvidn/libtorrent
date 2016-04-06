/*

Copyright (c) 2003-2016, Arvid Norberg
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

#ifndef TORRENT_NO_DEPRECATE
#include "libtorrent/rss.hpp" // for feed_handle
#endif
#include "libtorrent/operations.hpp" // for operation_t enum
#include "libtorrent/close_reason.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_from_native

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/shared_array.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
// this is to suppress the warnings for using std::auto_ptr
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace libtorrent
{

	namespace aux {
		struct stack_allocator;
	}
	struct piece_block;

	// maps an operation id (from peer_error_alert and peer_disconnected_alert)
	// to its name. See peer_connection for the constants
	TORRENT_EXPORT char const* operation_name(int op);

	// user defined alerts should use IDs greater than this
	static const int user_alert_id = 10000;

	// This is a base class for alerts that are associated with a
	// specific torrent. It contains a handle to the torrent.
	struct TORRENT_EXPORT torrent_alert : alert
	{
		// internal
		torrent_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		// internal
		static const int alert_type = 0;

		// returns the message associated with this alert
		virtual std::string message() const TORRENT_OVERRIDE;

		// The torrent_handle pointing to the torrent this
		// alert is associated with.
		torrent_handle handle;

		char const* torrent_name() const;

#ifndef TORRENT_NO_DEPRECATE
		std::string name;
#endif

	protected:
		aux::stack_allocator const& m_alloc;
	private:
		int m_name_idx;
	};

	// The peer alert is a base class for alerts that refer to a specific peer. It includes all
	// the information to identify the peer. i.e. ``ip`` and ``peer-id``.
	struct TORRENT_EXPORT peer_alert : torrent_alert
	{
		// internal
		peer_alert(aux::stack_allocator& alloc, torrent_handle const& h,
			tcp::endpoint const& i, peer_id const& pi);

		static const int alert_type = 1;
		static const int static_category = alert::peer_notification;
		virtual int category() const TORRENT_OVERRIDE { return static_category; }
		virtual std::string message() const TORRENT_OVERRIDE;

		// The peer's IP address and port.
		tcp::endpoint ip;

		// the peer ID, if known.
		peer_id pid;
	};

	// This is a base class used for alerts that are associated with a
	// specific tracker. It derives from torrent_alert since a tracker
	// is also associated with a specific torrent.
	struct TORRENT_EXPORT tracker_alert : torrent_alert
	{
		// internal
		tracker_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, std::string const& u);

		static const int alert_type = 2;
		static const int static_category = alert::tracker_notification;
		virtual int category() const TORRENT_OVERRIDE { return static_category; }
		virtual std::string message() const TORRENT_OVERRIDE;

		// returns a null-terminated string of the tracker's URL
		char const* tracker_url() const;

#ifndef TORRENT_NO_DEPRECATE
		// The tracker URL
		std::string url;
#endif
	private:
		int m_url_idx;
	};

#ifndef TORRENT_NO_DEPRECATE
	#define TORRENT_CLONE(name) \
		virtual std::auto_ptr<alert> clone_impl() const TORRENT_OVERRIDE \
		{ return std::auto_ptr<alert>(new name(*this)); }
#else
	#define TORRENT_CLONE(name)
#endif

	// we can only use = default in C++11
	// the purpose of this is just to make all alert types non-copyable from user
	// code. The heterogeneous queue does not yet have an emplace_back(), so it
	// still needs to copy alerts, but the important part is that it's not
	// copyable for clients.
	// TODO: Once the backwards compatibility of clone() is removed, and once
	// C++11 is required, this can be simplified to just say = delete
#if __cplusplus >= 201103L
	#define TORRENT_PROTECTED_CCTOR(name) \
	protected: \
		template <class T> friend struct heterogeneous_queue; \
		name(name const&) = default; \
	public:
#else
	#define TORRENT_PROTECTED_CCTOR(name)
#endif

#define TORRENT_DEFINE_ALERT_IMPL(name, seq, prio) \
	TORRENT_PROTECTED_CCTOR(name) \
	static const int priority = prio; \
	static const int alert_type = seq; \
	virtual int type() const TORRENT_OVERRIDE { return alert_type; } \
	TORRENT_CLONE(name) \
	virtual int category() const TORRENT_OVERRIDE { return static_category; } \
	virtual char const* what() const TORRENT_OVERRIDE { return #name; }

#define TORRENT_DEFINE_ALERT(name, seq) \
	TORRENT_DEFINE_ALERT_IMPL(name, seq, 0)

#define TORRENT_DEFINE_ALERT_PRIO(name, seq) \
	TORRENT_DEFINE_ALERT_IMPL(name, seq, 1)

	// The ``torrent_added_alert`` is posted once every time a torrent is successfully
	// added. It doesn't contain any members of its own, but inherits the torrent handle
	// from its base class.
	// It's posted when the ``status_notification`` bit is set in the alert_mask.
	struct TORRENT_EXPORT torrent_added_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_added_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(torrent_added_alert, 3)
		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

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
	struct TORRENT_EXPORT torrent_removed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_removed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT_PRIO(torrent_removed_alert, 4)
		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
		sha1_hash info_hash;
	};

	// This alert is posted when the asynchronous read operation initiated by
	// a call to torrent_handle::read_piece() is completed. If the read failed, the torrent
	// is paused and an error state is set and the buffer member of the alert
	// is 0. If successful, ``buffer`` points to a buffer containing all the data
	// of the piece. ``piece`` is the piece index that was read. ``size`` is the
	// number of bytes that was read.
	// 
	// If the operation fails, ec will indicate what went wrong.
	struct TORRENT_EXPORT read_piece_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		read_piece_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, int p, boost::shared_array<char> d, int s);
		read_piece_alert(aux::stack_allocator& alloc, torrent_handle h, int p, error_code e);

		TORRENT_DEFINE_ALERT_PRIO(read_piece_alert, 5)

		static const int static_category = alert::storage_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		error_code ec;
		boost::shared_array<char> buffer;
		int piece;
		int size;
	};

	// This is posted whenever an individual file completes its download. i.e.
	// All pieces overlapping this file have passed their hash check.
	struct TORRENT_EXPORT file_completed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		file_completed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, int idx);

		TORRENT_DEFINE_ALERT(file_completed_alert, 6)

		static const int static_category = alert::progress_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// refers to the index of the file that completed.
		int index;
	};

	// This is posted as a response to a torrent_handle::rename_file() call, if the rename
	// operation succeeds.
	struct TORRENT_EXPORT file_renamed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		file_renamed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, std::string const& n
			, int idx);

		TORRENT_DEFINE_ALERT_PRIO(file_renamed_alert, 7)

		static const int static_category = alert::storage_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
#ifndef TORRENT_NO_DEPRECATE
		std::string name;
#endif

		char const* new_name() const;

		// refers to the index of the file that was renamed,
		int index;
	private:
		int m_name_idx;
	};

	// This is posted as a response to a torrent_handle::rename_file() call, if the rename
	// operation failed.
	struct TORRENT_EXPORT file_rename_failed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		file_rename_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, int idx
			, error_code ec);

		TORRENT_DEFINE_ALERT_PRIO(file_rename_failed_alert, 8)

		static const int static_category = alert::storage_notification;

		virtual std::string message() const TORRENT_OVERRIDE;

		// refers to the index of the file that was supposed to be renamed,
		// ``error`` is the error code returned from the filesystem.
		int index;
		error_code error;
	};

	// This alert is generated when a limit is reached that might have a negative impact on
	// upload or download rate performance.
	struct TORRENT_EXPORT performance_alert TORRENT_FINAL : torrent_alert
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
			// as follows::
			// 
			//   min(512, max(upload_rate * send_buffer_watermark_factor / 100, send_buffer_watermark))
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
			bittyrant_with_no_uplimit,

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

		static const int static_category = alert::performance_warning;

		virtual std::string message() const TORRENT_OVERRIDE;

		performance_warning_t warning_code;
	};

	// Generated whenever a torrent changes its state.
	struct TORRENT_EXPORT state_changed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		state_changed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, torrent_status::state_t st
			, torrent_status::state_t prev_st);

		TORRENT_DEFINE_ALERT(state_changed_alert, 10)

		static const int static_category = alert::status_notification;

		virtual std::string message() const TORRENT_OVERRIDE;

		// the new state of the torrent.
		torrent_status::state_t state;

		// the previous state.
		torrent_status::state_t prev_state;
	};

	// This alert is generated on tracker time outs, premature disconnects,
	// invalid response or a HTTP response other than "200 OK". From the alert
	// you can get the handle to the torrent the tracker belongs to.
	//
	// The ``times_in_row`` member says how many times in a row this tracker has
	// failed. ``status_code`` is the code returned from the HTTP server. 401
	// means the tracker needs authentication, 404 means not found etc. If the
	// tracker timed out, the code will be set to 0.
	struct TORRENT_EXPORT tracker_error_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		tracker_error_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, int times
			, int status
			, std::string const& u
			, error_code const& e
			, std::string const& m);

		TORRENT_DEFINE_ALERT(tracker_error_alert, 11)

		static const int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		int times_in_row;
		int status_code;
		error_code error;
#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif

		// the message associated with this error
		char const* error_message() const;

	private:
		int m_msg_idx;
	};

	// This alert is triggered if the tracker reply contains a warning field.
	// Usually this means that the tracker announce was successful, but the
	// tracker has a message to the client.
	struct TORRENT_EXPORT tracker_warning_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		tracker_warning_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, std::string const& u
			, std::string const& m);

		TORRENT_DEFINE_ALERT(tracker_warning_alert, 12)

		static const int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#ifndef TORRENT_NO_DEPRECATE
		// contains the warning message from the tracker.
		std::string msg;
#endif

		// the message associated with this warning
		char const* warning_message() const;

	private:
		int m_msg_idx;
	};

	// This alert is generated when a scrape request succeeds.
	struct TORRENT_EXPORT scrape_reply_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		scrape_reply_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, int incomp
			, int comp
			, std::string const& u);

		TORRENT_DEFINE_ALERT(scrape_reply_alert, 13)

		virtual std::string message() const TORRENT_OVERRIDE;

		// the data returned in the scrape response. These numbers
		// may be -1 if the response was malformed.
		int incomplete;
		int complete;
	};

	// If a scrape request fails, this alert is generated. This might be due
	// to the tracker timing out, refusing connection or returning an http response
	// code indicating an error.
	struct TORRENT_EXPORT scrape_failed_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		scrape_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, std::string const& u
			, error_code const& e);
		scrape_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, std::string const& u
			, std::string const& m);

		TORRENT_DEFINE_ALERT(scrape_failed_alert, 14)

		static const int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#ifndef TORRENT_NO_DEPRECATE
		// contains a message describing the error.
		std::string msg;
#endif

		// the error itself. This may indicate that the tracker sent an error
		// message (``error::tracker_failure``), in which case it can be
		// retrieved by calling ``error_message()``.
		error_code error;

		// if the error indicates there is an associated message, this returns
		// that message. Otherwise and empty string.
		char const* error_message() const;

	private:
		int m_msg_idx;
	};

	// This alert is only for informational purpose. It is generated when a tracker announce
	// succeeds. It is generated regardless what kind of tracker was used, be it UDP, HTTP or
	// the DHT.
	struct TORRENT_EXPORT tracker_reply_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		tracker_reply_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, int np
			, std::string const& u);

		TORRENT_DEFINE_ALERT(tracker_reply_alert, 15)

		virtual std::string message() const TORRENT_OVERRIDE;

		// tells how many peers the tracker returned in this response. This is
		// not expected to be more thant the ``num_want`` settings. These are not necessarily
		// all new peers, some of them may already be connected.
		int num_peers;
	};

	// This alert is generated each time the DHT receives peers from a node. ``num_peers``
	// is the number of peers we received in this packet. Typically these packets are
	// received from multiple DHT nodes, and so the alerts are typically generated
	// a few at a time.
	struct TORRENT_EXPORT dht_reply_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		dht_reply_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, int np);

		TORRENT_DEFINE_ALERT(dht_reply_alert, 16)

		virtual std::string message() const TORRENT_OVERRIDE;

		int num_peers;
	};

	// This alert is generated each time a tracker announce is sent (or attempted to be sent).
	// There are no extra data members in this alert. The url can be found in the base class
	// however.
	struct TORRENT_EXPORT tracker_announce_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		tracker_announce_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, std::string const& u, int e);

		TORRENT_DEFINE_ALERT(tracker_announce_alert, 17)

		virtual std::string message() const TORRENT_OVERRIDE;

		// specifies what event was sent to the tracker. It is defined as:
		//
		// 0. None
		// 1. Completed
		// 2. Started
		// 3. Stopped
		int event;
	};

	// This alert is generated when a finished piece fails its hash check. You can get the handle
	// to the torrent which got the failed piece and the index of the piece itself from the alert.
	struct TORRENT_EXPORT hash_failed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		hash_failed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, int index);

		TORRENT_DEFINE_ALERT(hash_failed_alert, 18)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		int piece_index;
	};

	// This alert is generated when a peer is banned because it has sent too many corrupt pieces
	// to us. ``ip`` is the endpoint to the peer that was banned.
	struct TORRENT_EXPORT peer_ban_alert TORRENT_FINAL : peer_alert
	{
		// internal
		peer_ban_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		TORRENT_DEFINE_ALERT(peer_ban_alert, 19)

		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is generated when a peer is unsnubbed. Essentially when it was snubbed for stalling
	// sending data, and now it started sending data again.
	struct TORRENT_EXPORT peer_unsnubbed_alert TORRENT_FINAL : peer_alert
	{
		// internal
		peer_unsnubbed_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		TORRENT_DEFINE_ALERT(peer_unsnubbed_alert, 20)

		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is generated when a peer is snubbed, when it stops sending data when we request
	// it.
	struct TORRENT_EXPORT peer_snubbed_alert TORRENT_FINAL : peer_alert
	{
		// internal
		peer_snubbed_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		TORRENT_DEFINE_ALERT(peer_snubbed_alert, 21)

		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is generated when a peer sends invalid data over the peer-peer protocol. The peer
	// will be disconnected, but you get its ip address from the alert, to identify it.
	struct TORRENT_EXPORT peer_error_alert TORRENT_FINAL : peer_alert
	{
		// internal
		peer_error_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& ep, peer_id const& peer_id, int op
			, error_code const& e);

		TORRENT_DEFINE_ALERT(peer_error_alert, 22)

		static const int static_category = alert::peer_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// a NULL-terminated string of the low-level operation that failed, or NULL if
		// there was no low level disk operation.
		int operation;

		// tells you what error caused this alert.
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This alert is posted every time an outgoing peer connect attempts succeeds.
	struct TORRENT_EXPORT peer_connect_alert TORRENT_FINAL : peer_alert
	{
		// internal
		peer_connect_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int type);

		TORRENT_DEFINE_ALERT(peer_connect_alert, 23)

		static const int static_category = alert::debug_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		int socket_type;
	};

	// This alert is generated when a peer is disconnected for any reason (other than the ones
	// covered by peer_error_alert ).
	struct TORRENT_EXPORT peer_disconnected_alert TORRENT_FINAL : peer_alert
	{
		// internal
		peer_disconnected_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, operation_t op, int type, error_code const& e
			, close_reason_t r);

		TORRENT_DEFINE_ALERT(peer_disconnected_alert, 24)

		static const int static_category = alert::debug_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the kind of socket this peer was connected over
		int socket_type;

		// the operation or level where the error occurred. Specified as an
		// value from the operation_t enum. Defined in operations.hpp.
		operation_t operation;

		// tells you what error caused peer to disconnect.
		error_code error;

		// the reason the peer disconnected (if specified)
		close_reason_t reason;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This is a debug alert that is generated by an incoming invalid piece request.
	// ``ip`` is the address of the peer and the ``request`` is the actual incoming
	// request from the peer. See peer_request for more info.
	struct TORRENT_EXPORT invalid_request_alert TORRENT_FINAL : peer_alert
	{
		// internal
		invalid_request_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, peer_request const& r
			, bool we_have, bool peer_interested, bool withheld);

		TORRENT_DEFINE_ALERT(invalid_request_alert, 25)

		virtual std::string message() const TORRENT_OVERRIDE;

		// the request we received from the peer
		peer_request request;

		// true if we have this piece
		bool we_have;

		// true if the peer indicated that it was interested to download before
		// sending the request
		bool peer_interested;

		// if this is true, the peer is not allowed to download this piece because
		// of superseeding rules.
		bool withheld;
	};

	// This alert is generated when a torrent switches from being a downloader to a seed.
	// It will only be generated once per torrent. It contains a torrent_handle to the
	// torrent in question.
	struct TORRENT_EXPORT torrent_finished_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_finished_alert(aux::stack_allocator& alloc,
			torrent_handle h);

		TORRENT_DEFINE_ALERT(torrent_finished_alert, 26)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// this alert is posted every time a piece completes downloading
	// and passes the hash check. This alert derives from torrent_alert
	// which contains the torrent_handle to the torrent the piece belongs to.
	struct TORRENT_EXPORT piece_finished_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		piece_finished_alert(aux::stack_allocator& alloc,
			torrent_handle const& h, int piece_num);

		TORRENT_DEFINE_ALERT(piece_finished_alert, 27)

		static const int static_category = alert::progress_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the index of the piece that finished
		int piece_index;
	};

	// This alert is generated when a peer rejects or ignores a piece request.
	struct TORRENT_EXPORT request_dropped_alert TORRENT_FINAL : peer_alert
	{
		// internal
		request_dropped_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
			, int piece_num);

		TORRENT_DEFINE_ALERT(request_dropped_alert, 28)

		static const int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		int block_index;
		int piece_index;
	};

	// This alert is generated when a block request times out.
	struct TORRENT_EXPORT block_timeout_alert TORRENT_FINAL : peer_alert
	{
		// internal
		block_timeout_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
			, int piece_num);

		TORRENT_DEFINE_ALERT(block_timeout_alert, 29)

		static const int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		int block_index;
		int piece_index;
	};

	// This alert is generated when a block request receives a response.
	struct TORRENT_EXPORT block_finished_alert TORRENT_FINAL : peer_alert
	{
		// internal
		block_finished_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
			, int piece_num);

		TORRENT_DEFINE_ALERT(block_finished_alert, 30)

		static const int static_category = alert::progress_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		int block_index;
		int piece_index;
	};

	// This alert is generated when a block request is sent to a peer.
	struct TORRENT_EXPORT block_downloading_alert TORRENT_FINAL : peer_alert
	{
		// internal
		block_downloading_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num);

		TORRENT_DEFINE_ALERT(block_downloading_alert, 31)

		static const int static_category = alert::progress_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#ifndef TORRENT_NO_DEPRECATE
		char const* peer_speedmsg;
#endif
		int block_index;
		int piece_index;
	};

	// This alert is generated when a block is received that was not requested or
	// whose request timed out.
	struct TORRENT_EXPORT unwanted_block_alert TORRENT_FINAL : peer_alert
	{
		// internal
		unwanted_block_alert(aux::stack_allocator& alloc, torrent_handle h
			, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num);

		TORRENT_DEFINE_ALERT(unwanted_block_alert, 32)

		virtual std::string message() const TORRENT_OVERRIDE;

		int block_index;
		int piece_index;
	};

	// The ``storage_moved_alert`` is generated when all the disk IO has completed and the
	// files have been moved, as an effect of a call to ``torrent_handle::move_storage``. This
	// is useful to synchronize with the actual disk. The ``path`` member is the new path of
	// the storage.
	struct TORRENT_EXPORT storage_moved_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		storage_moved_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, std::string const& p);

		TORRENT_DEFINE_ALERT(storage_moved_alert, 33)

		static const int static_category = alert::storage_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#ifndef TORRENT_NO_DEPRECATE
		std::string path;
#endif

		// the path the torrent was moved to
		char const* storage_path() const;

	private:
		int m_path_idx;
	};

	// The ``storage_moved_failed_alert`` is generated when an attempt to move the storage,
	// via torrent_handle::move_storage(), fails.
	struct TORRENT_EXPORT storage_moved_failed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		storage_moved_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, error_code const& e
			, std::string const& file
			, char const* op);

		TORRENT_DEFINE_ALERT(storage_moved_failed_alert, 34)

		static const int static_category = alert::storage_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		// If the error happened for a specific file, ``file`` is its path.
		std::string file;
#endif

		// If the error happened for a specific file, this returns its path.
		char const* file_path() const;

		// If the error happened in a specific disk operation this is a NULL
		// terminated string naming which one, otherwise it's NULL.
		char const* operation;
	private:
		int m_file_idx;
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
	struct TORRENT_EXPORT torrent_deleted_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_deleted_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT_PRIO(torrent_deleted_alert, 35)

		static const int static_category = alert::storage_notification;
		virtual std::string message() const TORRENT_OVERRIDE;


		sha1_hash info_hash;
	};

	// This alert is generated when a request to delete the files of a torrent fails.
	// Just removing a torrent from the session cannot fail
	struct TORRENT_EXPORT torrent_delete_failed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_delete_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& e, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT_PRIO(torrent_delete_failed_alert, 36)

		static const int static_category = alert::storage_notification
			| alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// tells you why it failed.
		error_code error;

		// the info hash of the torrent whose files failed to be deleted
		sha1_hash info_hash;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This alert is generated as a response to a ``torrent_handle::save_resume_data`` request.
	// It is generated once the disk IO thread is done writing the state for this torrent.
	struct TORRENT_EXPORT save_resume_data_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		save_resume_data_alert(aux::stack_allocator& alloc
			, boost::shared_ptr<entry> const& rd
			, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(save_resume_data_alert, 37)

		static const int static_category = alert::storage_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// points to the resume data.
		boost::shared_ptr<entry> resume_data;
	};

	// This alert is generated instead of ``save_resume_data_alert`` if there was an error
	// generating the resume data. ``error`` describes what went wrong.
	struct TORRENT_EXPORT save_resume_data_failed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		save_resume_data_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& e);

		TORRENT_DEFINE_ALERT_PRIO(save_resume_data_failed_alert, 38)

		static const int static_category = alert::storage_notification
			| alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the error code from the resume_data failure
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This alert is generated as a response to a ``torrent_handle::pause`` request. It is
	// generated once all disk IO is complete and the files in the torrent have been closed.
	// This is useful for synchronizing with the disk.
	struct TORRENT_EXPORT torrent_paused_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_paused_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(torrent_paused_alert, 39)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is generated as a response to a torrent_handle::resume() request. It is
	// generated when a torrent goes from a paused state to an active state.
	struct TORRENT_EXPORT torrent_resumed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_resumed_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(torrent_resumed_alert, 40)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is posted when a torrent completes checking. i.e. when it transitions
	// out of the ``checking files`` state into a state where it is ready to start downloading
	struct TORRENT_EXPORT torrent_checked_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_checked_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(torrent_checked_alert, 41)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is generated when a HTTP seed name lookup fails.
	struct TORRENT_EXPORT url_seed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, std::string const& u, error_code const& e);
		url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, std::string const& u, std::string const& m);

		TORRENT_DEFINE_ALERT(url_seed_alert, 42)

		static const int static_category = alert::peer_notification | alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#ifndef TORRENT_NO_DEPRECATE
		// the HTTP seed that failed
		std::string url;

		// the error message, potentially from the server
		std::string msg;
#endif

		// the error the web seed encountered. If this is not set, the server
		// sent an error message, call ``error_message()``.
		error_code error;

		// the URL the error is associated with
		char const* server_url() const;

		// in case the web server sent an error message, this function returns
		// it.
		char const* error_message() const;

	private:
		int m_url_idx;
		int m_msg_idx;
	};

	// If the storage fails to read or write files that it needs access to, this alert is
	// generated and the torrent is paused.
	struct TORRENT_EXPORT file_error_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		file_error_alert(aux::stack_allocator& alloc
			, error_code const& ec
			, std::string const& file
			, char const* op
			, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(file_error_alert, 43)

		static const int static_category = alert::status_notification
			| alert::error_notification
			| alert::storage_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#ifndef TORRENT_NO_DEPRECATE
		// the path to the file that was accessed when the error occurred.
		std::string file;
#endif

		// the error code describing the error.
		error_code error;
		char const* operation;

		// the file that experienced the error
		char const* filename() const;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	private:
		int m_file_idx;
	};

	// This alert is generated when the metadata has been completely received and the info-hash
	// failed to match it. i.e. the metadata that was received was corrupt. libtorrent will
	// automatically retry to fetch it in this case. This is only relevant when running a
	// torrent-less download, with the metadata extension provided by libtorrent.
	struct TORRENT_EXPORT metadata_failed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		metadata_failed_alert(aux::stack_allocator& alloc
			, torrent_handle const& h, error_code const& ec);

		TORRENT_DEFINE_ALERT(metadata_failed_alert, 44)

		static const int static_category = alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// indicates what failed when parsing the metadata. This error is
		// what's returned from lazy_bdecode().
		error_code error;
	};

	// This alert is generated when the metadata has been completely received and the torrent
	// can start downloading. It is not generated on torrents that are started with metadata, but
	// only those that needs to download it from peers (when utilizing the libtorrent extension).
	// 
	// There are no additional data members in this alert.
	// 
	// Typically, when receiving this alert, you would want to save the torrent file in order
	// to load it back up again when the session is restarted. Here's an example snippet of
	// code to do that::
	// 
	//	torrent_handle h = alert->handle();
	//	if (h.is_valid()) {
	//		boost::shared_ptr<torrent_info const> ti = h.torrent_file();
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
	struct TORRENT_EXPORT metadata_received_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		metadata_received_alert(aux::stack_allocator& alloc
			, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(metadata_received_alert, 45)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is posted when there is an error on the UDP socket. The
	// UDP socket is used for all uTP, DHT and UDP tracker traffic. It's
	// global to the session.
	struct TORRENT_EXPORT udp_error_alert TORRENT_FINAL : alert
	{
		// internal
		udp_error_alert(
			aux::stack_allocator& alloc
			, udp::endpoint const& ep
			, error_code const& ec);

		TORRENT_DEFINE_ALERT(udp_error_alert, 46)

		static const int static_category = alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the source address associated with the error (if any)
		udp::endpoint endpoint;

		// the error code describing the error
		error_code error;
	};

	// Whenever libtorrent learns about the machines external IP, this alert is
	// generated. The external IP address can be acquired from the tracker (if it
	// supports that) or from peers that supports the extension protocol.
	// The address can be accessed through the ``external_address`` member.
	struct TORRENT_EXPORT external_ip_alert TORRENT_FINAL : alert
	{
		// internal
		external_ip_alert(aux::stack_allocator& alloc, address const& ip);

		TORRENT_DEFINE_ALERT(external_ip_alert, 47)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the IP address that is believed to be our external IP
		address external_address;
	};

	// This alert is generated when none of the ports, given in the port range, to
	// session can be opened for listening. The ``endpoint`` member is the
	// interface and port that failed, ``error`` is the error code describing
	// the failure.
	//
	// libtorrent may sometimes try to listen on port 0, if all other ports failed.
	// Port 0 asks the operating system to pick a port that's free). If that fails
	// you may see a listen_failed_alert with port 0 even if you didn't ask to
	// listen on it.
	struct TORRENT_EXPORT listen_failed_alert TORRENT_FINAL : alert
	{
		enum socket_type_t { tcp, tcp_ssl, udp, i2p, socks5, utp_ssl };

		// internal
		listen_failed_alert(
			aux::stack_allocator& alloc
			, std::string const& iface
			, int port
			, int op
			, error_code const& ec
			, socket_type_t t);

		TORRENT_DEFINE_ALERT_PRIO(listen_failed_alert, 48)

		static const int static_category = alert::status_notification | alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the interface libtorrent attempted to listen on that failed.
		char const* listen_interface() const;

		// the error the system returned
		error_code error;

		enum op_t
		{
			parse_addr, open, bind, listen, get_peer_name, accept
		};

		// the specific low level operation that failed. See op_t.
		int operation;

		// the type of listen socket this alert refers to.
		socket_type_t sock_type;

		// the address and port libtorrent attempted to listen on
		tcp::endpoint endpoint;

	private:
		aux::stack_allocator const& m_alloc;
		int m_interface_idx;
	};

	// This alert is posted when the listen port succeeds to be opened on a
	// particular interface. ``endpoint`` is the endpoint that successfully
	// was opened for listening.
	struct TORRENT_EXPORT listen_succeeded_alert TORRENT_FINAL : alert
	{
		enum socket_type_t { tcp, tcp_ssl, udp, utp_ssl };

		// internal
		listen_succeeded_alert(aux::stack_allocator& alloc, tcp::endpoint const& ep
			, socket_type_t t);

		TORRENT_DEFINE_ALERT_PRIO(listen_succeeded_alert, 49)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the endpoint libtorrent ended up listening on. The address
		// refers to the local interface and the port is the listen port.
		tcp::endpoint endpoint;

		// the type of listen socket this alert refers to.
		socket_type_t sock_type;
	};

	// This alert is generated when a NAT router was successfully found but some
	// part of the port mapping request failed. It contains a text message that
	// may help the user figure out what is wrong. This alert is not generated in
	// case it appears the client is not running on a NAT:ed network or if it
	// appears there is no NAT router that can be remote controlled to add port
	// mappings.
	struct TORRENT_EXPORT portmap_error_alert TORRENT_FINAL : alert
	{
		// internal
		portmap_error_alert(aux::stack_allocator& alloc, int i, int t
			, error_code const& e);

		TORRENT_DEFINE_ALERT(portmap_error_alert, 50)

		static const int static_category = alert::port_mapping_notification
			| alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// refers to the mapping index of the port map that failed, i.e.
		// the index returned from add_mapping().
		int mapping;

		// is 0 for NAT-PMP and 1 for UPnP.
		int map_type;

		// tells you what failed.
		error_code error;
#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This alert is generated when a NAT router was successfully found and
	// a port was successfully mapped on it. On a NAT:ed network with a NAT-PMP
	// capable router, this is typically generated once when mapping the TCP
	// port and, if DHT is enabled, when the UDP port is mapped.
	struct TORRENT_EXPORT portmap_alert TORRENT_FINAL : alert
	{
		// internal
		portmap_alert(aux::stack_allocator& alloc, int i, int port, int t, int protocol);

		TORRENT_DEFINE_ALERT(portmap_alert, 51)

		static const int static_category = alert::port_mapping_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// refers to the mapping index of the port map that failed, i.e.
		// the index returned from add_mapping().
		int mapping;

		// the external port allocated for the mapping.
		int external_port;

		// 0 for NAT-PMP and 1 for UPnP.
		int map_type;

		enum protocol_t
		{
			tcp,
			udp
		};

		// the protocol this mapping was for. one of protocol_t enums
		int protocol;
	};

#ifndef TORRENT_DISABLE_LOGGING

	// This alert is generated to log informational events related to either
	// UPnP or NAT-PMP. They contain a log line and the type (0 = NAT-PMP
	// and 1 = UPnP). Displaying these messages to an end user is only useful
	// for debugging the UPnP or NAT-PMP implementation. This alert is only
	// posted if the alert::port_mapping_log_notification flag is enabled in
	// the alert mask.
	struct TORRENT_EXPORT portmap_log_alert TORRENT_FINAL : alert
	{
		// internal
		portmap_log_alert(aux::stack_allocator& alloc, int t, const char* m);

		TORRENT_DEFINE_ALERT(portmap_log_alert, 52)

		static const int static_category = alert::port_mapping_log_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		int map_type;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif

		// the message associated with this log line
		char const* log_message() const;

	private:

		// TODO: 2 should the alert baseclass have this object instead?
		aux::stack_allocator const& m_alloc;

		int m_log_idx;
	};

#endif

	// This alert is generated when a fastresume file has been passed to
	// add_torrent() but the files on disk did not match the fastresume file.
	// The error_code explains the reason why the resume file was rejected.
	struct TORRENT_EXPORT fastresume_rejected_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		fastresume_rejected_alert(aux::stack_allocator& alloc
			, torrent_handle const& h
			, error_code const& ec
			, std::string const& file
			, char const* op);

		TORRENT_DEFINE_ALERT(fastresume_rejected_alert, 53)

		static const int static_category = alert::status_notification
			| alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		// If the error happened to a specific file, ``file`` is the path to it.
		std::string file;
#endif

		// If the error happened to a specific file, this returns the path to it.
		char const* file_path() const;

		// If the error happened in a disk operation. a NULL-terminated string of
		// the name of that operation. ``operation`` is NULL otherwise.
		char const* operation;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	private:
		int m_path_idx;
	};

	// This alert is posted when an incoming peer connection, or a peer that's about to be added
	// to our peer list, is blocked for some reason. This could be any of:
	// 
	// * the IP filter
	// * i2p mixed mode restrictions (a normal peer is not allowed on an i2p swarm)
	// * the port filter
	// * the peer has a low port and ``no_connect_privileged_ports`` is enabled
	// * the protocol of the peer is blocked (uTP/TCP blocking)
	struct TORRENT_EXPORT peer_blocked_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		peer_blocked_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, address const& i, int r);

		TORRENT_DEFINE_ALERT(peer_blocked_alert, 54)

		static const int static_category = alert::ip_block_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the address that was blocked.
		address ip;

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

		int reason;
	};

	// This alert is generated when a DHT node announces to an info-hash on our
	// DHT node. It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_announce_alert TORRENT_FINAL : alert
	{
		// internal
		dht_announce_alert(aux::stack_allocator& alloc, address const& i, int p
			, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT(dht_announce_alert, 55)

		static const int static_category = alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		address ip;
		int port;
		sha1_hash info_hash;
	};

	// This alert is generated when a DHT node sends a ``get_peers`` message to
	// our DHT node. It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_get_peers_alert TORRENT_FINAL : alert
	{
		// internal
		dht_get_peers_alert(aux::stack_allocator& alloc, sha1_hash const& ih);

		TORRENT_DEFINE_ALERT(dht_get_peers_alert, 56)

		static const int static_category = alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		sha1_hash info_hash;
	};

	// This alert is posted approximately once every second, and it contains
	// byte counters of most statistics that's tracked for torrents. Each active
	// torrent posts these alerts regularly.
	struct TORRENT_EXPORT stats_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		stats_alert(aux::stack_allocator& alloc, torrent_handle const& h, int interval
			, stat const& s);

		TORRENT_DEFINE_ALERT(stats_alert, 57)

		static const int static_category = alert::stats_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		enum stats_channel
		{
			upload_payload,
			upload_protocol,
			download_payload,
			download_protocol,
			upload_ip_protocol,
#ifndef TORRENT_NO_DEPRECATE
			upload_dht_protocol,
			upload_tracker_protocol,
#else
			deprecated1,
			deprecated2,
#endif
			download_ip_protocol,
#ifndef TORRENT_NO_DEPRECATE
			download_dht_protocol,
			download_tracker_protocol,
#else
			deprecated3,
			deprecated4,
#endif
			num_channels
		};

		// an array of samples. The enum describes what each sample is a
		// measurement of. All of these are raw, and not smoothing is performed.
		int transferred[num_channels];

		// the number of milliseconds during which these stats were collected.
		// This is typically just above 1000, but if CPU is limited, it may be
		// higher than that.
		int interval;
	};

	// This alert is posted when the disk cache has been flushed for a specific
	// torrent as a result of a call to torrent_handle::flush_cache(). This
	// alert belongs to the ``storage_notification`` category, which must be
	// enabled to let this alert through. The alert is also posted when removing
	// a torrent from the session, once the outstanding cache flush is complete
	// and the torrent does no longer have any files open.
	struct TORRENT_EXPORT cache_flushed_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		cache_flushed_alert(aux::stack_allocator& alloc, torrent_handle const& h);

		TORRENT_DEFINE_ALERT(cache_flushed_alert, 58)

		static const int static_category = alert::storage_notification;
	};

	// This alert is posted when a bittorrent feature is blocked because of the
	// anonymous mode. For instance, if the tracker proxy is not set up, no
	// trackers will be used, because trackers can only be used through proxies
	// when in anonymous mode.
	struct TORRENT_EXPORT anonymous_mode_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		anonymous_mode_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, int k, std::string const& s);

		TORRENT_DEFINE_ALERT(anonymous_mode_alert, 59)

		static const int static_category = alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		enum kind_t
		{
			// means that there's no proxy set up for tracker
			// communication and the tracker will not be contacted.
			// The tracker which this failed for is specified in the ``str`` member.
			tracker_not_anonymous = 0
		};

		// specifies what error this is,  see kind_t.
		int kind;
		std::string str;
	};

	// This alert is generated when we receive a local service discovery message
	// from a peer for a torrent we're currently participating in.
	struct TORRENT_EXPORT lsd_peer_alert TORRENT_FINAL : peer_alert
	{
		// internal
		lsd_peer_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& i);

		TORRENT_DEFINE_ALERT(lsd_peer_alert, 60)

		static const int static_category = alert::peer_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

	// This alert is posted whenever a tracker responds with a ``trackerid``.
	// The tracker ID is like a cookie. The libtorrent will store the tracker ID
	// for this tracker and repeat it in subsequent announces.
	struct TORRENT_EXPORT trackerid_alert TORRENT_FINAL : tracker_alert
	{
		// internal
		trackerid_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, std::string const& u
			, const std::string& id);

		TORRENT_DEFINE_ALERT(trackerid_alert, 61)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#ifndef TORRENT_NO_DEPRECATE
		// The tracker ID returned by the tracker
		std::string trackerid;
#endif

		// The tracker ID returned by the tracker
		char const* tracker_id() const;

	private:
		int m_tracker_idx;
	};

	// This alert is posted when the initial DHT bootstrap is done.
	struct TORRENT_EXPORT dht_bootstrap_alert TORRENT_FINAL : alert
	{
		// internal
		dht_bootstrap_alert(aux::stack_allocator& alloc);

		TORRENT_DEFINE_ALERT(dht_bootstrap_alert, 62)

		static const int static_category = alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;
	};

#ifndef TORRENT_NO_DEPRECATE
	// This alert is posted on RSS feed events such as start of RSS feed updates,
	// successful completed updates and errors during updates.
	// 
	// This alert is only posted if the ``rss_notifications`` category is enabled
	// in the alert_mask.
	struct TORRENT_DEPRECATED TORRENT_EXPORT rss_alert TORRENT_FINAL : alert
	{
		// internal
		rss_alert(aux::stack_allocator& alloc, feed_handle h
			, std::string const& u, int s, error_code const& ec);

		TORRENT_DEFINE_ALERT(rss_alert, 63)

		static const int static_category = alert::rss_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		enum state_t
		{
			// An update of this feed was just initiated, it will either succeed
			// or fail soon.
			state_updating,

			// The feed just completed a successful update, there may be new items
			// in it. If you're adding torrents manually, you may want to request
			// the feed status of the feed and look through the ``items`` vector.
			state_updated,

			// An error just occurred. See the ``error`` field for information on
			// what went wrong.
			state_error
		};

		// the handle to the feed which generated this alert.
		feed_handle handle;

		// a short cut to access the url of the feed, without
		// having to call feed_handle::get_settings().
		std::string url;

		// one of the values from rss_alert::state_t.
		int state;

		// an error code used for when an error occurs on the feed.
		error_code error;
	};
#endif // TORRENT_NO_DEPRECATE

	// This is posted whenever a torrent is transitioned into the error state.
	struct TORRENT_EXPORT torrent_error_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_error_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, error_code const& e, std::string const& f);

		TORRENT_DEFINE_ALERT(torrent_error_alert, 64)

		static const int static_category = alert::error_notification | alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// specifies which error the torrent encountered.
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		// the filename (or object) the error occurred on.
		std::string error_file;
#endif

		// the filename (or object) the error occurred on.
		char const* filename() const;

	private:
		int m_file_idx;
	};

	// This is always posted for SSL torrents. This is a reminder to the client that
	// the torrent won't work unless torrent_handle::set_ssl_certificate() is called with
	// a valid certificate. Valid certificates MUST be signed by the SSL certificate
	// in the .torrent file.
	struct TORRENT_EXPORT torrent_need_cert_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_need_cert_alert(aux::stack_allocator& alloc
			, torrent_handle const& h);

		TORRENT_DEFINE_ALERT_PRIO(torrent_need_cert_alert, 65)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		error_code error;
	};

	// The incoming connection alert is posted every time we successfully accept
	// an incoming connection, through any mean. The most straight-forward ways
	// of accepting incoming connections are through the TCP listen socket and
	// the UDP listen socket for uTP sockets. However, connections may also be
	// accepted offer a Socks5 or i2p listen socket, or via a torrent specific
	// listen socket for SSL torrents.
	struct TORRENT_EXPORT incoming_connection_alert TORRENT_FINAL : alert
	{
		// internal
		incoming_connection_alert(aux::stack_allocator& alloc, int t
			, tcp::endpoint const& i);

		TORRENT_DEFINE_ALERT(incoming_connection_alert, 66)

		static const int static_category = alert::peer_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

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
		int socket_type;

		// is the IP address and port the connection came from.
		tcp::endpoint ip;
	};

	// This alert is always posted when a torrent was attempted to be added
	// and contains the return status of the add operation. The torrent handle of the new
	// torrent can be found in the base class' ``handle`` member. If adding
	// the torrent failed, ``error`` contains the error code.
	struct TORRENT_EXPORT add_torrent_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		add_torrent_alert(aux::stack_allocator& alloc, torrent_handle h
			, add_torrent_params const& p, error_code ec);

		TORRENT_DEFINE_ALERT_PRIO(add_torrent_alert, 67)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

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
	struct TORRENT_EXPORT state_update_alert TORRENT_FINAL : alert
	{
		state_update_alert(aux::stack_allocator& alloc
			, std::vector<torrent_status> st);

		TORRENT_DEFINE_ALERT_PRIO(state_update_alert, 68)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// contains the torrent status of all torrents that changed since last
		// time this message was posted. Note that you can map a torrent status
		// to a specific torrent via its ``handle`` member. The receiving end is
		// suggested to have all torrents sorted by the torrent_handle or hashed
		// by it, for efficient updates.
		std::vector<torrent_status> status;
	};

	struct TORRENT_EXPORT mmap_cache_alert TORRENT_FINAL : alert
	{
		mmap_cache_alert(aux::stack_allocator& alloc
			, error_code const& ec);
		TORRENT_DEFINE_ALERT(mmap_cache_alert, 69)

		static const int static_category = alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		error_code error;
	};

	// The session_stats_alert is posted when the user requests session statistics by
	// calling post_session_stats() on the session object. Its category is
	// ``status_notification``, but it is not subject to filtering, since it's only
	// manually posted anyway.
	struct TORRENT_EXPORT session_stats_alert TORRENT_FINAL : alert
	{
		session_stats_alert(aux::stack_allocator& alloc, counters const& cnt);
		TORRENT_DEFINE_ALERT_PRIO(session_stats_alert, 70)

		static const int static_category = alert::stats_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// An array are a mix of *counters* and *gauges*, which meanings can be
		// queries via the session_stats_metrics() function on the session. The
		// mapping from a specific metric to an index into this array is constant
		// for a specific version of libtorrent, but may differ for other
		// versions. The intended usage is to request the mapping, i.e. call
		// session_stats_metrics(), once on startup, and then use that mapping to
		// interpret these values throughout the process' runtime.
		//
		// For more information, see the session-statistics_ section.
		boost::uint64_t values[counters::num_counters];
	};

	// hidden
	// When a torrent changes its info-hash, this alert is posted. This only
	// happens in very specific cases. For instance, when a torrent is
	// downloaded from a URL, the true info hash is not known immediately. First
	// the .torrent file must be downloaded and parsed.
	// 
	// Once this download completes, the ``torrent_update_alert`` is posted to
	// notify the client of the info-hash changing.
	struct TORRENT_EXPORT torrent_update_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_update_alert(aux::stack_allocator& alloc, torrent_handle h
			, sha1_hash const& old_hash, sha1_hash const& new_hash);

		TORRENT_DEFINE_ALERT_PRIO(torrent_update_alert, 71)

		static const int static_category = alert::status_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// ``old_ih`` and ``new_ih`` are the previous and new info-hash for the torrent, respectively.
		sha1_hash old_ih;
		sha1_hash new_ih;
	};

#ifndef TORRENT_NO_DEPRECATE
	// This alert is posted every time a new RSS item (i.e. torrent) is received
	// from an RSS feed.
	// 
	// It is only posted if the ``rss_notifications`` category is enabled in the
	// alert_mask.
	struct TORRENT_EXPORT rss_item_alert TORRENT_FINAL : alert
	{
		// internal
		rss_item_alert(aux::stack_allocator& alloc, feed_handle h
			, feed_item const& item);

		TORRENT_DEFINE_ALERT(rss_item_alert, 72)

		static const int static_category = alert::rss_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		feed_handle handle;
		feed_item item;
	};
#endif

	// posted when something fails in the DHT. This is not necessarily a fatal
	// error, but it could prevent proper operation
	struct TORRENT_EXPORT dht_error_alert TORRENT_FINAL : alert
	{
		// internal
		dht_error_alert(aux::stack_allocator& alloc, int op, error_code const& ec);

		TORRENT_DEFINE_ALERT(dht_error_alert, 73)

		static const int static_category = alert::error_notification | alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the error code
		error_code error;

		enum op_t
		{
			unknown,
			hostname_lookup
		};

		// the operation that failed
		op_t operation;
	};

	// this alert is posted as a response to a call to session::get_item(),
	// specifically the overload for looking up immutable items in the DHT.
	struct TORRENT_EXPORT dht_immutable_item_alert TORRENT_FINAL : alert
	{
		dht_immutable_item_alert(aux::stack_allocator& alloc, sha1_hash const& t
			, entry const& i);

		TORRENT_DEFINE_ALERT_PRIO(dht_immutable_item_alert, 74)

		static const int static_category = alert::dht_notification;

		virtual std::string message() const TORRENT_OVERRIDE;

		// the target hash of the immutable item. This must
		// match the sha-1 hash of the bencoded form of ``item``.
		sha1_hash target;

		// the data for this item
		entry item;
	};

	// this alert is posted as a response to a call to session::get_item(),
	// specifically the overload for looking up mutable items in the DHT.
	struct TORRENT_EXPORT dht_mutable_item_alert TORRENT_FINAL : alert
	{
		dht_mutable_item_alert(aux::stack_allocator& alloc
			, boost::array<char, 32> k
			, boost::array<char, 64> sig
			, boost::uint64_t sequence
			, std::string const& s
			, entry const& i
			, bool a);

		TORRENT_DEFINE_ALERT_PRIO(dht_mutable_item_alert, 75)

		static const int static_category = alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the public key that was looked up
		boost::array<char, 32> key;

		// the signature of the data. This is not the signature of the
		// plain encoded form of the item, but it includes the sequence number
		// and possibly the hash as well. See the dht_store document for more
		// information. This is primarily useful for echoing back in a store
		// request.
		boost::array<char, 64> signature;

		// the sequence number of this item
		boost::uint64_t seq;

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
	struct TORRENT_EXPORT dht_put_alert TORRENT_FINAL : alert
	{
		// internal
		dht_put_alert(aux::stack_allocator& alloc, sha1_hash const& t, int n);
		dht_put_alert(aux::stack_allocator& alloc, boost::array<char, 32> key
			, boost::array<char, 64> sig
			, std::string s
			, boost::uint64_t sequence_number
			, int n);

		TORRENT_DEFINE_ALERT(dht_put_alert, 76)

		static const int static_category = alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the target hash the item was stored under if this was an *immutable*
		// item.
		sha1_hash target;

		// if a mutable item was stored, these are the public key, signature,
		// salt and sequence number the item was stored under.
		boost::array<char, 32> public_key;
		boost::array<char, 64> signature;
		std::string salt;
		boost::uint64_t seq;

		// DHT put operation usually writes item to k nodes, maybe the node
		// is stale so no response, or the node doesn't support 'put', or the
		// token for write is out of date, etc. num_success is the number of
		// successful responses we got from the puts.
		int num_success;
	};

	// this alert is used to report errors in the i2p SAM connection
	struct TORRENT_EXPORT i2p_alert TORRENT_FINAL : alert
	{
		i2p_alert(aux::stack_allocator& alloc, error_code const& ec);

		TORRENT_DEFINE_ALERT(i2p_alert, 77)

		static const int static_category = alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the error that occurred in the i2p SAM connection
		error_code error;
	};

	// This alert is generated when we send a get_peers request
	// It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_outgoing_get_peers_alert TORRENT_FINAL : alert
	{
		// internal
		dht_outgoing_get_peers_alert(aux::stack_allocator& alloc
			, sha1_hash const& ih, sha1_hash const& obfih
			, udp::endpoint ep);

		TORRENT_DEFINE_ALERT(dht_outgoing_get_peers_alert, 78)

		static const int static_category = alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// the info_hash of the torrent we're looking for peers for.
		sha1_hash info_hash;

		// if this was an obfuscated lookup, this is the info-hash target
		// actually sent to the node.
		sha1_hash obfuscated_info_hash;

		// the endpoint we're sending this query to
		udp::endpoint ip;
	};

#ifndef TORRENT_DISABLE_LOGGING
	// This alert is posted by some session wide event. Its main purpose is
	// trouble shooting and debugging. It's not enabled by the default alert
	// mask and is enabled by the ``alert::session_log_notification`` bit.
	// Furthermore, it's by default disabled as a build configuration.
	struct TORRENT_EXPORT log_alert TORRENT_FINAL : alert
	{
		// internal
		log_alert(aux::stack_allocator& alloc, char const* log);

		TORRENT_DEFINE_ALERT(log_alert, 79)

		static const int static_category = alert::session_log_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// returns the log message
		char const* msg() const;

	private:
		aux::stack_allocator const& m_alloc;
		int m_str_idx;
	};

	// This alert is posted by torrent wide events. It's meant to be used for
	// trouble shooting and debugging. It's not enabled by the default alert
	// mask and is enabled by the ``alert::torrent_log_notification`` bit. By
	// default it is disabled as a build configuration.
	struct TORRENT_EXPORT torrent_log_alert TORRENT_FINAL : torrent_alert
	{
		// internal
		torrent_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, char const* log);

		TORRENT_DEFINE_ALERT(torrent_log_alert, 80)

		static const int static_category = alert::torrent_log_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// returns the log message
		char const* msg() const;

	private:
		int m_str_idx;
	};

	// This alert is posted by events specific to a peer. It's meant to be used
	// for trouble shooting and debugging. It's not enabled by the default alert
	// mask and is enabled by the ``alert::peer_log_notification`` bit. By
	// default it is disabled as a build configuration.
	struct TORRENT_EXPORT peer_log_alert TORRENT_FINAL : peer_alert
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
			, char const* event, char const* log);

		TORRENT_DEFINE_ALERT(peer_log_alert, 81)

		static const int static_category = alert::peer_log_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// string literal indicating the kind of event. For messages, this is the
		// message name.
		char const* event_type;

		direction_t direction;

		// returns the log message
		char const* msg() const;

	private:
		int m_str_idx;
	};

#endif

	// posted if the local service discovery socket fails to start properly.
	// it's categorized as ``error_notification``.
	struct TORRENT_EXPORT lsd_error_alert TORRENT_FINAL : alert
	{
		// internal
		lsd_error_alert(aux::stack_allocator& alloc, error_code const& ec);

		TORRENT_DEFINE_ALERT(lsd_error_alert, 82)

		static const int static_category = alert::error_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

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

	// contains current DHT state. Posted in response to session::post_dht_stats().
	struct TORRENT_EXPORT dht_stats_alert TORRENT_FINAL : alert
	{
		// internal
		dht_stats_alert(aux::stack_allocator& alloc
			, std::vector<dht_routing_bucket> const& table
			, std::vector<dht_lookup> const& requests);

		TORRENT_DEFINE_ALERT(dht_stats_alert, 83)

		static const int static_category = alert::stats_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		// a vector of the currently running DHT lookups.
		std::vector<dht_lookup> active_requests;

		// contains information about every bucket in the DHT routing
		// table.
		std::vector<dht_routing_bucket> routing_table;
	};

	// posted every time an incoming request from a peer is accepted and queued
	// up for being serviced. This alert is only posted if
	// the alert::incoming_request_notification flag is enabled in the alert
	// mask.
	struct TORRENT_EXPORT incoming_request_alert TORRENT_FINAL : peer_alert
	{
		// internal
		incoming_request_alert(aux::stack_allocator& alloc
			, peer_request r, torrent_handle h
			, tcp::endpoint const& ep, peer_id const& peer_id);

		static const int static_category = alert::incoming_request_notification;
		TORRENT_DEFINE_ALERT(incoming_request_alert, 84)

		virtual std::string message() const TORRENT_OVERRIDE;

		// the request this peer sent to us
		peer_request req;
	};

	struct TORRENT_EXPORT dht_log_alert TORRENT_FINAL : alert
	{
		enum dht_module_t
		{
			tracker,
			node,
			routing_table,
			rpc_manager,
			traversal
		};

		dht_log_alert(aux::stack_allocator& alloc
			, dht_module_t m, char const* msg);

		static const int static_category = alert::dht_log_notification;
		TORRENT_DEFINE_ALERT(dht_log_alert, 85)

		virtual std::string message() const TORRENT_OVERRIDE;

		// the log message
		char const* log_message() const;

		// the module, or part, of the DHT that produced this log message.
		dht_module_t module;

	private:
		aux::stack_allocator& m_alloc;
		int m_msg_idx;
	};

	// This alert is posted every time a DHT message is sent or received. It is
	// only posted if the ``alert::dht_log_notification`` alert category is
	// enabled. It contains a verbatim copy of the message.
	struct TORRENT_EXPORT dht_pkt_alert TORRENT_FINAL : alert
	{
		enum direction_t
		{ incoming, outgoing };

		dht_pkt_alert(aux::stack_allocator& alloc, char const* buf, int size
			, dht_pkt_alert::direction_t d, udp::endpoint ep);

		static const int static_category = alert::dht_log_notification;
		TORRENT_DEFINE_ALERT(dht_pkt_alert, 86)

		virtual std::string message() const TORRENT_OVERRIDE;

		// returns a pointer to the packet buffer and size of the packet,
		// respectively. This buffer is only valid for as long as the alert itself
		// is valid, which is owned by libtorrent and reclaimed whenever
		// pop_alerts() is called on the session.
		char const* pkt_buf() const;
		int pkt_size() const;

		// whether this is an incoming or outgoing packet.
		direction_t dir;

		// the DHT node we received this packet from, or sent this packet to
		// (depending on ``dir``).
		udp::endpoint node;

	private:
		aux::stack_allocator& m_alloc;
		int m_msg_idx;
		int m_size;
	};

	struct TORRENT_EXPORT dht_get_peers_reply_alert TORRENT_FINAL : alert {

		dht_get_peers_reply_alert(aux::stack_allocator& alloc
			, sha1_hash const& ih
			, std::vector<tcp::endpoint> const& v);

		static const int static_category = alert::dht_operation_notification;
		TORRENT_DEFINE_ALERT(dht_get_peers_reply_alert, 87)

		virtual std::string message() const TORRENT_OVERRIDE;

		sha1_hash info_hash;

		int num_peers() const;
		void peers(std::vector<tcp::endpoint>& peers) const;

	private:
		aux::stack_allocator& m_alloc;
		int m_num_peers;
		int m_peers_idx;
	};

	// This is posted exactly once for every call to session_handle::dht_direct_request.
	// If the request failed, response() will return a default constructed bdecode_node.
	struct TORRENT_EXPORT dht_direct_response_alert TORRENT_FINAL : alert
	{
		dht_direct_response_alert(aux::stack_allocator& alloc, void* userdata
			, udp::endpoint const& addr, bdecode_node const& response);

		// for when there was a timeout so we don't have a response
		dht_direct_response_alert(aux::stack_allocator& alloc, void* userdata
			, udp::endpoint const& addr);

		TORRENT_DEFINE_ALERT(dht_direct_response_alert, 88)

		static const int static_category = alert::dht_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

		void* userdata;
		udp::endpoint addr;

		bdecode_node response() const;

	private:
		aux::stack_allocator& m_alloc;
		int m_response_idx;
		int m_response_size;
	};

	// this is posted when one or more blocks are picked by the piece picker,
	// assuming the verbose piece picker logging is enabled (see
	// picker_log_notification).
	struct TORRENT_EXPORT picker_log_alert : peer_alert
	{
#ifndef TORRENT_DISABLE_LOGGING

		// internal
		picker_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
			, tcp::endpoint const& ep, peer_id const& peer_id, boost::uint32_t flags
			, piece_block const* blocks, int num_blocks);

		TORRENT_DEFINE_ALERT(picker_log_alert, 89)

		static const int static_category = alert::picker_log_notification;
		virtual std::string message() const TORRENT_OVERRIDE;

#endif // TORRENT_DISABLE_LOGGING

		enum picker_flags_t
		{
			// the ratio of partial pieces is too high. This forces a preference
			// for picking blocks from partial pieces.
			partial_ratio          = 0x1,
			prioritize_partials    = 0x2,
			rarest_first_partials  = 0x4,
			rarest_first           = 0x8,
			reverse_rarest_first   = 0x10,
			suggested_pieces       = 0x20,
			prio_sequential_pieces = 0x40,
			sequential_pieces      = 0x80,
			reverse_pieces         = 0x100,
			time_critical          = 0x200,
			random_pieces          = 0x400,
			prefer_contiguous      = 0x800,
			reverse_sequential     = 0x1000,
			backup1                = 0x2000,
			backup2                = 0x4000,
			end_game               = 0x8000
		};

#ifndef TORRENT_DISABLE_LOGGING

		// this is a bitmask of which features were enabled for this particular
		// pick. The bits are defined in the picker_flags_t enum.
		boost::uint32_t picker_flags;

		std::vector<piece_block> blocks() const;

	private:
		int m_array_idx;
		int m_num_blocks;
#endif // TORRENT_DISABLE_LOGGING
	};

#undef TORRENT_DEFINE_ALERT_IMPL
#undef TORRENT_DEFINE_ALERT
#undef TORRENT_DEFINE_ALERT_PRIO
#undef TORRENT_CLONE

	enum { num_alert_types = 90 }; // this enum represents "max_alert_index" + 1
}


#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif

