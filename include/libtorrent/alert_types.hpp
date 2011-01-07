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
#include "libtorrent/stat.hpp"

namespace libtorrent
{
	struct TORRENT_EXPORT torrent_alert: alert
	{
		torrent_alert(torrent_handle const& h)
			: handle(h)
		{}
		
		virtual std::string message() const
		{ return handle.is_valid()?handle.name():" - "; }

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

	struct TORRENT_EXPORT read_piece_alert: torrent_alert
	{
		read_piece_alert(torrent_handle const& h
			, int p, boost::shared_array<char> d, int s)
			: torrent_alert(h)
			, buffer(d)
			, piece(p)
			, size(s)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new read_piece_alert(*this)); }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "read piece"; }
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

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new file_completed_alert(*this)); }
		const static int static_category = alert::progress_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "file completed"; }
		virtual std::string message() const
		{
			char msg[200];
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

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new file_renamed_alert(*this)); }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "file renamed"; }
		virtual std::string message() const
		{
			char msg[200 + NAME_MAX];
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

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new file_rename_failed_alert(*this)); }

		virtual char const* what() const { return "file rename failed"; }
		virtual std::string message() const
		{
			char ret[200 + NAME_MAX];
			snprintf(ret, sizeof(ret), "%s: failed to rename file %d: %s"
				, torrent_alert::message().c_str(), index, error.message().c_str());
			return ret;
		}

		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }

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
			send_buffer_watermark_too_low,

			num_warnings
		};

		performance_alert(torrent_handle const& h
			, performance_warning_t w)
			: torrent_alert(h)
			, warning_code(w)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new performance_alert(*this)); }

		virtual char const* what() const { return "performance warning"; }
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

		const static int static_category = alert::performance_warning;
		virtual int category() const { return static_category; }

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

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new state_changed_alert(*this)); }

		virtual char const* what() const { return "torrent state changed"; }
		virtual std::string message() const
		{
			static char const* state_str[] =
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating"
				, "checking (r)"};

			return torrent_alert::message() + ": state changed to: "
				+ state_str[state];
		}


		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }

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

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_error_alert(*this)); }
		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "tracker error"; }
		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s (%d) %s (%d)"
				, tracker_alert::message().c_str(), status_code
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

		std::string msg;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_warning_alert(*this)); }
		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "tracker warning"; }
		virtual std::string message() const
		{
			return tracker_alert::message() + " warning: " + msg;
		}
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

		int incomplete;
		int complete;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new scrape_reply_alert(*this)); }
		virtual char const* what() const { return "tracker scrape reply"; }
		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s scrape reply: %u %u"
				, tracker_alert::message().c_str(), incomplete, complete);
			return ret;
		}
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

		std::string msg;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new scrape_failed_alert(*this)); }
		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "tracker scrape failed"; }
		virtual std::string message() const
		{
			return tracker_alert::message() + " scrape failed: " + msg;
		}
	};

	struct TORRENT_EXPORT tracker_reply_alert: tracker_alert
	{
		tracker_reply_alert(torrent_handle const& h
			, int np
			, std::string const& url_)
			: tracker_alert(h, url_)
			, num_peers(np)
		{ TORRENT_ASSERT(!url.empty()); }

		int num_peers;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_reply_alert(*this)); }
		virtual char const* what() const { return "tracker reply"; }
		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s received peers: %u"
				, tracker_alert::message().c_str(), num_peers);
			return ret;
		}
	};

	struct TORRENT_EXPORT dht_reply_alert: tracker_alert
	{
		dht_reply_alert(torrent_handle const& h
			, int np)
			: tracker_alert(h, "")
			, num_peers(np)
		{}

		int num_peers;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new dht_reply_alert(*this)); }
		virtual char const* what() const { return "DHT reply"; }
		virtual std::string message() const
		{
			char ret[400];
			snprintf(ret, sizeof(ret), "%s received DHT peers: %u"
				, tracker_alert::message().c_str(), num_peers);
			return ret;
		}
	};

	struct TORRENT_EXPORT tracker_announce_alert: tracker_alert
	{
		tracker_announce_alert(torrent_handle const& h
			, std::string const& url_, int event_)
			: tracker_alert(h, url_)
			, event(event_)
		{ TORRENT_ASSERT(!url.empty()); }

		int event;
	
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_announce_alert(*this)); }
		virtual char const* what() const { return "tracker announce sent"; }
		virtual std::string message() const
		{
			const static char* event_str[] = {"none", "completed", "started", "stopped", "paused"};
			TORRENT_ASSERT(event < sizeof(event_str)/sizeof(event_str[0]));
			return tracker_alert::message() + " sending announce (" + event_str[event] + ")";
		}
	};
	
	struct TORRENT_EXPORT hash_failed_alert: torrent_alert
	{
		hash_failed_alert(
			torrent_handle const& h
			, int index)
			: torrent_alert(h)
			, piece_index(index)
		{ TORRENT_ASSERT(index >= 0);}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new hash_failed_alert(*this)); }
		virtual char const* what() const { return "piece hash failed"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
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
		peer_ban_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id)
			: peer_alert(h, ep, peer_id)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_ban_alert(*this)); }
		virtual char const* what() const { return "peer banned"; }
		virtual std::string message() const
		{
			error_code ec;
			return peer_alert::message() + " banned peer";
		}
	};

	struct TORRENT_EXPORT peer_unsnubbed_alert: peer_alert
	{
		peer_unsnubbed_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id)
			: peer_alert(h, ep, peer_id)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_unsnubbed_alert(*this)); }
		virtual char const* what() const { return "peer unsnubbed"; }
		virtual std::string message() const
		{
			return peer_alert::message() + " peer unsnubbed";
		}
	};

	struct TORRENT_EXPORT peer_snubbed_alert: peer_alert
	{
		peer_snubbed_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id)
			: peer_alert(h, ep, peer_id)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_snubbed_alert(*this)); }
		virtual char const* what() const { return "peer snubbed"; }
		virtual std::string message() const
		{
			return peer_alert::message() + " peer snubbed";
		}
	};

	struct TORRENT_EXPORT peer_error_alert: peer_alert
	{
		peer_error_alert(torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, error_code const& e)
			: peer_alert(h, ep, peer_id)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_error_alert(*this)); }
		virtual char const* what() const { return "peer error"; }
		const static int static_category = alert::peer_notification;
		virtual int category() const { return static_category; }
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
		peer_connect_alert(torrent_handle h, tcp::endpoint const& ep
			, peer_id const& peer_id)
			: peer_alert(h, ep, peer_id)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_connect_alert(*this)); }
		virtual char const* what() const { return "connecting to peer"; }
		const static int static_category = alert::debug_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return peer_alert::message() + " connecting to peer";
		}
	};

	struct TORRENT_EXPORT peer_disconnected_alert: peer_alert
	{
		peer_disconnected_alert(torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, error_code const& e)
			: peer_alert(h, ep, peer_id)
			, error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_disconnected_alert(*this)); }
		virtual char const* what() const { return "peer disconnected"; }
		const static int static_category = alert::debug_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return peer_alert::message() + " disconnecting: " + error.message();
		}

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
	};

	struct TORRENT_EXPORT invalid_request_alert: peer_alert
	{
		invalid_request_alert(torrent_handle const& h, tcp::endpoint const& ep
			, peer_id const& peer_id, peer_request const& r)
			: peer_alert(h, ep, peer_id)
			, request(r)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new invalid_request_alert(*this)); }
		virtual char const* what() const { return "invalid piece request"; }
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

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_finished_alert(*this)); }
		virtual char const* what() const { return "torrent finished"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " torrent finished downloading";
		}
	};

	struct TORRENT_EXPORT piece_finished_alert: torrent_alert
	{
		piece_finished_alert(
			const torrent_handle& h
			, int piece_num)
			: torrent_alert(h)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(piece_index >= 0);}

		int piece_index;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new piece_finished_alert(*this)); }
		virtual char const* what() const { return "piece finished downloading"; }
		const static int static_category = alert::progress_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s piece: %u finished downloading"
				, torrent_alert::message().c_str(), piece_index);
			return ret;
		}
	};

	struct TORRENT_EXPORT request_dropped_alert: peer_alert
	{
		request_dropped_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		int block_index;
		int piece_index;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new request_dropped_alert(*this)); }
		virtual char const* what() const { return "block request dropped"; }
		const static int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s peer dropped block ( piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}
	};

	struct TORRENT_EXPORT block_timeout_alert: peer_alert
	{
		block_timeout_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		int block_index;
		int piece_index;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new block_timeout_alert(*this)); }
		virtual char const* what() const { return "block timed out"; }
		const static int static_category = alert::progress_notification
			| alert::peer_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s peer timed out request ( piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}
	};

	struct TORRENT_EXPORT block_finished_alert: peer_alert
	{
		block_finished_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		int block_index;
		int piece_index;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new block_finished_alert(*this)); }
		virtual char const* what() const { return "block finished downloading"; }
		const static int static_category = alert::progress_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s block finished downloading (piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}
	};

	struct TORRENT_EXPORT block_downloading_alert: peer_alert
	{
		block_downloading_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, char const* speedmsg, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, peer_speedmsg(speedmsg)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		char const* peer_speedmsg;
		int block_index;
		int piece_index;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new block_downloading_alert(*this)); }
		virtual char const* what() const { return "block requested"; }
		const static int static_category = alert::progress_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s requested block (piece: %u block: %u) %s"
				, torrent_alert::message().c_str(), piece_index, block_index, peer_speedmsg);
			return ret;
		}
	};

	struct TORRENT_EXPORT unwanted_block_alert: peer_alert
	{
		unwanted_block_alert(const torrent_handle& h, tcp::endpoint const& ep
			, peer_id const& peer_id, int block_num, int piece_num)
			: peer_alert(h, ep, peer_id)
			, block_index(block_num)
			, piece_index(piece_num)
		{ TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);}

		int block_index;
		int piece_index;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new unwanted_block_alert(*this)); }
		virtual char const* what() const { return "unwanted block received"; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "%s received block not in download queue (piece: %u block: %u)"
				, torrent_alert::message().c_str(), piece_index, block_index);
			return ret;
		}
	};

	struct TORRENT_EXPORT storage_moved_alert: torrent_alert
	{
		storage_moved_alert(torrent_handle const& h, std::string const& path_)
			: torrent_alert(h)
			, path(path_)
		{}
	
		std::string path;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new storage_moved_alert(*this)); }
		virtual char const* what() const { return "storage moved"; }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " moved storage to: "
				+ path;
		}
	};

	struct TORRENT_EXPORT storage_moved_failed_alert: torrent_alert
	{
		storage_moved_failed_alert(torrent_handle const& h, error_code const& ec_)
			: torrent_alert(h)
			, error(ec_)
		{}
	
		error_code error;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new storage_moved_failed_alert(*this)); }
		virtual char const* what() const { return "storage moved failed"; }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " storage move failed: "
				+ error.message();
		}
	};

	struct TORRENT_EXPORT torrent_deleted_alert: torrent_alert
	{
		torrent_deleted_alert(torrent_handle const& h, sha1_hash const& ih)
			: torrent_alert(h)
		{ info_hash = ih; }
	
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_deleted_alert(*this)); }
		virtual char const* what() const { return "torrent deleted"; }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " deleted";
		}

		sha1_hash info_hash;
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
	
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_delete_failed_alert(*this)); }
		virtual char const* what() const { return "torrent delete failed"; }
		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " torrent deletion failed: "
				+ error.message();
		}
	};

	struct TORRENT_EXPORT save_resume_data_alert: torrent_alert
	{
		save_resume_data_alert(boost::shared_ptr<entry> const& rd
			, torrent_handle const& h)
			: torrent_alert(h)
			, resume_data(rd)
		{}
	
		boost::shared_ptr<entry> resume_data;
		
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new save_resume_data_alert(*this)); }
		virtual char const* what() const { return "save resume data complete"; }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " resume data generated";
		}
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
	
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif
		
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new save_resume_data_failed_alert(*this)); }
		virtual char const* what() const { return "save resume data failed"; }
		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " resume data was not generated: "
				+ error.message();
		}
	};

	struct TORRENT_EXPORT torrent_paused_alert: torrent_alert
	{
		torrent_paused_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}
	
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_paused_alert(*this)); }
		virtual char const* what() const { return "torrent paused"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " paused";
		}
	};

	struct TORRENT_EXPORT torrent_resumed_alert: torrent_alert
	{
		torrent_resumed_alert(torrent_handle const& h)
			: torrent_alert(h) {}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_resumed_alert(*this)); }
		virtual char const* what() const { return "torrent resumed"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " resumed";
		}
	};

	struct TORRENT_EXPORT torrent_checked_alert: torrent_alert
	{
		torrent_checked_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_checked_alert(*this)); }
		virtual char const* what() const { return "torrent checked"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " checked";
		}
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

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new url_seed_alert(*this)); }
		virtual char const* what() const { return "web seed error"; }
		const static int static_category = alert::peer_notification | alert::error_notification;
		virtual int category() const { return static_category; }
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

		std::string file;
		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new file_error_alert(*this)); }
		virtual char const* what() const { return "file error"; }
		const static int static_category = alert::status_notification
			| alert::error_notification
			| alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " file (" + file + ") error: "
				+ error.message();
		}
	};

	struct TORRENT_EXPORT metadata_failed_alert: torrent_alert
	{
		metadata_failed_alert(const torrent_handle& h)
			: torrent_alert(h)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new metadata_failed_alert(*this)); }
		virtual char const* what() const { return "metadata failed"; }
		const static int static_category = alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " invalid metadata received";
		}
	};
	
	struct TORRENT_EXPORT metadata_received_alert: torrent_alert
	{
		metadata_received_alert(
			const torrent_handle& h)
			: torrent_alert(h)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new metadata_received_alert(*this)); }
		virtual char const* what() const { return "metadata received"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " metadata successfully received";
		}
	};

	struct TORRENT_EXPORT udp_error_alert: alert
	{
		udp_error_alert(
			udp::endpoint const& ep
			, error_code const& ec)
			: endpoint(ep)
			, error(ec)
		{}

		udp::endpoint endpoint;
		error_code error;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new udp_error_alert(*this)); }
		virtual char const* what() const { return "udp error"; }
		const static int static_category = alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			error_code ec;
			return "UDP error: " + error.message() + " from: " + endpoint.address().to_string(ec);
		}
	};

	struct TORRENT_EXPORT external_ip_alert: alert
	{
		external_ip_alert(address const& ip)
			: external_address(ip)
		{}

		address external_address;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new external_ip_alert(*this)); }
		virtual char const* what() const { return "external IP received"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			error_code ec;
			return "external IP received: " + external_address.to_string(ec);
		}
	};

	struct TORRENT_EXPORT listen_failed_alert: alert
	{
		listen_failed_alert(
			tcp::endpoint const& ep
			, error_code const& ec)
			: endpoint(ep)
			, error(ec)
		{}

		tcp::endpoint endpoint;
		error_code error;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new listen_failed_alert(*this)); }
		virtual char const* what() const { return "listen failed"; }
		const static int static_category = alert::status_notification | alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "listening on %s failed: %s"
				, print_endpoint(endpoint).c_str(), error.message().c_str());
			return ret;
		}
	};

	struct TORRENT_EXPORT listen_succeeded_alert: alert
	{
		listen_succeeded_alert(tcp::endpoint const& ep)
			: endpoint(ep)
		{}

		tcp::endpoint endpoint;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new listen_succeeded_alert(*this)); }
		virtual char const* what() const { return "listen succeeded"; }
		const static int static_category = alert::status_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ret[200];
			snprintf(ret, sizeof(ret), "successfully listening on %s", print_endpoint(endpoint).c_str());
			return ret;
		}
	};

	struct TORRENT_EXPORT portmap_error_alert: alert
	{
		portmap_error_alert(int i, int t, error_code const& e)
			:  mapping(i), type(t), error(e)
		{
#ifndef TORRENT_NO_DEPRECATE
			msg = error.message();
#endif
		}

		int mapping;
		int type;
		error_code error;
#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new portmap_error_alert(*this)); }
		virtual char const* what() const { return "port map error"; }
		const static int static_category = alert::port_mapping_notification
			| alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			static char const* type_str[] = {"NAT-PMP", "UPnP"};
			return std::string("could not map port using ") + type_str[type]
				+ ": " + error.message();
		}
	};

	struct TORRENT_EXPORT portmap_alert: alert
	{
		portmap_alert(int i, int port, int t)
			: mapping(i), external_port(port), type(t)
		{}

		int mapping;
		int external_port;
		int type;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new portmap_alert(*this)); }
		virtual char const* what() const { return "port map succeeded"; }
		const static int static_category = alert::port_mapping_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			static char const* type_str[] = {"NAT-PMP", "UPnP"};
			char ret[200];
			snprintf(ret, sizeof(ret), "successfully mapped port using %s. external port: %u"
				, type_str[type], external_port);
			return ret;
		}
	};

	struct TORRENT_EXPORT portmap_log_alert: alert
	{
		portmap_log_alert(int t, std::string const& m)
			: type(t), msg(m)
		{}

		int type;
		std::string msg;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new portmap_log_alert(*this)); }
		virtual char const* what() const { return "port map log"; }
		const static int static_category = alert::port_mapping_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			static char const* type_str[] = {"NAT-PMP", "UPnP"};
			char ret[600];
			snprintf(ret, sizeof(ret), "%s: %s", type_str[type], msg.c_str());
			return ret;
		}
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

		error_code error;

#ifndef TORRENT_NO_DEPRECATE
		std::string msg;
#endif

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new fastresume_rejected_alert(*this)); }
		virtual char const* what() const { return "resume data rejected"; }
		const static int static_category = alert::status_notification
			| alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " fast resume rejected: " + error.message();
		}
	};

	struct TORRENT_EXPORT peer_blocked_alert: alert
	{
		peer_blocked_alert(address const& ip_)
			: ip(ip_)
		{}
		
		address ip;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_blocked_alert(*this)); }
		virtual char const* what() const { return "peer blocked"; }
		const static int static_category = alert::ip_block_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			error_code ec;
			return "blocked peer: " + ip.to_string(ec);
		}
	};

	struct TORRENT_EXPORT dht_announce_alert: alert
	{
		dht_announce_alert(address const& ip_, int port_
			, sha1_hash const& info_hash_)
			: ip(ip_)
			, port(port_)
			, info_hash(info_hash_)
		{}
		
		address ip;
		int port;
		sha1_hash info_hash;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new dht_announce_alert(*this)); }
		virtual char const* what() const { return "incoming dht announce"; }
		const static int static_category = alert::dht_notification;
		virtual int category() const { return static_category; }
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
	};

	struct TORRENT_EXPORT dht_get_peers_alert: alert
	{
		dht_get_peers_alert(sha1_hash const& info_hash_)
			: info_hash(info_hash_)
		{}
		
		sha1_hash info_hash;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new dht_get_peers_alert(*this)); }
		virtual char const* what() const { return "incoming dht get_peers request"; }
		const static int static_category = alert::dht_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			char ih_hex[41];
			to_hex((const char*)&info_hash[0], 20, ih_hex);
			char msg[200];
			snprintf(msg, sizeof(msg), "incoming dht get_peers: %s", ih_hex);
			return msg;
		}
	};

	struct TORRENT_EXPORT stats_alert: torrent_alert
	{
		stats_alert(torrent_handle const& h, int interval
			, stat const& s);

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new stats_alert(*this)); }
		const static int static_category = alert::stats_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "stats_alert"; }

		virtual std::string message() const;

		enum stats_channel
		{
			upload_payload,
			upload_protocol,
			upload_ip_protocol,
			upload_dht_protocol,
			upload_tracker_protocol,
			download_payload,
			download_protocol,
			download_ip_protocol,
			download_dht_protocol,
			download_tracker_protocol,
			num_channels
		};

		int transferred[num_channels];
		int interval;
	};

	struct TORRENT_EXPORT cache_flushed_alert: torrent_alert
	{
		cache_flushed_alert(torrent_handle const& h);

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new cache_flushed_alert(*this)); }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "cache_flushed_alert"; }
	};
}


#endif
