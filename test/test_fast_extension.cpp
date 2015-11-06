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
#include "test_utils.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <cstring>
#include <boost/bind.hpp>
#include <iostream>
#include <cstdarg>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace libtorrent;
namespace lt = libtorrent;

void log(char const* fmt, ...)
{
	va_list v;
	va_start(v, fmt);

	char buf[1024];
	vsnprintf(buf, sizeof(buf), fmt, v);
	va_end(v);

	fprintf(stderr, "\x1b[1m\x1b[36m%s: %s\x1b[0m\n"
		, time_now_string(), buf);
}

void print_session_log(lt::session& ses)
{
	print_alerts(ses, "ses", true, true);
}

int read_message(tcp::socket& s, char* buffer, int max_size)
{
	using namespace libtorrent::detail;
	error_code ec;
	boost::asio::read(s, boost::asio::buffer(buffer, 4)
		, boost::asio::transfer_all(), ec);
	if (ec)
	{
		TEST_ERROR(ec.message());
		return -1;
	}
	char* ptr = buffer;
	int length = read_int32(ptr);
	if (length > max_size)
	{
		log("message size: %d", length);
		TEST_ERROR("message size exceeds max limt");
		return -1;
	}

	boost::asio::read(s, boost::asio::buffer(buffer, length)
		, boost::asio::transfer_all(), ec);
	if (ec)
	{
		TEST_ERROR(ec.message());
		return -1;
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
		else if (msg == 20)
			snprintf(message, sizeof(message), "extension msg [%d]", buffer[1]);
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
		else if (msg == 20 && len > 4 && buffer[1] == 0 )
		{
			snprintf(extra, sizeof(extra), "%s"
				, bdecode(buffer + 2, buffer + len).to_string().c_str());
		}
	}

	log("<== %s %s", message, extra);
}

void send_allow_fast(tcp::socket& s, int piece)
{
	log("==> allow fast: %d", piece);
	using namespace libtorrent::detail;
	char msg[] = "\0\0\0\x05\x11\0\0\0\0";
	char* ptr = msg + 5;
	write_int32(piece, ptr);
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 9)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_suggest_piece(tcp::socket& s, int piece)
{
	log("==> suggest piece: %d", piece);
	using namespace libtorrent::detail;
	char msg[] = "\0\0\0\x05\x0d\0\0\0\0";
	char* ptr = msg + 5;
	write_int32(piece, ptr);
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 9)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_keepalive(tcp::socket& s)
{
	log("==> keepalive");
	char msg[] = "\0\0\0\0";
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 4)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_unchoke(tcp::socket& s)
{
	log("==> unchoke");
	char msg[] = "\0\0\0\x01\x01";
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 5)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_have_all(tcp::socket& s)
{
	log("==> have_all");
	char msg[] = "\0\0\0\x01\x0e"; // have_all
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 5)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_have_none(tcp::socket& s)
{
	log("==> have_none");
	char msg[] = "\0\0\0\x01\x0f"; // have_none
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 5)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_bitfield(tcp::socket& s, char const* bits)
{
	using namespace libtorrent::detail;

	int num_pieces = strlen(bits);
	int packet_size = (num_pieces+7)/8 + 5;
	char* msg = (char*)TORRENT_ALLOCA(char, packet_size);
	memset(msg, 0, packet_size);
	char* ptr = msg;
	write_int32(packet_size-4, ptr);
	write_int8(5, ptr);
	log("==> bitfield [%s]", bits);
	for (int i = 0; i < num_pieces; ++i)
	{
		ptr[i/8] |= (bits[i] == '1' ? 1 : 0) << i % 8;
	}
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, packet_size)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void do_handshake(tcp::socket& s, sha1_hash const& ih, char* buffer)
{
	char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\x10\0\x04"
		"                    " // space for info-hash
		"aaaaaaaaaaaaaaaaaaaa"; // peer-id
	log("==> handshake");
	error_code ec;
	std::memcpy(handshake + 28, ih.begin(), 20);
	boost::asio::write(s, boost::asio::buffer(handshake, sizeof(handshake) - 1)
		, boost::asio::transfer_all(), ec);
	if (ec)
	{
		TEST_ERROR(ec.message());
		return;
	}

	// read handshake
	boost::asio::read(s, boost::asio::buffer(buffer, 68)
		, boost::asio::transfer_all(), ec);
	if (ec)
	{
		TEST_ERROR(ec.message());
		return;
	}
	log("<== handshake");

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

void send_extension_handshake(tcp::socket& s, entry const& e)
{
	std::vector<char> buf;

	// reserve space for the message header
	// uint32: packet-length
	//  uint8: 20 (extension message)
	//  uint8: 0 (handshake)
	buf.resize(4 + 1 + 1);

	bencode(std::back_inserter(buf), e);

	using namespace libtorrent::detail;

	char* ptr = &buf[0];
	write_uint32(buf.size() - 4, ptr);
	write_uint8(20, ptr);
	write_uint8(0, ptr);

	error_code ec;
	boost::asio::write(s, boost::asio::buffer(&buf[0], buf.size())
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_request(tcp::socket& s, peer_request req)
{
	using namespace libtorrent::detail;

	log("==> request %d (%d,%d)", req.piece, req.start, req.length);
	char msg[] = "\0\0\0\x0d\x06            "; // have_none
	char* ptr = msg + 5;
	write_uint32(req.piece, ptr);
	write_uint32(req.start, ptr);
	write_uint32(req.length, ptr);
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 17)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

entry read_extension_handshake(tcp::socket& s, char* recv_buffer, int size)
{
	for (;;)
	{
		int len = read_message(s, recv_buffer, size);
		if (len == -1)
		{
			TEST_ERROR("failed to read message");
			return entry();
		}
		print_message(recv_buffer, len);

		if (len < 4) continue;
		int msg = recv_buffer[0];
		if (msg != 20) continue;
		int extmsg = recv_buffer[1];
		if (extmsg != 0) continue;

		return bdecode(recv_buffer + 2, recv_buffer + len);
	}
}

void send_ut_metadata_msg(tcp::socket& s, int ut_metadata_msg, int type, int piece)
{
	std::vector<char> buf;

	// reserve space for the message header
	// uint32: packet-length
	//  uint8: 20 (extension message)
	//  uint8: <ut_metadata_msg> (ut_metadata)
	buf.resize(4 + 1 + 1);

	entry e;
	e["msg_type"] = type;
	e["piece"] = piece;
	bencode(std::back_inserter(buf), e);

	using namespace libtorrent::detail;

	char* ptr = &buf[0];
	write_uint32(buf.size() - 4, ptr);
	write_uint8(20, ptr);
	write_uint8(ut_metadata_msg, ptr);

	log("==> ut_metadata [ type: %d piece: %d ]", type, piece);

	error_code ec;
	boost::asio::write(s, boost::asio::buffer(&buf[0], buf.size())
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

entry read_ut_metadata_msg(tcp::socket& s, char* recv_buffer, int size)
{
	for (;;)
	{
		int len = read_message(s, recv_buffer, size);
		if (len == -1)
		{
			TEST_ERROR("failed to read message");
			return entry();
		}
		print_message(recv_buffer, len);

		if (len < 4) continue;
		int msg = recv_buffer[0];
		if (msg != 20) continue;
		int extmsg = recv_buffer[1];
		if (extmsg != 1) continue;

		return bdecode(recv_buffer + 2, recv_buffer + len);
	}
}

boost::shared_ptr<torrent_info> setup_peer(tcp::socket& s, sha1_hash& ih
	, boost::shared_ptr<lt::session>& ses, torrent_handle* th = NULL)
{
	boost::shared_ptr<torrent_info> t = ::create_torrent();
	ih = t->info_hash();
	settings_pack sett;
	sett.set_str(settings_pack::listen_interfaces, "0.0.0.0:48900");
	sett.set_int(settings_pack::alert_mask, alert::all_categories);
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_dht, false);
	ses.reset(new lt::session(sett, lt::session::add_default_plugins));

	error_code ec;
	add_torrent_params p;
	p.flags &= ~add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	p.ti = t;
	p.save_path = "./tmp1_fast";

	remove("./tmp1_fast/temporary", ec);
	if (ec) log("remove(): %s", ec.message().c_str());
	ec.clear();
	torrent_handle ret = ses->add_torrent(p, ec);
	if (th) *th = ret;

	// wait for the torrent to be ready
	wait_for_downloading(*ses, "ses");

	s.connect(tcp::endpoint(address::from_string("127.0.0.1", ec), ses->listen_port()), ec);
	if (ec) TEST_ERROR(ec.message());

	print_session_log(*ses);

	return t;
}

// makes sure that pieces that are allowed and then
// rejected aren't requested again
TORRENT_TEST(reject_fast)
{
	std::cerr << "\n === test reject ===\n" << std::endl;

	sha1_hash ih;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	setup_peer(s, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	std::vector<int> allowed_fast;
	allowed_fast.push_back(0);
	allowed_fast.push_back(1);
	allowed_fast.push_back(2);
	allowed_fast.push_back(3);

	std::for_each(allowed_fast.begin(), allowed_fast.end()
		, boost::bind(&send_allow_fast, boost::ref(s), _1));
	print_session_log(*ses);

	while (!allowed_fast.empty())
	{
		print_session_log(*ses);
		int len = read_message(s, recv_buffer, sizeof(recv_buffer));
		if (len == -1) break;
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
		log("==> reject");
		boost::asio::write(s, boost::asio::buffer("\0\0\0\x0d", 4)
			, boost::asio::transfer_all(), ec);
		if (ec)
		{
			TEST_ERROR(ec.message());
			break;
		}
		boost::asio::write(s, boost::asio::buffer(recv_buffer, 13)
			, boost::asio::transfer_all(), ec);
		if (ec)
		{
			TEST_ERROR(ec.message());
			break;
		}
	}
	print_session_log(*ses);
	s.close();
	test_sleep(500);
	print_session_log(*ses);
}

TORRENT_TEST(invalid_suggest)
{
	std::cerr << "\n === test suggest ===\n" << std::endl;

	sha1_hash ih;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	setup_peer(s, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	// this is an invalid suggest message. We would not expect to receive a
	// request for that piece index.
	send_suggest_piece(s, -234);
	send_unchoke(s);
	test_sleep(500);
	print_session_log(*ses);

	int len = read_message(s, recv_buffer, sizeof(recv_buffer));
	int idx = -1;
	while (len > 0)
	{
		if (recv_buffer[0] == 6)
		{
			char* ptr = recv_buffer + 1;
			idx = detail::read_int32(ptr);
			break;
		}
		len = read_message(s, recv_buffer, sizeof(recv_buffer));
	}
	TEST_CHECK(idx != -234);
	TEST_CHECK(idx != -1);
	s.close();
}

TORRENT_TEST(reject_suggest)
{
	std::cerr << "\n === test suggest ===\n" << std::endl;

	sha1_hash ih;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	setup_peer(s, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	std::vector<int> suggested;
	suggested.push_back(0);
	suggested.push_back(1);
	suggested.push_back(2);
	suggested.push_back(3);

	std::for_each(suggested.begin(), suggested.end()
		, boost::bind(&send_suggest_piece, boost::ref(s), _1));
	print_session_log(*ses);

	send_unchoke(s);
	print_session_log(*ses);

	send_keepalive(s);
	print_session_log(*ses);

	int fail_counter = 100;
	while (!suggested.empty() && fail_counter > 0)
	{
		print_session_log(*ses);
		int len = read_message(s, recv_buffer, sizeof(recv_buffer));
		if (len == -1) break;
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
		log("==> reject");
		boost::asio::write(s, boost::asio::buffer("\0\0\0\x0d", 4)
			, boost::asio::transfer_all(), ec);
		if (ec)
		{
			TEST_ERROR(ec.message());
			break;
		}
		boost::asio::write(s, boost::asio::buffer(recv_buffer, 13)
			, boost::asio::transfer_all(), ec);
		if (ec)
		{
			TEST_ERROR(ec.message());
			break;
		}
	}
	print_session_log(*ses);
	TEST_CHECK(fail_counter > 0);

	s.close();
	test_sleep(500);
	print_session_log(*ses);
}

TORRENT_TEST(multiple_bitfields)
{
	std::cerr << "\n === test multiple bitfields ===\n" << std::endl;

	sha1_hash ih;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	boost::shared_ptr<torrent_info> ti = setup_peer(s, ih, ses);
	print_session_log(*ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);

	std::string bitfield;
	bitfield.resize(ti->num_pieces(), '0');
	send_bitfield(s, bitfield.c_str());
	print_session_log(*ses);
	bitfield[0] = '1';
	send_bitfield(s, bitfield.c_str());
	print_session_log(*ses);
	bitfield[1] = '1';
	send_bitfield(s, bitfield.c_str());
	print_session_log(*ses);
	bitfield[2] = '1';
	send_bitfield(s, bitfield.c_str());
	print_session_log(*ses);

	s.close();
	test_sleep(500);
	print_session_log(*ses);
}

TORRENT_TEST(multiple_have_all)
{
	std::cerr << "\n === test multiple have_all ===\n" << std::endl;

	sha1_hash ih;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	boost::shared_ptr<torrent_info> ti = setup_peer(s, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);

	print_session_log(*ses);

	send_have_all(s);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);
	send_have_none(s);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	s.close();
	print_session_log(*ses);
	test_sleep(500);
	print_session_log(*ses);
}

#ifndef TORRENT_DISABLE_EXTENSIONS
// makes sure that pieces that are lost are not requested
TORRENT_TEST(dont_have)
{
	using namespace libtorrent::detail;

	std::cerr << "\n === test dont_have ===\n" << std::endl;

	sha1_hash ih;
	torrent_handle th;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	boost::shared_ptr<torrent_info> ti = setup_peer(s, ih, ses, &th);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	test_sleep(300);
	print_session_log(*ses);

	std::vector<peer_info> pi;
	th.get_peer_info(pi);

	TEST_EQUAL(pi.size(), 1);
	if (pi.size() != 1) return;

	// at this point, the peer should be considered a seed
	TEST_CHECK(pi[0].flags & peer_info::seed);

	int lt_dont_have = 0;
	error_code ec;
	while (lt_dont_have == 0)
	{
		print_session_log(*ses);

		int len = read_message(s, recv_buffer, sizeof(recv_buffer));
		if (len == -1) break;
		print_message(recv_buffer, len);
		if (len == 0) continue;
		int msg = recv_buffer[0];
		if (msg != 20) continue;
		int ext_msg = recv_buffer[1];
		if (ext_msg != 0) continue;

		bdecode_node e;
		int pos = 0;
		int ret = bdecode(recv_buffer + 2, recv_buffer + len, e, ec, &pos);
		if (ret != 0)
		{
			log("failed to parse extension handshake: %s at pos %d"
				, ec.message().c_str(), pos);
		}
		TEST_EQUAL(ret, 0);

		log("extension handshake: %s", print_entry(e).c_str());
		bdecode_node m = e.dict_find_dict("m");
		TEST_CHECK(m);
		if (!m) return;
		bdecode_node dont_have = m.dict_find_int("lt_donthave");
		TEST_CHECK(dont_have);
		if (!dont_have) return;

		lt_dont_have = dont_have.int_value();
	}
	print_session_log(*ses);

	char* ptr = recv_buffer;
	write_uint32(6, ptr);
	write_uint8(20, ptr);
	write_uint8(lt_dont_have, ptr);
	write_uint32(3, ptr);

	boost::asio::write(s, boost::asio::buffer(recv_buffer, 10)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());

	print_session_log(*ses);

	test_sleep(1000);

	print_session_log(*ses);

	th.get_peer_info(pi);

	TEST_EQUAL(pi.size(), 1);
	if (pi.size() != 1) return;

	TEST_EQUAL(pi[0].flags & peer_info::seed, 0);
	TEST_EQUAL(pi[0].pieces.count(), pi[0].pieces.size() - 1);
	TEST_EQUAL(pi[0].pieces[3], false);
	TEST_EQUAL(pi[0].pieces[2], true);
	TEST_EQUAL(pi[0].pieces[1], true);
	TEST_EQUAL(pi[0].pieces[0], true);

	print_session_log(*ses);
}

// TEST metadata extension messages and edge cases

// this tests sending a request for a metadata piece that's too high. This is
// pos
TORRENT_TEST(invalid_metadata_request)
{
	using namespace libtorrent::detail;

	std::cerr << "\n === test invalid metadata ===\n" << std::endl;

	sha1_hash ih;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	boost::shared_ptr<torrent_info> ti = setup_peer(s, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	entry extensions;
	extensions["m"]["ut_metadata"] = 1;
	send_extension_handshake(s, extensions);

	extensions = read_extension_handshake(s, recv_buffer, sizeof(recv_buffer));

	int ut_metadata = extensions["m"]["ut_metadata"].integer();

	log("ut_metadata: %d", ut_metadata);

	// 0 = request
	// 1 = piece
	// 2 = dont-have
	// first send an invalid request
	send_ut_metadata_msg(s, ut_metadata, 0, 1);

	// then send a valid one. If we get a response to the second one,
	// we assume we were not disconnected because of the invalid one
	send_ut_metadata_msg(s, ut_metadata, 0, 0);

	entry ut_metadata_msg = read_ut_metadata_msg(s, recv_buffer
		, sizeof(recv_buffer));

	// the first response should be "dont-have"
	TEST_EQUAL(ut_metadata_msg["msg_type"].integer(), 2);
	TEST_EQUAL(ut_metadata_msg["piece"].integer(), 1);

	ut_metadata_msg = read_ut_metadata_msg(s, recv_buffer
		, sizeof(recv_buffer));

	// the second response should be the payload
	TEST_EQUAL(ut_metadata_msg["msg_type"].integer(), 1);
	TEST_EQUAL(ut_metadata_msg["piece"].integer(), 0);

	print_session_log(*ses);
}

TORRENT_TEST(invalid_request)
{
	std::cerr << "\n === test request ===\n" << std::endl;

	sha1_hash ih;
	boost::shared_ptr<lt::session> ses;
	io_service ios;
	tcp::socket s(ios);
	setup_peer(s, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_none(s);

	peer_request req;
	req.piece = 124134235;
	req.start = 0;
	req.length = 0x4000;
	send_request(s, req);
}

#endif // TORRENT_DISABLE_EXTENSIONS

// TODO: test sending invalid requests (out of bound piece index, offsets and
// sizes)

