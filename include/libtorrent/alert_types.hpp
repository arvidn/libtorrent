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

namespace libtorrent
{
	struct tracker_alert: alert
	{
		tracker_alert(const torrent_handle& h
			, const std::string& msg)
			: alert(alert::warning, msg)
			, handle(h)
			{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new tracker_alert(*this)); }

		torrent_handle handle;
	};

	struct hash_failed_alert: alert
	{
		hash_failed_alert(
			const torrent_handle& h
			, int index
			, const std::string& msg)
			: alert(alert::info, msg)
			, handle(h)
			, piece_index(index)
			{ assert(index >= 0);}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new hash_failed_alert(*this)); }

		torrent_handle handle;
		int piece_index;
	};

	struct peer_error_alert: alert
	{
		peer_error_alert(const peer_id& pid, const std::string& msg)
			: alert(alert::debug, msg)
			, id(pid)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new peer_error_alert(*this)); }

		// TODO: use address instead of peer_id
		peer_id id;
	};

	struct chat_message_alert: alert
	{
		chat_message_alert(
			const torrent_handle& h
			, const peer_id& send
			, const std::string& msg)
			: alert(alert::critical, msg)
			, handle(h)
			, sender(send)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new chat_message_alert(*this)); }

		torrent_handle handle;
		// TODO: use address instead of peer_id
		peer_id sender;
	};

	struct invalid_request_alert: alert
	{
		invalid_request_alert(
			const peer_request& r
			, const torrent_handle& h
			, const peer_id& send
			, const std::string& msg)
			: alert(alert::debug, msg)
			, handle(h)
			, sender(send)
			, request(r)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new invalid_request_alert(*this)); }

		torrent_handle handle;
		// TODO: use address instead of peer_id
		peer_id sender;
		peer_request request;
	};

	struct torrent_finished_alert: alert
	{
		torrent_finished_alert(
			const torrent_handle& h
			, const std::string& msg)
			: alert(alert::warning, msg)
			, handle(h)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new torrent_finished_alert(*this)); }

		torrent_handle handle;
	};

	struct file_error_alert: alert
	{
		file_error_alert(
			const torrent_handle& h
			, const std::string& msg)
			: alert(alert::fatal, msg)
			, handle(h)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new file_error_alert(*this)); }

		torrent_handle handle;
	};

	struct listen_failed_alert: alert
	{
		listen_failed_alert(
			const std::string& msg)
			: alert(alert::fatal, msg)
		{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new listen_failed_alert(*this)); }
	};
}


#endif
