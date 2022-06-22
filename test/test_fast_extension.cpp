/*

Copyright (c) 2007-2009, 2011-2012, 2014-2021, Arvid Norberg
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016, 2018, Steven Siloti
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
#include "settings.hpp"
#include "test_utils.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/aux_/alloca.hpp" // for use of private TORRENT_ALLOCA
#include "libtorrent/time.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"

#include <cstring>
#include <functional>
#include <iostream>
#include <fstream>
#include <cstdarg>
#include <cstdio> // for vsnprintf

using namespace lt;
using namespace std::placeholders;

namespace {

void log(char const* fmt, ...)
{
	va_list v;
	va_start(v, fmt);

	char buf[1024];
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	std::vsnprintf(buf, sizeof(buf), fmt, v);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	va_end(v);

	std::printf("\x1b[1m\x1b[36m%s: %s\x1b[0m\n"
		, time_now_string().c_str(), buf);
}

void print_session_log(lt::session& ses)
{
	print_alerts(ses, "ses", true);
}

int read_message(tcp::socket& s, span<char> buffer)
{
	using namespace lt::aux;
	error_code ec;
	boost::asio::read(s, boost::asio::buffer(buffer.data(), 4)
		, boost::asio::transfer_all(), ec);
	if (ec)
	{
		TEST_ERROR(ec.message());
		return -1;
	}
	char const* ptr = buffer.data();
	int const length = read_int32(ptr);
	if (length > buffer.size())
	{
		log("message size: %d", length);
		TEST_ERROR("message size exceeds max limit");
		return -1;
	}

	boost::asio::read(s, boost::asio::buffer(buffer.data(), std::size_t(length))
		, boost::asio::transfer_all(), ec);
	if (ec)
	{
		TEST_ERROR(ec.message());
		return -1;
	}
	return length;
}

void print_message(span<char const> buffer)
{
	char const* message_name[] = {"choke", "unchoke", "interested", "not_interested"
		, "have", "bitfield", "request", "piece", "cancel", "dht_port", "", "", ""
		, "suggest_piece", "have_all", "have_none", "reject_request", "allowed_fast"};

	std::stringstream message;
	char extra[300];
	extra[0] = 0;
	if (buffer.empty())
	{
		message << "keepalive";
	}
	else
	{
		int const msg = buffer[0];
		if (msg >= 0 && msg < int(sizeof(message_name)/sizeof(message_name[0])))
			message << message_name[msg];
		else if (msg == 20)
			message << "extension msg [" << int(buffer[1]) << "]";
		else
			message << "unknown[" << msg << "]";

		if (msg == 0x6 && buffer.size() == 13)
		{
			peer_request r;
			const char* ptr = buffer.data() + 1;
			r.piece = piece_index_t(aux::read_int32(ptr));
			r.start = aux::read_int32(ptr);
			r.length = aux::read_int32(ptr);
			std::snprintf(extra, sizeof(extra), "p: %d s: %d l: %d"
				, static_cast<int>(r.piece), r.start, r.length);
		}
		else if (msg == 0x11 && buffer.size() == 5)
		{
			const char* ptr = buffer.data() + 1;
			int index = aux::read_int32(ptr);
			std::snprintf(extra, sizeof(extra), "p: %d", index);
		}
		else if (msg == 20 && buffer.size() > 4 && buffer[1] == 0 )
		{
			std::snprintf(extra, sizeof(extra), "%s"
				, print_entry(bdecode(buffer.subspan(2))).c_str());
		}
	}

	log("<== %s %s", message.str().c_str(), extra);
}

void send_allow_fast(tcp::socket& s, int piece)
{
	log("==> allow fast: %d", piece);
	using namespace lt::aux;
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
	using namespace lt::aux;
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

void send_dht_port(tcp::socket& s, int port)
{
	using namespace lt::aux;

	log("==> dht_port");
	char msg[] = "\0\0\0\x03\x09\0\0"; // dht_port
	char* ptr = msg + 5;
	write_uint16(port, ptr);
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 7)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_bitfield(tcp::socket& s, char const* bits)
{
	using namespace lt::aux;

	int num_pieces = int(strlen(bits));
	int packet_size = (num_pieces+7)/8 + 5;
	TORRENT_ALLOCA(msg, char, packet_size);
	std::fill(msg.begin(), msg.end(), 0);
	char* ptr = msg.data();
	write_int32(packet_size-4, ptr);
	write_int8(5, ptr);
	log("==> bitfield [%s]", bits);
	for (int i = 0; i < num_pieces; ++i)
	{
		ptr[i/8] |= (bits[i] == '1' ? 1 : 0) << i % 8;
	}
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg.data(), std::size_t(msg.size()))
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void do_handshake(tcp::socket& s, info_hash_t const& ih, char* buffer)
{
	char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\x10\0\x04"
		"                    " // space for info-hash
		"aaaaaaaaaaaaaaaaaaaa"; // peer-id
	log("==> handshake");
	error_code ec;
	std::memcpy(handshake + 28, ih.v1.data(), 20);
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

	// check for extension protocol support
	bool const lt_extension_protocol = (extensions[5] & 0x10) != 0;
	TEST_CHECK(lt_extension_protocol == true);

	// check for DHT support
	bool const dht_support = (extensions[7] & 0x1) != 0;
#ifndef TORRENT_DISABLE_DHT
	TEST_CHECK(dht_support == true);
#else
	TEST_CHECK(dht_support == false);
#endif

	TEST_CHECK(std::memcmp(buffer + 28, ih.v1.data(), 20) == 0);
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

	using namespace lt::aux;

	char* ptr = &buf[0];
	write_uint32(int(buf.size()) - 4, ptr);
	write_uint8(20, ptr);
	write_uint8(0, ptr);

	error_code ec;
	boost::asio::write(s, boost::asio::buffer(&buf[0], buf.size())
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

void send_request(tcp::socket& s, peer_request req)
{
	using namespace lt::aux;

	log("==> request %d (%d,%d)", static_cast<int>(req.piece), req.start, req.length);
	char msg[] = "\0\0\0\x0d\x06            "; // have_none
	char* ptr = msg + 5;
	write_uint32(static_cast<int>(req.piece), ptr);
	write_uint32(req.start, ptr);
	write_uint32(req.length, ptr);
	error_code ec;
	boost::asio::write(s, boost::asio::buffer(msg, 17)
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

entry read_extension_handshake(tcp::socket& s, span<char> recv_buffer)
{
	for (;;)
	{
		int const len = read_message(s, recv_buffer);
		if (len == -1)
		{
			TEST_ERROR("failed to read message");
			return entry();
		}
		recv_buffer = recv_buffer.first(len);
		print_message(recv_buffer);

		if (len < 4) continue;
		int msg = recv_buffer[0];
		if (msg != 20) continue;
		int extmsg = recv_buffer[1];
		if (extmsg != 0) continue;

		return bdecode(recv_buffer.subspan(2));
	}
}

#ifndef TORRENT_DISABLE_EXTENSIONS
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

	using namespace lt::aux;

	char* ptr = &buf[0];
	write_uint32(int(buf.size()) - 4, ptr);
	write_uint8(20, ptr);
	write_uint8(ut_metadata_msg, ptr);

	log("==> ut_metadata [ type: %d piece: %d ]", type, piece);

	error_code ec;
	boost::asio::write(s, boost::asio::buffer(&buf[0], buf.size())
		, boost::asio::transfer_all(), ec);
	if (ec) TEST_ERROR(ec.message());
}

entry read_ut_metadata_msg(tcp::socket& s, span<char> recv_buffer)
{
	for (;;)
	{
		int const len = read_message(s, recv_buffer);
		if (len == -1)
		{
			TEST_ERROR("failed to read message");
			return entry();
		}
		auto const buffer = recv_buffer.first(len);
		print_message(buffer);

		if (len < 4) continue;
		int const msg = buffer[0];
		if (msg != 20) continue;
		int const extmsg = buffer[1];
		if (extmsg != 1) continue;

		return bdecode(buffer.subspan(2));
	}
}
#endif // TORRENT_DISABLE_EXTENSIONS

std::shared_ptr<torrent_info> setup_peer(tcp::socket& s, io_context& ioc
	, info_hash_t& ih
	, std::shared_ptr<lt::session>& ses, bool incoming = true
	, bool const magnet_link = false, bool const dht = false
	, torrent_flags_t const flags = torrent_flags_t{}
	, torrent_handle* th = nullptr)
{
	std::ofstream out_file;
	std::ofstream* file = nullptr;
	if (flags & torrent_flags::seed_mode)
	{
		error_code ec;
		create_directory("tmp1_fast", ec);
		out_file.open(combine_path("tmp1_fast", "temporary").c_str(), std::ios_base::trunc | std::ios_base::binary);
		file = &out_file;
	}
	else
	{
		error_code ec;
		remove(combine_path("tmp1_fast","temporary").c_str(), ec);
		if (ec) log("remove(): %s", ec.message().c_str());
	}

	std::shared_ptr<torrent_info> t = ::create_torrent(file);
	out_file.close();
	ih = t->info_hashes();

	settings_pack sett = settings();
	sett.set_str(settings_pack::listen_interfaces, test_listen_interface());
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_dht, dht);
	sett.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	sett.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	sett.set_bool(settings_pack::enable_outgoing_utp, false);
	sett.set_bool(settings_pack::enable_incoming_utp, false);
#if TORRENT_ABI_VERSION == 1
	sett.set_bool(settings_pack::rate_limit_utp, true);
#endif
	ses.reset(new lt::session(sett));

	add_torrent_params p;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.flags |= flags;
	if (magnet_link)
		p.info_hashes = ih;
	else
		p.ti = t;
	p.save_path = "tmp1_fast";

	torrent_handle ret = ses->add_torrent(p);
	if (th) *th = ret;

	// wait for the torrent to be ready
	wait_for_downloading(*ses, "ses");

	if (incoming)
	{
		error_code ec;
		s.connect(ep("127.0.0.1", ses->listen_port()), ec);
		if (ec) TEST_ERROR(ec.message());
	}
	else
	{
		tcp::acceptor l(ioc);
		l.open(tcp::v4());
		l.bind(ep("127.0.0.1", 0));
		l.listen();
		tcp::endpoint addr = l.local_endpoint();

		ret.connect_peer(addr);
		print_session_log(*ses);
		l.accept(s);
	}

	print_session_log(*ses);

	return t;
}

} // anonymous namespace

// makes sure that pieces that are allowed and then
// rejected aren't requested again
TORRENT_TEST(reject_fast)
{
	std::cout << "\n === test reject ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	setup_peer(s, ios, ih, ses);

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
		, std::bind(&send_allow_fast, std::ref(s), _1));
	print_session_log(*ses);

	while (!allowed_fast.empty())
	{
		print_session_log(*ses);
		int const len = read_message(s, recv_buffer);
		if (len == -1) break;
		auto buffer = span<char const>(recv_buffer).first(len);
		print_message(buffer);
		int msg = buffer[0];
		if (msg != 0x6) continue;

		using namespace lt::aux;
		char const* ptr = buffer.data() + 1;
		int const piece = read_int32(ptr);

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
	std::this_thread::sleep_for(lt::milliseconds(500));
	print_session_log(*ses);
}

TORRENT_TEST(invalid_suggest)
{
	std::cout << "\n === test suggest ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	setup_peer(s, ios, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	// this is an invalid suggest message. We would not expect to receive a
	// request for that piece index.
	send_suggest_piece(s, -234);
	send_unchoke(s);
	std::this_thread::sleep_for(lt::milliseconds(500));
	print_session_log(*ses);

	int len = read_message(s, recv_buffer);
	auto buffer = span<char const>(recv_buffer).first(len);
	int idx = -1;
	while (len > 0)
	{
		if (buffer[0] == 6)
		{
			char const* ptr = buffer.data() + 1;
			idx = aux::read_int32(ptr);
			break;
		}
		len = read_message(s, recv_buffer);
		buffer = span<char const>(recv_buffer).first(len);
	}
	TEST_CHECK(idx != -234);
	TEST_CHECK(idx != -1);
	s.close();
}

TORRENT_TEST(reject_suggest)
{
	std::cout << "\n === test suggest ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	setup_peer(s, ios, ih, ses);

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
		, std::bind(&send_suggest_piece, std::ref(s), _1));
	print_session_log(*ses);

	send_unchoke(s);
	print_session_log(*ses);

	send_keepalive(s);
	print_session_log(*ses);

	int fail_counter = 100;
	while (!suggested.empty() && fail_counter > 0)
	{
		print_session_log(*ses);
		int const len = read_message(s, recv_buffer);
		if (len == -1) break;
		auto buffer = span<char const>(recv_buffer).first(len);
		print_message(buffer);
		int const msg = buffer[0];
		fail_counter--;
		if (msg != 0x6) continue;

		using namespace lt::aux;
		char const* ptr = buffer.data() + 1;
		int const piece = read_int32(ptr);

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
	std::this_thread::sleep_for(lt::milliseconds(500));
	print_session_log(*ses);
}

TORRENT_TEST(suggest_order)
{
	std::cout << "\n === test suggest ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	setup_peer(s, ios, ih, ses);

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
		, std::bind(&send_suggest_piece, std::ref(s), _1));
	print_session_log(*ses);

	send_unchoke(s);
	print_session_log(*ses);

	int fail_counter = 100;
	while (!suggested.empty() && fail_counter > 0)
	{
		print_session_log(*ses);
		int const len = read_message(s, recv_buffer);
		if (len == -1) break;
		auto const buffer = span<char const>(recv_buffer).first(len);
		print_message({recv_buffer, len});
		int const msg = recv_buffer[0];
		fail_counter--;

		// we're just interested in requests
		if (msg != 0x6) continue;

		using namespace lt::aux;
		char const* ptr = buffer.data() + 1;
		int const piece = read_int32(ptr);

		// make sure we receive the requests inverse order of sending the suggest
		// messages. The last suggest should be the highest priority
		int const expected_piece = suggested.back();
		suggested.pop_back();
		TEST_EQUAL(piece, expected_piece);
	}
	print_session_log(*ses);
	TEST_CHECK(fail_counter > 0);

	s.close();
	std::this_thread::sleep_for(lt::milliseconds(500));
	print_session_log(*ses);
}

TORRENT_TEST(multiple_bitfields)
{
	std::cout << "\n === test multiple bitfields ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	std::shared_ptr<torrent_info> ti = setup_peer(s, ios, ih, ses);
	print_session_log(*ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);

	std::string bitfield;
	bitfield.resize(std::size_t(ti->num_pieces()), '0');
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
	std::this_thread::sleep_for(lt::milliseconds(500));
	print_session_log(*ses);
}

TORRENT_TEST(multiple_have_all)
{
	std::cout << "\n === test multiple have_all ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	std::shared_ptr<torrent_info> ti = setup_peer(s, ios, ih, ses);

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
	std::this_thread::sleep_for(lt::milliseconds(500));
	print_session_log(*ses);
}

// makes sure that pieces that are lost are not requested
TORRENT_TEST(dont_have)
{
	using namespace lt::aux;

	std::cout << "\n === test dont_have ===\n" << std::endl;

	info_hash_t ih;
	torrent_handle th;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	std::shared_ptr<torrent_info> ti = setup_peer(s, ios, ih, ses, true
		, false, false, torrent_flags_t{}, &th);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	std::this_thread::sleep_for(lt::milliseconds(300));
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

		int const len = read_message(s, recv_buffer);
		if (len == -1) break;
		auto const buffer = span<char const>(recv_buffer).first(len);
		print_message(buffer);
		if (len == 0) continue;
		int const msg = buffer[0];
		if (msg != 20) continue;
		int const ext_msg = buffer[1];
		if (ext_msg != 0) continue;

		int pos = 0;
		ec.clear();
		bdecode_node e = bdecode(buffer.subspan(2), ec, &pos);
		if (ec)
		{
			log("failed to parse extension handshake: %s at pos %d"
				, ec.message().c_str(), pos);
		}
		TEST_CHECK(!ec);

		log("extension handshake: %s", print_entry(e).c_str());
		bdecode_node m = e.dict_find_dict("m");
		TEST_CHECK(m);
		if (!m) return;
		bdecode_node dont_have = m.dict_find_int("lt_donthave");
		TEST_CHECK(dont_have);
		if (!dont_have) return;

		lt_dont_have = int(dont_have.int_value());
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

	std::this_thread::sleep_for(lt::milliseconds(1000));

	print_session_log(*ses);

	th.get_peer_info(pi);

	TEST_EQUAL(pi.size(), 1);
	if (pi.size() != 1) return;

	TEST_CHECK(!(pi[0].flags & peer_info::seed));
	TEST_EQUAL(pi[0].pieces.count(), pi[0].pieces.size() - 1);
	TEST_EQUAL(pi[0].pieces[3_piece], false);
	TEST_EQUAL(pi[0].pieces[2_piece], true);
	TEST_EQUAL(pi[0].pieces[1_piece], true);
	TEST_EQUAL(pi[0].pieces[0_piece], true);

	print_session_log(*ses);
}

TORRENT_TEST(extension_handshake)
{
	using namespace lt::aux;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	std::shared_ptr<torrent_info> ti = setup_peer(s, ios, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	entry extensions;
	send_extension_handshake(s, extensions);

	extensions = read_extension_handshake(s, recv_buffer);

	std::cout << extensions << '\n';

	// these extensions are built-in
	TEST_CHECK(extensions["m"]["lt_donthave"].integer() != 0);
#ifndef TORRENT_DISABLE_SHARE_MODE
	TEST_CHECK(extensions["m"]["share_mode"].integer() != 0);
#endif
	TEST_CHECK(extensions["m"]["upload_only"].integer() != 0);
	TEST_CHECK(extensions["m"]["ut_holepunch"].integer() != 0);

	// these require extensions to be enabled
#ifndef TORRENT_DISABLE_EXTENSIONS
	TEST_CHECK(extensions["m"]["ut_metadata"].integer() != 0);
	TEST_CHECK(extensions["m"]["ut_pex"].integer() != 0);
#endif
}

#ifndef TORRENT_DISABLE_EXTENSIONS
// TEST metadata extension messages and edge cases

// this tests sending a request for a metadata piece that's too high. This is
// pos
TORRENT_TEST(invalid_metadata_request)
{
	using namespace lt::aux;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	std::shared_ptr<torrent_info> ti = setup_peer(s, ios, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_all(s);
	print_session_log(*ses);

	entry extensions;
	extensions["m"]["ut_metadata"] = 1;
	send_extension_handshake(s, extensions);

	extensions = read_extension_handshake(s, recv_buffer);

	int ut_metadata = int(extensions["m"]["ut_metadata"].integer());

	log("ut_metadata: %d", ut_metadata);

	// 0 = request
	// 1 = piece
	// 2 = dont-have
	// first send an invalid request
	send_ut_metadata_msg(s, ut_metadata, 0, 1);

	// then send a valid one. If we get a response to the second one,
	// we assume we were not disconnected because of the invalid one
	send_ut_metadata_msg(s, ut_metadata, 0, 0);

	entry ut_metadata_msg = read_ut_metadata_msg(s, recv_buffer);

	// the first response should be "dont-have"
	TEST_EQUAL(ut_metadata_msg["msg_type"].integer(), 2);
	TEST_EQUAL(ut_metadata_msg["piece"].integer(), 1);

	ut_metadata_msg = read_ut_metadata_msg(s, recv_buffer);

	// the second response should be the payload
	TEST_EQUAL(ut_metadata_msg["msg_type"].integer(), 1);
	TEST_EQUAL(ut_metadata_msg["piece"].integer(), 0);

	print_session_log(*ses);
}
#endif // TORRENT_DISABLE_EXTENSIONS


TORRENT_TEST(invalid_request)
{
	std::cout << "\n === test request ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	setup_peer(s, ios, ih, ses);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);
	send_have_none(s);

	peer_request req;
	req.piece = 124134235_piece;
	req.start = 0;
	req.length = 0x4000;
	send_request(s, req);
}

namespace {

void have_all_test(bool const incoming)
{
	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	setup_peer(s, ios, ih, ses, incoming, false, false, torrent_flags::seed_mode);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	print_session_log(*ses);

	// expect to receive a have-all (not a bitfield)
	// since we advertised support for FAST extensions
	for (;;)
	{
		int const len = read_message(s, recv_buffer);
		if (len == -1)
		{
			TEST_ERROR("failed to receive have-all despite advertising support for FAST");
			break;
		}
		auto const buffer = span<char const>(recv_buffer).first(len);
		print_message(buffer);
		int const msg = buffer[0];
		if (msg == 0xe) // have-all
		{
			// success!
			break;
		}
		if (msg == 5) // bitfield
		{
			TEST_ERROR("received bitfield from seed despite advertising support for FAST");
			break;
		}
	}
}

} // anonymous namespace

TORRENT_TEST(outgoing_have_all)
{
	std::cout << "\n === test outgoing have-all ===\n" << std::endl;
	have_all_test(false);
}

TORRENT_TEST(incoming_have_all)
{
	std::cout << "\n === test incoming have-all ===\n" << std::endl;
	have_all_test(true);
}

TORRENT_TEST(dht_port_no_support)
{
	std::cout << "\n === test DHT port (without advertising support) ===\n" << std::endl;

	info_hash_t ih;
	std::shared_ptr<lt::session> ses;
	io_context ios;
	tcp::socket s(ios);
	setup_peer(s, ios, ih, ses, true, true, true);

	char recv_buffer[1000];
	do_handshake(s, ih, recv_buffer);
	send_dht_port(s, 6881);
	print_session_log(*ses);

	s.close();
	std::this_thread::sleep_for(lt::milliseconds(500));
	print_session_log(*ses);
}
// TODO: test sending invalid requests (out of bound piece index, offsets and
// sizes)
