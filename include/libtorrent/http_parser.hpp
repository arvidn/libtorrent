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

#ifndef TORRENT_HTTP_PARSER_HPP_INCLUDED
#define TORRENT_HTTP_PARSER_HPP_INCLUDED

#include <map>
#include <string>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/cstdint.hpp>
#include <boost/tuple/tuple.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/size_type.hpp"

namespace libtorrent
{
	
	// return true if the status code is 200, 206, or in the 300-400 range
	bool is_ok_status(int http_status);

	// return true if the status code is a redirect
	bool is_redirect(int http_status);

	class TORRENT_EXPORT http_parser
	{
	public:
		http_parser();
		std::string const& header(char const* key) const
		{
			static std::string empty;
			std::map<std::string, std::string>::const_iterator i
				= m_header.find(key);
			if (i == m_header.end()) return empty;
			return i->second;
		}

		std::string const& protocol() const { return m_protocol; }
		int status_code() const { return m_status_code; }
		std::string const& method() const { return m_method; }
		std::string const& path() const { return m_path; }
		std::string const& message() const { return m_server_message; }
		buffer::const_interval get_body() const;
		bool header_finished() const { return m_state == read_body; }
		bool finished() const { return m_finished; }
		boost::tuple<int, int> incoming(buffer::const_interval recv_buffer
			, bool& error);
		int body_start() const { return m_body_start_pos; }
		size_type content_length() const { return m_content_length; }
		std::pair<size_type, size_type> content_range() const
		{ return std::make_pair(m_range_start, m_range_end); }

		void reset();

		std::map<std::string, std::string> const& headers() const { return m_header; }
		
	private:
		int m_recv_pos;
		int m_status_code;
		std::string m_method;
		std::string m_path;
		std::string m_protocol;
		std::string m_server_message;

		size_type m_content_length;
		size_type m_range_start;
		size_type m_range_end;

		enum { read_status, read_header, read_body, error_state } m_state;

		std::map<std::string, std::string> m_header;
		buffer::const_interval m_recv_buffer;
		int m_body_start_pos;

		bool m_finished;
	};

}

#endif // TORRENT_HTTP_PARSER_HPP_INCLUDED

