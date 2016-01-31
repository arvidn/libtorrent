/*

Copyright (c) 2016, Arvid Norberg
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

#ifndef BITTORRENT_PEER_HPP
#define BITTORRENT_PEER_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/torrent_info.hpp"
#include "test.hpp" // for EXPORT
#include <boost/function.hpp>

using namespace libtorrent;

struct EXPORT peer_conn
{
	enum peer_mode_t
	{ uploader, downloader, idle };

	peer_conn(io_service& ios
		, boost::function<void(int, char const*, int)> on_msg
		, libtorrent::torrent_info const& ti
		, libtorrent::tcp::endpoint const& ep
		, peer_mode_t mode);

	void start_conn();

	void on_connect(error_code const& ec);
	void on_handshake(char* h, error_code const& ec, size_t bytes_transferred);
	void on_handshake2(error_code const& ec, size_t bytes_transferred);
	void write_have_all();
	void on_have_all_sent(error_code const& ec, size_t bytes_transferred);
	bool write_request();
	void on_req_sent(char* m, error_code const& ec, size_t bytes_transferred);
	void close(char const* fmt, error_code const& ec);
	void work_download();
	void on_msg_length(error_code const& ec, size_t bytes_transferred);
	void on_message(error_code const& ec, size_t bytes_transferred);
	bool verify_piece(int piece, int start, char const* ptr, int size);
	void write_piece(int piece, int start, int length);
	void write_have(int piece);

	void abort();

private:

	tcp::socket s;
	char write_buf_proto[100];
	boost::uint32_t write_buffer[17*1024/4];
	boost::uint32_t buffer[17*1024/4];
	
	peer_mode_t m_mode;
	torrent_info const& m_ti;

	int read_pos;

	boost::function<void(int, char const*, int)> m_on_msg;

	enum state_t
	{
		handshaking,
		sending_request,
		receiving_message
	};
	int state;
	std::vector<int> pieces;
	std::vector<int> suggested_pieces;
	std::vector<int> allowed_fast;
	bool choked;
	int current_piece; // the piece we're currently requesting blocks from
	bool m_current_piece_is_allowed;
	int block;
	int const m_blocks_per_piece;
	int outstanding_requests;
	// if this is true, this connection is a seed
	bool fast_extension;
	int blocks_received;
	int blocks_sent;
	time_point start_time;
	time_point end_time;
	tcp::endpoint endpoint;
	bool restarting;
};

#endif

