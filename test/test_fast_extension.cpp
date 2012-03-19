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

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/io.hpp"
#include <cstring>
#include <boost/bind.hpp>
#include <iostream>

using namespace libtorrent;

int read_message(stream_socket& s, char* buffer)
{
	using namespace libtorrent::detail;
	error_code ec;
	libtorrent::asio::read(s, libtorrent::asio::buffer(buffer, 4)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		std::cout << time_now_string() << ": " << ec.message() << std::endl;
		exit(1);
	}
	char* ptr = buffer;
	int length = read_int32(ptr);

	libtorrent::asio::read(s, libtorrent::asio::buffer(buffer, length)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		std::cout << time_now_string() << ": " << ec.message() << std::endl;
		exit(1);
	}
	return length;
}

void print_message(char const* buffer, int len)
{
	char const* message_name[] = {"choke", "unchoke", "interested", "not_interested"
		, "have", "bitfield", "request", "piece", "cancel", "dht_port", "", "", ""
		, "suggest_piece", "have_all", "have_none", "reject_request", "allowed_fast"};

	char message[50];
	char extra[300];
	extra[0] = 0;
	if (len == 0)
	{
		strcpy(message, "keepalive");
	}
	else
	{
		int msg = buffer[0];
		if (msg >= 0 && msg < int(sizeof(message_name)/sizeof(message_name[0])))
			strcpy(message, message_name[msg]);
		else
			snprintf(message, sizeof(message), "unknown[%d]", msg);

		if (msg == 0x6 && len == 13)
		{
			peer_request r;
			const char* ptr = buffer + 1;
			r.piece = detail::read_int32(ptr);
			r.start = detail::read_int32(ptr);
			r.length = detail::read_int32(ptr);
			snprintf(extra, sizeof(extra), "p: %d s: %d l: %d", r.piece, r.start, r.length);
		}
		else if (msg == 0x11 && len == 5)
		{
			const char* ptr = buffer + 1;
			int index = detail::read_int32(ptr);
			snprintf(extra, sizeof(extra), "p: %d", index);
		}
	}

	fprintf(stderr, "%s <== %s %s\n", time_now_string(), message, extra);
}

void send_allow_fast(stream_socket& s, int piece)
{
	std::cout << time_now_string() << " ==> allow fast: " << piece << std::endl;
	using namespace libtorrent::detail;
	char msg[] = "\0\0\0\x05\x11\0\0\0\0";
	char* ptr = msg + 5;
	write_int32(piece, ptr);
	error_code ec;
	libtorrent::asio::write(s, libtorrent::asio::buffer(msg, 9)
		, libtorrent::asio::transfer_all(), ec);
}

void send_suggest_piece(stream_socket& s, int piece)
{
	std::cout << time_now_string() << " ==> suggest piece: " << piece << std::endl;
	using namespace libtorrent::detail;
	char msg[] = "\0\0\0\x05\x0d\0\0\0\0";
	char* ptr = msg + 5;
	write_int32(piece, ptr);
	error_code ec;
	libtorrent::asio::write(s, libtorrent::asio::buffer(msg, 9)
		, libtorrent::asio::transfer_all(), ec);
}

void send_keepalive(stream_socket& s)
{
	std::cout << time_now_string() << " ==> keepalive" << std::endl;
	char msg[] = "\0\0\0\0";
	error_code ec;
	libtorrent::asio::write(s, libtorrent::asio::buffer(msg, 4)
		, libtorrent::asio::transfer_all(), ec);
}

void send_unchoke(stream_socket& s)
{
	std::cout << time_now_string() << " ==> unchoke" << std::endl;
	char msg[] = "\0\0\0\x01\x01";
	error_code ec;
	libtorrent::asio::write(s, libtorrent::asio::buffer(msg, 5)
		, libtorrent::asio::transfer_all(), ec);
}

void do_handshake(stream_socket& s, sha1_hash const& ih, char* buffer)
{
	char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
		"                    " // space for info-hash
		"aaaaaaaaaaaaaaaaaaaa" // peer-id
		"\0\0\0\x01\x0e"; // have_all
	std::cout << time_now_string() << " ==> handshake" << std::endl;
	std::cout << time_now_string() << " ==> have_all" << std::endl;
	error_code ec;
	std::memcpy(handshake + 28, ih.begin(), 20);
	libtorrent::asio::write(s, libtorrent::asio::buffer(handshake, sizeof(handshake) - 1)
		, libtorrent::asio::transfer_all(), ec);

	// read handshake
	libtorrent::asio::read(s, libtorrent::asio::buffer(buffer, 68)
		, libtorrent::asio::transfer_all(), ec);
	if (ec)
	{
		std::cout << time_now_string() << ": " << ec.message() << std::endl;
		exit(1);
	}
	std::cout << time_now_string() << " <== handshake" << std::endl;

	TEST_CHECK(buffer[0] == 19);
	TEST_CHECK(std::memcmp(buffer + 1, "BitTorrent protocol", 19) == 0);

	char* extensions = buffer + 20;
	// check for fast extension support
	TEST_CHECK(extensions[7] & 0x4);
	
#ifndef TORRENT_DISABLE_EXTENSIONS
	// check for extension protocol support
	TEST_CHECK(extensions[5] & 0x10);
#endif
	
#ifndef TORRENT_DISABLE_DHT
	// check for DHT support
	TEST_CHECK(extensions[7] & 0x1);
#endif
	
	TEST_CHECK(std::memcmp(buffer + 28, ih.begin(), 20) == 0);
}

// makes sure that pieces that are allowed and then
// rejected aren't requested again
void test_reject_fast()
{
	std::cerr << " === test reject ===" << std::endl;

	boost::intrusive_ptr<torrent_info> t = ::create_torrent();
	sha1_hash ih = t->info_hash();
	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48900, 49000), "0.0.0.0", 0);
	error_code ec;
	add_torrent_params p;
	p.flags &= ~add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	p.ti = t;
	p.save_path = "./tmp1_fast";

	remove("./tmp1_fast/temporary", ec);
	if (ec) fprintf(stderr, "remove(): %s\n", ec.message().c_str());
	ec.clear();
	ses1.add_torrent(p, ec);

	test_sleep(300);

	io_service ios;
	stream_socket s(ios);
	s.connect(tcp::endpoint(address::from_string("127.0.0.1", ec), ses1.listen_port()), ec);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	
	std::vector<int> allowed_fast;
	allowed_fast.push_back(0);
	allowed_fast.push_back(1);
	allowed_fast.push_back(2);
	allowed_fast.push_back(3);

	std::for_each(allowed_fast.begin(), allowed_fast.end()
		, boost::bind(&send_allow_fast, boost::ref(s), _1));

	while (!allowed_fast.empty())
	{
		int len = read_message(s, recv_buffer);
		print_message(recv_buffer, len);
		int msg = recv_buffer[0];
		if (msg != 0x6) continue;

		using namespace libtorrent::detail;
		char* ptr = recv_buffer + 1;
		int piece = read_int32(ptr);

		std::vector<int>::iterator i = std::find(allowed_fast.begin()
			, allowed_fast.end(), piece);
		TEST_CHECK(i != allowed_fast.end());
		if (i != allowed_fast.end())
			allowed_fast.erase(i);
		// send reject request
		recv_buffer[0] = 0x10;
		error_code ec;
		std::cerr << time_now_string() << " ==> reject" << std::endl;
		libtorrent::asio::write(s, libtorrent::asio::buffer("\0\0\0\x0d", 4)
			, libtorrent::asio::transfer_all(), ec);
		libtorrent::asio::write(s, libtorrent::asio::buffer(recv_buffer, 13)
			, libtorrent::asio::transfer_all(), ec);
	}
}

void test_respect_suggest()
{
	std::cerr << " === test suggest ===" << std::endl;
	boost::intrusive_ptr<torrent_info> t = ::create_torrent();
	sha1_hash ih = t->info_hash();
	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48900, 49000), "0.0.0.0", 0);

	error_code ec;
	add_torrent_params p;
	p.flags &= ~add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	p.ti = t;
	p.save_path = "./tmp1_fast";

	remove("./tmp1_fast/temporary", ec);
	if (ec) fprintf(stderr, "remove(): %s\n", ec.message().c_str());
	ec.clear();
	ses1.add_torrent(p, ec);

	test_sleep(300);

	io_service ios;
	stream_socket s(ios);
	s.connect(tcp::endpoint(address::from_string("127.0.0.1", ec), ses1.listen_port()), ec);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	
	std::vector<int> suggested;
	suggested.push_back(0);
	suggested.push_back(1);
	suggested.push_back(2);
	suggested.push_back(3);

	std::for_each(suggested.begin(), suggested.end()
		, boost::bind(&send_suggest_piece, boost::ref(s), _1));

	send_unchoke(s);

	send_keepalive(s);

	int fail_counter = 100;	
	while (!suggested.empty() && fail_counter > 0)
	{
		int len = read_message(s, recv_buffer);
		print_message(recv_buffer, len);
		int msg = recv_buffer[0];
		fail_counter--;
		if (msg != 0x6) continue;

		using namespace libtorrent::detail;
		char* ptr = recv_buffer + 1;
		int piece = read_int32(ptr);

		std::vector<int>::iterator i = std::find(suggested.begin()
			, suggested.end(), piece);
		TEST_CHECK(i != suggested.end());
		if (i != suggested.end())
			suggested.erase(i);
		// send reject request
		recv_buffer[0] = 0x10;
		error_code ec;
		std::cerr << time_now_string() << " ==> reject" << std::endl;
		libtorrent::asio::write(s, libtorrent::asio::buffer("\0\0\0\x0d", 4)
			, libtorrent::asio::transfer_all(), ec);
		libtorrent::asio::write(s, libtorrent::asio::buffer(recv_buffer, 13)
			, libtorrent::asio::transfer_all(), ec);
	}
	TEST_CHECK(fail_counter > 0);
}

int test_main()
{
	test_reject_fast();
	test_respect_suggest();
	return 0;
}

