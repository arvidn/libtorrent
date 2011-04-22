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
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/hasher.hpp"
#include <cstring>
#include <boost/bind.hpp>
#include <iostream>
#include <boost/array.hpp>

using namespace libtorrent;
using namespace libtorrent::detail; // for write_* and read_*

void generate_block(boost::uint32_t* buffer, int piece, int start, int length)
{
	boost::uint32_t fill = (piece << 16) | (start / 0x4000);
	for (int i = 0; i < length / 4; ++i)
	{
		buffer[i] = fill;
	}
}

struct peer_conn
{
	peer_conn(io_service& ios, int num_pieces, int blocks_pp, tcp::endpoint const& ep
		, char const* ih, bool seed_)
		: s(ios)
		, read_pos(0)
		, state(handshaking)
		, block(0)
		, blocks_per_piece(blocks_pp)
		, info_hash(ih)
		, outstanding_requests(0)
		, seed(seed_)
		, blocks_received(0)
		, num_pieces(num_pieces)
	{
		pieces.reserve(num_pieces);
		s.async_connect(ep, boost::bind(&peer_conn::on_connect, this, _1));
	}

	stream_socket s;
	boost::uint32_t write_buffer[17*1024/4];
	boost::uint32_t buffer[17*1024/4];
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
	// if this is true, this connection is a seed
	bool seed;
	int blocks_received;
	int num_pieces;

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
		// for seeds, don't send the interested message
		boost::asio::async_write(s, libtorrent::asio::buffer(h, (sizeof(handshake) - 1) - (seed ? 5 : 0))
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
		boost::asio::async_read(s, libtorrent::asio::buffer((char*)buffer, 68)
			, boost::bind(&peer_conn::on_handshake2, this, _1, _2));
	}

	void on_handshake2(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR READ HANDSHAKE: %s\n", ec.message().c_str());
			return;
		}

		if (seed)
		{
			write_have_all();
		}
		else
		{
			work_download();
		}
	}

	void write_have_all()
	{
		// have_all and unchoke
		static char msg[] = "\0\0\0\x01\x0e\0\0\0\x01\x01";
		error_code ec;
		boost::asio::async_write(s, libtorrent::asio::buffer(msg, sizeof(msg) - 1)
			, boost::bind(&peer_conn::on_have_all_sent, this, _1, _2));
	
	}

	void on_have_all_sent(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR SEND HAVE ALL: %s\n", ec.message().c_str());
			return;
		}

		// read message
		boost::asio::async_read(s, asio::buffer((char*)buffer, 4)
			, boost::bind(&peer_conn::on_msg_length, this, _1, _2));
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
	
		work_download();
	}

	void work_download()
	{
		if (pieces.empty()
			&& outstanding_requests == 0
			&& blocks_received >= num_pieces * blocks_per_piece)
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
		boost::asio::async_read(s, asio::buffer((char*)buffer, 4)
			, boost::bind(&peer_conn::on_msg_length, this, _1, _2));
	}

	void on_msg_length(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR RECEIVE MESSAGE PREFIX: %s\n", ec.message().c_str());
			return;
		}
		char* ptr = (char*)buffer;
		unsigned int length = read_uint32(ptr);
		if (length > sizeof(buffer))
		{
			fprintf(stderr, "ERROR RECEIVE MESSAGE PREFIX: packet too big\n");
			return;
		}
		boost::asio::async_read(s, asio::buffer((char*)buffer, length)
			, boost::bind(&peer_conn::on_message, this, _1, _2));
	}

	void on_message(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			fprintf(stderr, "ERROR RECEIVE MESSAGE: %s\n", ec.message().c_str());
			return;
		}
		char* ptr = (char*)buffer;
		int msg = read_uint8(ptr);

		//printf("msg: %d len: %d\n", msg, int(bytes_transferred));

		if (seed)
		{
			if (msg == 6 && bytes_transferred == 13)
			{
				int piece = detail::read_int32(ptr);
				int start = detail::read_int32(ptr);
				int length = detail::read_int32(ptr);
				write_piece(piece, start, length);
			}
			else
			{
				// read another message
				boost::asio::async_read(s, asio::buffer(buffer, 4)
					, boost::bind(&peer_conn::on_msg_length, this, _1, _2));
			}
		}
		else
		{
			if (msg == 0xe) // have_all
			{
				// build a list of all pieces and request them all!
				pieces.resize(num_pieces);
				for (int i = 0; i < int(pieces.size()); ++i)
					pieces[i] = i;
				std::random_shuffle(pieces.begin(), pieces.end());
			}
			else if (msg == 4) // have
			{
				int piece = detail::read_int32(ptr);
				if (pieces.empty()) pieces.push_back(piece);
				else pieces.insert(pieces.begin() + (rand() % pieces.size()), piece);
			}
			else if (msg == 5)
			{
				// todo: support bitfield
			}
			else if (msg == 7)
			{
				++blocks_received;
				--outstanding_requests;
			}
			work_download();
		}
	}

	void write_piece(int piece, int start, int length)
	{
		generate_block(write_buffer, piece, start, length);
		static char msg[] = "    \x07"
			"    " // piece
			"    "; // start
		char* ptr = msg;
		write_uint32(9 + length, ptr);
		assert(length == 0x4000);
		assert(*ptr == 7);
		++ptr; // skip message id
		write_uint32(piece, ptr);
		write_uint32(start, ptr);
		boost::array<libtorrent::asio::const_buffer, 2> vec;
		vec[0] = libtorrent::asio::buffer(msg, sizeof(msg)-1);
		vec[1] = libtorrent::asio::buffer(write_buffer, length);
		boost::asio::async_write(s, vec, boost::bind(&peer_conn::on_have_all_sent, this, _1, _2));
	}
};

void print_usage()
{
	fprintf(stderr, "usage: connection_tester command ...\n\n"
		"command is one of:\n"
		"  gen-torrent         generate a test torrent\n"
		"    this command takes one extra argument, specifying the file to save\n"
		"    the .torrent file to\n\n"
		"  upload              start an uploader test\n"
		"  download            start a downloader test\n"
		"  dual                start a download and upload test\n"
		"    these commands set takes 4 additional arguments\n"
		"    1. num-connections - the number of connections to make to the target\n"
		"    2. destination-IP - the IP address of the target\n"
		"    3. destination-port - the port the target listens on\n"
		"    4. torrent-file - the torrent file previously generated by gen-torrent\n\n"
		"examples:\n\n"
		"connection_tester gen-torrent test.torrent\n"
		"connection_tester upload 200 127.0.0.1 6881 test.torrent\n"
		"connection_tester download 200 127.0.0.1 6881 test.torrent\n"
		"connection_tester dual 200 127.0.0.1 6881 test.torrent\n");
	exit(1);
}

void generate_torrent(std::vector<char>& buf)
{
	file_storage fs;
	// 1 MiB piece size
	const int piece_size = 1024 * 1024;
	// 50 GiB should be enough to not fit in physical RAM
	const int num_pieces = 1 * 1024;
	const size_type total_size = size_type(piece_size) * num_pieces;
	fs.add_file("stress_test_file", total_size);
	libtorrent::create_torrent t(fs, piece_size);

	boost::uint32_t piece[0x4000 / 4];
	for (int i = 0; i < num_pieces; ++i)
	{
		hasher ph;
		for (int j = 0; j < piece_size; j += 0x4000)
		{
			generate_block(piece, i, j, 0x4000);
			ph.update((char*)piece, 0x4000);
		}
		t.set_hash(i, ph.final());
	}

	std::back_insert_iterator<std::vector<char> > out(buf);

	bencode(out, t.generate());
}

int main(int argc, char* argv[])
{
	if (argc <= 1) print_usage();

	enum { none, upload_test, download_test, dual_test } test_mode = none;

	if (strcmp(argv[1], "gen-torrent") == 0)
	{
		if (argc != 3) print_usage();

		std::vector<char> tmp;
		generate_torrent(tmp);

		FILE* output = stdout;
		if (strcmp("-", argv[2]) != 0)
			output = fopen(argv[2], "wb+");
		fwrite(&tmp[0], 1, tmp.size(), output);
		if (output != stdout)
			fclose(output);

		return 0;
	}
	else if (strcmp(argv[1], "upload") == 0)
	{
		if (argc != 6) print_usage();
		test_mode = upload_test;
	}
	else if (strcmp(argv[1], "download") == 0)
	{
		if (argc != 6) print_usage();
		test_mode = download_test;
	}
	else if (strcmp(argv[1], "dual") == 0)
	{
		if (argc != 6) print_usage();
		test_mode = dual_test;
	}

	if (!download_test && !upload_test) print_usage();

	int num_connections = atoi(argv[2]);
	error_code ec;
	address_v4 addr = address_v4::from_string(argv[3], ec);
	if (ec)
	{
		fprintf(stderr, "ERROR RESOLVING %s: %s\n", argv[3], ec.message().c_str());
		return 1;
	}
	int port = atoi(argv[4]);
	tcp::endpoint ep(addr, port);
	
	torrent_info ti(argv[5], ec);
	if (ec)
	{
		fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", ec.message().c_str());
		return 1;
	}
			
	std::list<peer_conn*> conns;
	io_service ios;
	for (int i = 0; i < num_connections; ++i)
	{
		bool seed = false;
		if (test_mode == upload_test) seed = true;
		else if (test_mode == dual_test) seed = (i & 1);
		conns.push_back(new peer_conn(ios, ti.num_pieces(), ti.piece_length() / 16 / 1024
			, ep, (char const*)&ti.info_hash()[0], seed));
		libtorrent::sleep(1);
		ios.poll_one(ec);
		if (ec)
		{
			fprintf(stderr, "ERROR: %s\n", ec.message().c_str());
			break;
		}
	}

	ios.run(ec);
	if (ec) fprintf(stderr, "ERROR: %s\n", ec.message().c_str());

	return 0;
}


