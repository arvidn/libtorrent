/*

Copyright (c) 2008-2017, 2019, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2017, Pavel Pimenov
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

#ifndef TORRENT_HTTP_PARSER_HPP_INCLUDED
#define TORRENT_HTTP_PARSER_HPP_INCLUDED

#include <map>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>
#include <tuple>

#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/time.hpp" // for seconds32
#include "libtorrent/optional.hpp"
#include "libtorrent/aux_/strview_less.hpp"

namespace libtorrent {

	// return true if the status code is 200, 206, or in the 300-400 range
	TORRENT_EXTRA_EXPORT bool is_ok_status(int http_status);

	// return true if the status code is a redirect
	TORRENT_EXTRA_EXPORT bool is_redirect(int http_status);

	TORRENT_EXTRA_EXPORT std::string resolve_redirect_location(std::string referrer
		, std::string location);

	class TORRENT_EXTRA_EXPORT http_parser
	{
	public:
		enum flags_t { dont_parse_chunks = 1 };
		explicit http_parser(int flags = 0);
		~http_parser();
		std::string const& header(string_view key) const;
		boost::optional<seconds32> header_duration(string_view key) const;
		std::string const& protocol() const { return m_protocol; }
		int status_code() const { return m_status_code; }
		std::string const& method() const { return m_method; }
		std::string const& path() const { return m_path; }
		std::string const& message() const { return m_server_message; }
		span<char const> get_body() const;
		bool header_finished() const { return m_state == read_body; }
		bool finished() const { return m_finished; }
		std::tuple<int, int> incoming(span<char const> recv_buffer
			, bool& error);
		int body_start() const { return m_body_start_pos; }
		std::int64_t content_length() const { return m_content_length; }
		std::pair<std::int64_t, std::int64_t> content_range() const
		{ return std::make_pair(m_range_start, m_range_end); }

		// returns true if this response is using chunked encoding.
		// in this case the body is split up into chunks. You need
		// to call parse_chunk_header() for each chunk, starting with
		// the start of the body.
		bool chunked_encoding() const { return m_chunked_encoding; }

		// removes the chunk headers from the supplied buffer. The buffer
		// must be the stream received from the http server this parser
		// instanced parsed. It will use the internal chunk list to determine
		// where the chunks are in the buffer. It returns the new length of
		// the buffer
		span<char> collapse_chunk_headers(span<char> buffer) const;

		// returns false if the buffer doesn't contain a complete
		// chunk header. In this case, call the function again with
		// a bigger buffer once more bytes have been received.
		// chunk_size is filled in with the number of bytes in the
		// chunk that follows. 0 means the response terminated. In
		// this case there might be additional headers in the parser
		// object.
		// header_size is filled in with the number of bytes the header
		// itself was. Skip this number of bytes to get to the actual
		// chunk data.
		// if the function returns false, the chunk size and header
		// size may still have been modified, but their values are
		// undefined
		bool parse_chunk_header(span<char const> buf
			, std::int64_t* chunk_size, int* header_size);

		// reset the whole state and start over
		void reset();

		bool connection_close() const { return m_connection_close; }

		std::multimap<std::string, std::string, aux::strview_less> const& headers() const { return m_header; }
		std::vector<std::pair<std::int64_t, std::int64_t>> const& chunks() const { return m_chunked_ranges; }

	private:
		std::int64_t m_recv_pos = 0;
		std::string m_method;
		std::string m_path;
		std::string m_protocol;
		std::string m_server_message;

		std::int64_t m_content_length = -1;
		std::int64_t m_range_start = -1;
		std::int64_t m_range_end = -1;

		std::multimap<std::string, std::string, aux::strview_less> m_header;
		span<char const> m_recv_buffer;
		// contains offsets of the first and one-past-end of
		// each chunked range in the response
		std::vector<std::pair<std::int64_t, std::int64_t>> m_chunked_ranges;

		// while reading a chunk, this is the offset where the
		// current chunk will end (it refers to the first character
		// in the chunk tail header or the next chunk header)
		std::int64_t m_cur_chunk_end = -1;

		int m_status_code = -1;

		// the sum of all chunk headers read so far
		int m_chunk_header_size = 0;

		int m_partial_chunk_header = 0;

		// controls some behaviors of the parser
		int m_flags;

		int m_body_start_pos = 0;

		enum { read_status, read_header, read_body, error_state } m_state = read_status;

		// this is true if the server is HTTP/1.0 or
		// if it sent "connection: close"
		bool m_connection_close = false;
		bool m_chunked_encoding = false;
		bool m_finished = false;
	};

}

#endif // TORRENT_HTTP_PARSER_HPP_INCLUDED
