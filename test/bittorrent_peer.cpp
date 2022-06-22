/*

Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
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

#include "libtorrent/socket.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/assert.hpp"
#include "bittorrent_peer.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/random.hpp"

#include <cstdlib>
#include <functional>
#include <utility>

using namespace lt;
using namespace std::placeholders;

peer_conn::peer_conn(io_context& ios
	, std::function<void(int, char const*, int)> on_msg
	, torrent_info const& ti
	, tcp::endpoint const& ep
	, peer_mode_t const mode)
	: s(ios)
	, m_mode(mode)
	, m_ti(ti)
	, m_on_msg(std::move(on_msg))
	, m_blocks_per_piece((m_ti.piece_length() + 0x3fff) / 0x4000)
	, endpoint(ep)
{
	pieces.reserve(static_cast<std::size_t>(m_ti.num_pieces()));
	start_conn();
}

void peer_conn::start_conn()
{
	restarting = false;
	s.async_connect(endpoint, std::bind(&peer_conn::on_connect, this, _1));
}

void peer_conn::on_connect(error_code const& ec)
{
	if (ec)
	{
		close("ERROR CONNECT: %s", ec);
		return;
	}

	char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
		"                    " // space for info-hash
		"aaaaaaaaaaaaaaaaaaaa" // peer-id
		"\0\0\0\x01\x02"; // interested
	char* h = static_cast<char*>(malloc(sizeof(handshake)));
	memcpy(h, handshake, sizeof(handshake));
	std::memcpy(h + 28, m_ti.info_hash().data(), 20);
	lt::aux::random_bytes({h + 48, 20});
	// for seeds, don't send the interested message
	boost::asio::async_write(s, boost::asio::buffer(h, (sizeof(handshake) - 1)
		- (m_mode == peer_mode_t::uploader ? 5 : 0))
		, std::bind(&peer_conn::on_handshake, this, h, _1, _2));
}

void peer_conn::on_handshake(char* h, error_code const& ec, size_t)
{
	free(h);
	if (ec)
	{
		close("ERROR SEND HANDSHAKE: %s", ec);
		return;
	}

	// read handshake
	boost::asio::async_read(s, boost::asio::buffer(buffer.data(), 68)
		, std::bind(&peer_conn::on_handshake2, this, _1, _2));
}

void peer_conn::on_handshake2(error_code const& ec, size_t)
{
	if (ec)
	{
		close("ERROR READ HANDSHAKE: %s", ec);
		return;
	}

	// buffer is the full 68 byte handshake
	// look at the extension bits

	fast_extension = (buffer[27] & 4) != 0;

	if (m_mode == peer_mode_t::uploader)
	{
		write_have_all();
	}
	else
	{
		work_download();
	}
}

void peer_conn::write_have_all()
{
	using namespace lt::aux;

	if (fast_extension)
	{
		char* ptr = write_buf_proto.data();
		// have_all
		write_uint32(1, ptr);
		write_uint8(0xe, ptr);
		// unchoke
		write_uint32(1, ptr);
		write_uint8(1, ptr);
		boost::asio::async_write(s, boost::asio::buffer(write_buf_proto.data()
			, static_cast<std::size_t>(ptr - write_buf_proto.data()))
			, std::bind(&peer_conn::on_have_all_sent, this, _1, _2));
	}
	else
	{
		// bitfield
		int len = (m_ti.num_pieces() + 7) / 8;
		char* ptr = buffer.data();
		write_uint32(len + 1, ptr);
		write_uint8(5, ptr);
		std::fill(ptr, ptr + len, 255);
		ptr += len;
		// unchoke
		write_uint32(1, ptr);
		write_uint8(1, ptr);
		boost::asio::async_write(s, boost::asio::buffer(buffer.data()
			, static_cast<std::size_t>(len + 10))
			, std::bind(&peer_conn::on_have_all_sent, this, _1, _2));
	}
}

void peer_conn::on_have_all_sent(error_code const& ec, size_t)
{
	if (ec)
	{
		close("ERROR SEND HAVE ALL: %s", ec);
		return;
	}

	// read message
	boost::asio::async_read(s, boost::asio::buffer(buffer.data(), 4)
		, std::bind(&peer_conn::on_msg_length, this, _1, _2));
}

bool peer_conn::write_request()
{
	using namespace lt::aux;

	// if we're choked (and there are no allowed-fast pieces left)
	if (choked && allowed_fast.empty() && !m_current_piece_is_allowed) return false;

	// if there are no pieces left to request
	if (pieces.empty() && suggested_pieces.empty() && current_piece == -1) return false;

	if (current_piece == -1)
	{
		// pick a new piece
		if (choked && allowed_fast.size() > 0)
		{
			current_piece = allowed_fast.front();
			allowed_fast.erase(allowed_fast.begin());
			m_current_piece_is_allowed = true;
		}
		else if (suggested_pieces.size() > 0)
		{
			current_piece = suggested_pieces.front();
			suggested_pieces.erase(suggested_pieces.begin());
			m_current_piece_is_allowed = false;
		}
		else if (pieces.size() > 0)
		{
			current_piece = pieces.front();
			pieces.erase(pieces.begin());
			m_current_piece_is_allowed = false;
		}
		else
		{
			TORRENT_ASSERT_FAIL();
		}
	}
	char msg[] = "\0\0\0\xd\x06"
		"    " // piece
		"    " // offset
		"    "; // length
	char* m = static_cast<char*>(malloc(sizeof(msg)));
	std::copy(msg, msg + sizeof(msg), m);
	char* ptr = m + 5;
	write_uint32(current_piece, ptr);
	write_uint32(block * 16 * 1024, ptr);
	write_uint32(16 * 1024, ptr);
	boost::asio::async_write(s, boost::asio::buffer(m, sizeof(msg) - 1)
		, std::bind(&peer_conn::on_req_sent, this, m, _1, _2));

	++outstanding_requests;
	++block;
	if (block == m_blocks_per_piece)
	{
		block = 0;
		current_piece = -1;
		m_current_piece_is_allowed = false;
	}
	return true;
}

void peer_conn::on_req_sent(char* m, error_code const& ec, size_t)
{
	free(m);
	if (ec)
	{
		close("ERROR SEND REQUEST: %s", ec);
		return;
	}

	work_download();
}

void peer_conn::close(char const* fmt, error_code const& ec)
{
	end_time = clock_type::now();
	char tmp[1024];

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	std::snprintf(tmp, sizeof(tmp), fmt, ec.message().c_str());
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	int time = int(total_milliseconds(end_time - start_time));
	if (time == 0) time = 1;
	double const up = (std::int64_t(blocks_sent) * 0x4000) / time / 1000.0;
	double const down = (std::int64_t(blocks_received) * 0x4000) / time / 1000.0;
	error_code e;

	char ep_str[200];
	address const& addr = s.local_endpoint(e).address();
	if (addr.is_v6())
		std::snprintf(ep_str, sizeof(ep_str), "[%s]:%d", addr.to_string().c_str()
			, s.local_endpoint(e).port());
	else
		std::snprintf(ep_str, sizeof(ep_str), "%s:%d", addr.to_string().c_str()
			, s.local_endpoint(e).port());
	std::printf("%s ep: %s sent: %d received: %d duration: %d ms up: %.1fMB/s down: %.1fMB/s\n"
		, tmp, ep_str, blocks_sent, blocks_received, time, up, down);
}

void peer_conn::work_download()
{
	if (pieces.empty()
		&& suggested_pieces.empty()
		&& current_piece == -1
		&& outstanding_requests == 0
		&& blocks_received >= m_ti.num_pieces() * m_blocks_per_piece)
	{
		close("COMPLETED DOWNLOAD", error_code());
		return;
	}

	// send requests
	if (outstanding_requests < 40)
	{
		if (write_request()) return;
	}

	// read message
	boost::asio::async_read(s, boost::asio::buffer(buffer.data(), 4)
		, std::bind(&peer_conn::on_msg_length, this, _1, _2));
}

void peer_conn::on_msg_length(error_code const& ec, size_t)
{
	using namespace lt::aux;

	if ((ec == boost::asio::error::operation_aborted || ec == boost::asio::error::bad_descriptor)
		&& restarting)
	{
		start_conn();
		return;
	}

	if (ec)
	{
		close("ERROR RECEIVE MESSAGE PREFIX: %s", ec);
		return;
	}
	char* ptr = buffer.data();
	unsigned int length = read_uint32(ptr);
	if (length > buffer.size())
	{
		std::printf("len: %u\n", length);
		close("ERROR RECEIVE MESSAGE PREFIX: packet too big", error_code());
		return;
	}
	if (length == 0)
	{
		// keep-alive messate. read another length prefix
		boost::asio::async_read(s, boost::asio::buffer(buffer.data(), 4)
			, std::bind(&peer_conn::on_msg_length, this, _1, _2));
	}
	else
	{
		boost::asio::async_read(s, boost::asio::buffer(buffer.data(), length)
			, std::bind(&peer_conn::on_message, this, _1, _2));
	}
}

void peer_conn::on_message(error_code const& ec, size_t bytes_transferred)
{
	using namespace lt::aux;

	if ((ec == boost::asio::error::operation_aborted || ec == boost::asio::error::bad_descriptor)
		&& restarting)
	{
		start_conn();
		return;
	}

	if (ec)
	{
		close("ERROR RECEIVE MESSAGE: %s", ec);
		return;
	}
	char* ptr = buffer.data();
	int msg = read_uint8(ptr);

	m_on_msg(msg, ptr, int(bytes_transferred));

	switch (m_mode)
	{
	case peer_mode_t::uploader:
		if (msg == 6)
		{
			if (bytes_transferred != 13)
			{
				close("REQUEST packet has invalid size", error_code());
				return;
			}
			int piece = aux::read_int32(ptr);
			int start = aux::read_int32(ptr);
			int length = aux::read_int32(ptr);
			write_piece(piece, start, length);
		}
		else if (msg == 3) // not-interested
		{
			close("DONE", error_code());
			return;
		}
		else
		{
			// read another message
			boost::asio::async_read(s, boost::asio::buffer(buffer.data(), 4)
				, std::bind(&peer_conn::on_msg_length, this, _1, _2));
		}
		break;
	case peer_mode_t::downloader:
		if (msg == 0xe) // have_all
		{
			// build a list of all pieces and request them all!
			pieces.clear();
			for (int i = 0; i < int(pieces.size()); ++i)
				pieces.push_back(i);
			aux::random_shuffle(pieces);
		}
		else if (msg == 4) // have
		{
			int piece = aux::read_int32(ptr);
			if (pieces.empty()) pieces.push_back(piece);
			else pieces.insert(pieces.begin() + static_cast<int>(lt::random(static_cast<std::uint32_t>(pieces.size()))), piece);
		}
		else if (msg == 5) // bitfield
		{
			pieces.reserve(static_cast<std::size_t>(m_ti.num_pieces()));
			int piece = 0;
			for (int i = 0; i < int(bytes_transferred); ++i)
			{
				int mask = 0x80;
				for (int k = 0; k < 8; ++k)
				{
					if (piece > m_ti.num_pieces()) break;
					if (*ptr & mask) pieces.push_back(piece);
					mask >>= 1;
					++piece;
				}
				++ptr;
			}
			aux::random_shuffle(pieces);
		}
		else if (msg == 7) // piece
		{
/*
			if (verify_downloads)
			{
				int piece = read_uint32(ptr);
				int start = read_uint32(ptr);
				int size = bytes_transferred - 9;
				verify_piece(piece, start, ptr, size);
			}
*/
			++blocks_received;
			--outstanding_requests;
			int const piece = aux::read_int32(ptr);
			int const start = aux::read_int32(ptr);

			if ((start + int(bytes_transferred)) / 0x4000 == m_blocks_per_piece)
			{
				write_have(piece);
				return;
			}
		}
		else if (msg == 13) // suggest
		{
			int piece = aux::read_int32(ptr);
			std::vector<int>::iterator i = std::find(pieces.begin(), pieces.end(), piece);
			if (i != pieces.end())
			{
				pieces.erase(i);
				suggested_pieces.push_back(piece);
			}
		}
		else if (msg == 16) // reject request
		{
			int piece = aux::read_int32(ptr);
			int start = aux::read_int32(ptr);
			int length = aux::read_int32(ptr);

			// put it back!
			if (current_piece != piece)
			{
				if (pieces.empty() || pieces.back() != piece)
					pieces.push_back(piece);
			}
			else
			{
				block = std::min(start / 0x4000, block);
				if (block == 0)
				{
					pieces.push_back(current_piece);
					current_piece = -1;
					m_current_piece_is_allowed = false;
				}
			}
			--outstanding_requests;
			std::printf("REJECT: [ piece: %d start: %d length: %d ]\n", piece, start, length);
		}
		else if (msg == 0) // choke
		{
			choked = true;
		}
		else if (msg == 1) // unchoke
		{
			choked = false;
		}
		else if (msg == 17) // allowed_fast
		{
			int piece = aux::read_int32(ptr);
			std::vector<int>::iterator i = std::find(pieces.begin(), pieces.end(), piece);
			if (i != pieces.end())
			{
				pieces.erase(i);
				allowed_fast.push_back(piece);
			}
		}
		work_download();
		break;
	case peer_mode_t::idle:
		// read another message
		boost::asio::async_read(s, boost::asio::buffer(buffer.data(), 4)
			, std::bind(&peer_conn::on_msg_length, this, _1, _2));
		break;
	}
}
/*
bool peer_conn::verify_piece(int piece, int start, char const* ptr, int size)
{
	std::uint32_t* buf = (std::uint32_t*)ptr;
	std::uint32_t fill = (piece << 8) | ((start / 0x4000) & 0xff);
	for (int i = 0; i < size / 4; ++i)
	{
		if (buf[i] != fill)
		{
			std::printf("received invalid block. piece %d block %d\n", piece, start / 0x4000);
			exit(1);
			return false;
		}
	}
	return true;
}
*/
void peer_conn::write_piece(int piece, int start, int length)
{
	using namespace lt::aux;

//	generate_block(write_buffer, piece, start, length);

	char* ptr = write_buf_proto.data();
	write_uint32(9 + length, ptr);
	TORRENT_ASSERT(length == 0x4000);
	write_uint8(7, ptr);
	write_uint32(piece, ptr);
	write_uint32(start, ptr);
	std::array<boost::asio::const_buffer, 2> vec;
	vec[0] = boost::asio::buffer(write_buf_proto.data(), static_cast<std::size_t>(ptr - write_buf_proto.data()));
	vec[1] = boost::asio::buffer(write_buffer.data(), static_cast<std::size_t>(length));
	boost::asio::async_write(s, vec, std::bind(&peer_conn::on_have_all_sent, this, _1, _2));
	++blocks_sent;
}

void peer_conn::write_have(int piece)
{
	using namespace lt::aux;

	char* ptr = write_buf_proto.data();
	write_uint32(5, ptr);
	write_uint8(4, ptr);
	write_uint32(piece, ptr);
	boost::asio::async_write(s, boost::asio::buffer(write_buf_proto.data(), 9)
		, std::bind(&peer_conn::on_have_all_sent, this, _1, _2));
}

void peer_conn::abort()
{
	error_code ec;
	s.close(ec);
}

