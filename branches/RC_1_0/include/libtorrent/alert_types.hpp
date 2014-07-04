/*

Copyright (c) 2003-2014, Arvid Norberg
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

#include "libtorrent/alert.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/rss.hpp" // for feed_handle

// lines reserved for future includes
// the type-ids of the alert types
// are derived from the line on which
// they are declared




namespace libtorrent
{

	// user defined alerts should use IDs greater than this
	const static int user_alert_id = 10000;

	// This is a base class for alerts that are associated with a
	// specific torrent. It contains a handle to the torrent.
	struct TORRENT_EXPORT torrent_alert: alert
	{
		// internal
		torrent_alert(torrent_handle const& h)
			: handle(h)
		{}
		
		// internal
		const static int alert_type = 1;
		virtual std::string message() const;

		// The torrent_handle pointing to the torrent this
		// alert is associated with.
		torrent_handle handle;
	};

	// The peer alert is a base class for alerts that refer to a specific peer. It includes all
	// the information to identify the peer. i.e. ``ip`` and ``peer-id``.
	struct TORRENT_EXPORT peer_alert: torrent_alert
	{
		// internal
		peer_alert(torrent_handle const& h, tcp::endpoint const& i
			, peer_id const& pi)
			: torrent_alert(h)
			, ip(i)
			, pid(pi)
		{}

		const static int alert_type = 2;
		const static int static_category = alert::peer_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const;

		// The peer's IP address and port.
		tcp::endpoint ip;

		// the peer ID, if known.
		peer_id pid;
	};

	// This is a base class used for alerts that are associated with a
	// specific tracker. It derives from torrent_alert since a tracker
	// is also associated with a specific torrent.
	struct TORRENT_EXPORT tracker_alert: torrent_alert
	{
		// internal
		tracker_alert(torrent_handle const& h
			, std::string const& u)
			: torrent_alert(h)
			, url(u)
		{}

		const static int alert_type = 3;
		const static int static_category = alert::tracker_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const;

		// The tracker URL
		std::string url;
	};

#define TORRENT_DEFINE_ALERT(name) \
	const static int alert_type = __LINE__; \
	virtual int type() const { return alert_type; } \
	virtual std::auto_ptr<alert> clone() const \
	{ return std::auto_ptr<alert>(new name(*this)); } \
	virtual int category() const { return static_category; } \
	virtual char const* what() const { return #name; }

	// The ``torrent_added_alert`` is posted once every time a torrent is successfully
	// added. It doesn't contain any members of its own, but inherits the torrent handle
	// from its base class.
	// It's posted when the ``status_notification`` bit is set in the alert_mask.
	struct TORRENT_EXPORT torrent_added_alert: torrent_alert
	{
		// internal
		torrent_added_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(torrent_added_alert);
		const static int static_category = alert::status_notification;
		virtual std::string message() const;
	};

	// The ``torrent_removed_alert`` is posted whenever a torrent is removed. Since
	// the torrent handle in its baseclass will always be invalid (since the torrent
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
	struct TORRENT_EXPORT torrent_removed_alert: torrent_alert
	{
		// internal
		torrent_removed_alert(torrent_handle const& h, sha1_hash const& ih)
			: torrent_alert(h)
			, info_hash(ih)
		{}

		TORRENT_DEFINE_ALERT(torrent_removed_alert);
		const static int static_category = alert::status_notification;
		virtual std::string message() const;
		sha1_hash info_hash;
	};

	// This alert is posted when the asynchronous read operation initiated by
	// a call to torrent_handle::read_piece() is completed. If the read failed, the torrent
	// is paused and an error state is set and the buffer member of the alert
	// is 0. If successful, ``buffer`` points to a buffer containing all the data
	// of the piece. ``piece`` is the piece index that was read. ``size`` is the
	// number of bytes that was read.
	// 
	// If the operation fails, ec will indicat what went wrong.
 	struct TORRENT_EXPORT read_piece_alert: torrent_alert
	{
		// internal
		read_piece_alert(torrent_handle const& h
			, int p, boost::shared_array<char> d, int s)
			: torrent_alert(h)
			, buffer(d)
			, piece(p)
			, size(s)
		{}
		read_piece_alert(torrent_handle h, int p, error_code e)
			: torrent_alert(h)
			, ec(e)
			, piece(p)
			, size(0)
		{}

		TORRENT_DEFINE_ALERT(read_piece_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		error_code ec;
		boost::shared_array<char> buffer;
		int piece;
		int size;
	};

	// This is posted whenever an individual file completes its download. i.e.
	// All pieces overlapping this file have passed their hash check.
	struct TORRENT_EXPORT file_completed_alert: torrent_alert
	{
		// internal
		file_completed_alert(torrent_handle const& h
			, int idx)
			: torrent_alert(h)
			, index(idx)
		{}

		TORRENT_DEFINE_ALERT(file_completed_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const;

		// refers to the index of the file that completed.
		int index;
	};

	// This is posted as a response to a torrent_handle::rename_file() call, if the rename
	// operation succeeds.
	struct TORRENT_EXPORT file_renamed_alert: torrent_alert
	{
		// internal
		file_renamed_alert(torrent_handle const& h
			, std::string const& n
			, int idx)
			: torrent_alert(h)
			, name(n)
			, index(idx)
		{}

		TORRENT_DEFINE_ALERT(file_renamed_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		std::string name;

		// refers to the index of the file that was renamed,
		// ``name`` is the new name of the file.
		int index;
	};

	// This is posted as a response to a torrent_handle::rename_file() call, if the rename
	// operation failed.
	struct TORRENT_EXPORT file_rename_failed_alert: torrent_alert
	{
		// internal
		file_rename_failed_alert(torrent_handle const& h
			, int idx
			, error_code ec)
			: torrent_alert(h)
			, index(idx)
			, error(ec)
		{}

		TORRENT_DEFINE_ALERT(file_rename_failed_alert);

		const static int static_category = alert::storage_notification;

		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		// refers to the index of the file that was supposed to be renamed,
		// ``error`` is the error code returned from the filesystem.
		int index;
		error_code error;
	};

	// This alert is generated when a limit is reached that might have a negative impact on
	// upload or download rate performance.
	struct TORRENT_EXPORT performance_alert: torrent_alert
	{
		enum performance_warning_t
		{

			// This warning means that the number of bytes queued to be written to disk
			// exceeds the max disk byte queue setting (``session_settings::max_queued_disk_bytes``).
			// This might restrict the download rate, by not queuing up enough write jobs
			// to the disk I/O thread. When this alert is posted, peer connections are
			// temporarily stopped from downloading, until the queued disk bytes have fallen
			// below the limit again. Unless your ``max_queued_disk_bytes`` setting is already
			// high, you might want to increase it to get better performance.
			outstanding_disk_buffer_limit_reached,

			// This is posted when libtorrent would like to send more requests to a peer,
			// but it's limited by ``session_settings::max_out_request_queue``. The queue length
			// libtorrent is trying to achieve is determined by the download rate and the
			// assumed round-trip-time (``session_settings::request_queue_time``). The assumed
			// rount-trip-time is not limited to just the network RTT, but also the remote disk
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
			// limit instead of upload. This suggests that your download rate limit is mcuh lower
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
			// If you receive this alert, you migth want to either increase your ``send_buffer_watermark``
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

			bittyrant_with_no_uplimit,

			// This is generated if outgoing peer connections are failing because of *address in use*
			// errors, indicating that ``session_settings::outgoing_ports`` is set and is too small of
			// a range. Consider not using the ``outgoing_ports`` setting at all, or widen the range to
			// include more ports.
			too_few_outgoing_ports,

			too_few_file_descriptors,

			num_warnings
		};

		// internal
		performance_alert(torrent_handle const& h
			, performance_warning_t w)
			: torrent_alert(h)
			, warning_code(w)
		{}

		TORRENT_DEFINE_ALERT(performance_alert);

		const static int static_category = alert::performance_warning;

		virtual std::string message() const;

		performance_warning_t warning_code;
	};

	// Generated whenever a torrent changes its state.
	struct TORRENT_EXPORT state_changed_alert: torrent_alert
	{
		// internal
		state_changed_alert(torrent_handle const& h
			, torrent_status::state_t st
			, torrent_status::state_t prev_st)
			: torrent_alert(h)
			, state(st)
			, prev_state(prev_st)
		{}

		TORRENT_DEFINE_ALERT(state_changed_alert);

		const static int static_category = alert::status_notification;

		virtual std::string message() const;

		// the new state of the torrent.
		torrent_status::state_t state;

		// the previous state.
		torrent_status::state_t prev_state;
	};

	// This alert is generated on tracker time outs, premature disconnects, invalid response or
	// a HTTP response other than "200 OK". From the alert you can get the handle to the torrent
	// the tracker belongs to.
	//
	// The ``times_in_row`` member says how many times in a row this tracker has failed.
	// ``status_code`` is the code returned from the HTTP server. 401 means the tracker needs
	// authentication, 404 means not found etc. If the tracker timed out, the code will be set
	// to 0.
	struct TORRENT_EXPORT tracker_error_alert: tracker_alert
	{
		// internal
		tracker_error_alert(torrent_handle const& h
			, int times
			, int status
			, std::string const& u
			, error_code const& e
			, std::string const& m)
			: tracker_alert(h, u)
			, times_in_row(times)
			, status_code(status)
			, error(e)
			, msg(m)
		{
			TORRENT_ASSERT(!url.empty());
		}

		TORRENT_DEFINE_ALERT(tracker_error_alert);

		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const;

		int times_in_row;
		int status_code;
		error_code error;
		std::string msg;
	};

	// This alert is triggered if the tracker reply contains a warning field. Usually this
	// means that the tracker announce was successful, but the tracker has a message to
	// the client.
	struct TORRENT_EXPORT tracker_warning_alert: tracker_alert
	{
		// internal
		tracker_warning_alert(torrent_handle const& h
			, std::string const& u
			, std::string const& m)
			: tracker_alert(h, u)
			, msg(m)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(tracker_warning_alert);

		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const;

		// contains the warning message from the tracker.
		std::string msg;
	};

	// This alert is generated when a scrape request succeeds.
	struct TORRENT_EXPORT scrape_reply_alert: tracker_alert
	{
		// internal
		scrape_reply_alert(torrent_handle const& h
			, int incomp
			, int comp
			, std::string const& u)
			: tracker_alert(h, u)
			, incomplete(incomp)
			, complete(comp)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(scrape_reply_alert);

		virtual std::string message() const;

		// the data returned in the scrape response. These numbers
		// may be -1 if the reponse was malformed.
		int incomplete;
		int complete;
	};

	// If a scrape request fails, this alert is generated. This might be due
	// to the tracker timing out, refusing connection or returning an http response
	// code indicating an error.
	struct TORRENT_EXPORT scrape_failed_alert: tracker_alert
	{
		// internal
		scrape_failed_alert(torrent_handle const& h
			, std::string const& u
			, error_code const& e)
			: tracker_alert(h, u)
			, msg(convert_from_native(e.message()))
		{ TORRENT_ASSERT(!url.empty()); }

		scrape_failed_alert(torrent_handle const& h
			, std::string const& u
			, std::string const& m)
			: tracker_alert(h, u)
			, msg(m)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(scrape_failed_alert);

		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const;

		// contains a message describing the error.
		std::string msg;
	};

	// This alert is only for informational purpose. It is generated when a tracker announce
	// succeeds. It is generated regardless what kind of tracker was used, be it UDP, HTTP or
	// the DHT.
	struct TORRENT_EXPORT tracker_reply_alert: tracker_alert
	{
		// internal
		tracker_reply_alert(torrent_handle const& h
			, int np
			, std::string const& u)
			: tracker_alert(h, u)
			, num_peers(np)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(tracker_reply_alert);

		virtual std::string message() const;

		// tells how many peers the tracker returned in this response. This is
		// not expected to be more thant the ``num_want`` settings. These are not necessarily
		// all new peers, some of them may already be connected.
		int num_peers;
	};

	// This alert is generated each time the DHT receives peers from a node. ``num_peers``
	// is the number of peers we received in this packet. Typically these packets are
	// received from multiple DHT nodes, and so the alerts are typically generated
	// a few at a time.
	struct TORRENT_EXPORT dht_reply_alert: tracker_alert
	{
		// internal
		dht_reply_alert(torrent_handle const& h
			, int np)
			: tracker_alert(h, "")
			, num_peers(np)
		{}

		TORRENT_DEFINE_ALERT(dht_reply_alert);

		virtual std::string message() const;

		int num_peers;
	};

	// This alert is generated each time a tracker announce is sent (or attempted to be sent).
	// There are no extra data members in this alert. The url can be found in the base class
	// however.
	struct TORRENT_EXPORT tracker_announce_alert: tracker_alert
	{
		// internal
		tracker_announce_alert(torrent_handle const& h
			, std::string const& u, int e)
			: tracker_alert(h, u)
			, event(e)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(tracker_announce_alert);

		virtual std::string message() const;

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
	struct TORRENT_EXPORT hash_failed_alert: torrent_alert
	{
		// internal
		hash_failed_alert(
			torrent_handle const& h
			, int index)
			: torrent_alert(h)
			, piece_index(index)
		{ TORRENT_ASSERT(index >= 0);}

		TORRENT_DEFINE_ALERT(hash_failed_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const;

		int piece_index;
	};

	// This alert is generated when a peer is banned because it has sent too many corrupt pieces
	// to us. ``ip`` is the endpoint to the peer that was banned.
	struct TORRENT_EXPORT peer_ban_alert: peer_alert
	{
		// internal
		peer_ban_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id)
			: peer_alert(h, ep, peer_id)
		{}

		TORRENT_DEFINE_ALERT(peer_ban_alert);

		virtual std::string message() const;
	};

	// This alert is generated when a peer is unsnubbed. Essentially when it was snubbed for stalling
	// sending data, and now it started sending data again.
	struct TORRENT_EXPORT peer_unsnubbed_alert: peer_alert
	{
		// internal
		peer_unsnubbed_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id)
			: peer_alert(h, ep, peer_id)
		{}

		TORRENT_DEFINE_ALERT(peer_unsnubbed_alert);

		virtual std::string message() const;
	};

	// This alert is generated when a peer is snubbed, when it stops sending data when we request
	// it.
	struct TORRENT_EXPORT peer_snubbed_alert: peer_alert
	{
		// internal
		peer_snubbed_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id)
			: peer_alert(h, ep, peer_id)
		{}

		TORRENT_DEFINE_ALERT(peer_snubbed_alert);

		virtual std::string message() const;
	};

	// This alert is generated when a peer sends invalid data over the peer-peer protocol. The peer
	// will be disconnected, but you get its ip address from the alert, to identify it.
	struct TORRENT_EXPORT peer_error_alert: peer_alert
	{
		// internal
		peer_error_alert(torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, error_code const& e)
			: peer_alert(h, ep, peer_id)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = convert_from_native(error.message());
#endif
		}

		TORRENT_DEFINE_ALERT(peer_error_alert);

		const static int static_category = alert::peer_notification;
		virtual std::string message() const
		{
			return peer_alert::message() + " peer error: " + convert_from_native(error.message());
		}

		// tells you what error caused this alert.
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This alert is posted every time an outgoing peer connect attempts succeeds.
	struct TORRENT_EXPORT peer_connect_alert: peer_alert
	{
		// internal
		peer_connect_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id, int type)
			: peer_alert(h, ep, peer_id)
			, socket_type(type)
		{}

		TORRENT_DEFINE_ALERT(peer_connect_alert);

		const static int static_category = alert::debug_notification;
		virtual std::string message() const;

		int socket_type;
	};

	// This alert is generated when a peer is disconnected for any reason (other than the ones
	// covered by peer_error_alert ).
	struct TORRENT_EXPORT peer_disconnected_alert: peer_alert
	{
		// internal
		peer_disconnected_alert(torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, error_code const& e)
			: peer_alert(h, ep, peer_id)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = convert_from_native(error.message());
#endif
		}

		TORRENT_DEFINE_ALERT(peer_disconnected_alert);

		const static int static_category = alert::debug_notification;
		virtual std::string message() const;

		// tells you what error caused peer to disconnect.
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This is a debug alert that is generated by an incoming invalid piece request.
	// ``ip`` is the address of the peer and the ``request`` is the actual incoming
	// request from the peer. See peer_request for more info.
	struct TORRENT_EXPORT invalid_request_alert: peer_alert
	{
		// internal
		invalid_request_alert(torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, peer_request const& r)
			: peer_alert(h, ep, peer_id)
			, request(r)
		{}

		TORRENT_DEFINE_ALERT(invalid_request_alert);

		virtual std::string message() const;

		peer_request request;
	};

	// This alert is generated when a torrent switches from being a downloader to a seed.
	// It will only be generated once per torrent. It contains a torrent_handle to the
	// torrent in question.
	struct TORRENT_EXPORT torrent_finished_alert: torrent_alert
	{
		// internal
		torrent_finished_alert(
			const torrent_handle& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(torrent_finished_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " torrent finished downloading"; }
	};

	// this alert is posted every time a piece completes downloading
	// and passes the hash check. This alert derives from torrent_alert
	// which contains the torrent_handle to the torrent the piece belongs to.
	struct TORRENT_EXPORT piece_finished_alert: torrent_alert
	{
		// internal
		piece_finished_alert(
			const torrent_handle& h
			, int piece_num)
			: torrent_alert(h)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(piece_index >= 0);}

		TORRENT_DEFINE_ALERT(piece_finished_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const;

		// the index of the piece that finished
		int piece_index;
	};

	// This alert is generated when a peer rejects or ignores a piece request.
	struct TORRENT_EXPORT request_dropped_alert: peer_alert
	{
		// internal
		request_dropped_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(request_dropped_alert);

		const static int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual std::string message() const;

		int block_index;
		int piece_index;
	};

	// This alert is generated when a block request times out.
	struct TORRENT_EXPORT block_timeout_alert: peer_alert
	{
		// internal
		block_timeout_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(block_timeout_alert);

		const static int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual std::string message() const;

		int block_index;
		int piece_index;
	};

	// This alert is generated when a block request receives a response.
	struct TORRENT_EXPORT block_finished_alert: peer_alert
	{
		// internal
		block_finished_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(block_finished_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const;

		int block_index;
		int piece_index;
	};

	// This alert is generated when a block request is sent to a peer.
	struct TORRENT_EXPORT block_downloading_alert: peer_alert
	{
		// internal
		block_downloading_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, char const* speedmsg, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, peer_speedmsg(speedmsg)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0); }

		TORRENT_DEFINE_ALERT(block_downloading_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const;

		char const* peer_speedmsg;
		int block_index;
		int piece_index;
	};

	// This alert is generated when a block is received that was not requested or
	// whose request timed out.
	struct TORRENT_EXPORT unwanted_block_alert: peer_alert
	{
		// internal
		unwanted_block_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(unwanted_block_alert);

		virtual std::string message() const;

		int block_index;
		int piece_index;
	};

	// The ``storage_moved_alert`` is generated when all the disk IO has completed and the
	// files have been moved, as an effect of a call to ``torrent_handle::move_storage``. This
	// is useful to synchronize with the actual disk. The ``path`` member is the new path of
	// the storage.
	struct TORRENT_EXPORT storage_moved_alert: torrent_alert
	{
		// internal
		storage_moved_alert(torrent_handle const& h, std::string const& p)
			: torrent_alert(h)
			, path(p)
		{}
	
		TORRENT_DEFINE_ALERT(storage_moved_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " moved storage to: "
				+ path;
		}

		std::string path;
	};

	// The ``storage_moved_failed_alert`` is generated when an attempt to move the storage,
	// via torrent_handle::move_storage(), fails.
	struct TORRENT_EXPORT storage_moved_failed_alert: torrent_alert
	{
		// internal
		storage_moved_failed_alert(torrent_handle const& h, error_code const& e)
			: torrent_alert(h)
			, error(e)
		{}
	
		TORRENT_DEFINE_ALERT(storage_moved_failed_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " storage move failed: "
				+ convert_from_native(error.message());
		}

		error_code error;
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
	struct TORRENT_EXPORT torrent_deleted_alert: torrent_alert
	{
		// internal
		torrent_deleted_alert(torrent_handle const& h, sha1_hash const& ih)
			: torrent_alert(h)
		{ info_hash = ih; }
	
		TORRENT_DEFINE_ALERT(torrent_deleted_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " deleted"; }
		virtual bool discardable() const { return false; }

		sha1_hash info_hash;
	};

	// This alert is generated when a request to delete the files of a torrent fails.
	// Just removing a torrent from the session cannot fail
	struct TORRENT_EXPORT torrent_delete_failed_alert: torrent_alert
	{
		// internal
		torrent_delete_failed_alert(torrent_handle const& h, error_code const& e, sha1_hash const& ih)
			: torrent_alert(h)
			, error(e)
			, info_hash(ih)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = convert_from_native(error.message());
#endif
		}
	
		TORRENT_DEFINE_ALERT(torrent_delete_failed_alert);

		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " torrent deletion failed: "
				+convert_from_native(error.message());
		}
		virtual bool discardable() const { return false; }

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
	struct TORRENT_EXPORT save_resume_data_alert: torrent_alert
	{
		// internal
		save_resume_data_alert(boost::shared_ptr<entry> const& rd
			, torrent_handle const& h)
			: torrent_alert(h)
			, resume_data(rd)
		{}
	
		TORRENT_DEFINE_ALERT(save_resume_data_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " resume data generated"; }
		virtual bool discardable() const { return false; }

		// points to the resume data.
		boost::shared_ptr<entry> resume_data;
	};

	// This alert is generated instead of ``save_resume_data_alert`` if there was an error
	// generating the resume data. ``error`` describes what went wrong.
	struct TORRENT_EXPORT save_resume_data_failed_alert: torrent_alert
	{
		// internal
		save_resume_data_failed_alert(torrent_handle const& h
			, error_code const& e)
			: torrent_alert(h)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = convert_from_native(error.message());
#endif
		}
	
		TORRENT_DEFINE_ALERT(save_resume_data_failed_alert);

		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " resume data was not generated: "
				+ convert_from_native(error.message());
		}
		virtual bool discardable() const { return false; }

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This alert is generated as a response to a ``torrent_handle::pause`` request. It is
	// generated once all disk IO is complete and the files in the torrent have been closed.
	// This is useful for synchronizing with the disk.
	struct TORRENT_EXPORT torrent_paused_alert: torrent_alert
	{
		// internal
		torrent_paused_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}
	
		TORRENT_DEFINE_ALERT(torrent_paused_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " paused"; }
	};

	// This alert is generated as a response to a torrent_handle::resume() request. It is
	// generated when a torrent goes from a paused state to an active state.
	struct TORRENT_EXPORT torrent_resumed_alert: torrent_alert
	{
		// internal
		torrent_resumed_alert(torrent_handle const& h)
			: torrent_alert(h) {}

		TORRENT_DEFINE_ALERT(torrent_resumed_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " resumed"; }
	};

	// This alert is posted when a torrent completes checking. i.e. when it transitions
	// out of the ``checking files`` state into a state where it is ready to start downloading
	struct TORRENT_EXPORT torrent_checked_alert: torrent_alert
	{
		// internal
		torrent_checked_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(torrent_checked_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " checked"; }
	};

	// This alert is generated when a HTTP seed name lookup fails.
	struct TORRENT_EXPORT url_seed_alert: torrent_alert
	{
		// internal
		url_seed_alert(
			torrent_handle const& h
			, std::string const& u
			, error_code const& e)
			: torrent_alert(h)
			, url(u)
			, msg(convert_from_native(e.message()))
		{}
		url_seed_alert(
			torrent_handle const& h
			, std::string const& u
			, std::string const& m)
			: torrent_alert(h)
			, url(u)
			, msg(m)
		{}

		TORRENT_DEFINE_ALERT(url_seed_alert);

		const static int static_category = alert::peer_notification | alert::error_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " url seed ("
				+ url + ") failed: " + msg;
		}

		// the HTTP seed that failed
		std::string url;

		// the error message, potentially from the server
		std::string msg;
	};

	// If the storage fails to read or write files that it needs access to, this alert is
	// generated and the torrent is paused.
	struct TORRENT_EXPORT file_error_alert: torrent_alert
	{
		// internal
		file_error_alert(
			std::string const& f
			, torrent_handle const& h
			, error_code const& e)
			: torrent_alert(h)
			, file(f)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = convert_from_native(error.message());
#endif
		}

		TORRENT_DEFINE_ALERT(file_error_alert);

		const static int static_category = alert::status_notification
			| alert::error_notification
			| alert::storage_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " file (" + file + ") error: "
				+ convert_from_native(error.message());
		}
		
		// the path to the file that was accessed when the error occurred.
		std::string file;

		// the error code describing the error.
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	// This alert is generated when the metadata has been completely received and the info-hash
	// failed to match it. i.e. the metadata that was received was corrupt. libtorrent will
	// automatically retry to fetch it in this case. This is only relevant when running a
	// torrent-less download, with the metadata extension provided by libtorrent.
	struct TORRENT_EXPORT metadata_failed_alert: torrent_alert
	{
		// internal
		metadata_failed_alert(const torrent_handle& h, error_code e)
			: torrent_alert(h)
			, error(e)
		{}

		TORRENT_DEFINE_ALERT(metadata_failed_alert);

		const static int static_category = alert::error_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " invalid metadata received"; }

		// the error that occurred
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
	//		boost::intrusive_ptr<torrent_info const> ti = h.torrent_file();
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
	struct TORRENT_EXPORT metadata_received_alert: torrent_alert
	{
		// internal
		metadata_received_alert(
			const torrent_handle& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(metadata_received_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " metadata successfully received"; }
	};

	// This alert is posted when there is an error on the UDP socket. The
	// UDP socket is used for all uTP, DHT and UDP tracker traffic. It's
	// global to the session.
	struct TORRENT_EXPORT udp_error_alert: alert
	{
		// internal
		udp_error_alert(
			udp::endpoint const& ep
			, error_code const& ec)
			: endpoint(ep)
			, error(ec)
		{}

		TORRENT_DEFINE_ALERT(udp_error_alert);

		const static int static_category = alert::error_notification;
		virtual std::string message() const
		{
			error_code ec;
			return "UDP error: " + convert_from_native(error.message()) + " from: " + endpoint.address().to_string(ec);
		}

		// the source address associated with the error (if any)
		udp::endpoint endpoint;

		// the error code describing the error
		error_code error;
	};

	// Whenever libtorrent learns about the machines external IP, this alert is
	// generated. The external IP address can be acquired from the tracker (if it
	// supports that) or from peers that supports the extension protocol.
	// The address can be accessed through the ``external_address`` member.
	struct TORRENT_EXPORT external_ip_alert: alert
	{
		// internal
		external_ip_alert(address const& ip)
			: external_address(ip)
		{}

		TORRENT_DEFINE_ALERT(external_ip_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{
			error_code ec;
			return "external IP received: " + external_address.to_string(ec);
		}

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
	struct TORRENT_EXPORT listen_failed_alert: alert
	{
		enum socket_type_t { tcp, tcp_ssl, udp, i2p, socks5 };

		// internal
		listen_failed_alert(
			tcp::endpoint const& ep
			, int op
			, error_code const& ec
			, socket_type_t t)
			: endpoint(ep)
			, error(ec)
			, operation(op)
			, sock_type(t)
		{}

		TORRENT_DEFINE_ALERT(listen_failed_alert);

		const static int static_category = alert::status_notification | alert::error_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		// the endpoint libtorrent attempted to listen on
		tcp::endpoint endpoint;

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
	};

	// This alert is posted when the listen port succeeds to be opened on a
	// particular interface. ``endpoint`` is the endpoint that successfully
	// was opened for listening.
	struct TORRENT_EXPORT listen_succeeded_alert: alert
	{
		enum socket_type_t { tcp, tcp_ssl, udp };

		// internal
		listen_succeeded_alert(tcp::endpoint const& ep, socket_type_t t)
			: endpoint(ep)
			, sock_type(t)
		{}

		TORRENT_DEFINE_ALERT(listen_succeeded_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

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
	struct TORRENT_EXPORT portmap_error_alert: alert
	{
		// internal
		portmap_error_alert(int i, int t, error_code const& e)
			:  mapping(i), map_type(t), error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = convert_from_native(error.message());
#endif
		}

		TORRENT_DEFINE_ALERT(portmap_error_alert);

		const static int static_category = alert::port_mapping_notification
			| alert::error_notification;
		virtual std::string message() const;

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
	struct TORRENT_EXPORT portmap_alert: alert
	{
		// internal
		portmap_alert(int i, int port, int t)
			: mapping(i), external_port(port), map_type(t)
		{}

		TORRENT_DEFINE_ALERT(portmap_alert);

		const static int static_category = alert::port_mapping_notification;
		virtual std::string message() const;

		// refers to the mapping index of the port map that failed, i.e.
		// the index returned from add_mapping().
		int mapping;

		// the external port allocated for the mapping.
		int external_port;

		// 0 for NAT-PMP and 1 for UPnP.
		int map_type;
	};

	// This alert is generated to log informational events related to either
	// UPnP or NAT-PMP. They contain a log line and the type (0 = NAT-PMP
	// and 1 = UPnP). Displaying these messages to an end user is only useful
	// for debugging the UPnP or NAT-PMP implementation.
	struct TORRENT_EXPORT portmap_log_alert: alert
	{
		// internal
		portmap_log_alert(int t, std::string const& m)
			: map_type(t), msg(m)
		{}

		TORRENT_DEFINE_ALERT(portmap_log_alert);

		const static int static_category = alert::port_mapping_notification;
		virtual std::string message() const;

		int map_type;
		std::string msg;
	};

	// This alert is generated when a fastresume file has been passed to add_torrent() but the
	// files on disk did not match the fastresume file. The error_code explains the reason why the
	// resume file was rejected.
	struct TORRENT_EXPORT fastresume_rejected_alert: torrent_alert
	{
		// internal
		fastresume_rejected_alert(torrent_handle const& h
			, error_code const& e)
			: torrent_alert(h)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = convert_from_native(error.message());
#endif
		}

		TORRENT_DEFINE_ALERT(fastresume_rejected_alert);

		const static int static_category = alert::status_notification
			| alert::error_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " fast resume rejected: " + convert_from_native(error.message()); }

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
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
	struct TORRENT_EXPORT peer_blocked_alert: torrent_alert
	{
		// internal
		peer_blocked_alert(torrent_handle const& h, address const& i
			, int r)
			: torrent_alert(h)
			, ip(i)
			, reason(r)
		{}
		
		TORRENT_DEFINE_ALERT(peer_blocked_alert);

		const static int static_category = alert::ip_block_notification;
		virtual std::string message() const;

		// the address that was blocked.
		address ip;

		enum reason_t
		{
			ip_filter,
			port_filter,
			i2p_mixed,
			privileged_ports,
			utp_disabled,
			tcp_disabled
		};

		int reason;
	};

	// This alert is generated when a DHT node announces to an info-hash on our
	// DHT node. It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_announce_alert: alert
	{
		// internal
		dht_announce_alert(address const& i, int p
			, sha1_hash const& ih)
			: ip(i)
			, port(p)
			, info_hash(ih)
		{}
		
		TORRENT_DEFINE_ALERT(dht_announce_alert);

		const static int static_category = alert::dht_notification;
		virtual std::string message() const;

		address ip;
		int port;
		sha1_hash info_hash;
	};

	// This alert is generated when a DHT node sends a ``get_peers`` message to
	// our DHT node. It belongs to the ``dht_notification`` category.
	struct TORRENT_EXPORT dht_get_peers_alert: alert
	{
		// internal
		dht_get_peers_alert(sha1_hash const& ih)
			: info_hash(ih)
		{}

		TORRENT_DEFINE_ALERT(dht_get_peers_alert);

		const static int static_category = alert::dht_notification;
		virtual std::string message() const;

		sha1_hash info_hash;
	};

	// This alert is posted approximately once every second, and it contains
	// byte counters of most statistics that's tracked for torrents. Each active
	// torrent posts these alerts regularly.
	struct TORRENT_EXPORT stats_alert: torrent_alert
	{
		// internal
		stats_alert(torrent_handle const& h, int interval
			, stat const& s);

		TORRENT_DEFINE_ALERT(stats_alert);

		const static int static_category = alert::stats_notification;
		virtual std::string message() const;

		enum stats_channel
		{
			upload_payload,
			upload_protocol,
			download_payload,
			download_protocol,
#ifndef TORRENT_DISABLE_FULL_STATS
			upload_ip_protocol,
			upload_dht_protocol,
			upload_tracker_protocol,
			download_ip_protocol,
			download_dht_protocol,
			download_tracker_protocol,
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
	struct TORRENT_EXPORT cache_flushed_alert: torrent_alert
	{
		// internal
		cache_flushed_alert(torrent_handle const& h);

		TORRENT_DEFINE_ALERT(cache_flushed_alert);

		const static int static_category = alert::storage_notification;
	};

	// This alert is posted when a bittorrent feature is blocked because of the
	// anonymous mode. For instance, if the tracker proxy is not set up, no
	// trackers will be used, because trackers can only be used through proxies
	// when in anonymous mode.
	struct TORRENT_EXPORT anonymous_mode_alert: torrent_alert
	{
		// internal
		anonymous_mode_alert(torrent_handle const& h
			, int k, std::string const& s)
			: torrent_alert(h)
			, kind(k)
			, str(s)
		{}

		TORRENT_DEFINE_ALERT(anonymous_mode_alert);

		const static int static_category = alert::error_notification;
		virtual std::string message() const;

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
	struct TORRENT_EXPORT lsd_peer_alert: peer_alert
	{
		// internal
		lsd_peer_alert(torrent_handle const& h
			, tcp::endpoint const& i)
			: peer_alert(h, i, peer_id(0))
		{}

		TORRENT_DEFINE_ALERT(lsd_peer_alert);

		const static int static_category = alert::peer_notification;
		virtual std::string message() const;
	};

	// This alert is posted whenever a tracker responds with a ``trackerid``.
	// The tracker ID is like a cookie. The libtorrent will store the tracker ID
	// for this tracker and repeat it in subsequent announces.
	struct TORRENT_EXPORT trackerid_alert: tracker_alert
	{
		// internal
		trackerid_alert(torrent_handle const& h
			, std::string const& u
                        , const std::string& id)
			: tracker_alert(h, u)
			, trackerid(id)
		{}

		TORRENT_DEFINE_ALERT(trackerid_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const;

		// The tracker ID returned by the tracker
		std::string trackerid;
	};

	// This alert is posted when the initial DHT bootstrap is done.
	struct TORRENT_EXPORT dht_bootstrap_alert: alert
	{
		// internal
		dht_bootstrap_alert() {}
		
		TORRENT_DEFINE_ALERT(dht_bootstrap_alert);

		const static int static_category = alert::dht_notification;
		virtual std::string message() const;
	};

	// This alert is posted on RSS feed events such as start of RSS feed updates,
	// successful completed updates and errors during updates.
	// 
	// This alert is only posted if the ``rss_notifications`` category is enabled
	// in the alert_mask.
	struct TORRENT_EXPORT rss_alert: alert
	{
		// internal
		rss_alert(feed_handle h, std::string const& u, int s, error_code const& ec)
			: handle(h), url(u), state(s), error(ec)
		{}

		TORRENT_DEFINE_ALERT(rss_alert);

		const static int static_category = alert::rss_notification;
		virtual std::string message() const;

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

	// This is posted whenever a torrent is transitioned into the error state.
	struct TORRENT_EXPORT torrent_error_alert: torrent_alert
	{
		// internal
		torrent_error_alert(torrent_handle const& h
			, error_code const& e)
			: torrent_alert(h)
			, error(e)
		{}

		TORRENT_DEFINE_ALERT(torrent_error_alert);

		const static int static_category = alert::error_notification | alert::status_notification;
		virtual std::string message() const;

		// specifies which error the torrent encountered.
		error_code error;
	};

	// This is always posted for SSL torrents. This is a reminder to the client that
	// the torrent won't work unless torrent_handle::set_ssl_certificate() is called with
	// a valid certificate. Valid certificates MUST be signed by the SSL certificate
	// in the .torrent file.
	struct TORRENT_EXPORT torrent_need_cert_alert: torrent_alert
	{
		// internal
		torrent_need_cert_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(torrent_need_cert_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		error_code error;
	};

	// The incoming connection alert is posted every time we successfully accept
	// an incoming connection, through any mean. The most straigh-forward ways
	// of accepting incoming connections are through the TCP listen socket and
	// the UDP listen socket for uTP sockets. However, connections may also be
	// accepted ofer a Socks5 or i2p listen socket, or via a torrent specific
	// listen socket for SSL torrents.
	struct TORRENT_EXPORT incoming_connection_alert: alert
	{
		// internal
		incoming_connection_alert(int t, tcp::endpoint const& i)
			: socket_type(t)
			, ip(i)
		{}

		TORRENT_DEFINE_ALERT(incoming_connection_alert);

		const static int static_category = alert::peer_notification;
		virtual std::string message() const;

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
	struct TORRENT_EXPORT add_torrent_alert : torrent_alert
	{
		// internal
		add_torrent_alert(torrent_handle h, add_torrent_params const& p, error_code ec)
			: torrent_alert(h)
			, params(p)
			, error(ec)
		{}

		TORRENT_DEFINE_ALERT(add_torrent_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		// a copy of the parameters used when adding the torrent, it can be used
		// to identify which invocation to ``async_add_torrent()`` caused this alert.
		add_torrent_params params;

		// set to the error, if one occurred while adding the torrent.
		error_code error;
	};

	// This alert is only posted when requested by the user, by calling session::post_torrent_updates()
	// on the session. It contains the torrent status of all torrents that changed
	// since last time this message was posted. Its category is ``status_notification``, but
	// it's not subject to filtering, since it's only manually posted anyway.
	struct TORRENT_EXPORT state_update_alert : alert
	{
		TORRENT_DEFINE_ALERT(state_update_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		// contains the torrent status of all torrents that changed since last time
		// this message was posted. Note that you can map a torrent status to a specific torrent
		// via its ``handle`` member. The receiving end is suggested to have all torrents sorted
		// by the torrent_handle or hashed by it, for efficient updates.
		std::vector<torrent_status> status;
	};

	// When a torrent changes its info-hash, this alert is posted. This only happens in very
	// specific cases. For instance, when a torrent is downloaded from a URL, the true info
	// hash is not known immediately. First the .torrent file must be downloaded and parsed.
	// 
	// Once this download completes, the ``torrent_update_alert`` is posted to notify the client
	// of the info-hash changing.
	struct TORRENT_EXPORT torrent_update_alert : torrent_alert
	{
		// internal
		torrent_update_alert(torrent_handle h, sha1_hash const& old_hash, sha1_hash const& new_hash)
			: torrent_alert(h)
			, old_ih(old_hash)
			, new_ih(new_hash)
		{}

		TORRENT_DEFINE_ALERT(torrent_update_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		// ``old_ih`` and ``new_ih`` are the previous and new info-hash for the torrent, respectively.
		sha1_hash old_ih;
		sha1_hash new_ih;
	};

	// This alert is posted every time a new RSS item (i.e. torrent) is received
	// from an RSS feed.
	// 
	// It is only posted if the ``rss_notifications`` category is enabled in the
	// alert_mask.
	struct TORRENT_EXPORT rss_item_alert : alert
	{
		// internal
		rss_item_alert(feed_handle h, feed_item const& item)
			: handle(h)
			, item(item)
		{}

		TORRENT_DEFINE_ALERT(rss_item_alert);

		const static int static_category = alert::rss_notification;
		virtual std::string message() const;

		feed_handle handle;
		feed_item item;
	};

	// posted when something fails in the DHT. This is not necessarily a fatal
	// error, but it could prevent proper operation
	struct TORRENT_EXPORT dht_error_alert: alert
	{
		// internal
		dht_error_alert(int op, error_code const& ec)
			: error(ec), operation(op_t(op)) {}
		
		TORRENT_DEFINE_ALERT(dht_error_alert);

		const static int static_category = alert::error_notification
			| alert::dht_notification;
		virtual std::string message() const;

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
	struct TORRENT_EXPORT dht_immutable_item_alert: alert
	{
		dht_immutable_item_alert(sha1_hash const& t, entry const& i)
			: target(t), item(i) {}
		
		TORRENT_DEFINE_ALERT(dht_immutable_item_alert);

		const static int static_category = alert::error_notification
			| alert::dht_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

		// the target hash of the immutable item. This must
		// match the sha-1 hash of the bencoded form of ``item``.
		sha1_hash target;

		// the data for this item
		entry item;
	};

	// this alert is posted as a response to a call to session::get_item(),
	// specifically the overload for looking up mutable items in the DHT.
	struct TORRENT_EXPORT dht_mutable_item_alert: alert
	{
		dht_mutable_item_alert(boost::array<char, 32> k
			, boost::array<char, 64> sig
			, boost::uint64_t sequence
			, std::string const& s
			, entry const& i)
			: key(k), signature(sig), seq(sequence), salt(s), item(i) {}
		
		TORRENT_DEFINE_ALERT(dht_mutable_item_alert);

		const static int static_category = alert::error_notification
			| alert::dht_notification;
		virtual std::string message() const;
		virtual bool discardable() const { return false; }

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

		// the salf, if any, used to lookup and store this item. If no
		// salt was used, this is an empty string
		std::string salt;

		// the data for this item
		entry item;
	};

	// this is posted when a DHT put operation completes. This is useful if the
	// client is waiting for a put to complete before shutting down for instance.
	struct TORRENT_EXPORT dht_put_alert: alert
	{
		// internal
		dht_put_alert(sha1_hash const& t)
			: target(t)
			, seq(0)
		{}
		dht_put_alert(boost::array<char, 32> key
			, boost::array<char, 64> sig
			, std::string s
			, boost::uint64_t sequence_number)
			: target(0)
			, public_key(key)
			, signature(sig)
			, salt(s)
			, seq(sequence_number)
		{}

		TORRENT_DEFINE_ALERT(dht_put_alert);

		const static int static_category = alert::dht_notification;
		virtual std::string message() const;

		// the target hash the item was stored under if this was an *immutable*
		// item.
		sha1_hash target;

		// if a mutable item was stored, these are the public key, signature,
		// salt and sequence number the item was stored under.
		boost::array<char, 32> public_key;
		boost::array<char, 64> signature;
		std::string salt;
		boost::uint64_t seq;
	};

	// this alert is used to report errors in the i2p SAM connection
	struct TORRENT_EXPORT i2p_alert : alert
	{
		i2p_alert(error_code const& ec) : error(ec) {}

		TORRENT_DEFINE_ALERT(i2p_alert);

		const static int static_category = alert::error_notification;
		virtual std::string message() const;

		// the error that occurred in the i2p SAM connection
		error_code error;
	};

#undef TORRENT_DEFINE_ALERT

}


#endif
