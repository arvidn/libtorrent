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

#include <vector>
#include <iostream>
#include <cctype>
#include <iomanip>
#include <sstream>

#include "zlib.h"

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/io.hpp"

using namespace libtorrent;

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


	http_tracker_connection::http_tracker_connection(
		tracker_request const& req
		, std::string const& hostname
		, unsigned short port
		, std::string const& request
		, request_callback* c
		, const http_settings& stn
		, std::string const& password)
		: tracker_connection(c)
		, m_state(read_status)
		, m_content_encoding(plain)
		, m_content_length(0)
		, m_recv_pos(0)
		, m_request_time(boost::posix_time::second_clock::local_time())
		, m_settings(stn)
	{
		const std::string* connect_to_host;
		bool using_proxy = false;

		// should we use the proxy?
		if (!m_settings.proxy_ip.empty())
		{
			connect_to_host = &m_settings.proxy_ip;
			if (m_settings.proxy_port != 0) port = m_settings.proxy_port;
			using_proxy = true;
		}
		else
		{
			connect_to_host = &hostname;
		}

		// TODO: this is a problem. DNS-lookup is blocking!
		// (may block up to 5 seconds)
		address a(connect_to_host->c_str(), port);
		if (c) c->m_tracker_address = a;
		boost::shared_ptr<socket> s(new socket(socket::tcp, false));
		s->connect(a);

		m_send_buffer.assign("GET ");
		if (using_proxy)
		{
			m_send_buffer += "http://";
			m_send_buffer += hostname;
			if (port != 80)
				m_send_buffer += boost::lexical_cast<std::string>(port);
		}

		m_send_buffer += request;

		m_send_buffer += "?info_hash=";
		m_send_buffer += escape_string(
			reinterpret_cast<const char*>(req.info_hash.begin()), 20);

		m_send_buffer += "&peer_id=";
		m_send_buffer += escape_string(
			reinterpret_cast<const char*>(req.id.begin()), 20);

		m_send_buffer += "&port=";
		m_send_buffer += boost::lexical_cast<std::string>(req.listen_port);

		m_send_buffer += "&uploaded=";
		m_send_buffer += boost::lexical_cast<std::string>(req.uploaded);

		m_send_buffer += "&downloaded=";
		m_send_buffer += boost::lexical_cast<std::string>(req.downloaded);

		m_send_buffer += "&left=";
		m_send_buffer += boost::lexical_cast<std::string>(req.left);

		if (req.event != tracker_request::none)
		{
			const char* event_string[] = {"completed", "started", "stopped"};
			m_send_buffer += "&event=";
			m_send_buffer += event_string[req.event - 1];
		}
		m_send_buffer += "&key=";
		std::stringstream key_string;
		key_string << std::hex << req.key;
		m_send_buffer += key_string.str();
		m_send_buffer += "&compact=1";
		m_send_buffer += "&numwant=";
		m_send_buffer += boost::lexical_cast<std::string>(req.num_want);

		// extension that tells the tracker that
		// we don't need any peer_id's in the response
		m_send_buffer += "&no_peer_id=1";

		m_send_buffer += " HTTP/1.0\r\nAccept-Encoding: gzip\r\n"
			"User-Agent: ";
		m_send_buffer += m_settings.user_agent;
		m_send_buffer += " (libtorrent)\r\n"
			"Host: ";
		m_send_buffer += hostname;
		if (port != 80)
		{
			m_send_buffer += ':';
			m_send_buffer += boost::lexical_cast<std::string>(port);
		}
		if (using_proxy && !m_settings.proxy_login.empty())
		{
			m_send_buffer += "\r\nProxy-Authorization: Basic ";
			m_send_buffer += base64encode(m_settings.proxy_login + ":" + m_settings.proxy_password);
		}
		if (password != "")
		{
			m_send_buffer += "\r\nAuthorization: Basic ";
			m_send_buffer += base64encode(password);
		}
		m_send_buffer += "\r\n\r\n";
	#ifndef NDEBUG
		if (c) c->debug_log("==> TRACKER_REQUEST [ str: " + m_send_buffer + " ]");
	#endif
		m_socket = s;
	}

	// returns true if this connection is finished and should be removed from
	// the connections list.
	bool http_tracker_connection::tick()
	{
#ifndef NDEBUG
		try
		{
#endif
	
		using namespace boost::posix_time;

		time_duration d = second_clock::local_time() - m_request_time;
		if (d > seconds(m_settings.tracker_timeout) ||
			(requester() == 0 && d > seconds(m_settings.stop_tracker_timeout)))
		{
			if (requester()) requester()->tracker_request_timed_out();
			return true;
		}

	#ifndef NDEBUG
		if (requester()) requester()->debug_log("tracker connection tick");
	#endif

		// if we have a send buffer and the socket is ready for writing
		// send the buffer
		if (!m_send_buffer.empty() && m_socket->is_writable())
		{
			int sent = m_socket->send(m_send_buffer.c_str(),(int)m_send_buffer.size());

			if (sent == (int)m_send_buffer.size())
			{
#if defined(_MSC_VER) && _MSC_VER < 1300
				m_send_buffer.erase(m_send_buffer.begin(), m_send_buffer.end());
#else
				m_send_buffer.clear();
#endif
			}
			else if (sent > 0)
			{
				m_send_buffer.erase(
					m_send_buffer.begin()
					, m_send_buffer.begin() + sent);
			}

			if (sent != 0)
				m_request_time = boost::posix_time::second_clock::local_time();
		}


		// if the socket isn't ready for reading, there's no point in continuing
		// trying to read from it
		if (!m_socket->is_readable()) return false;
		m_request_time = boost::posix_time::second_clock::local_time();

	#ifndef NDEBUG
		if (requester()) requester()->debug_log("tracker connection socket readable");
	#endif

		// if the receive buffer is full, expand it with http_buffer_size
		if ((int)m_buffer.size() == m_recv_pos)
		{
			if ((int)m_buffer.size() > m_settings.tracker_maximum_response_length)
			{
				if (requester())
				{
					requester()->tracker_request_error(
						200
						, "too large tracker response");
				}
				return true;
			}
			assert(http_buffer_size > 0);
			m_buffer.resize(m_buffer.size() + http_buffer_size);
		}


		assert(m_recv_pos >= 0);
		assert(m_recv_pos < (int)m_buffer.size());
		int received = m_socket->receive(&m_buffer[m_recv_pos], (int)m_buffer.size() - m_recv_pos);

		assert(received <= (int)m_buffer.size() - m_recv_pos);

		if (received > 0) m_recv_pos += received;

	#ifndef NDEBUG
		if (requester()) requester()->debug_log("received: " + boost::lexical_cast<std::string>(m_recv_pos));
	#endif

		if (m_state == read_status)
		{
			if (received <= 0)
			{
				if (requester())
				{
					requester()->tracker_request_error(
						-1
						, "invalid tracker response, connection closed");
				}
				return true;
			}

			std::vector<char>::iterator end = m_buffer.begin()+m_recv_pos;
			std::vector<char>::iterator newline = std::find(m_buffer.begin(), end, '\n');
			// if we don't have a full line yet, wait.
			if (newline == end) return false;

	#ifndef NDEBUG
			if (requester()) requester()->debug_log(std::string(m_buffer.begin(), newline));
	#endif

			std::istringstream line(std::string(m_buffer.begin(), newline));
			++newline;
			m_recv_pos -= (int)std::distance(m_buffer.begin(), newline);
			m_buffer.erase(m_buffer.begin(), newline);

			std::string protocol;
			line >> m_server_protocol;
			if (m_server_protocol.substr(0, 5) != "HTTP/")
			{
				std::string error_msg = "unknown protocol in response: " + m_server_protocol;
				if (requester()) requester()->tracker_request_error(-1, error_msg.c_str());
				return true;
			}
			int code;
			line >> code;
			std::getline(line, m_server_message);
			m_state = read_header;

			if (code != 200)
			{
				std::string error_msg = boost::lexical_cast<std::string>(code) + " " + m_server_message;
				if (requester()) requester()->tracker_request_error(code, error_msg.c_str());
				return true;
			}
		}

		if (m_state == read_header)
		{
			if (received <= 0)
			{
				if (requester())
					requester()->tracker_request_error(-1, "invalid tracker response, connection closed while reading header");
				return true;
			}

			std::vector<char>::iterator end = m_buffer.begin()+m_recv_pos;
			std::vector<char>::iterator newline = std::find(m_buffer.begin(), end, '\n');
			std::string line;

			while (newline != end && m_state == read_header)
			{
				line.assign(m_buffer.begin(), newline);

	#ifndef NDEBUG
				if (requester()) requester()->debug_log(line);
	#endif

				if (line.substr(0, 16) == "Content-Length: ")
				{
					try
					{
						m_content_length = boost::lexical_cast<int>(line.substr(16));
					}
					catch(boost::bad_lexical_cast&)
					{
						if (requester())
						{
							requester()->tracker_request_error(
								-1, 
								"invalid content-length in tracker response");
						}
						return true;
					}
					if (m_content_length > m_settings.tracker_maximum_response_length)
					{
						if (requester())
						{
							requester()->tracker_request_error(
								-1
								, "content-length is greater than maximum response length");
						}
						return true;
					}

					if (m_content_length < minimum_tracker_response_length)
					{
						if (requester())
						{
							requester()->tracker_request_error(
								-1
								, "content-length is smaller than minimum response length");
						}
						return true;
					}
				}
				else if (line.substr(0, 18) == "Content-Encoding: ")
				{
					if (line.substr(18, 4) == "gzip" || line.substr(18, 6) == "x-gzip")
					{
						m_content_encoding = gzip;
					}
					else
					{
						std::string error_str = "unknown content encoding in response: \"";
						error_str += line.substr(18, line.length() - 18 - 2);
						error_str += "\"";
						if (requester())
						{
							requester()->tracker_request_error(-1, error_str.c_str());
						}
						return true;
					}
				}
				else if (line.size() < 3)
				{
					m_state = read_body;
	#ifndef NDEBUG
					if (requester()) requester()->debug_log("end of http header");
	#endif
				}

				++newline;
				assert(m_recv_pos <= (int)m_buffer.size());
				m_recv_pos -= (int)std::distance(m_buffer.begin(), newline);
				m_buffer.erase(m_buffer.begin(), newline);
				assert(m_recv_pos <= (int)m_buffer.size());
				end = m_buffer.begin() + m_recv_pos;
				newline = std::find(m_buffer.begin(), end, '\n');
			}

		}

		if (m_state == read_body)
		{
			if (m_recv_pos == m_content_length || received <= 0)
			{
				// GZIP
				if (m_content_encoding == gzip)
				{
					if (inflate_gzip(m_buffer,
						requester(),
						m_settings.tracker_maximum_response_length))
						return true;
				}

				// handle tracker response
				entry e = bdecode(m_buffer.begin(), m_buffer.end());
				parse(e);
				return true;
			}
			return false;
		}
		else if (m_recv_pos > m_content_length && m_content_length > 0)
		{
			if (requester())
			{
				requester()->tracker_request_error(
					-1
					, "invalid tracker response (body > content_length)");
			}
			return true;
		}

		return false;
#ifndef NDEBUG
		}
		catch (std::exception&)
		{
			if (requester())
				requester()->debug_log(std::string(m_buffer.begin(), m_buffer.end());
			throw;			
		}
#endif
}

	peer_entry http_tracker_connection::extract_peer_info(const entry& info)
	{
		peer_entry ret;

		// extract peer id (if any)
		entry const* i = info.find_key("peer id");
		if (i != 0)
		{
			if (i->string().length() != 20)
				throw std::runtime_error("invalid response from tracker");
			std::copy(i->string().begin(), i->string().end(), ret.id.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			std::fill_n(ret.id.begin(), 20, 0);
		}

		// extract ip
		i = info.find_key("ip");
		if (i == 0) throw std::runtime_error("invalid response from tracker");
		ret.ip = i->string();

		// extract port
		i = info.find_key("port");
		if (i == 0) throw std::runtime_error("invalid response from tracker");
		ret.port = (unsigned short)i->integer();

		return ret;
	}

	void http_tracker_connection::parse(const entry& e)
	{
		if (requester() == 0) return;

		std::vector<peer_entry> peer_list;
		try
		{
			// parse the response

			try
			{
				const entry& failure = e["failure reason"];
				throw std::runtime_error(failure.string().c_str());
			}
			catch (const type_error&) {}

			int interval = e["interval"].integer();

			peer_list.clear();

			if (e["peers"].type() == entry::string_t)
			{
				std::string const& peers = e["peers"].string();
				for (std::string::const_iterator i = peers.begin();
					i != peers.end();)
				{
					if (std::distance(i, peers.end()) < 6) break;

					peer_entry p;
					p.id.clear();
					std::stringstream ip_str;
					ip_str << (int)detail::read_uint8(i) << ".";
					ip_str << (int)detail::read_uint8(i) << ".";
					ip_str << (int)detail::read_uint8(i) << ".";
					ip_str << (int)detail::read_uint8(i);
					p.ip = ip_str.str();
					p.port = detail::read_uint16(i);
					peer_list.push_back(p);
				}
			}
			else
			{
				const entry::list_type& l = e["peers"].list();
				for(entry::list_type::const_iterator i = l.begin(); i != l.end(); ++i)
				{
					peer_entry p = extract_peer_info(*i);
					peer_list.push_back(p);
				}
			}

			requester()->tracker_response(peer_list, interval);
		}
		catch(type_error& e)
		{
			requester()->tracker_request_error(-1, e.what());
		}
		catch(std::runtime_error& e)
		{
			requester()->tracker_request_error(-1, e.what());
		}
	}

}
