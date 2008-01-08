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

#include "libtorrent/pch.hpp"

#include <vector>
#include <iostream>
#include <cctype>
#include <iomanip>
#include <sstream>

#include "zlib.h"

#include <boost/bind.hpp>

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_connection.hpp"

using namespace libtorrent;
using boost::tuples::make_tuple;
using boost::tuples::tuple;
using boost::bind;

namespace
{
	enum
	{
		minimum_tracker_response_length = 3,
		http_buffer_size = 2048
	};


	enum
	{
		FTEXT = 0x01,
		FHCRC = 0x02,
		FEXTRA = 0x04,
		FNAME = 0x08,
		FCOMMENT = 0x10,
		FRESERVED = 0xe0,

		GZIP_MAGIC0 = 0x1f,
		GZIP_MAGIC1 = 0x8b
	};

}

namespace libtorrent
{
	// returns -1 if gzip header is invalid or the header size in bytes
	int gzip_header(const char* buf, int size)
	{
		TORRENT_ASSERT(buf != 0);
		TORRENT_ASSERT(size > 0);

		const unsigned char* buffer = reinterpret_cast<const unsigned char*>(buf);
		const int total_size = size;

		// The zip header cannot be shorter than 10 bytes
		if (size < 10) return -1;

		// check the magic header of gzip
		if ((buffer[0] != GZIP_MAGIC0) || (buffer[1] != GZIP_MAGIC1)) return -1;

		int method = buffer[2];
		int flags = buffer[3];

		// check for reserved flag and make sure it's compressed with the correct metod
		if (method != Z_DEFLATED || (flags & FRESERVED) != 0) return -1;

		// skip time, xflags, OS code
		size -= 10;
		buffer += 10;

		if (flags & FEXTRA)
		{
			int extra_len;

			if (size < 2) return -1;

			extra_len = (buffer[1] << 8) | buffer[0];

			if (size < (extra_len+2)) return -1;
			size -= (extra_len + 2);
			buffer += (extra_len + 2);
		}

		if (flags & FNAME)
		{
			while (size && *buffer)
			{
				--size;
				++buffer;
			}
			if (!size || *buffer) return -1;

			--size;
			++buffer;
		}

		if (flags & FCOMMENT)
		{
			while (size && *buffer)
			{
				--size;
				++buffer;
			}
			if (!size || *buffer) return -1;

			--size;
			++buffer;
		}

		if (flags & FHCRC)
		{
			if (size < 2) return -1;

			size -= 2;
			buffer += 2;
		}

		return total_size - size;
	}

	bool inflate_gzip(
		std::vector<char>& buffer
		, tracker_request const& req
		, request_callback* requester
		, int maximum_tracker_response_length)
	{
		TORRENT_ASSERT(maximum_tracker_response_length > 0);

		int header_len = gzip_header(&buffer[0], (int)buffer.size());
		if (header_len < 0)
		{
			requester->tracker_request_error(req, 200, "invalid gzip header in tracker response");
			return true;
		}

		// start off wth one kilobyte and grow
		// if needed
		std::vector<char> inflate_buffer(1024);

		// initialize the zlib-stream
		z_stream str;

		// subtract 8 from the end of the buffer since that's CRC32 and input size
		// and those belong to the gzip file
		str.avail_in = (int)buffer.size() - header_len - 8;
		str.next_in = reinterpret_cast<Bytef*>(&buffer[header_len]);
		str.next_out = reinterpret_cast<Bytef*>(&inflate_buffer[0]);
		str.avail_out = (int)inflate_buffer.size();
		str.zalloc = Z_NULL;
		str.zfree = Z_NULL;
		str.opaque = 0;
		// -15 is really important. It will make inflate() not look for a zlib header
		// and just deflate the buffer
		if (inflateInit2(&str, -15) != Z_OK)
		{
			requester->tracker_request_error(req, 200, "gzip out of memory");
			return true;
		}

		// inflate and grow inflate_buffer as needed
		int ret = inflate(&str, Z_SYNC_FLUSH);
		while (ret == Z_OK)
		{
			if (str.avail_out == 0)
			{
				if (inflate_buffer.size() >= (unsigned)maximum_tracker_response_length)
				{
					inflateEnd(&str);
					requester->tracker_request_error(req, 200
						, "tracker response too large");
					return true;
				}
				int new_size = (int)inflate_buffer.size() * 2;
				if (new_size > maximum_tracker_response_length) new_size = maximum_tracker_response_length;
				int old_size = (int)inflate_buffer.size();

				inflate_buffer.resize(new_size);
				str.next_out = reinterpret_cast<Bytef*>(&inflate_buffer[old_size]);
				str.avail_out = new_size - old_size;
			}

			ret = inflate(&str, Z_SYNC_FLUSH);
		}

		inflate_buffer.resize(inflate_buffer.size() - str.avail_out);
		inflateEnd(&str);

		if (ret != Z_STREAM_END)
		{
			requester->tracker_request_error(req, 200, "gzip error");
			return true;
		}

		// commit the resulting buffer
		std::swap(buffer, inflate_buffer);
		return false;
	}

	timeout_handler::timeout_handler(io_service& ios)
		: m_start_time(time_now())
		, m_read_time(time_now())
		, m_timeout(ios)
		, m_completion_timeout(0)
		, m_read_timeout(0)
		, m_abort(false)
	{}

	void timeout_handler::set_timeout(int completion_timeout, int read_timeout)
	{
		m_completion_timeout = completion_timeout;
		m_read_timeout = read_timeout;
		m_start_time = m_read_time = time_now();

		if (m_abort) return;

		int timeout = (std::min)(
			m_read_timeout, (std::min)(m_completion_timeout, m_read_timeout));
		asio::error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(bind(
			&timeout_handler::timeout_callback, self(), _1));
	}

	void timeout_handler::restart_read_timeout()
	{
		m_read_time = time_now();
	}

	void timeout_handler::cancel()
	{
		m_abort = true;
		m_completion_timeout = 0;
		asio::error_code ec;
		m_timeout.cancel(ec);
	}

	void timeout_handler::timeout_callback(asio::error_code const& error)
	{
		if (error) return;
		if (m_completion_timeout == 0) return;
		
		ptime now(time_now());
		time_duration receive_timeout = now - m_read_time;
		time_duration completion_timeout = now - m_start_time;
		
		if (m_read_timeout
			< total_seconds(receive_timeout)
			|| m_completion_timeout
			< total_seconds(completion_timeout))
		{
			on_timeout();
			return;
		}

		if (m_abort) return;

		int timeout = (std::min)(
			m_read_timeout, (std::min)(m_completion_timeout, m_read_timeout));
		asio::error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(
			bind(&timeout_handler::timeout_callback, self(), _1));
	}

	tracker_connection::tracker_connection(
		tracker_manager& man
		, tracker_request const& req
		, io_service& ios
		, address bind_interface_
		, boost::weak_ptr<request_callback> r)
		: timeout_handler(ios)
		, m_requester(r)
		, m_bind_interface(bind_interface_)
		, m_man(man)
		, m_req(req)
	{}

	boost::shared_ptr<request_callback> tracker_connection::requester()
	{
		return m_requester.lock();
	}

	void tracker_connection::fail(int code, char const* msg)
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->tracker_request_error(m_req, code, msg);
		close();
	}

	void tracker_connection::fail_timeout()
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->tracker_request_timed_out(m_req);
		close();
	}
	
	void tracker_connection::close()
	{
		cancel();
		m_man.remove_request(this);
	}

	void tracker_manager::remove_request(tracker_connection const* c)
	{
		mutex_t::scoped_lock l(m_mutex);

		tracker_connections_t::iterator i = std::find(m_connections.begin()
			, m_connections.end(), boost::intrusive_ptr<const tracker_connection>(c));
		if (i == m_connections.end()) return;

		m_connections.erase(i);
	}

	// returns protocol, auth, hostname, port, path	
	tuple<std::string, std::string, std::string, int, std::string>
		parse_url_components(std::string url)
	{
		std::string hostname; // hostname only
		std::string auth; // user:pass
		std::string protocol; // should be http
		int port = 80;

		std::string::iterator at;
		std::string::iterator colon;
		std::string::iterator port_pos;

		// PARSE URL
		std::string::iterator start = url.begin();
		// remove white spaces in front of the url
		while (start != url.end() && (*start == ' ' || *start == '\t'))
			++start;
		std::string::iterator end
			= std::find(url.begin(), url.end(), ':');
		protocol.assign(start, end);

		if (end == url.end()) goto exit;
		++end;
		if (end == url.end()) goto exit;
		if (*end != '/') goto exit;
		++end;
		if (end == url.end()) goto exit;
		if (*end != '/') goto exit;
		++end;
		start = end;

		at = std::find(start, url.end(), '@');
		colon = std::find(start, url.end(), ':');
		end = std::find(start, url.end(), '/');

		if (at != url.end()
			&& colon != url.end()
			&& colon < at
			&& at < end)
		{
			auth.assign(start, at);
			start = at;
			++start;
		}

		// this is for IPv6 addresses
		if (start != url.end() && *start == '[')
		{
			port_pos = std::find(start, url.end(), ']');
			if (port_pos == url.end()) goto exit;
			port_pos = std::find(port_pos, url.end(), ':');
		}
		else
		{
			port_pos = std::find(start, url.end(), ':');
		}

		if (port_pos < end)
		{
			hostname.assign(start, port_pos);
			++port_pos;
			port = atoi(std::string(port_pos, end).c_str());
		}
		else
		{
			hostname.assign(start, end);
		}

		start = end;
exit:
		return make_tuple(protocol, auth, hostname, port
			, std::string(start, url.end()));
	}

	void tracker_manager::queue_request(
		io_service& ios
		, connection_queue& cc
		, tracker_request req
		, std::string const& auth
		, address bind_infc
		, boost::weak_ptr<request_callback> c)
	{
		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(req.num_want >= 0);
		if (req.event == tracker_request::stopped)
			req.num_want = 0;

		TORRENT_ASSERT(!m_abort || req.event == tracker_request::stopped);
		if (m_abort && req.event != tracker_request::stopped)
			return;

		std::string protocol;
		std::string hostname;
		int port;
		std::string request_string;

		using boost::tuples::ignore;
		// TODO: should auth be used here?
		boost::tie(protocol, ignore, hostname, port, request_string)
			= parse_url_components(req.url);

		boost::intrusive_ptr<tracker_connection> con;

		if (protocol == "http")
		{
			con = new http_tracker_connection(
				ios
				, cc
				, *this
				, req
				, hostname
				, port
				, request_string
				, bind_infc
				, c
				, m_settings
				, m_proxy
				, auth);
		}
		else if (protocol == "udp")
		{
			con = new udp_tracker_connection(
				ios
				, *this
				, req
				, hostname
				, port
				, bind_infc
				, c
				, m_settings);
		}
		else
		{
			if (boost::shared_ptr<request_callback> r = c.lock())
				r->tracker_request_error(req, -1, "unknown protocol in tracker url: "
					+ protocol);
		}

		m_connections.push_back(con);

		boost::shared_ptr<request_callback> cb = con->requester();
		if (cb) cb->m_manager = this;
	}

	void tracker_manager::abort_all_requests()
	{
		// removes all connections from m_connections
		// except those with a requester == 0 (since those are
		// 'event=stopped'-requests)
		mutex_t::scoped_lock l(m_mutex);

		m_abort = true;
		tracker_connections_t keep_connections;

		while (!m_connections.empty())
		{
			boost::intrusive_ptr<tracker_connection>& c = m_connections.back();
			if (!c)
			{
				m_connections.pop_back();
				continue;
			}
			tracker_request const& req = c->tracker_req();
			if (req.event == tracker_request::stopped)
			{
				keep_connections.push_back(c);
				m_connections.pop_back();
				continue;
			}
			// close will remove the entry from m_connections
			// so no need to pop
			c->close();
		}

		std::swap(m_connections, keep_connections);
	}
	
	bool tracker_manager::empty() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_connections.empty();
	}

	int tracker_manager::num_requests() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_connections.size();
	}
}
