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

#include "libtorrent/peer_id.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/thread.hpp"
#include <cstring>
#include <boost/bind.hpp>
#include <iostream>

using namespace libtorrent;
using namespace libtorrent::detail; // for write_* and read_*

struct peer_conn
{
	peer_conn(io_service& ios, int num_pieces, int blocks_pp, tcp::endpoint const& ep
		, char const* ih)
		: s(ios)
		, read_pos(0)
		, state(handshaking)
		, pieces(num_pieces)
		, block(0)
		, blocks_per_piece(blocks_pp)
		, info_hash(ih)
		, outstanding_requests(0)
	{
		// build a list of all pieces and request them all!
		for (int i = 0; i < pieces.size(); ++i)
			pieces[i] = i;
		std::random_shuffle(pieces.begin(), pieces.end());

		s.async_connect(ep, boost::bind(&peer_conn::on_connect, this, _1));
	}

	stream_socket s;
	char buffer[17*1024];
	int read_pos;

	enum state_t
	{
		handshaking,
		sending_request,
		receiving_message
	};
	int state;
	std::vector<int> pieces;
	int block;
	int blocks_per_piece;
	char const* info_hash;
	int outstanding_requests;

	void on_connect(error_code const& ec)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR CONNECT: %s\n", ec.message().c_str());
			return;
		}

		char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
			"                    " // space for info-hash
			"aaaaaaaaaaaaaaaaaaaa" // peer-id
			"\0\0\0\x01\x02"; // interested
		char* h = (char*)malloc(sizeof(handshake));
		memcpy(h, handshake, sizeof(handshake));
		std::memcpy(h + 28, info_hash, 20);
		std::generate(h + 48, h + 68, &rand);
		boost::asio::async_write(s, libtorrent::asio::buffer(h, sizeof(handshake) - 1)
			, boost::bind(&peer_conn::on_handshake, this, h, _1, _2));
	}

	void on_handshake(char* h, error_code const& ec, size_t bytes_transferred)
	{
		free(h);
		if (ec)
		{
			fprintf(stderr, "ERROR SEND HANDSHAKE: %s\n", ec.message().c_str());
			return;
		}

		// read handshake
		boost::asio::async_read(s, libtorrent::asio::buffer(buffer, 68)
			, boost::bind(&peer_conn::on_handshake2, this, _1, _2));
	}

	void on_handshake2(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR READ HANDSHAKE: %s\n", ec.message().c_str());
			return;
		}

		work();
	}

	void write_request()
	{
		if (pieces.empty()) return;

		int piece = pieces.back();

		char msg[] = "\0\0\0\xd\x06"
			"    " // piece
			"    " // offset
			"    "; // length
		char* m = (char*)malloc(sizeof(msg));
		memcpy(m, msg, sizeof(msg));
		char* ptr = m + 5;
		write_uint32(piece, ptr);
		write_uint32(block * 16 * 1024, ptr);
		write_uint32(16 * 1024, ptr);
		error_code ec;
		boost::asio::async_write(s, libtorrent::asio::buffer(m, sizeof(msg) - 1)
			, boost::bind(&peer_conn::on_req_sent, this, m, _1, _2));
	
		++block;
		if (block == blocks_per_piece)
		{
			block = 0;
			pieces.pop_back();
		}
	}

	void on_req_sent(char* m, error_code const& ec, size_t bytes_transferred)
	{
		free(m);
		if (ec)
		{
			fprintf(stderr, "ERROR SEND REQUEST: %s\n", ec.message().c_str());
			return;
		}

		++outstanding_requests;
	
		work();
	}

	void work()
	{
		if (pieces.empty() && outstanding_requests == 0)
		{
			fprintf(stderr, "COMPLETED DOWNLOAD\n");
			return;
		}

		// send requests
		if (outstanding_requests < 20 && !pieces.empty())
		{
			write_request();
			return;
		}

		// read message
		boost::asio::async_read(s, asio::buffer(buffer, 4)
			, boost::bind(&peer_conn::on_msg_length, this, _1, _2));
	}

	void on_msg_length(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR RECEIVE MESSAGE PREFIX: %s\n", ec.message().c_str());
			return;
		}
		char* ptr = buffer;
		unsigned int length = read_uint32(ptr);
		if (length > sizeof(buffer))
		{
			fprintf(stderr, "ERROR RECEIVE MESSAGE PREFIX: packet too big\n");
			return;
		}
		boost::asio::async_read(s, asio::buffer(buffer, length)
			, boost::bind(&peer_conn::on_message, this, _1, _2));
	}

	void on_message(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR RECEIVE MESSAGE: %s\n", ec.message().c_str());
			return;
		}
		char* ptr = buffer;
		int msg = read_uint8(ptr);
		if (msg == 7) --outstanding_requests;

		work();
	}
};

int main(int argc, char const* argv[])
{
	if (argc < 5)
	{
		fprintf(stderr, "usage: connection_tester number-of-connections destination-ip destination-port torrent-file\n");
		return 1;
	}
	int num_connections = atoi(argv[1]);
	address_v4 addr = address_v4::from_string(argv[2]);
	int port = atoi(argv[3]);
	tcp::endpoint ep(addr, port);
	error_code ec;
	torrent_info ti(argv[4], ec);
	if (ec)
	{
		fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", ec.message().c_str());
		return 1;
	}
	std::list<peer_conn*> conns;
	io_service ios;
	for (int i = 0; i < num_connections; ++i)
	{
		conns.push_back(new peer_conn(ios, ti.num_pieces(), ti.piece_length() / 16 / 1024
			, ep, (char const*)&ti.info_hash()[0]));
		libtorrent::sleep(1);
	}

	ios.run();

	return 0;
}


