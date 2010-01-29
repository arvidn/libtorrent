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

int read_message(stream_socket& s, char* buffer)
{
	using namespace libtorrent::detail;
	error_code ec;
	libtorrent::asio::read(s, libtorrent::asio::buffer(buffer, 4)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		fprintf(stderr, "ERROR RECEIVE MESSAGE PREFIX: %s\n", ec.message().c_str());
		return -1;
	}
	char* ptr = buffer;
	int length = read_int32(ptr);

	libtorrent::asio::read(s, libtorrent::asio::buffer(buffer, length)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		fprintf(stderr, "ERROR RECEIVE MESSAGE: %s\n", ec.message().c_str());
		return -1;
	}
	return length;
}

void do_handshake(stream_socket& s, sha1_hash const& ih, char* buffer)
{
	char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
		"                    " // space for info-hash
		"aaaaaaaaaaaaaaaaaaaa"; // peer-id
	error_code ec;
	std::memcpy(handshake + 28, ih.begin(), 20);
	std::generate(handshake + 48, handshake + 68, &rand);
	libtorrent::asio::write(s, libtorrent::asio::buffer(handshake, sizeof(handshake) - 1)
		, libtorrent::asio::transfer_all(), ec);

	if (ec)
	{
		fprintf(stderr, "ERROR SEND HANDSHAKE: %s\n", ec.message().c_str());
		return;
	}

	// read handshake
	libtorrent::asio::read(s, libtorrent::asio::buffer(buffer, 68)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		fprintf(stderr, "ERROR RECEIVE HANDSHAKE: %s\n", ec.message().c_str());
		return;
	}
}

void send_interested(stream_socket& s)
{
	char msg[] = "\0\0\0\x01\x02";
	error_code ec;
	libtorrent::asio::write(s, libtorrent::asio::buffer(msg, 5)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		fprintf(stderr, "ERROR SEND INTERESTED: %s\n", ec.message().c_str());
		return;
	}
}

void send_request(stream_socket& s, int piece, int block)
{
	char msg[] = "\0\0\0\xd\x06"
		"    " // piece
		"    " // offset
		"    "; // length
	char* ptr = msg + 5;
	write_uint32(piece, ptr);
	write_uint32(block * 16 * 1024, ptr);
	write_uint32(16 * 1024, ptr);
	error_code ec;
	libtorrent::asio::write(s, libtorrent::asio::buffer(msg, sizeof(msg)-1)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		fprintf(stderr, "ERROR SEND REQUEST: %s\n", ec.message().c_str());
		return;
	}
}

// makes sure that pieces that are allowed and then
// rejected aren't requested again
void requester_thread(torrent_info const* ti, tcp::endpoint const* ep, io_service* ios)
{
	sha1_hash const& ih = ti->info_hash();

	stream_socket s(*ios);
	error_code ec;
	s.connect(*ep, ec);
	if (ec)
	{
		fprintf(stderr, "ERROR CONNECT: %s\n", ec.message().c_str());
		return;
	}

	char recv_buffer[16 * 1024 + 1000];
	do_handshake(s, ih, recv_buffer);
	send_interested(s);
	
	// build a list of all pieces and request them all!
	std::vector<int> pieces(ti->num_pieces());
	for (int i = 0; i < pieces.size(); ++i)
		pieces[i] = i;

	std::random_shuffle(pieces.begin(), pieces.end());
	int block = 0;
	int blocks_per_piece = ti->piece_length() / 16 / 1024;

	int outstanding_reqs = 0;

	while (true)
	{
		while (outstanding_reqs < 16)
		{
			send_request(s, pieces.back(), block++);
			++outstanding_reqs;
			if (block == blocks_per_piece)
			{
				block = 0;
				pieces.pop_back();
			}
			if (pieces.empty())
			{
				fprintf(stderr, "COMPLETED DOWNLOAD\n");
				return;
			}
		}

		int length = read_message(s, recv_buffer);
		if (length == -1) return;
		int msg = recv_buffer[0];
		if (msg == 7) --outstanding_reqs;
	}
}

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
	std::list<thread*> threads;
	io_service ios;
	for (int i = 0; i < num_connections; ++i)
	{
		threads.push_back(new thread(boost::bind(&requester_thread, &ti, &ep, &ios)));
		libtorrent::sleep(10);
	}

	for (int i = 0; i < num_connections; ++i)
	{
		threads.back()->join();
		delete threads.back();
		threads.pop_back();
	}

	return 0;
}


