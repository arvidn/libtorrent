/*

Copyright (c) 2003, Arvid Norberg
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
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/socket_io.hpp"

namespace libtorrent
{
	struct TORRENT_EXPORT torrent_alert: alert
	{
		torrent_alert(torrent_handle const& h)
			: handle(h)
		{}
		
		virtual std::string message() const
		{
			if (!handle.is_valid()) return " - ";
			if (handle.name().empty())
			{
				char msg[41];
				to_hex((char const*)&handle.info_hash()[0], 20, msg);
				return msg;
			}
			return handle.name();
		}

		torrent_handle handle;
	};

	struct TORRENT_EXPORT peer_alert: torrent_alert
	{
		peer_alert(torrent_handle const& h, tcp::endpoint const& ip_
			, peer_id const& pid_)
			: torrent_alert(h)
			, ip(ip_)
			, pid(pid_)
		{}

		const static int static_category = alert::peer_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			error_code ec;
			return torrent_alert::message() + " peer (" + ip.address().to_string(ec)
				+ ", " + identify_client(pid) + ")";
		}

		tcp::endpoint ip;
		peer_id pid;
	};

	struct TORRENT_EXPORT tracker_alert: torrent_alert
	{
		tracker_alert(torrent_handle const& h
			, std::string const& url_)
			: torrent_alert(h)
			, url(url_)
		{}

		const static int static_category = alert::tracker_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " (" + url + ")";
		}

		std::string url;
	};

#define TORRENT_DEFINE_ALERT(name) \
	const static int alert_type = __LINE__; \
	virtual int type() const { return alert_type; } \
	virtual std::auto_ptr<alert> clone() const \
	{ return std::auto_ptr<alert>(new name(*this)); } \
	virtual int category() const { return static_category; } \
	virtual char const* what() const { return #name; }

	struct TORRENT_EXPORT read_piece_alert: torrent_alert
	{
		read_piece_alert(torrent_handle const& h
			, int p, boost::shared_array<char> d, int s)
			: torrent_alert(h)
			, buffer(d)
			, piece(p)
			, size(s)
		{}

		TORRENT_DEFINE_ALERT(read_piece_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{
			char msg[200];
			snprintf(msg, sizeof(msg), "%s: piece %s %u", torrent_alert::message().c_str()
				, buffer ? "successful" : "failed", piece);
			return msg;
		}

		boost::shared_array<char> buffer;
		int piece;
		int size;
	};

	struct TORRENT_EXPORT file_completed_alert: torrent_alert
	{
		file_completed_alert(torrent_handle const& h
			, int index_)
			: torrent_alert(h)
			, index(index_)
		{}

		TORRENT_DEFINE_ALERT(file_completed_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const
		{
			char msg[200 + TORRENT_MAX_PATH];
			snprintf(msg, sizeof(msg), "%s: file %d finished downloading"
				, torrent_alert::message().c_str(), index);
			return msg;
		}

		int index;
	};

	struct TORRENT_EXPORT file_renamed_alert: torrent_alert
	{
		file_renamed_alert(torrent_handle const& h
			, std::string const& name_
			, int index_)
			: torrent_alert(h)
			, name(name_)
			, index(index_)
		{}

		TORRENT_DEFINE_ALERT(file_renamed_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{
			char msg[200 + TORRENT_MAX_PATH * 2];
			snprintf(msg, sizeof(msg), "%s: file %d renamed to %s", torrent_alert::message().c_str()
				, index, name.c_str());
			return msg;
		}

		std::string name;
		int index;
	};

	struct TORRENT_EXPORT file_rename_failed_alert: torrent_alert
	{
		file_rename_failed_alert(torrent_handle const& h
			, int index_
			, error_code ec_)
			: torrent_alert(h)
			, index(index_)
			, error(ec_)
		{}

		TORRENT_DEFINE_ALERT(file_rename_failed_alert);

		const static int static_category = alert::storage_notification;

		virtual std::string message() const
		{
			char ret[200 + TORRENT_MAX_PATH * 2];
			snprintf(ret, sizeof(ret), "%s: failed to rename file %d: %s"
				, torrent_alert::message().c_str(), index, error.message().c_str());
			return ret;
		}

		int index;
		error_code error;
	};

	struct TORRENT_EXPORT performance_alert: torrent_alert
	{
		enum performance_warning_t
		{
			outstanding_disk_buffer_limit_reached,
			outstanding_request_limit_reached,
			upload_limit_too_low,
			download_limit_too_low,
			send_buffer_watermark_too_low
		};

		performance_alert(torrent_handle const& h
			, performance_warning_t w)
			: torrent_alert(h)
			, warning_code(w)
		{}

		TORRENT_DEFINE_ALERT(performance_alert);

		const static int static_category = alert::performance_warning;

		virtual std::string message() const
		{
			static char const* warning_str[] =
			{
				"max outstanding disk writes reached",
				"max outstanding piece requests reached",
				"upload limit too low (download rate will suffer)",
				"download limit too low (upload rate will suffer)",
				"send buffer watermark too low (upload rate will suffer)"
			};

			return torrent_alert::message() + ": performance warning: "
				+ warning_str[warning_code];
		}

		performance_warning_t warning_code;
	};

	struct TORRENT_EXPORT state_changed_alert: torrent_alert
	{
		state_changed_alert(torrent_handle const& h
			, torrent_status::state_t state_
			, torrent_status::state_t prev_state_)
			: torrent_alert(h)
			, state(state_)
			, prev_state(prev_state_)
		{}

		TORRENT_DEFINE_ALERT(state_changed_alert);

		const static int static_category = alert::status_notification;

		virtual std::string message() const
		{
			static char const* state_str[] =
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating"
				, "checking (r)"};

			return torrent_alert::message() + ": state changed to: "
				+ state_str[state];
		}

		torrent_status::state_t state;
		torrent_status::state_t prev_state;
	};

	struct TORRENT_EXPORT tracker_error_alert: tracker_alert
	{
		tracker_error_alert(torrent_handle const& h
			, int times
			, int status
			, std::string const& url_
			, error_code const& e)
			: tracker_alert(h, url_)
			, times_in_row(times)
			, status_code(status)
			, msg(e.message())
		{
			TORRENT_ASSERT(!url.empty());
		}

		tracker_error_alert(torrent_handle const& h
			, int times
			, int status
			, std::string const& url_
			, std::string const& msg_)
			: tracker_alert(h, url_)
			, times_in_row(times)
			, status_code(status)
			, msg(msg_)
		{
			TORRENT_ASSERT(!url.empty());
		}

		TORRENT_DEFINE_ALERT(tracker_error_alert);

		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s (%d) %s (%d)"
				, torrent_alert::message().c_str(), status_code
				, msg.c_str(), times_in_row);
			return ret;
		}

		int times_in_row;
		int status_code;
		std::string msg;
	};

	struct TORRENT_EXPORT tracker_warning_alert: tracker_alert
	{
		tracker_warning_alert(torrent_handle const& h
			, std::string const& url_
			, std::string const& msg_)
			: tracker_alert(h, url_)
			, msg(msg_)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(tracker_warning_alert);

		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const
		{
			return tracker_alert::message() + " warning: " + msg;
		}

		std::string msg;
	};

	struct TORRENT_EXPORT scrape_reply_alert: tracker_alert
	{
		scrape_reply_alert(torrent_handle const& h
			, int incomplete_
			, int complete_
			, std::string const& url_)
			: tracker_alert(h, url_)
			, incomplete(incomplete_)
			, complete(complete_)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(scrape_reply_alert);

		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s scrape reply: %u %u"
				, torrent_alert::message().c_str(), incomplete, complete);
			return ret;
		}

		int incomplete;
		int complete;
	};

	struct TORRENT_EXPORT scrape_failed_alert: tracker_alert
	{
		scrape_failed_alert(torrent_handle const& h
			, std::string const& url_
			, error_code const& e)
			: tracker_alert(h, url_)
			, msg(e.message())
		{ TORRENT_ASSERT(!url.empty()); }

		scrape_failed_alert(torrent_handle const& h
			, std::string const& url_
			, std::string const& msg_)
			: tracker_alert(h, url_)
			, msg(msg_)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(scrape_failed_alert);

		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual std::string message() const
		{ return tracker_alert::message() + " scrape failed: " + msg; }

		std::string msg;
	};

	struct TORRENT_EXPORT tracker_reply_alert: tracker_alert
	{
		tracker_reply_alert(torrent_handle const& h
			, int np
			, std::string const& url_)
			: tracker_alert(h, url_)
			, num_peers(np)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(tracker_reply_alert);

		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s received peers: %u"
				, torrent_alert::message().c_str(), num_peers);
			return ret;
		}

		int num_peers;
	};

	struct TORRENT_EXPORT dht_reply_alert: tracker_alert
	{
		dht_reply_alert(torrent_handle const& h
			, int np)
			: tracker_alert(h, "")
			, num_peers(np)
		{}

		TORRENT_DEFINE_ALERT(dht_reply_alert);

		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s received DHT peers: %u"
				, torrent_alert::message().c_str(), num_peers);
			return ret;
		}

		int num_peers;
	};

	struct TORRENT_EXPORT tracker_announce_alert: tracker_alert
	{
		tracker_announce_alert(torrent_handle const& h
			, std::string const& url_, int event_)
			: tracker_alert(h, url_)
			, event(event_)
		{ TORRENT_ASSERT(!url.empty()); }

		TORRENT_DEFINE_ALERT(tracker_announce_alert);

		virtual std::string message() const
		{
			const static char* event_str[] = {"none", "completed", "started", "stopped"};
			return tracker_alert::message() + " sending announce (" + event_str[event] + ")";
		}

		int event;
	};
	
	struct TORRENT_EXPORT hash_failed_alert: torrent_alert
	{
		hash_failed_alert(
			torrent_handle const& h
			, int index)
			: torrent_alert(h)
			, piece_index(index)
		{ TORRENT_ASSERT(index >= 0);}

		TORRENT_DEFINE_ALERT(hash_failed_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s hash for piece %u failed"
				, torrent_alert::message().c_str(), piece_index);
			return ret;
		}

		int piece_index;
	};

	struct TORRENT_EXPORT peer_ban_alert: peer_alert
	{
		peer_ban_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
		{}

		TORRENT_DEFINE_ALERT(peer_ban_alert);

		virtual std::string message() const
		{ return peer_alert::message() + " banned peer"; }
	};

	struct TORRENT_EXPORT peer_unsnubbed_alert: peer_alert
	{
		peer_unsnubbed_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
		{}

		TORRENT_DEFINE_ALERT(peer_unsnubbed_alert);

		virtual std::string message() const
		{ return peer_alert::message() + " peer unsnubbed"; }
	};

	struct TORRENT_EXPORT peer_snubbed_alert: peer_alert
	{
		peer_snubbed_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
		{}

		TORRENT_DEFINE_ALERT(peer_snubbed_alert);

		virtual std::string message() const
		{ return peer_alert::message() + " peer snubbed"; }
	};

	struct TORRENT_EXPORT peer_error_alert: peer_alert
	{
		peer_error_alert(torrent_handle const& h, tcp::endpoint const& ip
			, peer_id const& pid, error_code const& e)
			: peer_alert(h, ip, pid)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		TORRENT_DEFINE_ALERT(peer_error_alert);

		const static int static_category = alert::peer_notification;
		virtual std::string message() const
		{
			error_code ec;
			return peer_alert::message() + " peer error: " + error.message();
		}

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT peer_connect_alert: peer_alert
	{
		peer_connect_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
		{}

		TORRENT_DEFINE_ALERT(peer_connect_alert);

		const static int static_category = alert::debug_notification;
		virtual std::string message() const
		{ return peer_alert::message() + " connecting to peer"; }
	};

	struct TORRENT_EXPORT peer_disconnected_alert: peer_alert
	{
		peer_disconnected_alert(torrent_handle const& h, tcp::endpoint const& ip
			, peer_id const& pid, error_code const& e)
			: peer_alert(h, ip, pid)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		TORRENT_DEFINE_ALERT(peer_disconnected_alert);

		const static int static_category = alert::debug_notification;
		virtual std::string message() const
		{ return peer_alert::message() + " disconnecting: " + error.message(); }

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT invalid_request_alert: peer_alert
	{
		invalid_request_alert(torrent_handle const& h, tcp::endpoint const& ip
			, peer_id const& pid, peer_request const& r)
			: peer_alert(h, ip, pid)
			, request(r)
		{}

		TORRENT_DEFINE_ALERT(invalid_request_alert);

		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s peer sent an invalid piece request (piece: %u start: %u len: %u)"
				, torrent_alert::message().c_str(), request.piece, request.start, request.length);
			return ret;
		}

		peer_request request;
	};

	struct TORRENT_EXPORT torrent_finished_alert: torrent_alert
	{
		torrent_finished_alert(
			const torrent_handle& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(torrent_finished_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " torrent finished downloading"; }
	};

	struct TORRENT_EXPORT piece_finished_alert: torrent_alert
	{
		piece_finished_alert(
			const torrent_handle& h
			, int piece_num)
			: torrent_alert(h)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(piece_index >= 0);}

		TORRENT_DEFINE_ALERT(piece_finished_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s piece: %u finished downloading"
				, torrent_alert::message().c_str(), piece_index);
			return ret;
		}

		int piece_index;
	};

	struct TORRENT_EXPORT request_dropped_alert: peer_alert
	{
		request_dropped_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(request_dropped_alert);

		const static int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s peer dropped block ( piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}

		int block_index;
		int piece_index;
	};

	struct TORRENT_EXPORT block_timeout_alert: peer_alert
	{
		block_timeout_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(block_timeout_alert);

		const static int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s peer timed out request ( piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}

		int block_index;
		int piece_index;
	};

	struct TORRENT_EXPORT block_finished_alert: peer_alert
	{
		block_finished_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(block_finished_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s block finished downloading (piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}

		int block_index;
		int piece_index;
	};

	struct TORRENT_EXPORT block_downloading_alert: peer_alert
	{
		block_downloading_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, char const* speedmsg, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
			, peer_speedmsg(speedmsg)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0); }

		TORRENT_DEFINE_ALERT(block_downloading_alert);

		const static int static_category = alert::progress_notification;
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s requested block (piece: %u block: %u) %s"
				, torrent_alert::message().c_str(), piece_index, block_index, peer_speedmsg);
			return ret;
		}

		char const* peer_speedmsg;
		int block_index;
		int piece_index;
	};

	struct TORRENT_EXPORT unwanted_block_alert: peer_alert
	{
		unwanted_block_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		TORRENT_DEFINE_ALERT(unwanted_block_alert);

		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s received block not in download queue (piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}

		int block_index;
		int piece_index;
	};

	struct TORRENT_EXPORT storage_moved_alert: torrent_alert
	{
		storage_moved_alert(torrent_handle const& h, std::string const& path_)
			: torrent_alert(h)
			, path(path_)
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

	struct TORRENT_EXPORT storage_moved_failed_alert: torrent_alert
	{
		storage_moved_failed_alert(torrent_handle const& h, error_code const& ec_)
			: torrent_alert(h)
			, error(ec_)
		{}
	
		TORRENT_DEFINE_ALERT(storage_moved_failed_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " storage move failed: "
				+ error.message();
		}

		error_code error;
	};

	struct TORRENT_EXPORT torrent_deleted_alert: torrent_alert
	{
		torrent_deleted_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}
	
		TORRENT_DEFINE_ALERT(torrent_deleted_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " deleted"; }
	};

	struct TORRENT_EXPORT torrent_delete_failed_alert: torrent_alert
	{
		torrent_delete_failed_alert(torrent_handle const& h, error_code const& e)
			: torrent_alert(h)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}
	
		TORRENT_DEFINE_ALERT(torrent_delete_failed_alert);

		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " torrent deletion failed: "
				+ error.message();
		}

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT save_resume_data_alert: torrent_alert
	{
		save_resume_data_alert(boost::shared_ptr<entry> const& rd
			, torrent_handle const& h)
			: torrent_alert(h)
			, resume_data(rd)
		{}
	
		TORRENT_DEFINE_ALERT(save_resume_data_alert);

		const static int static_category = alert::storage_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " resume data generated"; }

		boost::shared_ptr<entry> resume_data;
	};

	struct TORRENT_EXPORT save_resume_data_failed_alert: torrent_alert
	{
		save_resume_data_failed_alert(torrent_handle const& h
			, error_code const& e)
			: torrent_alert(h)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}
	
		TORRENT_DEFINE_ALERT(save_resume_data_failed_alert);

		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " resume data was not generated: "
				+ error.message();
		}

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT torrent_paused_alert: torrent_alert
	{
		torrent_paused_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}
	
		TORRENT_DEFINE_ALERT(torrent_paused_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " paused"; }
	};

	struct TORRENT_EXPORT torrent_resumed_alert: torrent_alert
	{
		torrent_resumed_alert(torrent_handle const& h)
			: torrent_alert(h) {}

		TORRENT_DEFINE_ALERT(torrent_resumed_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " resumed"; }
	};

	struct TORRENT_EXPORT torrent_checked_alert: torrent_alert
	{
		torrent_checked_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(torrent_checked_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " checked"; }
	};

	struct TORRENT_EXPORT url_seed_alert: torrent_alert
	{
		url_seed_alert(
			torrent_handle const& h
			, std::string const& url_
			, error_code const& e)
			: torrent_alert(h)
			, url(url_)
			, msg(e.message())
		{}

		url_seed_alert(
			torrent_handle const& h
			, std::string const& url_
			, std::string const& msg_)
			: torrent_alert(h)
			, url(url_)
			, msg(msg_)
		{}

		TORRENT_DEFINE_ALERT(url_seed_alert);

		const static int static_category = alert::peer_notification | alert::error_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " url seed ("
				+ url + ") failed: " + msg;
		}

		std::string url;
		std::string msg;
	};

	struct TORRENT_EXPORT file_error_alert: torrent_alert
	{
		file_error_alert(
			std::string const& f
			, torrent_handle const& h
			, error_code const& e)
			: torrent_alert(h)
			, file(f)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		TORRENT_DEFINE_ALERT(file_error_alert);

		const static int static_category = alert::status_notification
			| alert::error_notification
			| alert::storage_notification;
		virtual std::string message() const
		{
			return torrent_alert::message() + " file (" + file + ") error: "
				+ error.message();
		}

		std::string file;
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT metadata_failed_alert: torrent_alert
	{
		metadata_failed_alert(const torrent_handle& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(metadata_failed_alert);

		const static int static_category = alert::error_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " invalid metadata received"; }
	};
	
	struct TORRENT_EXPORT metadata_received_alert: torrent_alert
	{
		metadata_received_alert(
			const torrent_handle& h)
			: torrent_alert(h)
		{}

		TORRENT_DEFINE_ALERT(metadata_received_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " metadata successfully received"; }
	};

	struct TORRENT_EXPORT udp_error_alert: alert
	{
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
			return "UDP error: " + error.message() + " from: " + endpoint.address().to_string(ec);
		}

		udp::endpoint endpoint;
		error_code error;
	};

	struct TORRENT_EXPORT external_ip_alert: alert
	{
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

		address external_address;
	};

	struct TORRENT_EXPORT listen_failed_alert: alert
	{
		listen_failed_alert(
			tcp::endpoint const& ep
			, error_code const& ec)
			: endpoint(ep)
			, error(ec)
		{}

		TORRENT_DEFINE_ALERT(listen_failed_alert);

		const static int static_category = alert::status_notification | alert::error_notification;
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "listening on %s failed: %s"
				, print_endpoint(endpoint).c_str(), error.message().c_str());
			return ret;
		}

		tcp::endpoint endpoint;
		error_code error;
	};

	struct TORRENT_EXPORT listen_succeeded_alert: alert
	{
		listen_succeeded_alert(tcp::endpoint const& ep)
			: endpoint(ep)
		{}

		TORRENT_DEFINE_ALERT(listen_succeeded_alert);

		const static int static_category = alert::status_notification;
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "successfully listening on %s", print_endpoint(endpoint).c_str());
			return ret;
		}

		tcp::endpoint endpoint;
	};

	struct TORRENT_EXPORT portmap_error_alert: alert
	{
		portmap_error_alert(int i, int t, error_code const& e)
			:  mapping(i), map_type(t), error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		TORRENT_DEFINE_ALERT(portmap_error_alert);

		const static int static_category = alert::port_mapping_notification
			| alert::error_notification;
		virtual std::string message() const
		{
			static char const* type_str[] = {"NAT-PMP", "UPnP"};
			return std::string("could not map port using ") + type_str[map_type]
				+ ": " + error.message();
		}

		int mapping;
		int map_type;
		error_code error;
#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT portmap_alert: alert
	{
		portmap_alert(int i, int port, int t)
			: mapping(i), external_port(port), map_type(t)
		{}

		TORRENT_DEFINE_ALERT(portmap_alert);

		const static int static_category = alert::port_mapping_notification;
		virtual std::string message() const
		{
			static char const* type_str[] = {"NAT-PMP", "UPnP"};
			char ret[200];
			snprintf(ret, sizeof(ret), "successfully mapped port using %s. external port: %u"
				, type_str[map_type], external_port);
			return ret;
		}

		int mapping;
		int external_port;
		int map_type;
	};

	struct TORRENT_EXPORT portmap_log_alert: alert
	{
		portmap_log_alert(int t, std::string const& m)
			: map_type(t), msg(m)
		{}

		TORRENT_DEFINE_ALERT(portmap_log_alert);

		const static int static_category = alert::port_mapping_notification;
		virtual std::string message() const
		{
			static char const* type_str[] = {"NAT-PMP", "UPnP"};
			char ret[200];
			snprintf(ret, sizeof(ret), "%s: %s", type_str[map_type], msg.c_str());
			return ret;
		}

		int map_type;
		std::string msg;
	};

	struct TORRENT_EXPORT fastresume_rejected_alert: torrent_alert
	{
		fastresume_rejected_alert(torrent_handle const& h
			, error_code const& e)
			: torrent_alert(h)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		TORRENT_DEFINE_ALERT(fastresume_rejected_alert);

		const static int static_category = alert::status_notification
			| alert::error_notification;
		virtual std::string message() const
		{ return torrent_alert::message() + " fast resume rejected: " + error.message(); }

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT peer_blocked_alert: torrent_alert
	{
		peer_blocked_alert(torrent_handle const& h, address const& ip_)
			: torrent_alert(h)
			, ip(ip_)
		{}
		
		TORRENT_DEFINE_ALERT(peer_blocked_alert);

		const static int static_category = alert::ip_block_notification;
		virtual std::string message() const
		{
			error_code ec;
			return torrent_alert::message() + ": blocked peer: " + ip.to_string(ec);
		}

		address ip;
	};

	struct TORRENT_EXPORT dht_announce_alert: alert
	{
		dht_announce_alert(address const& ip_, int port_
			, sha1_hash const& info_hash_)
			: ip(ip_)
			, port(port_)
			, info_hash(info_hash_)
		{}
		
		TORRENT_DEFINE_ALERT(dht_announce_alert);

		const static int static_category = alert::dht_notification;
		virtual std::string message() const
		{
			error_code ec;
			char ih_hex[41];
			to_hex((const char*)&info_hash[0], 20, ih_hex);
			char msg[200];
			snprintf(msg, sizeof(msg), "incoming dht announce: %s:%u (%s)"
				, ip.to_string(ec).c_str(), port, ih_hex);
			return msg;
		}

		address ip;
		int port;
		sha1_hash info_hash;
	};

	struct TORRENT_EXPORT dht_get_peers_alert: alert
	{
		dht_get_peers_alert(sha1_hash const& info_hash_)
			: info_hash(info_hash_)
		{}

		TORRENT_DEFINE_ALERT(dht_get_peers_alert);

		const static int static_category = alert::dht_notification;
		virtual std::string message() const
		{
			char ih_hex[41];
			to_hex((const char*)&info_hash[0], 20, ih_hex);
			char msg[200];
			snprintf(msg, sizeof(msg), "incoming dht get_peers: %s", ih_hex);
			return msg;
		}

		sha1_hash info_hash;
	};
}


#endif
