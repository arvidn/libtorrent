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
#include <boost/cstdint.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/tuple/tuple.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/socket.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"

namespace libtorrent
{
	struct request_callback;
	class tracker_manager;
	struct timeout_handler;
	struct tracker_connection;

	// returns -1 if gzip header is invalid or the header size in bytes
	TORRENT_EXPORT int gzip_header(const char* buf, int size);

	struct TORRENT_EXPORT tracker_request
	{
		tracker_request()
			: kind(announce_request)
			, event(none)
			, key(0)
			, num_want(0)
		{}

		enum
		{
			announce_request,
			scrape_request
		} kind;

		enum event_t
		{
			none,
			completed,
			started,
			stopped
		};

		sha1_hash info_hash;
		peer_id pid;
		size_type downloaded;
		size_type uploaded;
		size_type left;
		size_type corrupt;
		unsigned short listen_port;
		event_t event;
		std::string url;
		int key;
		int num_want;
		std::string ipv6;
		std::string ipv4;
	};

	struct TORRENT_EXPORT request_callback
	{
		friend class tracker_manager;
		request_callback(): m_manager(0) {}
		virtual ~request_callback() {}
		virtual void tracker_warning(tracker_request const& req
			, std::string const& msg) = 0;
		virtual void tracker_scrape_response(tracker_request const& req
			, int complete, int incomplete, int downloads) {}
		virtual void tracker_response(
			tracker_request const& req
			, std::vector<peer_entry>& peers
			, int interval
			, int complete
			, int incomplete
			, address const& external_ip) = 0;
		virtual void tracker_request_timed_out(
			tracker_request const& req) = 0;
		virtual void tracker_request_error(
			tracker_request const& req
			, int response_code
			, const std::string& description) = 0;

		tcp::endpoint m_tracker_address;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		virtual void debug_log(const std::string& line) = 0;
#endif
	private:
		tracker_manager* m_manager;
	};

	struct TORRENT_EXPORT timeout_handler
		: intrusive_ptr_base<timeout_handler>
		, boost::noncopyable
	{
		timeout_handler(io_service& str);

		void set_timeout(int completion_timeout, int read_timeout);
		void restart_read_timeout();
		void cancel();

		virtual void on_timeout() = 0;
		virtual ~timeout_handler() {}

	private:
	
		void timeout_callback(error_code const&);

		boost::intrusive_ptr<timeout_handler> self()
		{ return boost::intrusive_ptr<timeout_handler>(this); }

		// used for timeouts
		// this is set when the request has been sent
		ptime m_start_time;
		// this is set every time something is received
		ptime m_read_time;
		// the asio async operation
		deadline_timer m_timeout;
		
		int m_completion_timeout;
		int m_read_timeout;

		typedef boost::mutex mutex_t;
		mutable mutex_t m_mutex;
		bool m_abort;
	};

	struct TORRENT_EXPORT tracker_connection
		: timeout_handler
	{
		tracker_connection(tracker_manager& man
			, tracker_request const& req
			, io_service& ios
			, address bind_interface
			, boost::weak_ptr<request_callback> r);

		boost::shared_ptr<request_callback> requester();
		virtual ~tracker_connection() {}

		tracker_request const& tracker_req() const { return m_req; }

		void fail(int code, char const* msg);
		void fail_timeout();
		virtual void start() = 0;
		virtual void close();
		address const& bind_interface() const { return m_bind_interface; }

	protected:
		boost::weak_ptr<request_callback> m_requester;
	private:
		address m_bind_interface;
		tracker_manager& m_man;
		const tracker_request m_req;
	};

	class TORRENT_EXPORT tracker_manager: boost::noncopyable
	{
	public:

		tracker_manager(session_settings const& s, proxy_settings const& ps)
			: m_settings(s)
			, m_proxy(ps)
	  		, m_abort(false) {}

		void queue_request(
			io_service& ios
			, connection_queue& cc
			, tracker_request r
			, std::string const& auth
			, address bind_infc
			, boost::weak_ptr<request_callback> c
				= boost::weak_ptr<request_callback>());
		void abort_all_requests();

		void remove_request(tracker_connection const*);
		bool empty() const;
		int num_requests() const;
		
	private:

		typedef boost::recursive_mutex mutex_t;
		mutable mutex_t m_mutex;

		typedef std::list<boost::intrusive_ptr<tracker_connection> >
			tracker_connections_t;
		tracker_connections_t m_connections;
		session_settings const& m_settings;
		proxy_settings const& m_proxy;
		bool m_abort;
	};
}

#endif // TORRENT_TRACKER_MANAGER_HPP_INCLUDED

