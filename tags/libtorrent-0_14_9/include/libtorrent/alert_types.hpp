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

#include <boost/lexical_cast.hpp>

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
			std::stringstream ret;
			ret << torrent_alert::message() << ": file "
				<< index << " renamed to " << name;
			return ret.str();
		}

		std::string name;
		int index;
	};

	struct TORRENT_EXPORT file_rename_failed_alert: torrent_alert
	{
		file_rename_failed_alert(torrent_handle const& h
			, std::string const& msg_
			, int index_)
			: torrent_alert(h)
			, msg(msg_)
			, index(index_)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new file_rename_failed_alert(*this)); }

		virtual char const* what() const { return "file rename failed"; }
		virtual std::string message() const
		{
			std::stringstream ret;
			ret << torrent_alert::message() << ": failed to rename file "
				<< index << ": " << msg;
			return ret.str();
		}

		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }

		std::string msg;
		int index;
	};

	struct TORRENT_EXPORT performance_alert: torrent_alert
	{
		enum performance_warning_t
		{
			outstanding_disk_buffer_limit_reached,
			outstanding_request_limit_reached
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
			, torrent_status::state_t const& state_)
			: torrent_alert(h)
			, state(state_)
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
	};

	struct TORRENT_EXPORT tracker_error_alert: tracker_alert
	{
		tracker_error_alert(torrent_handle const& h
			, int times
			, int status
			, std::string const& url_
			, std::string const& msg_)
			: tracker_alert(h, url_)
			, times_in_row(times)
			, status_code(status)
			, msg(msg_)
		{ TORRENT_ASSERT(!url.empty()); }

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_error_alert(*this)); }
		const static int static_category = alert::tracker_notification | alert::error_notification;
		virtual int category() const { return static_category; }
		virtual char const* what() const { return "tracker error"; }
		virtual std::string message() const
		{
			std::stringstream ret;
			ret << tracker_alert::message() << " (" << status_code << ") "
				<< msg << " (" << times_in_row << ")";
			return ret.str();
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
			std::stringstream ret;
			ret << tracker_alert::message() << " scrape reply: " << incomplete
				<< " " << complete;
			return ret.str();
		}
	};

	struct TORRENT_EXPORT scrape_failed_alert: tracker_alert
	{
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
			std::stringstream ret;
			ret << tracker_alert::message() << " received peers: "
				<< num_peers;
			return ret.str();
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
			std::stringstream ret;
			ret << tracker_alert::message() << " received DHT peers: "
				<< num_peers;
			return ret.str();
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
			const static char* event_str[] = {"none", "completed", "started", "stopped"};
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
			std::stringstream ret;
			ret << torrent_alert::message() << " hash for piece "
				<< piece_index << " failed";
			return ret.str();
		}

		int piece_index;
	};

	struct TORRENT_EXPORT peer_ban_alert: peer_alert
	{
		peer_ban_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
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
		peer_unsnubbed_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
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
		peer_snubbed_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
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
		peer_error_alert(torrent_handle const& h, tcp::endpoint const& ip
			, peer_id const& pid, std::string const& msg_)
			: peer_alert(h, ip, pid)
			, msg(msg_)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_error_alert(*this)); }
		virtual char const* what() const { return "peer error"; }
		const static int static_category = alert::peer_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			error_code ec;
			return peer_alert::message() + " peer error: " + msg;
		}

		std::string msg;
	};

	struct TORRENT_EXPORT peer_connect_alert: peer_alert
	{
		peer_connect_alert(torrent_handle h, tcp::endpoint const& ip
			, peer_id const& pid)
			: peer_alert(h, ip, pid)
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
		peer_disconnected_alert(torrent_handle const& h, tcp::endpoint const& ip
			, peer_id const& pid, std::string const& msg_)
			: peer_alert(h, ip, pid)
			, msg(msg_)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_disconnected_alert(*this)); }
		virtual char const* what() const { return "peer disconnected"; }
		const static int static_category = alert::debug_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return peer_alert::message() + " disconnecting: " + msg;
		}

		std::string msg;
	};

	struct TORRENT_EXPORT invalid_request_alert: peer_alert
	{
		invalid_request_alert(torrent_handle const& h, tcp::endpoint const& ip
			, peer_id const& pid, peer_request const& r)
			: peer_alert(h, ip, pid)
			, request(r)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new invalid_request_alert(*this)); }
		virtual char const* what() const { return "invalid piece request"; }
		virtual std::string message() const
		{
			std::stringstream ret;
			ret << peer_alert::message() << " peer sent an invalid piece request "
				"( piece: " << request.piece << " start: " << request.start
				<< " len: " << request.length << ")";
			return ret.str();
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
			std::stringstream ret;
			ret << torrent_alert::message() << " piece " << piece_index
				<< " finished downloading";
			return ret.str();
		}
	};

	struct TORRENT_EXPORT request_dropped_alert: peer_alert
	{
		request_dropped_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
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
			std::stringstream ret;
			ret << peer_alert::message() << " peer dropped block ( piece: "
				<< piece_index << " block: " << block_index << ")";
			return ret.str();
		}
	};

	struct TORRENT_EXPORT block_timeout_alert: peer_alert
	{
		block_timeout_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
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
			std::stringstream ret;
			ret << peer_alert::message() << " peer timed out request ( piece: "
				<< piece_index << " block: " << block_index << ")";
			return ret.str();
		}
	};

	struct TORRENT_EXPORT block_finished_alert: peer_alert
	{
		block_finished_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
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
			std::stringstream ret;
			ret << peer_alert::message() << " block finished downloading ( piece: "
				<< piece_index << " block: " << block_index << ")";
			return ret.str();
		}
	};

	struct TORRENT_EXPORT block_downloading_alert: peer_alert
	{
		block_downloading_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, char const* speedmsg, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
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
			std::stringstream ret;
			ret << peer_alert::message() << " requested block ( piece: "
				<< piece_index << " block: " << block_index << ") " << peer_speedmsg;
			return ret.str();
		}
	};

	struct TORRENT_EXPORT unwanted_block_alert: peer_alert
	{
		unwanted_block_alert(const torrent_handle& h, tcp::endpoint const& ip
			, peer_id const& pid, int block_num, int piece_num)
			: peer_alert(h, ip, pid)
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
			std::stringstream ret;
			ret << peer_alert::message() << " received block not in download queue ( piece: "
				<< piece_index << " block: " << block_index << ")";
			return ret.str();
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
		torrent_deleted_alert(torrent_handle const& h)
			: torrent_alert(h)
		{}
	
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_deleted_alert(*this)); }
		virtual char const* what() const { return "torrent deleted"; }
		const static int static_category = alert::storage_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " deleted";
		}
	};

	struct TORRENT_EXPORT torrent_delete_failed_alert: torrent_alert
	{
		torrent_delete_failed_alert(torrent_handle const& h, std::string msg_)
			: torrent_alert(h)
			, msg(msg_)
		{}
	
		std::string msg;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_delete_failed_alert(*this)); }
		virtual char const* what() const { return "torrent delete failed"; }
		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " torrent deletion failed: "
				+ msg;
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
			, std::string const& msg_)
			: torrent_alert(h)
			, msg(msg_)
		{}
	
		std::string msg;
		
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new save_resume_data_failed_alert(*this)); }
		virtual char const* what() const { return "save resume data failed"; }
		const static int static_category = alert::storage_notification
			| alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " resume data was not generated: "
				+ msg;
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
			, const std::string& url_
			, const std::string& msg_)
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
			, const torrent_handle& h
			, const std::string& msg_)
			: torrent_alert(h)
			, file(f)
			, msg(msg_)
		{}

		std::string file;
		std::string msg;

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
				+ msg;
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
			error_code ec;
			std::stringstream ret;
			ret << "listening on " << endpoint
				<< " failed: " << error.message();
			return ret.str();
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
			error_code ec;
			std::stringstream ret;
			ret << "successfully listening on " << endpoint;
			return ret.str();
		}
	};

	struct TORRENT_EXPORT portmap_error_alert: alert
	{
		portmap_error_alert(int i, int t, const std::string& msg_)
			:  mapping(i), type(t), msg(msg_)
		{}

		int mapping;
		int type;
		std::string msg;

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
				+ ": " + msg;
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
			std::stringstream ret;
			ret << "successfully mapped port using " << type_str[type]
				<< ". external port: " << external_port;
			return ret.str();
		}
	};

	struct TORRENT_EXPORT fastresume_rejected_alert: torrent_alert
	{
		fastresume_rejected_alert(torrent_handle const& h
			, std::string const& msg_)
			: torrent_alert(h)
			, msg(msg_)
		{}

		std::string msg;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new fastresume_rejected_alert(*this)); }
		virtual char const* what() const { return "resume data rejected"; }
		const static int static_category = alert::status_notification
			| alert::error_notification;
		virtual int category() const { return static_category; }
		virtual std::string message() const
		{
			return torrent_alert::message() + " fast resume rejected: " + msg;
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
}


#endif
