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

namespace libtorrent
{
	struct TORRENT_EXPORT torrent_alert: alert
	{
		torrent_alert(torrent_handle const& h, alert::severity_t s
			, std::string const& msg)
			: alert(s, msg)
			, handle(h)
		{}
		
		torrent_handle handle;
	};

	struct TORRENT_EXPORT tracker_alert: torrent_alert
	{
		tracker_alert(torrent_handle const& h
			, int times
			, int status
			, std::string const& msg)
			: torrent_alert(h, alert::warning, msg)
			, times_in_row(times)
			, status_code(status)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_alert(*this)); }

		int times_in_row;
		int status_code;
	};

	struct TORRENT_EXPORT tracker_warning_alert: torrent_alert
	{
		tracker_warning_alert(torrent_handle const& h
			, std::string const& msg)
			: torrent_alert(h, alert::warning, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_warning_alert(*this)); }
	};


	
	struct TORRENT_EXPORT tracker_reply_alert: torrent_alert
	{
		tracker_reply_alert(torrent_handle const& h
			, int np
			, std::string const& msg)
			: torrent_alert(h, alert::info, msg)
			, num_peers(np)
		{}

		int num_peers;

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_reply_alert(*this)); }
	};

	struct TORRENT_EXPORT tracker_announce_alert: torrent_alert
	{
		tracker_announce_alert(torrent_handle const& h, std::string const& msg)
			: torrent_alert(h, alert::info, msg)
		{}
	
		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_announce_alert(*this)); }
	};
	
	struct TORRENT_EXPORT hash_failed_alert: torrent_alert
	{
		hash_failed_alert(
			torrent_handle const& h
			, int index
			, std::string const& msg)
			: torrent_alert(h, alert::info, msg)
			, piece_index(index)
		{ assert(index >= 0);}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new hash_failed_alert(*this)); }

		int piece_index;
	};

	struct TORRENT_EXPORT peer_ban_alert: torrent_alert
	{
		peer_ban_alert(tcp::endpoint const& pip, torrent_handle h, std::string const& msg)
			: torrent_alert(h, alert::info, msg)
			, ip(pip)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_ban_alert(*this)); }

		tcp::endpoint ip;
	};

	struct TORRENT_EXPORT peer_error_alert: alert
	{
		peer_error_alert(tcp::endpoint const& pip, peer_id const& pid_, std::string const& msg)
			: alert(alert::debug, msg)
			, ip(pip)
			, pid(pid_)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_error_alert(*this)); }

		tcp::endpoint ip;
		peer_id pid;
	};

	struct TORRENT_EXPORT invalid_request_alert: torrent_alert
	{
		invalid_request_alert(
			peer_request const& r
			, torrent_handle const& h
			, tcp::endpoint const& sender
			, peer_id const& pid_
			, std::string const& msg)
			: torrent_alert(h, alert::debug, msg)
			, ip(sender)
			, request(r)
			, pid(pid_)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new invalid_request_alert(*this)); }

		tcp::endpoint ip;
		peer_request request;
		peer_id pid;
	};

	struct TORRENT_EXPORT torrent_finished_alert: torrent_alert
	{
		torrent_finished_alert(
			const torrent_handle& h
			, const std::string& msg)
			: torrent_alert(h, alert::warning, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_finished_alert(*this)); }
	};

	struct TORRENT_EXPORT url_seed_alert: torrent_alert
	{
		url_seed_alert(
			torrent_handle const& h
			, const std::string& url_
			, const std::string& msg)
			: torrent_alert(h, alert::warning, msg)
			, url(url_)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new url_seed_alert(*this)); }

		std::string url;
	};

	struct TORRENT_EXPORT file_error_alert: torrent_alert
	{
		file_error_alert(
			const torrent_handle& h
			, const std::string& msg)
			: torrent_alert(h, alert::fatal, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new file_error_alert(*this)); }
	};

	struct TORRENT_EXPORT metadata_failed_alert: torrent_alert
	{
		metadata_failed_alert(
			const torrent_handle& h
			, const std::string& msg)
			: torrent_alert(h, alert::info, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new metadata_failed_alert(*this)); }
	};
	
	struct TORRENT_EXPORT metadata_received_alert: torrent_alert
	{
		metadata_received_alert(
			const torrent_handle& h
			, const std::string& msg)
			: torrent_alert(h, alert::info, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new metadata_received_alert(*this)); }
	};

	struct TORRENT_EXPORT listen_failed_alert: alert
	{
		listen_failed_alert(
			const std::string& msg)
			: alert(alert::fatal, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new listen_failed_alert(*this)); }
	};

	struct TORRENT_EXPORT fastresume_rejected_alert: torrent_alert
	{
		fastresume_rejected_alert(torrent_handle const& h
			, std::string const& msg)
			: torrent_alert(h, alert::warning, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new fastresume_rejected_alert(*this)); }
	};

}


#endif
