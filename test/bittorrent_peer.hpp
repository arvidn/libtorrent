/*

Copyright (c) 2016-2017, 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef BITTORRENT_PEER_HPP
#define BITTORRENT_PEER_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/torrent_info.hpp"
#include "test.hpp" // for EXPORT
#include <functional>
#include <array>

struct EXPORT peer_conn
{
	enum class peer_mode_t
	{ uploader, downloader, idle };

	peer_conn(lt::io_context& ios
		, std::function<void(int, char const*, int)> on_msg
		, lt::torrent_info const& ti
		, lt::tcp::endpoint const& ep
		, peer_mode_t mode);

	void start_conn();

	void on_connect(lt::error_code const& ec);
	void on_handshake(char* h, lt::error_code const& ec, size_t bytes_transferred);
	void on_handshake2(lt::error_code const& ec, size_t bytes_transferred);
	void write_have_all();
	void on_have_all_sent(lt::error_code const& ec, size_t bytes_transferred);
	bool write_request();
	void on_req_sent(char* m, lt::error_code const& ec, size_t bytes_transferred);
	void close(char const* fmt, lt::error_code const& ec);
	void work_download();
	void on_msg_length(lt::error_code const& ec, size_t bytes_transferred);
	void on_message(lt::error_code const& ec, size_t bytes_transferred);
	bool verify_piece(int piece, int start, char const* ptr, int size);
	void write_piece(int piece, int start, int length);
	void write_have(int piece);

	void abort();

private:

	lt::tcp::socket s;
	std::array<char, 100> write_buf_proto;
	std::array<std::uint32_t, 17 * 1024 / 4> write_buffer;
	std::array<char, 17 * 1024> buffer;

	peer_mode_t const m_mode;
	lt::torrent_info const& m_ti;

	int read_pos = 0;

	std::function<void(int, char const*, int)> m_on_msg;

	std::vector<int> pieces;
	std::vector<int> suggested_pieces;
	std::vector<int> allowed_fast;
	bool choked = true;
	int current_piece = -1; // the piece we're currently requesting blocks from
	bool m_current_piece_is_allowed = false;
	int block = 0;
	int const m_blocks_per_piece;
	int outstanding_requests = 0;
	// if this is true, this connection is a seed
	bool fast_extension = false;
	int blocks_received = 0;
	int blocks_sent = 0;
	lt::time_point start_time = lt::clock_type::now();
	lt::time_point end_time;
	lt::tcp::endpoint endpoint;
	bool restarting = false;
};

#endif

