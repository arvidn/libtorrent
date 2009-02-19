/*

Copyright (c) 2008, Arvid Norberg
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

#include <cctype>
#include <algorithm>
#include <stdlib.h>

#include "libtorrent/config.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/assert.hpp"

using namespace libtorrent;

namespace
{
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

	boost::tuple<int, int> http_parser::incoming(
		buffer::const_interval recv_buffer, bool& error)
	{
		TORRENT_ASSERT(recv_buffer.left() >= m_recv_buffer.left());
		boost::tuple<int, int> ret(0, 0);
		int start_pos = m_recv_buffer.left();

		// early exit if there's nothing new in the receive buffer
		if (start_pos == recv_buffer.left()) return ret;
		m_recv_buffer = recv_buffer;

		if (m_state == error_state)
		{
			error = true;
			return ret;
		}

		char const* pos = recv_buffer.begin + m_recv_pos;
		if (m_state == read_status)
		{
			TORRENT_ASSERT(!m_finished);
			char const* newline = std::find(pos, recv_buffer.end, '\n');
			// if we don't have a full line yet, wait.
			if (newline == recv_buffer.end)
			{
				boost::get<1>(ret) += m_recv_buffer.left() - start_pos;
				return ret;
			}

			if (newline == pos)
			{
				m_state = error_state;
				error = true;
				return ret;
			}

			char const* line_end = newline;
			if (pos != line_end && *(line_end - 1) == '\r') --line_end;

			std::istringstream line(std::string(pos, line_end));
			++newline;
			int incoming = (int)std::distance(pos, newline);
			m_recv_pos += incoming;
			boost::get<1>(ret) += newline - (m_recv_buffer.begin + start_pos);
			pos = newline;

			line >> m_protocol;
			if (m_protocol.substr(0, 5) == "HTTP/")
			{
				line >> m_status_code;
				std::getline(line, m_server_message);
			}
			else
			{
				m_method = m_protocol;
				std::transform(m_method.begin(), m_method.end(), m_method.begin(), &to_lower);
				m_protocol.clear();
				line >> m_path >> m_protocol;
				m_status_code = 0;
			}
			m_state = read_header;
			start_pos = pos - recv_buffer.begin;
		}

		if (m_state == read_header)
		{
			TORRENT_ASSERT(!m_finished);
			char const* newline = std::find(pos, recv_buffer.end, '\n');
			std::string line;

			while (newline != recv_buffer.end && m_state == read_header)
			{
				// if the LF character is preceeded by a CR
				// charachter, don't copy it into the line string.
				char const* line_end = newline;
				if (pos != line_end && *(line_end - 1) == '\r') --line_end;
				line.assign(pos, line_end);
				++newline;
				m_recv_pos += newline - pos;
				pos = newline;

				std::string::size_type separator = line.find(':');
				if (separator == std::string::npos)
				{
					// this means we got a blank line,
					// the header is finished and the body
					// starts.
					m_state = read_body;
					m_body_start_pos = m_recv_pos;
					break;
				}

				std::string name = line.substr(0, separator);
				std::transform(name.begin(), name.end(), name.begin(), &to_lower);
				++separator;
				// skip whitespace
				while (separator < line.size()
					&& (line[separator] == ' ' || line[separator] == '\t'))
					++separator;
				std::string value = line.substr(separator, std::string::npos);
				m_header.insert(std::make_pair(name, value));

				if (name == "content-length")
				{
#ifdef TORRENT_WINDOWS
					m_content_length = _atoi64(value.c_str());
#else
					m_content_length = atoll(value.c_str());
#endif
				}
				else if (name == "content-range")
				{
					std::stringstream range_str(value);
					char dummy;
					std::string bytes;
					size_type range_start, range_end;
					// apparently some web servers do not send the "bytes"
					// in their content-range
					if (value.find(' ') != std::string::npos)
						range_str >> bytes;
					range_str >> range_start >> dummy >> range_end;
					if (!range_str || range_end < range_start)
					{
						m_state = error_state;
						error = true;
						return ret;
					}
					// the http range is inclusive
					m_content_length = range_end - range_start + 1;
				}

				TORRENT_ASSERT(m_recv_pos <= (int)recv_buffer.left());
				newline = std::find(pos, recv_buffer.end, '\n');
			}
			boost::get<1>(ret) += newline - (m_recv_buffer.begin + start_pos);
		}

		if (m_state == read_body)
		{
			int incoming = recv_buffer.end - pos;
			if (m_recv_pos - m_body_start_pos + incoming > m_content_length
				&& m_content_length >= 0)
				incoming = m_content_length - m_recv_pos + m_body_start_pos;

			TORRENT_ASSERT(incoming >= 0);
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
		TORRENT_ASSERT(m_state == read_body);
		if (m_content_length >= 0)
			return buffer::const_interval(m_recv_buffer.begin + m_body_start_pos
				, m_recv_buffer.begin + (std::min)(size_type(m_recv_pos)
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
	
}

