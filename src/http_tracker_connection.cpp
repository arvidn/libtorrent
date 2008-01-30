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
#include <algorithm>

#include "libtorrent/config.hpp"
#include "libtorrent/gzip.hpp"

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
#include "libtorrent/instantiate_connection.hpp"

using namespace libtorrent;
using boost::bind;

namespace
{
	enum
	{
		minimum_tracker_response_length = 3,
		http_buffer_size = 2048
	};
}

namespace
{
	char to_lower(char c) { return std::tolower(c); }
}

namespace libtorrent
{
	
	http_tracker_connection::http_tracker_connection(
		io_service& ios
		, connection_queue& cc
		, tracker_manager& man
		, tracker_request const& req
		, std::string const& protocol
		, std::string const& hostname
		, unsigned short port
		, std::string request
		, address bind_infc
		, boost::weak_ptr<request_callback> c
		, session_settings const& stn
		, proxy_settings const& ps
		, std::string const& auth)
		: tracker_connection(man, req, ios, bind_infc, c)
		, m_man(man)
		, m_name_lookup(ios)
		, m_port(port)
		, m_socket(ios)
#ifdef TORRENT_USE_OPENSSL
		, m_ssl(protocol == "https")
#endif
		, m_recv_pos(0)
		, m_buffer(http_buffer_size)
		, m_settings(stn)
		, m_proxy(ps)
		, m_password(auth)
		, m_timed_out(false)
		, m_connection_ticket(-1)
		, m_cc(cc)
	{
		m_send_buffer.assign("GET ");

		// should we use the proxy?
		if (m_proxy.type == proxy_settings::http
			|| m_proxy.type == proxy_settings::http_pw)
		{
			m_send_buffer += "http://";
			m_send_buffer += hostname;
			if (port != 80)
			{
				m_send_buffer += ":";
				m_send_buffer += boost::lexical_cast<std::string>(port);
			}
		}

		if (tracker_req().kind == tracker_request::scrape_request)
		{
			// find and replace "announce" with "scrape"
			// in request

			std::size_t pos = request.find("announce");
			if (pos == std::string::npos)
			{
				fail(-1, ("scrape is not available on url: '"
				+ tracker_req().url +"'").c_str());
				return;
			}
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
					(std::min)(req.num_want, 999));
				m_send_buffer += '&';
			}
			if (m_settings.announce_ip != address() && !url_has_argument(request, "ip"))
			{
				m_send_buffer += "ip=";
				m_send_buffer += m_settings.announce_ip.to_string();
				m_send_buffer += '&';
			}

#ifndef TORRENT_DISABLE_ENCRYPTION
			m_send_buffer += "supportcrypto=1&";
#endif

			if (!url_has_argument(request, "ipv6") && !req.ipv6.empty())
			{
				m_send_buffer += "ipv6=";
				m_send_buffer += req.ipv6;
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
		if (m_proxy.type == proxy_settings::http_pw)
		{
			m_send_buffer += "\r\nProxy-Authorization: Basic ";
			m_send_buffer += base64encode(m_proxy.username + ":" + m_proxy.password);
		}
		if (!auth.empty())
		{
			m_send_buffer += "\r\nAuthorization: Basic ";
			m_send_buffer += base64encode(auth);
		}
		m_send_buffer += "\r\n\r\n";

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)

		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("==> TRACKER_REQUEST [ ih: " + boost::lexical_cast<std::string>(req.info_hash)
				+ " str: " + m_send_buffer + " ]");
		}
#endif

		tcp::resolver::query q(hostname
			, boost::lexical_cast<std::string>(m_port));
		m_name_lookup.async_resolve(q,
			boost::bind(&http_tracker_connection::name_lookup, self(), _1, _2));
		set_timeout(req.event == tracker_request::stopped
			? m_settings.stop_tracker_timeout
			: m_settings.tracker_completion_timeout
			, m_settings.tracker_receive_timeout);
	}

	void http_tracker_connection::on_timeout()
	{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** HTTP_TRACKER [ timed out ]");
#endif
		m_timed_out = true;
		asio::error_code ec;
		m_socket.close(ec);
		m_name_lookup.cancel();
		if (m_connection_ticket > -1) m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;
		fail_timeout();
	}

	void http_tracker_connection::close()
	{
		asio::error_code ec;
		m_socket.close(ec);
		m_name_lookup.cancel();
		if (m_connection_ticket > -1) m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;
		m_timed_out = true;
		tracker_connection::close();
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** HTTP_TRACKER [ close: "
			+ boost::lexical_cast<std::string>(m_man.num_requests()) + " ]");
#endif
	}

	void http_tracker_connection::name_lookup(asio::error_code const& error
		, tcp::resolver::iterator i)
	{
		boost::shared_ptr<request_callback> cb = requester();
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (cb) cb->debug_log("*** HTTP_TRACKER [ tracker name lookup handler called ]");
#endif
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;

		if (error || i == tcp::resolver::iterator())
		{
			fail(-1, error.message().c_str());
			return;
		}
		
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (cb) cb->debug_log("*** HTTP_TRACKER [ name lookup successful ]");
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
			TORRENT_ASSERT(target_address.address().is_v4() != bind_interface().is_v4());
			if (cb)
			{
				std::string tracker_address_type = target_address.address().is_v4() ? "IPv4" : "IPv6";
				std::string bind_address_type = bind_interface().is_v4() ? "IPv4" : "IPv6";
				cb->tracker_warning("the tracker only resolves to an "
					+ tracker_address_type + " address, and you're listening on an "
					+ bind_address_type + " socket. This may prevent you from receiving incoming connections.");
			}
		}
		else
		{
			target_address = *target;
		}

		if (cb) cb->m_tracker_address = target_address;
		asio::io_service& ios = m_name_lookup.io_service();
#ifdef TORRENT_USE_OPENSSL
		if (m_ssl)
		{
			m_socket.instantiate<ssl_stream<socket_type> >(ios);
			ssl_stream<socket_type>& s = m_socket.get<ssl_stream<socket_type> >();
			bool ret = instantiate_connection(ios, m_proxy, s.next_layer());
			TORRENT_ASSERT(ret);
		}
		else
		{
			m_socket.instantiate<socket_type>(ios);
			bool ret = instantiate_connection(ios, m_proxy, m_socket.get<socket_type>());
			TORRENT_ASSERT(ret);
		}
#else
		bool ret = instantiate_connection(ios, m_proxy, m_socket);
		TORRENT_ASSERT(ret);
#endif

		if (m_proxy.type == proxy_settings::http
			|| m_proxy.type == proxy_settings::http_pw)
		{
			// the tracker connection will talk immediately to
			// the proxy, without requiring CONNECT support
			m_socket.get<http_stream>().set_no_connect(true);
		}

		asio::error_code ec;
		m_socket.open(target_address.protocol(), ec);
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}
		m_socket.bind(tcp::endpoint(bind_interface(), 0), ec);
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}
		m_cc.enqueue(bind(&http_tracker_connection::connect, self(), _1, target_address)
			, bind(&http_tracker_connection::on_timeout, self())
			, seconds(m_settings.tracker_receive_timeout));
	}

	void http_tracker_connection::connect(int ticket, tcp::endpoint target_address)
	{
		m_connection_ticket = ticket;
		m_socket.async_connect(target_address, bind(&http_tracker_connection::connected, self(), _1));
	}

	void http_tracker_connection::connected(asio::error_code const& error)
	{
		if (m_connection_ticket > -1) m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;
		if (error)
		{
			fail(-1, error.message().c_str());
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** HTTP_TRACKER [ connection successful ]");
#endif

		restart_read_timeout();
		async_write(m_socket, asio::buffer(m_send_buffer.c_str()
			, m_send_buffer.size()), bind(&http_tracker_connection::sent
			, self(), _1));
	}

	void http_tracker_connection::sent(asio::error_code const& error)
	{
		if (error == asio::error::operation_aborted) return;
		if (m_timed_out) return;
		if (error)
		{
			fail(-1, error.message().c_str());
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** HTTP_TRACKER [ send completed ]");
#endif
		restart_read_timeout();
		TORRENT_ASSERT(m_buffer.size() - m_recv_pos > 0);
		m_socket.async_read_some(asio::buffer(&m_buffer[m_recv_pos]
			, m_buffer.size() - m_recv_pos), bind(&http_tracker_connection::receive
			, self(), _1, _2));
	}
	
	void http_tracker_connection::receive(asio::error_code const& error
		, std::size_t bytes_transferred)
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
		TORRENT_ASSERT(bytes_transferred > 0);
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** HTTP_TRACKER [ reading: "
			+ boost::lexical_cast<std::string>(bytes_transferred) +  " ]");
#endif

		m_recv_pos += bytes_transferred;
		bool e = false;
		m_parser.incoming(buffer::const_interval(&m_buffer[0]
			, &m_buffer[0] + m_recv_pos), e);
		if (e)
		{
			fail(-1, "incorrect http response");
		}

		// if the receive buffer is full, expand it with http_buffer_size
		if ((int)m_buffer.size() == m_recv_pos)
		{
			if ((int)m_buffer.size() >= m_settings.tracker_maximum_response_length)
			{
				fail(200, "too large tracker response");
				return;
			}
			TORRENT_ASSERT(http_buffer_size > 0);
			if ((int)m_buffer.size() + http_buffer_size
				> m_settings.tracker_maximum_response_length)
				m_buffer.resize(m_settings.tracker_maximum_response_length);
			else
				m_buffer.resize(m_buffer.size() + http_buffer_size);
		}

		if (m_parser.header_finished())
		{
			int cl = atoi(m_parser.header("content-length").c_str());
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

		TORRENT_ASSERT(m_buffer.size() - m_recv_pos > 0);
		m_socket.async_read_some(asio::buffer(&m_buffer[m_recv_pos]
			, m_buffer.size() - m_recv_pos), bind(&http_tracker_connection::receive
			, self(), _1, _2));
	}
	
	void http_tracker_connection::on_response()
	{
		if (!m_parser.header_finished())
		{
			fail(-1, "premature end of file");
			return;
		}
	
		std::string location = m_parser.header("location");

		boost::shared_ptr<request_callback> cb = requester();
		
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
			if (cb) cb->debug_log("*** HTTP_TRACKER [ redirecting to: " + location + "]");
#endif
			if (cb) cb->tracker_warning("Redirecting to \"" + location + "\"");
			tracker_request req = tracker_req();

			req.url = location;

			m_man.queue_request(m_name_lookup.get_io_service(), m_cc, req
				, m_password, bind_interface(), m_requester);
			close();
			return;
		}

		if (m_parser.status_code() != 200)
		{
			fail(m_parser.status_code(), m_parser.message().c_str());
			return;
		}
	
		buffer::const_interval buf(&m_buffer[0] + m_parser.body_start(), &m_buffer[0] + m_recv_pos);

		std::string content_encoding = m_parser.header("content-encoding");

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (cb) cb->debug_log("*** HTTP_TRACKER [ content-encoding: " + content_encoding + "]");
#endif

		if (content_encoding == "gzip" || content_encoding == "x-gzip")
		{
			if (!cb)
			{
				close();
				return;
			}
			std::vector<char> buffer;
			std::string error;
			if (inflate_gzip(&m_buffer[0] + m_parser.body_start(), m_buffer.size(), buffer
				, m_settings.tracker_maximum_response_length, error))
			{
				cb->tracker_request_error(tracker_req(), 200, error);
				close();
				return;
			}
			m_buffer.swap(buffer);
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
		entry e = bdecode(buf.begin, buf.end);

		if (e.type() != entry::undefined_t)
		{
			parse(e);
		}
		else
		{
			std::string error_str("invalid bencoding of tracker response: \"");
			for (char const* i = buf.begin, *end(buf.end); i != end; ++i)
			{
				if (std::isprint(*i)) error_str += *i;
				else error_str += "0x" + boost::lexical_cast<std::string>((unsigned int)*i) + " ";
			}
			error_str += "\"";
			fail(m_parser.status_code(), error_str.c_str());
		}
		close();
	}

	bool http_tracker_connection::extract_peer_info(const entry& info, peer_entry& ret)
	{
		// extract peer id (if any)
		if (info.type() != entry::dictionary_t)
		{
			fail(-1, "invalid response from tracker (invalid peer entry)");
			return false;
		}
		entry const* i = info.find_key("peer id");
		if (i != 0)
		{
			if (i->type() != entry::string_t || i->string().length() != 20)
			{
				fail(-1, "invalid response from tracker (invalid peer id)");
				return false;
			}
			std::copy(i->string().begin(), i->string().end(), ret.pid.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			std::fill_n(ret.pid.begin(), 20, 0);
		}

		// extract ip
		i = info.find_key("ip");
		if (i == 0 || i->type() != entry::string_t)
		{
			fail(-1, "invalid response from tracker");
			return false;
		}
		ret.ip = i->string();

		// extract port
		i = info.find_key("port");
		if (i == 0 || i->type() != entry::int_t)
		{
			fail(-1, "invalid response from tracker");
			return false;
		}
		ret.port = (unsigned short)i->integer();

		return true;
	}

	void http_tracker_connection::parse(entry const& e)
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (!cb) return;

		// parse the response
		entry const* failure = e.find_key("failure reason");
		if (failure && failure->type() == entry::string_t)
		{
			fail(m_parser.status_code(), failure->string().c_str());
			return;
		}

		entry const* warning = e.find_key("warning message");
		if (warning && warning->type() == entry::string_t)
		{
			cb->tracker_warning(warning->string());
		}

		std::vector<peer_entry> peer_list;

		if (tracker_req().kind == tracker_request::scrape_request)
		{
			std::string ih;
			std::copy(tracker_req().info_hash.begin(), tracker_req().info_hash.end()
				, std::back_inserter(ih));

			entry const* files = e.find_key("files");
			if (files == 0 || files->type() != entry::dictionary_t)
			{
				fail(-1, "invalid or missing 'files' entry in scrape response");
				return;
			}

			entry const* scrape_data = e.find_key(ih.c_str());
			if (scrape_data == 0 || scrape_data->type() != entry::dictionary_t)
			{
				fail(-1, "missing or invalid info-hash entry in scrape response");
				return;
			}
			entry const* complete = scrape_data->find_key("complete");
			entry const* incomplete = scrape_data->find_key("incomplete");
			entry const* downloaded = scrape_data->find_key("downloaded");
			if (complete == 0 || incomplete == 0 || downloaded == 0
				|| complete->type() != entry::int_t
				|| incomplete->type() != entry::int_t
				|| downloaded->type() != entry::int_t)
			{
				fail(-1, "missing 'complete' or 'incomplete' entries in scrape response");
				return;
			}
			cb->tracker_scrape_response(tracker_req(), complete->integer()
				, incomplete->integer(), downloaded->integer());
			return;
		}

		entry const* interval = e.find_key("interval");
		if (interval == 0 || interval->type() != entry::int_t)
		{
			fail(-1, "missing or invalid 'interval' entry in tracker response");
			return;
		}

		entry const* peers_ent = e.find_key("peers");
		if (peers_ent == 0)
		{
			fail(-1, "missing 'peers' entry in tracker response");
			return;
		}

		if (peers_ent->type() == entry::string_t)
		{
			std::string const& peers = peers_ent->string();
			for (std::string::const_iterator i = peers.begin();
				i != peers.end();)
			{
				if (std::distance(i, peers.end()) < 6) break;

				peer_entry p;
				p.pid.clear();
				p.ip = detail::read_v4_address(i).to_string();
				p.port = detail::read_uint16(i);
				peer_list.push_back(p);
			}
		}
		else if (peers_ent->type() == entry::list_t)
		{
			entry::list_type const& l = peers_ent->list();
			for(entry::list_type::const_iterator i = l.begin(); i != l.end(); ++i)
			{
				peer_entry p;
				if (!extract_peer_info(*i, p)) return;
				peer_list.push_back(p);
			}
		}
		else
		{
			fail(-1, "invalid 'peers' entry in tracker response");
			return;
		}

		entry const* ipv6_peers = e.find_key("peers6");
		if (ipv6_peers && ipv6_peers->type() == entry::string_t)
		{
			std::string const& peers = ipv6_peers->string();
			for (std::string::const_iterator i = peers.begin();
				i != peers.end();)
			{
				if (std::distance(i, peers.end()) < 18) break;

				peer_entry p;
				p.pid.clear();
				p.ip = detail::read_v6_address(i).to_string();
				p.port = detail::read_uint16(i);
				peer_list.push_back(p);
			}
		}

		// look for optional scrape info
		int complete = -1;
		int incomplete = -1;

		entry const* complete_ent = e.find_key("complete");
		if (complete_ent && complete_ent->type() == entry::int_t)
			complete = complete_ent->integer();

		entry const* incomplete_ent = e.find_key("incomplete");
		if (incomplete_ent && incomplete_ent->type() == entry::int_t)
			incomplete = incomplete_ent->integer();

		cb->tracker_response(tracker_req(), peer_list, interval->integer(), complete
			, incomplete);
	}

}

