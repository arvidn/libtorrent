/*

Copyright (c) 2008-2018, Arvid Norberg
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

#include <cctype>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cinttypes>

#include "libtorrent/config.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/hex.hpp" // for hex_to_int
#include "libtorrent/assert.hpp"
#include "libtorrent/parse_url.hpp" // for parse_url_components
#include "libtorrent/string_util.hpp" // for ensure_trailing_slash, to_lower
#include "libtorrent/aux_/escape_string.hpp" // for read_until
#include "libtorrent/time.hpp" // for seconds32
#include "libtorrent/aux_/numeric_cast.hpp"

namespace libtorrent {

	bool is_ok_status(int http_status)
	{
		return http_status == 206 // partial content
			|| http_status == 200 // OK
			|| (http_status >= 300 // redirect
				&& http_status < 400);
	}

	bool is_redirect(int http_status)
	{
		return http_status >= 300
			&& http_status < 400;
	}

	std::string resolve_redirect_location(std::string referrer
		, std::string location)
	{
		if (location.empty()) return referrer;

		error_code ec;
		using std::ignore;
		std::tie(ignore, ignore, ignore, ignore, ignore)
			= parse_url_components(location, ec);

		// if location is a full URL, just return it
		if (!ec) return location;

		// otherwise it's likely to be just the path, or a relative path
		std::string url = referrer;

		if (location[0] == '/')
		{
			// it's an absolute path. replace the path component of
			// referrer with location.

			// first skip the url scheme of the referer
			std::size_t i = url.find("://");

			// if the referrer doesn't appear to have a proper URL scheme
			// just return the location verbatim (and probably fail)
			if (i == std::string::npos)
				return location;

			// then skip the hostname and port, it's fine for this to fail, in
			// case the referrer doesn't have a path component, it's just the
			// url-scheme and hostname, in which case we just append the location
			i = url.find_first_of('/', i + 3);
			if (i != std::string::npos)
				url.resize(i);

			url += location;
		}
		else
		{
			// some web servers send out relative paths
			// in the location header.

			// remove the leaf filename
			// first skip the url scheme of the referer
			std::size_t start = url.find("://");

			// the referrer is not a valid full URL
			if (start == std::string::npos)
				return location;

			std::size_t end = url.find_last_of('/');
			// if the / we find is part of the scheme, there is no / in the path
			// component or hostname.
			if (end <= start + 2) end = std::string::npos;

			// if this fails, the referrer is just url-scheme and hostname. We can
			// just append the location to it.
			if (end != std::string::npos)
				url.resize(end);

			// however, we may still need to insert a '/' in case neither side
			// has one. We know the location doesn't start with a / already.
			// so, if the referrer doesn't end with one, add it.
			ensure_trailing_slash(url);
			url += location;
		}
		return url;
	}

	std::string const& http_parser::header(string_view const key) const
	{
		static std::string const empty;
		// TODO: remove to_string() if we're in C++14
		auto const i = m_header.find(key.to_string());
		if (i == m_header.end()) return empty;
		return i->second;
	}

	boost::optional<seconds32> http_parser::header_duration(string_view const key) const
	{
		// TODO: remove to_string() if we're in C++14
		auto const i = m_header.find(key.to_string());
		if (i == m_header.end()) return boost::none;
		auto const val = std::atol(i->second.c_str());
		if (val <= 0) return boost::none;
		return seconds32(val);
	}

	http_parser::~http_parser() = default;

	http_parser::http_parser(int const flags) : m_flags(flags) {}

	std::tuple<int, int> http_parser::incoming(
		span<char const> recv_buffer, bool& error)
	{
		TORRENT_ASSERT(recv_buffer.size() >= m_recv_buffer.size());
		std::tuple<int, int> ret(0, 0);
		std::ptrdiff_t start_pos = m_recv_buffer.size();

		// early exit if there's nothing new in the receive buffer
		if (start_pos == recv_buffer.size()) return ret;
		m_recv_buffer = recv_buffer;

		if (m_state == error_state)
		{
			error = true;
			return ret;
		}

		char const* pos = recv_buffer.data() + m_recv_pos;

restart_response:

		if (m_state == read_status)
		{
			TORRENT_ASSERT(!m_finished);
			TORRENT_ASSERT(pos <= recv_buffer.end());
			char const* newline = std::find(pos, recv_buffer.end(), '\n');
			// if we don't have a full line yet, wait.
			if (newline == recv_buffer.end())
			{
				std::get<1>(ret) += int(m_recv_buffer.size() - start_pos);
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

			char const* line = pos;
			++newline;
			TORRENT_ASSERT(newline >= pos);
			int incoming = int(newline - pos);
			m_recv_pos += incoming;
			std::get<1>(ret) += int(newline - (m_recv_buffer.data() + start_pos));
			pos = newline;

			m_protocol = read_until(line, ' ', line_end);
			if (m_protocol.substr(0, 5) == "HTTP/")
			{
				m_status_code = atoi(read_until(line, ' ', line_end).c_str());
				m_server_message = read_until(line, '\r', line_end);

				// HTTP 1.0 always closes the connection after
				// each request
				if (m_protocol == "HTTP/1.0") m_connection_close = true;
			}
			else
			{
				m_method = m_protocol;
				std::transform(m_method.begin(), m_method.end(), m_method.begin(), &to_lower);
				// the content length is assumed to be 0 for requests
				m_content_length = 0;
				m_protocol.clear();
				m_path = read_until(line, ' ', line_end);
				m_protocol = read_until(line, ' ', line_end);
				m_status_code = 0;
			}
			m_state = read_header;
			start_pos = pos - recv_buffer.data();
		}

		if (m_state == read_header)
		{
			TORRENT_ASSERT(!m_finished);
			TORRENT_ASSERT(pos <= recv_buffer.end());
			char const* newline = std::find(pos, recv_buffer.end(), '\n');
			std::string line;

			while (newline != recv_buffer.end() && m_state == read_header)
			{
				// if the LF character is preceded by a CR
				// character, don't copy it into the line string.
				char const* line_end = newline;
				if (pos != line_end && *(line_end - 1) == '\r') --line_end;
				line.assign(pos, line_end);
				++newline;
				m_recv_pos += newline - pos;
				pos = newline;

				std::string::size_type separator = line.find(':');
				if (separator == std::string::npos)
				{
					if (m_status_code == 100)
					{
						// for 100 Continue, we need to read another response header
						// before reading the body
						m_state = read_status;
						goto restart_response;
					}
					// this means we got a blank line,
					// the header is finished and the body
					// starts.
					m_state = read_body;
					// if this is a request (not a response)
					// we're done once we reach the end of the headers
//					if (!m_method.empty()) m_finished = true;
					// the HTTP header should always be < 2 GB
					TORRENT_ASSERT(m_recv_pos < std::numeric_limits<int>::max());
					m_body_start_pos = int(m_recv_pos);
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
					m_content_length = std::strtoll(value.c_str(), nullptr, 10);
					if (m_content_length < 0
						|| m_content_length == std::numeric_limits<std::int64_t>::max())
					{
						m_state = error_state;
						error = true;
						return ret;
					}
				}
				else if (name == "connection")
				{
					m_connection_close = string_begins_no_case("close", value.c_str());
				}
				else if (name == "content-range")
				{
					bool success = true;
					char const* ptr = value.c_str();

					// apparently some web servers do not send the "bytes"
					// in their content-range. Don't treat it as an error
					// if we can't find it, just assume the byte counters
					// start immediately
					if (string_begins_no_case("bytes ", ptr)) ptr += 6;
					char* end;
					m_range_start = std::strtoll(ptr, &end, 10);
					if (m_range_start < 0
						|| m_range_start == std::numeric_limits<std::int64_t>::max())
					{
						m_state = error_state;
						error = true;
						return ret;
					}
					if (end == ptr) success = false;
					else if (*end != '-') success = false;
					else
					{
						ptr = end + 1;
						m_range_end = std::strtoll(ptr, &end, 10);
						if (m_range_end < 0
							|| m_range_end == std::numeric_limits<std::int64_t>::max())
						{
							m_state = error_state;
							error = true;
							return ret;
						}
						if (end == ptr) success = false;
					}

					if (!success || m_range_end < m_range_start)
					{
						m_state = error_state;
						error = true;
						return ret;
					}
					// the http range is inclusive
					m_content_length = m_range_end - m_range_start + 1;
				}
				else if (name == "transfer-encoding")
				{
					m_chunked_encoding = string_begins_no_case("chunked", value.c_str());
				}

				TORRENT_ASSERT(m_recv_pos <= int(recv_buffer.size()));
				TORRENT_ASSERT(pos <= recv_buffer.end());
				newline = std::find(pos, recv_buffer.end(), '\n');
			}
			std::get<1>(ret) += int(newline - (m_recv_buffer.data() + start_pos));
		}

		if (m_state == read_body)
		{
			int incoming = int(recv_buffer.end() - pos);

			if (m_chunked_encoding && (m_flags & dont_parse_chunks) == 0)
			{
				if (m_cur_chunk_end == -1)
					m_cur_chunk_end = m_body_start_pos;

				while (m_cur_chunk_end <= m_recv_pos + incoming && !m_finished && incoming > 0)
				{
					std::int64_t payload = m_cur_chunk_end - m_recv_pos;
					if (payload > 0)
					{
						TORRENT_ASSERT(payload < std::numeric_limits<int>::max());
						m_recv_pos += payload;
						std::get<0>(ret) += int(payload);
						incoming -= int(payload);
					}
					auto const buf = span<char const>(recv_buffer)
						.subspan(aux::numeric_cast<std::ptrdiff_t>(m_cur_chunk_end));
					std::int64_t chunk_size;
					int header_size;
					if (parse_chunk_header(buf, &chunk_size, &header_size))
					{
						if (chunk_size < 0
							|| chunk_size > std::numeric_limits<std::int64_t>::max() - m_cur_chunk_end - header_size)
						{
							m_state = error_state;
							error = true;
							return ret;
						}
						if (chunk_size > 0)
						{
							std::pair<std::int64_t, std::int64_t> chunk_range(m_cur_chunk_end + header_size
								, m_cur_chunk_end + header_size + chunk_size);
							m_chunked_ranges.push_back(chunk_range);
						}
						m_cur_chunk_end += header_size + chunk_size;
						if (chunk_size == 0)
						{
							m_finished = true;
						}
						header_size -= m_partial_chunk_header;
						m_partial_chunk_header = 0;
//						std::fprintf(stderr, "parse_chunk_header(%d, -> %" PRId64 ", -> %d) -> %d\n"
//							"  incoming = %d\n  m_recv_pos = %d\n  m_cur_chunk_end = %" PRId64 "\n"
//							"  content-length = %d\n"
//							, int(buf.size()), chunk_size, header_size, 1, incoming, int(m_recv_pos)
//							, m_cur_chunk_end, int(m_content_length));
					}
					else
					{
						m_partial_chunk_header += incoming;
						header_size = incoming;

//						std::fprintf(stderr, "parse_chunk_header(%d, -> %" PRId64 ", -> %d) -> %d\n"
//							"  incoming = %d\n  m_recv_pos = %d\n  m_cur_chunk_end = %" PRId64 "\n"
//							"  content-length = %d\n"
//							, int(buf.size()), chunk_size, header_size, 0, incoming, int(m_recv_pos)
//							, m_cur_chunk_end, int(m_content_length));
					}
					m_chunk_header_size += header_size;
					m_recv_pos += header_size;
					std::get<1>(ret) += header_size;
					incoming -= header_size;
				}
				if (incoming > 0)
				{
					m_recv_pos += incoming;
					std::get<0>(ret) += incoming;
//					incoming = 0;
				}
			}
			else
			{
				std::int64_t payload_received = m_recv_pos - m_body_start_pos + incoming;
				if (payload_received > m_content_length
					&& m_content_length >= 0)
				{
					TORRENT_ASSERT(m_content_length - m_recv_pos + m_body_start_pos
						< std::numeric_limits<int>::max());
					incoming = int(m_content_length - m_recv_pos + m_body_start_pos);
				}

				TORRENT_ASSERT(incoming >= 0);
				m_recv_pos += incoming;
				std::get<0>(ret) += incoming;
			}

			if (m_content_length >= 0
				&& !m_chunked_encoding
				&& m_recv_pos - m_body_start_pos >= m_content_length)
			{
				m_finished = true;
			}
		}
		return ret;
	}

	// this function signals error by assigning a negative value to "chunk_size"
	// the return value indicates whether enough data is available in "buf" to
	// completely parse the chunk header. Returning false means we need more data
	bool http_parser::parse_chunk_header(span<char const> buf
		, std::int64_t* chunk_size, int* header_size)
	{
		char const* pos = buf.data();

		// ignore one optional new-line. This is since each chunk
		// is terminated by a newline. we're likely to see one
		// before the actual header.

		if (pos < buf.end() && pos[0] == '\r') ++pos;
		if (pos < buf.end() && pos[0] == '\n') ++pos;
		if (pos == buf.end()) return false;

		TORRENT_ASSERT(pos <= buf.end());
		char const* newline = std::find(pos, buf.end(), '\n');
		if (newline == buf.end()) return false;
		++newline;

		// the chunk header is a single line, a hex length of the
		// chunk followed by an optional semi-colon with a comment
		// in case the length is 0, the stream is terminated and
		// there are extra tail headers, which is terminated by an
		// empty line

		*header_size = int(newline - buf.data());

		// first, read the chunk length
		std::int64_t size = 0;
		for (char const* i = pos; i != newline; ++i)
		{
			if (*i == '\r') continue;
			if (*i == '\n') continue;
			if (*i == ';') break;
			int const digit = aux::hex_to_int(*i);
			if (digit < 0)
			{
				*chunk_size = -1;
				return true;
			}
			if (size >= std::numeric_limits<std::int64_t>::max() / 16)
			{
				*chunk_size = -1;
				return true;
			}
			size *= 16;
			size += digit;
		}
		*chunk_size = size;

		if (*chunk_size != 0)
		{
			// the newline is at least 1 byte, and the length-prefix is at least 1
			// byte
			TORRENT_ASSERT(newline - buf.data() >= 2);
			return true;
		}

		// this is the terminator of the stream. Also read headers
		std::map<std::string, std::string> tail_headers;
		pos = newline;
		newline = std::find(pos, buf.end(), '\n');

		std::string line;
		while (newline != buf.end())
		{
			// if the LF character is preceded by a CR
			// character, don't copy it into the line string.
			char const* line_end = newline;
			if (pos != line_end && *(line_end - 1) == '\r') --line_end;
			line.assign(pos, line_end);
			++newline;
			pos = newline;

			std::string::size_type separator = line.find(':');
			if (separator == std::string::npos)
			{
				// this means we got a blank line,
				// the header is finished and the body
				// starts.
				*header_size = int(newline - buf.data());

				// the newline alone is two bytes
				TORRENT_ASSERT(newline - buf.data() > 2);

				// we were successful in parsing the headers.
				// add them to the headers in the parser
				for (auto const& p : tail_headers)
					m_header.insert(p);

				return true;
			}

			std::string name = line.substr(0, separator);
			std::transform(name.begin(), name.end(), name.begin(), &to_lower);
			++separator;
			// skip whitespace
			while (separator < line.size()
				&& (line[separator] == ' ' || line[separator] == '\t'))
				++separator;
			std::string value = line.substr(separator, std::string::npos);
			tail_headers.insert(std::make_pair(name, value));
//			std::fprintf(stderr, "tail_header: %s: %s\n", name.c_str(), value.c_str());

			newline = std::find(pos, buf.end(), '\n');
		}
		return false;
	}

	span<char const> http_parser::get_body() const
	{
		TORRENT_ASSERT(m_state == read_body);
		std::int64_t const received = m_recv_pos - m_body_start_pos;

		std::int64_t const body_length = m_chunked_encoding && !m_chunked_ranges.empty()
			? std::min(m_chunked_ranges.back().second - m_body_start_pos, received)
			: m_content_length < 0 ? received : std::min(m_content_length, received);

		return m_recv_buffer.subspan(m_body_start_pos, aux::numeric_cast<std::ptrdiff_t>(body_length));
	}

	void http_parser::reset()
	{
		m_method.clear();
		m_recv_pos = 0;
		m_body_start_pos = 0;
		m_status_code = -1;
		m_content_length = -1;
		m_range_start = -1;
		m_range_end = -1;
		m_finished = false;
		m_state = read_status;
		m_recv_buffer = span<char const>();
		m_header.clear();
		m_chunked_encoding = false;
		m_chunked_ranges.clear();
		m_cur_chunk_end = -1;
		m_chunk_header_size = 0;
		m_partial_chunk_header = 0;
	}

	span<char> http_parser::collapse_chunk_headers(span<char> buffer) const
	{
		if (!chunked_encoding()) return buffer;

		// go through all chunks and compact them
		// since we're bottled, and the buffer is our after all
		// it's OK to mutate it
		char* write_ptr = buffer.data();
		// the offsets in the array are from the start of the
		// buffer, not start of the body, so subtract the size
		// of the HTTP header from them
		int const offset = body_start();
		for (auto const& i : chunks())
		{
			auto const chunk_start = i.first;
			auto const chunk_end = i.second;
			TORRENT_ASSERT(i.second - i.first < std::numeric_limits<int>::max());
			TORRENT_ASSERT(chunk_end - offset <= buffer.size());
			span<char> chunk = buffer.subspan(
				aux::numeric_cast<std::ptrdiff_t>(chunk_start - offset)
				, aux::numeric_cast<std::ptrdiff_t>(chunk_end - chunk_start));
#if defined __GNUC__ && __GNUC__ >= 7
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
			std::memmove(write_ptr, chunk.data(), std::size_t(chunk.size()));
#if defined __GNUC__ && __GNUC__ >= 7
#pragma GCC diagnostic pop
#endif
			write_ptr += chunk.size();
		}
		return buffer.first(write_ptr - buffer.data());
	}
}
