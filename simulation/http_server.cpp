/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "simulator/simulator.hpp"
#include "http_server.hpp"

#include <functional>
#include <cstdio> // for printf

using namespace sim::asio;
using namespace sim::asio::ip;
using namespace std::placeholders;

using boost::system::error_code;

namespace sim {
	using namespace aux;

	namespace {
		char const* find(char const* hay, int const hsize, char const* needle, int const nsize)
		{
			for (int i = 0; i < hsize - nsize + 1; ++i)
			{
				if (memcmp(hay + i, needle, nsize) == 0) return hay + i;
			}
			return nullptr;
		}
	}

	std::string trim(std::string s)
	{
		if (s.empty()) return s;

		int start = 0;
		int end = int(s.size());
		while (strchr(" \r\n\t", s[start]) != NULL && start < end)
		{
			++start;
		}

		while (strchr(" \r\n\t", s[end - 1]) != NULL && end > start)
		{
			--end;
		}
		return s.substr(start, end - start);
	}

	std::string lower_case(std::string s)
	{
		std::string ret;
		std::transform(s.begin(), s.end(), std::back_inserter(ret), [](char c) {
			return static_cast<char>(tolower(c));
		});
		return ret;
	}

	std::string normalize(const std::string& s)
	{
		std::vector<std::string> elements;
		char const* start = s.c_str();
		if (*start == '/') ++start;
		char const* slash = strchr(start, '/');
		while (slash != NULL)
		{
			std::string element(start, slash - start);
			if (element != "..")
			{
				elements.push_back(element);
			}
			else if (!elements.empty())
			{
				elements.erase(elements.end() - 1);
			}
			start = slash + 1;
			slash = strchr(start, '/');
		}
		elements.push_back(start);

		std::string ret;
		for (auto const& e : elements)
		{
			ret += '/';
			ret += e;
		}

		return ret;
	}

	// TODO: extra_header should be a std::vector
	std::string send_response(
		int code, char const* status_message, int len, char const** extra_header)
	{
		std::string ret = "HTTP/1.1 " + std::to_string(code) + " " + status_message + "\r\n";

		ret += "content-length: " + std::to_string(len) + "\r\n";

		if (extra_header)
		{
			ret += extra_header[0];
			ret += extra_header[1];
			ret += extra_header[2];
			ret += extra_header[3];
		}
		ret += "\r\n";

		return ret;
	}

	http_server::http_server(io_context& ios, unsigned short listen_port, int flags)
		: m_ios(ios)
		, m_listen_socket(ios)
		, m_connection(ios)
		, m_bytes_used(0)
		, m_close(false)
		, m_flags(flags)
	{
		address local_ip = ios.get_ips().front();
		if (local_ip.is_v4())
		{
			m_listen_socket.open(tcp::v4());
			m_listen_socket.bind(tcp::endpoint(address_v4::any(), listen_port));
		}
		else
		{
			m_listen_socket.open(tcp::v6());
			m_listen_socket.bind(tcp::endpoint(address_v6::any(), listen_port));
		}
		m_listen_socket.listen();

		m_listen_socket.async_accept(
			m_connection, m_ep, std::bind(&http_server::on_accept, this, _1));
	}

	void http_server::on_accept(error_code const& ec)
	{
		if (ec)
		{
			std::printf("http_server::on_accept: (%d) %s\n", ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		++m_accepted_connections;

		std::printf("http_server accepted connection from: %s : %d\n",
			m_ep.address().to_string().c_str(),
			m_ep.port());

		read();
	}

	void http_server::register_handler(std::string const& path, handler_t h)
	{
		m_handlers[path] = std::move(h);
	}

	void http_server::register_content(
		std::string const& path, std::int64_t const size, generator_t gen)
	{
		m_handlers[path] = [gen, size](
							   std::string, std::string, std::map<std::string, std::string>& hdr) {
			std::int64_t start = 0;
			std::int64_t end = size;

			auto it = hdr.find("range");
			bool const range_req = it != hdr.end();
			if (range_req)
			{
				std::string range = it->second;
				// skip "bytes "
				range = range.substr(range.find_first_of('=') + 1);
				start = std::stoll(range.substr(0, range.find('-')));
				end = std::stoll(range.substr(range.find_first_of('-') + 1)) + 1;
			}

			std::string header = "Content-Range: bytes " + std::to_string(start) + "-"
				+ std::to_string(end - 1) + "/" + std::to_string(end - start) + "\r\n";
			char const* extra_headers[4] = {header.c_str(), "", "", ""};

			return sim::send_response(range_req ? 206 : 200,
					   range_req ? "Partial Content" : "OK",
					   int(end - start),
					   range_req ? extra_headers : nullptr)
				+ gen(start, end - start);
		};
	}

	void http_server::register_redirect(std::string const& path, std::string const& target)
	{
		m_handlers[path] = [target](std::string, std::string, std::map<std::string, std::string>&) {
			std::string header = "Location: " + target + "\r\n";
			char const* extra_headers[4] = {header.c_str(), "", "", ""};
			return sim::send_response(301, "Moved Permanently", 0, extra_headers);
		};
	}

	void http_server::register_stall_handler(std::string const& path)
	{
		m_stall_handlers.insert(path);
	}

	void http_server::read()
	{
		if (m_bytes_used >= int(m_recv_buffer.size()) / 2)
		{
			m_recv_buffer.resize((std::max)(500, m_bytes_used * 2));
		}
		assert(int(m_recv_buffer.size()) > m_bytes_used);
		m_connection.async_read_some(
			asio::buffer(&m_recv_buffer[m_bytes_used], m_recv_buffer.size() - m_bytes_used),
			std::bind(&http_server::on_read, this, _1, _2));
	}

	http_request parse_request(char const* start, int len)
	{
		http_request ret;

		char const* const end_of_request = start + len;
		char const* const space = find(start, len, " ", 1);
		if (space == nullptr)
		{
			std::printf(
				"http_server: failed to parse request:\n%s\n", std::string(start, len).c_str());
			throw std::runtime_error("parse failed");
		}

		char const* const space2 = find(space + 1, int(len - (space - start + 1)), " ", 1);
		if (space2 == nullptr)
		{
			std::printf(
				"http_server: failed to parse request:\n%s\n", std::string(start, len).c_str());
			throw std::runtime_error("parse failed");
		}
		ret.method.assign(start, space);
		ret.req.assign(space + 1, space2);
		if (ret.method != "CONNECT")
		{
			ret.path.assign(normalize(ret.req.substr(0, ret.req.find_first_of('?'))));
		}
		else
		{
			ret.path.assign(ret.req);
		}
		std::printf(
			"parse_request: %s %s [%s]\n", ret.method.c_str(), ret.path.c_str(), ret.req.c_str());

		char const* header = find(space2, int(len - (space2 - start)), "\r\n", 2);
		while (header != end_of_request - 4)
		{
			if (header == nullptr)
			{
				std::printf(
					"http_server: failed to parse request:\n%s\n", std::string(start, len).c_str());
				throw std::runtime_error("parse failed");
			}
			char const* const next = find(header + 2, int(len - (header + 2 - start)), "\r\n", 2);
			char const* const value =
				static_cast<char const*>(memchr(header, ':', len - (header - start)));
			if (value == nullptr || next == nullptr || value > next)
			{
				std::printf(
					"http_server: failed to parse request:\n%s\n", std::string(start, len).c_str());
				throw std::runtime_error("parse failed");
			}

			ret.headers[lower_case(trim(std::string(header, value)))] =
				trim(std::string(value + 1, next));

			header = next;
		}
		return ret;
	}

	int find_request_len(char const* buf, int const len)
	{
		char const* end_of_request = find(buf, len, "\r\n\r\n", 4);
		if (end_of_request == nullptr) return -1;
		return int(end_of_request - buf + 4);
	}

	void http_server::on_read(error_code const& ec, size_t bytes_transferred)
	try
	{
		if (ec)
		{
			std::printf("http_server::on_read: (%d) %s\n", ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		m_bytes_used += int(bytes_transferred);

		int const req_len = find_request_len(m_recv_buffer.data(), m_bytes_used);
		if (req_len < 0)
		{
			read();
			return;
		}

		http_request req = parse_request(m_recv_buffer.data(), req_len);

		m_recv_buffer.erase(m_recv_buffer.begin(), m_recv_buffer.begin() + req_len);
		m_bytes_used -= req_len;

		auto it = m_handlers.find(req.path);
		if (it == m_handlers.end())
		{
			if (m_stall_handlers.find(req.path) != m_stall_handlers.end())
			{
				return;
			}
			// no handler found, 404
			m_send_buffer = send_response(404, "Not Found");
		}
		else
		{
			m_send_buffer = it->second(req.method, req.req, req.headers);
		}

		// decide whether to close the connection after this response, and signal
		// it to the client appropriately.
		bool close;
		if (m_flags & http_1_0)
		{
			// an HTTP/1.0 server closes after every response and does not use the
			// Connection header (that is an HTTP/1.1 mechanism). Downgrade the
			// status line so the client detects this from the protocol version.
			close = true;
			auto const ver = m_send_buffer.find("HTTP/1.1");
			if (ver != std::string::npos) m_send_buffer.replace(ver, 8, "HTTP/1.0");
		}
		else
		{
			// close if the client asked us to, or if this server is not
			// configured for keep-alive. When we do, advertise it with a
			// "Connection: close" response header so the client knows not to
			// reuse the socket (rather than discovering it via a failed write).
			close = lower_case(req.headers["connection"]) == "close" || !(m_flags & keep_alive);
			if (close)
			{
				auto const status_end = m_send_buffer.find("\r\n");
				if (status_end != std::string::npos)
				{
					assert(m_send_buffer.find("Connection:") == std::string::npos);
					m_send_buffer.insert(status_end + 2, "Connection: close\r\n");
				}
			}
		}

		async_write(m_connection,
			asio::buffer(m_send_buffer.data(), m_send_buffer.size()),
			std::bind(&http_server::on_write, this, _1, _2, close));
	}
	catch (std::exception& e)
	{
		std::printf("http_server::on_read() failed: %s\n", e.what());
		close_connection();
	}

	void http_server::on_write(error_code const& ec,
		size_t /* bytes_transferred */
		,
		bool close)
	{
		if (ec)
		{
			std::printf("http_server::on_write: (%d) %s\n", ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		if (!close)
		{
			// try to read another request out of the buffer
			post(m_ios, std::bind(&http_server::on_read, this, error_code(), 0));
		}
		else
		{
			close_connection();
		}
	}

	void http_server::stop()
	{
		m_close = true;
		m_listen_socket.close();
	}

	void http_server::close_connection()
	{
		m_recv_buffer.clear();
		m_bytes_used = 0;

		error_code err;
		m_connection.close(err);
		if (err)
		{
			std::printf("http_server::close: failed to close connection (%d) %s\n",
				err.value(),
				err.message().c_str());
			return;
		}

		if (m_close) return;

		// now we can accept another connection
		m_listen_socket.async_accept(
			m_connection, m_ep, std::bind(&http_server::on_accept, this, _1));
	}
}
