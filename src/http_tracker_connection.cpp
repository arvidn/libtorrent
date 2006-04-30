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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/io.hpp"

using namespace libtorrent;
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

using namespace boost::posix_time;

namespace libtorrent
{
	http_parser::http_parser()
		: m_recv_pos(0)
		, m_status_code(-1)
		, m_content_length(-1)
		, m_content_encoding(plain)
		, m_state(read_status)
		, m_recv_buffer(0, 0)
		, m_body_start_pos(0)
		, m_finished(false)
	{}

	boost::tuple<int, int> http_parser::incoming(buffer::const_interval recv_buffer)
	{
		m_recv_buffer = recv_buffer;
		boost::tuple<int, int> ret(0, 0);

		char const* pos = recv_buffer.begin + m_recv_pos;
		if (m_state == read_status)
		{
			assert(!m_finished);
			char const* newline = std::find(pos, recv_buffer.end, '\n');
			// if we don't have a full line yet, wait.
			if (newline == recv_buffer.end) return ret;

			if (newline == pos)
				throw std::runtime_error("unexpected newline in HTTP response");

			std::istringstream line(std::string(pos, newline - 1));
			++newline;
			int incoming = (int)std::distance(pos, newline);
			m_recv_pos += incoming;
			boost::get<1>(ret) += incoming;
			pos = newline;

			line >> m_protocol;
			if (m_protocol.substr(0, 5) != "HTTP/")
			{
				throw std::runtime_error("unknown protocol in HTTP response: "
					+ m_protocol);
			}
			line >> m_status_code;
			std::getline(line, m_server_message);
			m_state = read_header;
		}

		if (m_state == read_header)
		{
			assert(!m_finished);
			char const* newline = std::find(pos, recv_buffer.end, '\n');
			std::string line;

			while (newline != recv_buffer.end && m_state == read_header)
			{
				if (newline == pos)
					throw std::runtime_error("unexpected newline in HTTP response");
			
				line.assign(pos, newline - 1);
				m_recv_pos += newline - pos;
				boost::get<1>(ret) += newline - pos;
				pos = newline;

				std::string::size_type separator = line.find(": ");
				if (separator == std::string::npos)
				{
					++pos;
					++m_recv_pos;
					boost::get<1>(ret) += 1;
					
					m_state = read_body;
					m_body_start_pos = m_recv_pos;
					break;
				}

				std::string name = line.substr(0, separator);
				std::string value = line.substr(separator + 2, std::string::npos);
				m_header.insert(std::make_pair(name, value));

				if (name == "Content-Length")
				{
					try
					{
						m_content_length = boost::lexical_cast<int>(value);
					}
					catch(boost::bad_lexical_cast&) {}
				}
				else if (name == "Content-Encoding")
				{
					if (value == "gzip" || value == "x-gzip")
					{
						m_content_encoding = gzip;
					}
					else
					{
						std::string error_str = "unknown content encoding in response: \"";
						error_str += value;
						error_str += "\"";
						throw std::runtime_error(error_str);
					}
				}
				// TODO: make sure we don't step outside of the buffer
				++pos;
				++m_recv_pos;
				assert(m_recv_pos <= (int)recv_buffer.left());
				newline = std::find(pos, recv_buffer.end, '\n');
			}
		}

		if (m_state == read_body)
		{
			int incoming = recv_buffer.end - pos;
			if (m_recv_pos - m_body_start_pos + incoming > m_content_length
				&& m_content_length >= 0)
				incoming = m_content_length - m_recv_pos + m_body_start_pos;

			assert(incoming >= 0);
			m_recv_pos += incoming;
			boost::get<0>(ret) += incoming;

			if (m_content_length >= 0
				&& m_recv_pos - m_body_start_pos >= m_content_length)
			{
				m_finished = true;
			}
		}
		return ret;
	}
	
	buffer::const_interval http_parser::get_body()
	{
		char const* body_begin = m_recv_buffer.begin + m_body_start_pos;
		char const* body_end = m_recv_buffer.begin + m_recv_pos;

		m_recv_pos = 0;
		m_body_start_pos = 0;
		m_status_code = -1;
		m_content_length = -1;
		m_finished = false;
		m_state = read_status;
		m_header.clear();
		
		return buffer::const_interval(body_begin, body_end);
	}

	http_tracker_connection::http_tracker_connection(
		demuxer& d
		, tracker_manager& man
		, tracker_request const& req
		, std::string const& hostname
		, unsigned short port
		, std::string request
		, boost::weak_ptr<request_callback> c
		, const http_settings& stn
		, std::string const& auth)
		: tracker_connection(man, req, d, c)
		, m_man(man)
		, m_state(read_status)
		, m_content_encoding(plain)
		, m_content_length(0)
		, m_name_lookup(d)
		, m_port(port)
		, m_recv_pos(0)
		, m_buffer(http_buffer_size)
		, m_settings(stn)
		, m_password(auth)
		, m_code(0)
		, m_timed_out(false)
	{
		const std::string* connect_to_host;
		bool using_proxy = false;

		m_send_buffer.assign("GET ");

		// should we use the proxy?
		if (!m_settings.proxy_ip.empty())
		{
			connect_to_host = &m_settings.proxy_ip;
			using_proxy = true;
			m_send_buffer += "http://";
			m_send_buffer += hostname;
			if (port != 80)
			{
				m_send_buffer += ":";
				m_send_buffer += boost::lexical_cast<std::string>(port);
			}
			m_port = m_settings.proxy_port != 0
				? m_settings.proxy_port : 80 ;
		}
		else
		{
			connect_to_host = &hostname;
		}

		if (tracker_req().kind == tracker_request::scrape_request)
		{
			// find and replace "announce" with "scrape"
			// in request

			std::size_t pos = request.find("announce");
			if (pos == std::string::npos)
				throw std::runtime_error("scrape is not available on url: '"
				+ tracker_req().url +"'");
			request.replace(pos, 8, "scrape");
		}

		m_send_buffer += request;

		// if request-string already contains
		// some parameters, append an ampersand instead
		// of a question mark
		if (request.find('?') != std::string::npos)
			m_send_buffer += "&";
		else
			m_send_buffer += "?";

		m_send_buffer += "info_hash=";
		m_send_buffer += escape_string(
			reinterpret_cast<const char*>(req.info_hash.begin()), 20);

		if (tracker_req().kind == tracker_request::announce_request)
		{
			m_send_buffer += "&peer_id=";
			m_send_buffer += escape_string(
				reinterpret_cast<const char*>(req.pid.begin()), 20);

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
			m_send_buffer += boost::lexical_cast<std::string>(
				std::min(req.num_want, 999));

			// extension that tells the tracker that
			// we don't need any peer_id's in the response
			m_send_buffer += "&no_peer_id=1";
		}

		m_send_buffer += " HTTP/1.0\r\nAccept-Encoding: gzip\r\n"
			"User-Agent: ";
		m_send_buffer += escape_string(m_settings.user_agent.c_str()
			, m_settings.user_agent.length());
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
		if (auth != "")
		{
			m_send_buffer += "\r\nAuthorization: Basic ";
			m_send_buffer += base64encode(auth);
		}
		m_send_buffer += "\r\n\r\n";
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester())
		{
			requester().debug_log("==> TRACKER_REQUEST [ str: " + m_send_buffer + " ]");
			std::stringstream info_hash_str;
			info_hash_str << req.info_hash;
			requester().debug_log("info_hash: " + info_hash_str.str() + "\n");
		}
#endif

		m_name_lookup.async_by_name(m_host, *connect_to_host
			, bind(&http_tracker_connection::name_lookup, self(), _1));
		set_timeout(m_settings.tracker_completion_timeout
			, m_settings.tracker_receive_timeout);
	}

	void http_tracker_connection::on_timeout()
	{
		m_timed_out = true;
		m_socket.reset();
		m_name_lookup.cancel();
		fail_timeout();
	}

	void http_tracker_connection::name_lookup(asio::error const& error) try
	{
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;

		if (error)
		{
			fail(-1, error.what());
			return;
		}
		
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("tracker name lookup successful");
#endif
		restart_read_timeout();
		m_socket.reset(new stream_socket(m_name_lookup.io_service()));
		tcp::endpoint a(m_port, m_host.address(0));
		if (has_requester()) requester().m_tracker_address = a;
		m_socket->async_connect(a, bind(&http_tracker_connection::connected, self(), _1));
	}
	catch (std::exception& e)
	{
		assert(false);
		fail(-1, e.what());
	}

	void http_tracker_connection::connected(asio::error const& error) try
	{
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;
		if (error)
		{
			fail(-1, error.what());
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("tracker connection successful");
#endif

		restart_read_timeout();
		async_write(*m_socket, asio::buffer(m_send_buffer.c_str()
			, m_send_buffer.size()), bind(&http_tracker_connection::sent
			, self(), _1));
	}
	catch (std::exception& e)
	{
		assert(false);
		fail(-1, e.what());
	}

	void http_tracker_connection::sent(asio::error const& error) try
	{
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;
		if (error)
		{
			fail(-1, error.what());
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("tracker send data completed");
#endif
		restart_read_timeout();
		assert(m_buffer.size() - m_recv_pos > 0);
		m_socket->async_read_some(asio::buffer(&m_buffer[m_recv_pos]
			, m_buffer.size() - m_recv_pos), bind(&http_tracker_connection::receive
			, self(), _1, _2));
	}
	catch (std::exception& e)
	{
		assert(false);
		fail(-1, e.what());
	}; // msvc 7.1 seems to require this semi-colon

	
	void http_tracker_connection::receive(asio::error const& error
		, std::size_t bytes_transferred) try
	{
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;

		if (error)
		{
			if (error == asio::error::eof)
			{
				on_response();
				close();
				return;
			}

			fail(-1, error.what());
			return;
		}

		restart_read_timeout();
		assert(bytes_transferred > 0);
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("tracker connection reading "
			+ boost::lexical_cast<std::string>(bytes_transferred));
#endif

		m_recv_pos += bytes_transferred;

		// if the receive buffer is full, expand it with http_buffer_size
		if ((int)m_buffer.size() == m_recv_pos)
		{
			if ((int)m_buffer.size() >= m_settings.tracker_maximum_response_length)
			{
				fail(200, "too large tracker response");
				return;
			}
			assert(http_buffer_size > 0);
			if ((int)m_buffer.size() + http_buffer_size
				> m_settings.tracker_maximum_response_length)
				m_buffer.resize(m_settings.tracker_maximum_response_length);
			else
				m_buffer.resize(m_buffer.size() + http_buffer_size);
		}

		if (m_state == read_status)
		{
			std::vector<char>::iterator end = m_buffer.begin()+m_recv_pos;
			std::vector<char>::iterator newline = std::find(m_buffer.begin(), end, '\n');
			// if we don't have a full line yet, wait.
			if (newline == end) return;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester()) requester().debug_log(std::string(m_buffer.begin(), newline));
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
				fail(-1, error_msg.c_str());
				return;
			}
			line >> m_code;
			std::getline(line, m_server_message);
			m_state = read_header;
		}

		if (m_state == read_header)
		{
			std::vector<char>::iterator end = m_buffer.begin() + m_recv_pos;
			std::vector<char>::iterator newline
				= std::find(m_buffer.begin(), end, '\n');
			std::string line;

			while (newline != end && m_state == read_header)
			{
				line.assign(m_buffer.begin(), newline);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				if (has_requester()) requester().debug_log(line);
#endif

				if (line.substr(0, 16) == "Content-Length: ")
				{
					try
					{
						m_content_length = boost::lexical_cast<int>(
							line.substr(16, line.length() - 17));
					}
					catch(boost::bad_lexical_cast&)
					{
						fail(-1, "invalid content-length in tracker response");
						return;
					}
					if (m_content_length > m_settings.tracker_maximum_response_length)
					{
						fail(-1, "content-length is greater than maximum response length");
						return;
					}

					if (m_content_length < minimum_tracker_response_length && m_code == 200)
					{
						fail(-1, "content-length is smaller than minimum response length");
						return;
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
						fail(-1, error_str.c_str());
						return;
					}
				}
				else if (line.substr(0, 10) == "Location: ")
				{
					m_location.assign(line.begin() + 10, line.end());
				}
				else if (line.substr(0, 7) == "Server: ")
				{
					m_server.assign(line.begin() + 7, line.end());
				}
				else if (line.size() < 3)
				{
					m_state = read_body;
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
					if (has_requester()) requester().debug_log("end of http header");
#endif
					if (m_code >= 300 && m_code < 400)
					{
						if (m_location.empty())
						{
							std::string error_str = "got redirection response (";
							error_str += boost::lexical_cast<std::string>(m_code);
							error_str += ") without 'Location' header";
							fail(-1, error_str.c_str());
							return;
						}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						if (has_requester()) requester().debug_log("Redirecting to \"" + m_location + "\"");
#endif
						tracker_request req = tracker_req();
						std::string::size_type i = m_location.find('?');
						if (i == std::string::npos)
							req.url = m_location;
						else
							req.url.assign(m_location.begin(), m_location.begin() + i);

						m_man.queue_request(m_socket->io_service(), req
							, m_password, m_requester);
						close();
						return;
					}
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
			if (m_recv_pos == m_content_length)
			{
				on_response();
				close();
				return;
			}
		}
		else if (m_recv_pos > m_content_length && m_content_length > 0)
		{
			fail(-1, "invalid tracker response (body > content_length)");
			return;
		}

		assert(m_buffer.size() - m_recv_pos > 0);
		m_socket->async_read_some(asio::buffer(&m_buffer[m_recv_pos]
			, m_buffer.size() - m_recv_pos), bind(&http_tracker_connection::receive
			, self(), _1, _2));
	}
	catch (std::exception& e)
	{
		assert(false);
		fail(-1, e.what());
	};
	
	void http_tracker_connection::on_response()
	{
		// GZIP
		if (m_content_encoding == gzip)
		{
			boost::shared_ptr<request_callback> r = m_requester.lock();
			
			if (!r)
			{
				close();
				return;
			}
			if (inflate_gzip(m_buffer, tracker_request(), r.get(),
				m_settings.tracker_maximum_response_length))
			{
				close();
				return;
			}
		}

		// handle tracker response
		try
		{
			entry e = bdecode(m_buffer.begin(), m_buffer.end());
			parse(e);
		}
		catch (std::exception& e)
		{
			std::string error_str(e.what());
			error_str += ": ";
			error_str.append(m_buffer.begin(), m_buffer.end());
			fail(m_code, error_str.c_str());
		}
		#ifndef NDEBUG
		catch (...)
		{
			assert(false);
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
			std::copy(i->string().begin(), i->string().end(), ret.pid.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			std::fill_n(ret.pid.begin(), 20, 0);
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

	void http_tracker_connection::parse(entry const& e)
	{
		if (!has_requester()) return;

		try
		{
			// parse the response
			try
			{
				entry const& failure = e["failure reason"];

				fail(m_code, failure.string().c_str());
				return;
			}
			catch (type_error const&) {}

			try
			{
				entry const& warning = e["warning message"];
				if (has_requester())
					requester().tracker_warning(warning.string());
			}
			catch(type_error const&) {}
			
			std::vector<peer_entry> peer_list;

			if (tracker_req().kind == tracker_request::scrape_request)
			{
				std::string ih;
				std::copy(tracker_req().info_hash.begin(), tracker_req().info_hash.end()
					, std::back_inserter(ih));
				entry scrape_data = e["files"][ih];
				int complete = scrape_data["complete"].integer();
				int incomplete = scrape_data["incomplete"].integer();
				requester().tracker_response(tracker_request(), peer_list, 0, complete
					, incomplete);
				return;
			}

			int interval = (int)e["interval"].integer();

			if (e["peers"].type() == entry::string_t)
			{
				std::string const& peers = e["peers"].string();
				for (std::string::const_iterator i = peers.begin();
					i != peers.end();)
				{
					if (std::distance(i, peers.end()) < 6) break;

					peer_entry p;
					p.pid.clear();
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
				entry::list_type const& l = e["peers"].list();
				for(entry::list_type::const_iterator i = l.begin(); i != l.end(); ++i)
				{
					peer_entry p = extract_peer_info(*i);
					peer_list.push_back(p);
				}
			}

			// look for optional scrape info
			int complete = -1;
			int incomplete = -1;

			try { complete = e["complete"].integer(); }
			catch(type_error&) {}

			try { incomplete = e["incomplete"].integer(); }
			catch(type_error&) {}
			
			requester().tracker_response(tracker_request(), peer_list, interval, complete
				, incomplete);
		}
		catch(type_error& e)
		{
			requester().tracker_request_error(tracker_request(), m_code, e.what());
		}
		catch(std::runtime_error& e)
		{
			requester().tracker_request_error(tracker_request(), m_code, e.what());
		}
	}

}

