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
#include <algorithm>

#include "libtorrent/config.hpp"
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

namespace
{
	bool url_has_argument(std::string const& url, std::string argument)
	{
		size_t i = url.find('?');
		if (i == std::string::npos) return false;

		argument += '=';

		if (url.compare(i + 1, argument.size(), argument) == 0) return true;
		argument.insert(0, "&");
		return url.find(argument, i)
			!= std::string::npos;
	}

	char to_lower(char c) { return std::tolower(c); }
}

namespace libtorrent
{
	http_parser::http_parser()
		: m_recv_pos(0)
		, m_status_code(-1)
		, m_content_length(-1)
		, m_state(read_status)
		, m_recv_buffer(0, 0)
		, m_body_start_pos(0)
		, m_finished(false)
	{}

	boost::tuple<int, int> http_parser::incoming(buffer::const_interval recv_buffer)
	{
		assert(recv_buffer.left() >= m_recv_buffer.left());
		boost::tuple<int, int> ret(0, 0);

		// early exit if there's nothing new in the receive buffer
		if (recv_buffer.left() == m_recv_buffer.left()) return ret;
		m_recv_buffer = recv_buffer;

		char const* pos = recv_buffer.begin + m_recv_pos;
		if (m_state == read_status)
		{
			assert(!m_finished);
			char const* newline = std::find(pos, recv_buffer.end, '\n');
			// if we don't have a full line yet, wait.
			if (newline == recv_buffer.end) return ret;

			if (newline == pos)
				throw std::runtime_error("unexpected newline in HTTP response");

			char const* line_end = newline;
			if (pos != line_end && *(line_end - 1) == '\r') --line_end;

			std::istringstream line(std::string(pos, line_end));
			++newline;
			int incoming = (int)std::distance(pos, newline);
			m_recv_pos += incoming;
			boost::get<1>(ret) += incoming;
			pos = newline;

			line >> m_protocol;
			if (m_protocol.substr(0, 5) != "HTTP/")
			{
				throw std::runtime_error("unknown protocol in HTTP response: "
					+ m_protocol + " line: " + std::string(pos, newline));
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
				// if the LF character is preceeded by a CR
				// charachter, don't copy it into the line string.
				char const* line_end = newline;
				if (pos != line_end && *(line_end - 1) == '\r') --line_end;
				line.assign(pos, line_end);
				m_recv_pos += newline - pos;
				boost::get<1>(ret) += newline - pos;
				pos = newline;

				std::string::size_type separator = line.find(": ");
				if (separator == std::string::npos)
				{
					// this means we got a blank line,
					// the header is finished and the body
					// starts.
					++pos;
					++m_recv_pos;
					boost::get<1>(ret) += 1;
					
					m_state = read_body;
					m_body_start_pos = m_recv_pos;
					break;
				}

				std::string name = line.substr(0, separator);
				std::transform(name.begin(), name.end(), name.begin(), &to_lower);
				std::string value = line.substr(separator + 2, std::string::npos);
				m_header.insert(std::make_pair(name, value));

				if (name == "content-length")
				{
					try
					{
						m_content_length = boost::lexical_cast<int>(value);
					}
					catch(boost::bad_lexical_cast&) {}
				}
				else if (name == "content-range")
				{
					std::stringstream range_str(value);
					char dummy;
					std::string bytes;
					size_type range_start, range_end;
					range_str >> bytes >> range_start >> dummy >> range_end;
					if (!range_str || range_end < range_start)
					{
						throw std::runtime_error("invalid content-range in HTTP response: " + range_str.str());
					}
					// the http range is inclusive
					m_content_length = range_end - range_start + 1;
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
	
	buffer::const_interval http_parser::get_body() const
	{
		assert(m_state == read_body);
		if (m_content_length >= 0)
			return buffer::const_interval(m_recv_buffer.begin + m_body_start_pos
				, m_recv_buffer.begin + std::min(m_recv_pos
				, m_body_start_pos + m_content_length));
		else
			return buffer::const_interval(m_recv_buffer.begin + m_body_start_pos
				, m_recv_buffer.begin + m_recv_pos);
	}
	
	void http_parser::reset()
	{
		m_recv_pos = 0;
		m_body_start_pos = 0;
		m_status_code = -1;
		m_content_length = -1;
		m_finished = false;
		m_state = read_status;
		m_recv_buffer.begin = 0;
		m_recv_buffer.end = 0;
		m_header.clear();
	}
	
	http_tracker_connection::http_tracker_connection(
		asio::strand& str
		, tracker_manager& man
		, tracker_request const& req
		, std::string const& hostname
		, unsigned short port
		, std::string request
		, address bind_infc
		, boost::weak_ptr<request_callback> c
		, session_settings const& stn
		, std::string const& auth)
		: tracker_connection(man, req, str, bind_infc, c)
		, m_man(man)
		, m_strand(str)
		, m_name_lookup(m_strand.io_service())
		, m_port(port)
		, m_recv_pos(0)
		, m_buffer(http_buffer_size)
		, m_settings(stn)
		, m_password(auth)
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
		size_t arguments_start = request.find('?');
		if (arguments_start != std::string::npos)
			m_send_buffer += "&";
		else
			m_send_buffer += "?";

		if (!url_has_argument(request, "info_hash"))
		{
			m_send_buffer += "info_hash=";
			m_send_buffer += escape_string(
				reinterpret_cast<const char*>(req.info_hash.begin()), 20);
			m_send_buffer += '&';
		}

		if (tracker_req().kind == tracker_request::announce_request)
		{
			if (!url_has_argument(request, "peer_id"))
			{
				m_send_buffer += "peer_id=";
				m_send_buffer += escape_string(
					reinterpret_cast<const char*>(req.pid.begin()), 20);
				m_send_buffer += '&';
			}

			if (!url_has_argument(request, "port"))
			{
				m_send_buffer += "port=";
				m_send_buffer += boost::lexical_cast<std::string>(req.listen_port);
				m_send_buffer += '&';
			}

			if (!url_has_argument(request, "uploaded"))
			{
				m_send_buffer += "uploaded=";
				m_send_buffer += boost::lexical_cast<std::string>(req.uploaded);
				m_send_buffer += '&';
			}

			if (!url_has_argument(request, "downloaded"))
			{
				m_send_buffer += "downloaded=";
				m_send_buffer += boost::lexical_cast<std::string>(req.downloaded);
				m_send_buffer += '&';
			}

			if (!url_has_argument(request, "left"))
			{
				m_send_buffer += "left=";
				m_send_buffer += boost::lexical_cast<std::string>(req.left);
				m_send_buffer += '&';
			}

			if (req.event != tracker_request::none)
			{
				if (!url_has_argument(request, "event"))
				{
					const char* event_string[] = {"completed", "started", "stopped"};
					m_send_buffer += "event=";
					m_send_buffer += event_string[req.event - 1];
					m_send_buffer += '&';
				}
			}
			if (!url_has_argument(request, "key"))
			{
				m_send_buffer += "key=";
				std::stringstream key_string;
				key_string << std::hex << req.key;
				m_send_buffer += key_string.str();
				m_send_buffer += '&';
			}

			if (!url_has_argument(request, "compact"))
			{
				m_send_buffer += "compact=1&";
			}
			if (!url_has_argument(request, "numwant"))
			{
				m_send_buffer += "numwant=";
				m_send_buffer += boost::lexical_cast<std::string>(
					std::min(req.num_want, 999));
				m_send_buffer += '&';
			}

			// extension that tells the tracker that
			// we don't need any peer_id's in the response
			if (!url_has_argument(request, "no_peer_id"))
			{
				m_send_buffer += "no_peer_id=1";
			}
			else
			{
				// remove the trailing '&'
				m_send_buffer.resize(m_send_buffer.size() - 1);
			}
		}

		m_send_buffer += " HTTP/1.0\r\nAccept-Encoding: gzip\r\n"
			"User-Agent: ";
		m_send_buffer += m_settings.user_agent;
		m_send_buffer += "\r\n"
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
			requester().debug_log("info_hash: "
				+ boost::lexical_cast<std::string>(req.info_hash));
			requester().debug_log("name lookup: " + *connect_to_host);
		}
#endif

		tcp::resolver::query q(*connect_to_host
			, boost::lexical_cast<std::string>(m_port));
		m_name_lookup.async_resolve(q, m_strand.wrap(
			boost::bind(&http_tracker_connection::name_lookup, self(), _1, _2)));
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

	void http_tracker_connection::name_lookup(asio::error_code const& error
		, tcp::resolver::iterator i) try
	{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("tracker name lookup handler called");
#endif
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;

		if (error || i == tcp::resolver::iterator())
		{
			fail(-1, error.message().c_str());
			return;
		}
		
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("tracker name lookup successful");
#endif
		restart_read_timeout();

		// look for an address that has the same kind as the one
		// we're listening on. To make sure the tracker get our
		// correct listening address.
		tcp::resolver::iterator target = i;
		tcp::resolver::iterator end;
		tcp::endpoint target_address = *i;
		for (; target != end && target->endpoint().address().is_v4()
			!= bind_interface().is_v4(); ++target);
		if (target == end)
		{
			assert(target_address.address().is_v4() != bind_interface().is_v4());
			if (has_requester())
			{
				std::string tracker_address_type = target_address.address().is_v4() ? "IPv4" : "IPv6";
				std::string bind_address_type = bind_interface().is_v4() ? "IPv4" : "IPv6";
				requester().tracker_warning("the tracker only resolves to an "
					+ tracker_address_type + " address, and you're listening on an "
					+ bind_address_type + " socket. This may prevent you from receiving incoming connections.");
			}
		}
		else
		{
			target_address = *target;
		}

		if (has_requester()) requester().m_tracker_address = target_address;
		m_socket.reset(new stream_socket(m_name_lookup.io_service()));
		m_socket->open(target_address.protocol());
		m_socket->bind(tcp::endpoint(bind_interface(), 0));
		m_socket->async_connect(target_address, bind(&http_tracker_connection::connected, self(), _1));
	}
	catch (std::exception& e)
	{
		assert(false);
		fail(-1, e.what());
	};

	void http_tracker_connection::connected(asio::error_code const& error) try
	{
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;
		if (error)
		{
			fail(-1, error.message().c_str());
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

	void http_tracker_connection::sent(asio::error_code const& error) try
	{
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;
		if (error)
		{
			fail(-1, error.message().c_str());
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

	
	void http_tracker_connection::receive(asio::error_code const& error
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

			fail(-1, error.message().c_str());
			return;
		}

		restart_read_timeout();
		assert(bytes_transferred > 0);
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("tracker connection reading "
			+ boost::lexical_cast<std::string>(bytes_transferred));
#endif

		m_recv_pos += bytes_transferred;
		m_parser.incoming(buffer::const_interval(&m_buffer[0]
			, &m_buffer[0] + m_recv_pos));

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

		if (m_parser.header_finished())
		{
			int cl = m_parser.header<int>("content-length");
			if (cl > m_settings.tracker_maximum_response_length)
			{
				fail(-1, "content-length is greater than maximum response length");
				return;
			}

			if (cl > 0 && cl < minimum_tracker_response_length && m_parser.status_code() == 200)
			{
				fail(-1, "content-length is smaller than minimum response length");
				return;
			}
		}

		if (m_parser.finished())
		{
			on_response();
			close();
			return;
		}

		assert(m_buffer.size() - m_recv_pos > 0);
		m_socket->async_read_some(asio::buffer(&m_buffer[m_recv_pos]
			, m_buffer.size() - m_recv_pos), bind(&http_tracker_connection::receive
			, self(), _1, _2));
	}
	catch (std::exception& e)
	{
		fail(-1, e.what());
	};
	
	void http_tracker_connection::on_response()
	{
		if (!m_parser.header_finished())
		{
			fail(-1, "premature end of file");
			return;
		}
	
		std::string location = m_parser.header<std::string>("location");
		
		if (m_parser.status_code() >= 300 && m_parser.status_code() < 400)
		{
			if (location.empty())
			{
				std::string error_str = "got redirection response (";
				error_str += boost::lexical_cast<std::string>(m_parser.status_code());
				error_str += ") without 'Location' header";
				fail(-1, error_str.c_str());
				return;
			}
			
			// if the protocol isn't specified, assume http
			if (location.compare(0, 7, "http://") != 0
				&& location.compare(0, 6, "udp://") != 0)
			{
				location.insert(0, "http://");
			}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester()) requester().debug_log("Redirecting to \"" + location + "\"");
#endif
			if (has_requester()) requester().tracker_warning("Redirecting to \"" + location + "\"");
			tracker_request req = tracker_req();

			req.url = location;

			m_man.queue_request(m_strand, req
				, m_password, bind_interface(), m_requester);
			close();
			return;
		}
	
		buffer::const_interval buf(&m_buffer[0] + m_parser.body_start(), &m_buffer[0] + m_recv_pos);

		std::string content_encoding = m_parser.header<std::string>("content-encoding");

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("content-encoding: \"" + content_encoding + "\"");
#endif

		if (content_encoding == "gzip" || content_encoding == "x-gzip")
		{
			boost::shared_ptr<request_callback> r = m_requester.lock();
			
			if (!r)
			{
				close();
				return;
			}
			m_buffer.erase(m_buffer.begin(), m_buffer.begin() + m_parser.body_start());
			if (inflate_gzip(m_buffer, tracker_request(), r.get(),
				m_settings.tracker_maximum_response_length))
			{
				close();
				return;
			}
			buf.begin = &m_buffer[0];
			buf.end = &m_buffer[0] + m_buffer.size();
		}
		else if (!content_encoding.empty())
		{
			std::string error_str = "unknown content encoding in response: \"";
			error_str += content_encoding;
			error_str += "\"";
			fail(-1, error_str.c_str());
			return;
		}

		// handle tracker response
		try
		{
			entry e = bdecode(buf.begin, buf.end);
			parse(e);
		}
		catch (std::exception& e)
		{
			std::string error_str(e.what());
			error_str += ": \"";
			for (char const* i = buf.begin, *end(buf.end); i != end; ++i)
			{
				if (std::isprint(*i)) error_str += *i;
				else error_str += "0x" + boost::lexical_cast<std::string>((unsigned int)*i) + " ";
			}
			error_str += "\"";
			fail(m_parser.status_code(), error_str.c_str());
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

				fail(m_parser.status_code(), failure.string().c_str());
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
			requester().tracker_request_error(tracker_request(), m_parser.status_code(), e.what());
		}
		catch(std::runtime_error& e)
		{
			requester().tracker_request_error(tracker_request(), m_parser.status_code(), e.what());
		}
	}

}

