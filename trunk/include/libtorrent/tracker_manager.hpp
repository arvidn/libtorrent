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

#ifndef TORRENT_TRACKER_MANAGER_HPP_INCLUDED
#define TORRENT_TRACKER_MANAGER_HPP_INCLUDED

#include <vector>
#include <string>
#include <utility>
#include <ctime>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/cstdint.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/socket.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/http_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp"

namespace libtorrent
{
	struct request_callback;
	class tracker_manager;

	// encodes a string using the base64 scheme
	std::string base64encode(const std::string& s);

	// returns -1 if gzip header is invalid or the header size in bytes
	int gzip_header(const char* buf, int size);

	bool inflate_gzip(
		std::vector<char>& buffer
		, request_callback* requester
		, int maximum_tracker_response_length);

	struct tracker_request
	{
		enum event_t
		{
			none,
			completed,
			started,
			stopped
		};

		sha1_hash info_hash;
		peer_id id;
		size_type downloaded;
		size_type uploaded;
		size_type left;
		unsigned short listen_port;
		event_t event;
		std::string url;
	};

	struct request_callback
	{
		friend class tracker_manager;
		request_callback(): m_manager(0) {}
		virtual ~request_callback();
		virtual void tracker_response(
			std::vector<peer_entry>& peers
			, int interval) = 0;
		virtual void tracker_request_timed_out() = 0;
		virtual void tracker_request_error(
			int response_code
			, const std::string& description) = 0;

		address m_tracker_address;

#ifndef NDEBUG
		virtual void debug_log(const std::string& line) = 0;
#endif
	private:
		tracker_manager* m_manager;
	};

	struct tracker_connection: boost::noncopyable
	{
		tracker_connection(request_callback* r)
			: m_requester(r)
		{}

		virtual bool tick() = 0;
		virtual bool send_finished() const = 0;
		request_callback* requester() { return m_requester; }
		virtual ~tracker_connection() {}

	private:

		request_callback* m_requester;

	};

	class tracker_manager: boost::noncopyable
	{
	public:

		tracker_manager(const http_settings& s)
			: m_settings(s) {}
		
		void tick();
		void queue_request(
			tracker_request const& r
			, request_callback* c = 0
			, std::string const& password = "");
		void abort_request(request_callback* c);
		void abort_all_requests();
		bool send_finished() const;

	private:

		std::vector<boost::shared_ptr<tracker_connection> > m_connections;
		const http_settings& m_settings;
	};

	inline request_callback::~request_callback()
	{ if (m_manager) m_manager->abort_request(this); }

}

#endif // TORRENT_TRACKER_MANAGER_HPP_INCLUDED
