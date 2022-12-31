/*

Copyright (c) 2010-2022, Arvid Norberg
Copyright (c) 2015, Mike Tzou
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2017, Steven Siloti
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
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/session.hpp" // for default_disk_io_constructor
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include <random>
#include <cstring>
#include <thread>
#include <functional>
#include <iostream>
#include <atomic>
#include <array>
#include <chrono>

#ifdef BOOST_ASIO_DYN_LINK
#include <boost/asio/impl/src.hpp>
#endif

namespace {

using namespace lt;
using namespace lt::aux; // for write_* and read_*
using lt::make_address_v4;

using namespace std::placeholders;

void generate_block(span<std::uint32_t> buffer, piece_index_t const piece
	, int const offset)
{
	std::uint32_t const fill = static_cast<std::uint32_t>(
		(static_cast<int>(piece) << 8) | ((offset / 0x4000) & 0xff));
	for (auto& w : buffer) w = fill;
}

// in order to circumvent the restricton of only
// one connection per IP that most clients implement
// all sockets created by this tester are bound to
// uniqe local IPs in the range (127.0.0.1 - 127.255.255.255)
// it's only enabled if the target is also on the loopback
int local_if_counter = 0;
bool local_bind = false;

// when set to true, blocks downloaded are verified to match
// the test torrents
bool verify_downloads = false;

// if this is true, one block in 1000 will be sent corrupt.
// this only applies to dual and upload tests
bool test_corruption = false;

// number of seeds we've spawned. The test is terminated
// when this reaches zero, for dual tests
std::atomic<int> num_seeds(0);

// the kind of test to run. Upload sends data to a
// bittorrent client, download requests data from
// a client and dual uploads and downloads from a client
// at the same time (this is presumably the most realistic
// test)
enum test_mode_t{ none, upload_test, download_test, dual_test };
test_mode_t test_mode = none;

// the number of suggest messages received (total across all peers)
std::atomic<int> num_suggest(0);

// the number of requests made from suggested pieces
std::atomic<int> num_suggested_requests(0);

std::string leaf_path(std::string f)
{
	if (f.empty()) return "";
	char const* first = f.c_str();
	char const* sep = strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	char const* altsep = strrchr(first, '\\');
	if (sep == 0 || altsep > sep) sep = altsep;
#endif
	if (sep == nullptr) return f;

	if (sep - first == int(f.size()) - 1)
	{
		// if the last character is a / (or \)
		// ignore it
		std::size_t len = 0;
		while (sep > first)
		{
			--sep;
			if (*sep == '/'
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
				|| *sep == '\\'
#endif
				)
				return std::string(sep + 1, len);
			++len;
		}
		return std::string(first, len);
	}
	return std::string(sep + 1);
}

namespace {
std::random_device dev;
std::mt19937 rng(dev());
}

struct peer_conn
{
	peer_conn(io_context& ios, int piece_count, int blocks_pp, tcp::endpoint const& ep
		, char const* ih, bool seed_, int churn_, bool corrupt_)
		: s(ios)
		, read_pos(0)
		, state(handshaking)
		, choked(true)
		, current_piece(-1)
		, current_piece_is_allowed(false)
		, block(0)
		, blocks_per_piece(blocks_pp)
		, info_hash(ih)
		, outstanding_requests(0)
		, seed(seed_)
		, fast_extension(false)
		, blocks_received(0)
		, blocks_sent(0)
		, num_pieces(piece_count)
		, start_time(clock_type::now())
		, churn(churn_)
		, corrupt(corrupt_)
		, endpoint(ep)
		, restarting(false)
	{
		corruption_counter = rand() % 1000;
		if (seed) ++num_seeds;
		pieces.reserve(std::size_t(piece_count));
		start_conn();
	}

	void start_conn()
	{
		if (local_bind)
		{
			error_code ec;
			s.open(endpoint.protocol(), ec);
			if (ec)
			{
				close("ERROR OPEN", ec);
				return;
			}
			tcp::endpoint bind_if(address_v4(
				(127 << 24) + unsigned (local_if_counter + 1)), 0);
			++local_if_counter;
			s.bind(bind_if, ec);
			if (ec)
			{
				close("ERROR BIND", ec);
				return;
			}
		}
		restarting = false;
		s.async_connect(endpoint, std::bind(&peer_conn::on_connect, this, _1));
	}

	tcp::socket s;
	char write_buf_proto[100];
	std::uint32_t write_buffer[17*1024/4];
	std::uint32_t buffer[17*1024/4];
	int read_pos;
	int corruption_counter;

	enum state_t
	{
		handshaking,
		sending_request,
		receiving_message
	};
	int state;
	std::vector<piece_index_t> pieces;
	std::vector<piece_index_t> suggested_pieces;
	std::vector<piece_index_t> allowed_fast;
	bool choked;
	piece_index_t current_piece; // the piece we're currently requesting blocks from
	bool current_piece_is_allowed;
	int block;
	int blocks_per_piece;
	char const* info_hash;
	int outstanding_requests;
	// if this is true, this connection is a seed
	bool seed;
	bool fast_extension;
	int blocks_received;
	int blocks_sent;
	int num_pieces;
	time_point start_time;
	time_point end_time;
	int churn;
	bool corrupt;
	tcp::endpoint endpoint;
	bool restarting;

	void on_connect(error_code const& ec)
	{
		if (ec)
		{
			close("ERROR CONNECT", ec);
			return;
		}

		char handshake[] = "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
			"                    " // space for info-hash
			"aaaaaaaaaaaaaaaaaaaa" // peer-id
			"\0\0\0\x01\x02"; // interested
		char* h = static_cast<char*>(malloc(sizeof(handshake)));
		memcpy(h, handshake, sizeof(handshake));
		std::memcpy(h + 28, info_hash, 20);
		std::generate(h + 48, h + 68, [] { return char(rand()); });
		// for seeds, don't send the interested message
		boost::asio::async_write(s, boost::asio::buffer(h, (sizeof(handshake) - 1) - (seed ? 5 : 0))
			, std::bind(&peer_conn::on_handshake, this, h, _1, _2));
	}

	void on_handshake(char* h, error_code const& ec, size_t)
	{
		free(h);
		if (ec)
		{
			close("ERROR SEND HANDSHAKE", ec);
			return;
		}

		// read handshake
		boost::asio::async_read(s, boost::asio::buffer(buffer, 68)
			, std::bind(&peer_conn::on_handshake2, this, _1, _2));
	}

	void on_handshake2(error_code const& ec, size_t)
	{
		if (ec)
		{
			close("ERROR READ HANDSHAKE", ec);
			return;
		}

		// buffer is the full 68 byte handshake
		// look at the extension bits

		fast_extension = (reinterpret_cast<char const*>(buffer)[27] & 4) != 0;

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
		if (fast_extension)
		{
			char* ptr = write_buf_proto;
			// have_all
			write_uint32(1, ptr);
			write_uint8(0xe, ptr);
			// unchoke
			write_uint32(1, ptr);
			write_uint8(1, ptr);
			boost::asio::async_write(s, boost::asio::buffer(write_buf_proto, std::size_t(ptr - write_buf_proto))
				, std::bind(&peer_conn::on_sent, this, _1, _2, "ERROR SENT HAVE ALL"));
		}
		else
		{
			// bitfield
			int len = (num_pieces + 7) / 8;
			char* ptr = reinterpret_cast<char*>(buffer);
			write_uint32(len + 1, ptr);
			write_uint8(5, ptr);
			memset(ptr, 255, std::size_t(len));
			ptr += len;
			// unchoke
			write_uint32(1, ptr);
			write_uint8(1, ptr);
			boost::asio::async_write(s, boost::asio::buffer(buffer, std::size_t(len + 10))
				, std::bind(&peer_conn::on_sent, this, _1, _2, "ERROR SENT HAVE ALL"));
		}
	}

	void on_sent(error_code const& ec, size_t, char const* msg)
	{
		if (ec)
		{
			close(msg, ec);
			return;
		}

		// read message
		boost::asio::async_read(s, boost::asio::buffer(buffer, 4)
			, std::bind(&peer_conn::on_msg_length, this, _1, _2));
	}

	bool write_request()
	{
		// if we're choked (and there are no allowed-fast pieces left)
		if (choked && allowed_fast.empty() && !current_piece_is_allowed) return false;

		// if there are no pieces left to request
		if (pieces.empty() && suggested_pieces.empty()
			&& current_piece == piece_index_t(-1))
		{
			return false;
		}

		if (current_piece == piece_index_t(-1))
		{
			// pick a new piece
			if (choked && allowed_fast.size() > 0)
			{
				current_piece = allowed_fast.front();
				allowed_fast.erase(allowed_fast.begin());
				current_piece_is_allowed = true;
			}
			else if (suggested_pieces.size() > 0)
			{
				current_piece = suggested_pieces.front();
				suggested_pieces.erase(suggested_pieces.begin());
				++num_suggested_requests;
				current_piece_is_allowed = false;
			}
			else if (pieces.size() > 0)
			{
				current_piece = pieces.front();
				pieces.erase(pieces.begin());
				current_piece_is_allowed = false;
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
		memcpy(m, msg, sizeof(msg));
		char* ptr = m + 5;
		write_uint32(static_cast<int>(current_piece), ptr);
		write_uint32(block * 16 * 1024, ptr);
		write_uint32(16 * 1024, ptr);
		boost::asio::async_write(s, boost::asio::buffer(m, sizeof(msg) - 1)
			, std::bind(&peer_conn::on_req_sent, this, m, _1, _2));

		++outstanding_requests;
		++block;
		if (block == blocks_per_piece)
		{
			block = 0;
			current_piece = piece_index_t(-1);
			current_piece_is_allowed = false;
		}
		return true;
	}

	void on_req_sent(char* m, error_code const& ec, size_t)
	{
		free(m);
		if (ec)
		{
			close("ERROR SEND REQUEST", ec);
			return;
		}

		work_download();
	}

	void close(char const* msg, error_code const& ec)
	{
		end_time = clock_type::now();
		char tmp[1024];
		std::snprintf(tmp, sizeof(tmp), "%s: %s", msg, ec ? ec.message().c_str() : "");
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
		if (seed) --num_seeds;
	}

	void work_download()
	{
		if (pieces.empty()
			&& suggested_pieces.empty()
			&& current_piece == piece_index_t(-1)
			&& outstanding_requests == 0
			&& blocks_received >= num_pieces * blocks_per_piece)
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
		boost::asio::async_read(s, boost::asio::buffer(buffer, 4)
			, std::bind(&peer_conn::on_msg_length, this, _1, _2));
	}

	void on_msg_length(error_code const& ec, size_t)
	{
		if ((ec == boost::asio::error::operation_aborted || ec == boost::asio::error::bad_descriptor)
			&& restarting)
		{
			start_conn();
			return;
		}

		if (ec)
		{
			close("ERROR RECEIVE MESSAGE PREFIX", ec);
			return;
		}
		char* ptr = reinterpret_cast<char*>(buffer);
		unsigned int length = read_uint32(ptr);
		if (length > sizeof(buffer))
		{
			std::fprintf(stderr, "len: %u\n", length);
			close("ERROR RECEIVE MESSAGE PREFIX: packet too big", error_code());
			return;
		}
		boost::asio::async_read(s, boost::asio::buffer(buffer, length)
			, std::bind(&peer_conn::on_message, this, _1, _2));
	}

	void on_message(error_code const& ec, size_t bytes_transferred)
	{
		if ((ec == boost::asio::error::operation_aborted || ec == boost::asio::error::bad_descriptor)
			&& restarting)
		{
			start_conn();
			return;
		}

		if (ec)
		{
			close("ERROR RECEIVE MESSAGE", ec);
			return;
		}
		char* ptr = reinterpret_cast<char*>(buffer);
		int msg = read_uint8(ptr);

		if (test_mode == dual_test && num_seeds == 0)
		{
			TORRENT_ASSERT(!seed);
			close("NO MORE SEEDS, test done", error_code());
			return;
		}

		//std::printf("msg: %d len: %d\n", msg, int(bytes_transferred));

		if (seed)
		{
			if (msg == 6)
			{
				if (bytes_transferred != 13)
				{
					close("REQUEST packet has invalid size", error_code());
					return;
				}
				piece_index_t const piece = piece_index_t(aux::read_int32(ptr));
				int const start = aux::read_int32(ptr);
				int const length = aux::read_int32(ptr);
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
				boost::asio::async_read(s, boost::asio::buffer(buffer, 4)
					, std::bind(&peer_conn::on_msg_length, this, _1, _2));
			}
		}
		else
		{
			if (msg == 0xe) // have_all
			{
				// build a list of all pieces and request them all!
				pieces.resize(std::size_t(num_pieces));
				for (std::size_t i = 0; i < pieces.size(); ++i)
					pieces[i] = piece_index_t(int(i));
				std::shuffle(pieces.begin(), pieces.end(), rng);
			}
			else if (msg == 4) // have
			{
				piece_index_t const piece(aux::read_int32(ptr));
				if (pieces.empty()) pieces.push_back(piece);
				else pieces.insert(pieces.begin() + (unsigned(rand()) % pieces.size()), piece);
			}
			else if (msg == 5) // bitfield
			{
				pieces.reserve(std::size_t(num_pieces));
				piece_index_t piece(0);
				for (int i = 0; i < int(bytes_transferred); ++i)
				{
					int mask = 0x80;
					for (int k = 0; k < 8; ++k)
					{
						if (piece > piece_index_t(num_pieces)) break;
						if (*ptr & mask) pieces.push_back(piece);
						mask >>= 1;
						++piece;
					}
					++ptr;
				}
				std::shuffle(pieces.begin(), pieces.end(), rng);
			}
			else if (msg == 7) // piece
			{
				if (verify_downloads)
				{
					piece_index_t const piece(read_int32(ptr));
					int start = read_int32(ptr);
					int size = int(bytes_transferred) - 9;
					verify_piece(piece, start, ptr, size);
				}
				++blocks_received;
				--outstanding_requests;
				piece_index_t const piece = piece_index_t(aux::read_int32(ptr));
				int start = aux::read_int32(ptr);

				if (churn && (blocks_received % churn) == 0) {
					outstanding_requests = 0;
					restarting = true;
					s.close();
					return;
				}
				if ((start + int(bytes_transferred)) / 0x4000 == blocks_per_piece)
				{
					write_have(piece);
					return;
				}
			}
			else if (msg == 13) // suggest
			{
				piece_index_t const piece(aux::read_int32(ptr));
				auto i = std::find(pieces.begin(), pieces.end(), piece);
				if (i != pieces.end())
				{
					pieces.erase(i);
					suggested_pieces.push_back(piece);
					++num_suggest;
				}
			}
			else if (msg == 16) // reject request
			{
				piece_index_t const piece(aux::read_int32(ptr));
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
						current_piece = piece_index_t(-1);
						current_piece_is_allowed = false;
					}
				}
				--outstanding_requests;
				std::fprintf(stderr, "REJECT: [ piece: %d start: %d length: %d ]\n"
					, static_cast<int>(piece), start, length);
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
				piece_index_t const piece = piece_index_t(aux::read_int32(ptr));
				auto i = std::find(pieces.begin(), pieces.end(), piece);
				if (i != pieces.end())
				{
					pieces.erase(i);
					allowed_fast.push_back(piece);
				}
			}
			work_download();
		}
	}

	bool verify_piece(piece_index_t const piece, int start, char const* ptr, int size)
	{
		std::uint32_t const* buf = reinterpret_cast<std::uint32_t const*>(ptr);
		std::uint32_t const fill = static_cast<std::uint32_t>(
			(static_cast<int>(piece) << 8) | ((start / 0x4000) & 0xff));
		for (int i = 0; i < size / 4; ++i)
		{
			if (buf[i] != fill)
			{
				std::fprintf(stderr, "received invalid block. piece %d block %d\n"
					, static_cast<int>(piece), start / 0x4000);
				exit(1);
			}
		}
		return true;
	}

	void write_piece(piece_index_t const piece, int start, int length)
	{
		generate_block({write_buffer, length / 4}
			, piece, start);

		if (corrupt)
		{
			--corruption_counter;
			if (corruption_counter == 0)
			{
				corruption_counter = 1000;
				std::memset(write_buffer, 0, 10);
			}
		}
		char* ptr = write_buf_proto;
		write_uint32(9 + length, ptr);
		assert(length == 0x4000);
		write_uint8(7, ptr);
		write_uint32(static_cast<int>(piece), ptr);
		write_uint32(start, ptr);
		std::array<boost::asio::const_buffer, 2> vec;
		vec[0] = boost::asio::buffer(write_buf_proto, std::size_t(ptr - write_buf_proto));
		vec[1] = boost::asio::buffer(write_buffer, std::size_t(length));
		boost::asio::async_write(s, vec, std::bind(&peer_conn::on_sent, this, _1, _2, "ERROR SENT PIECE"));
		++blocks_sent;
		if (churn && (blocks_sent % churn) == 0 && seed) {
			outstanding_requests = 0;
			restarting = true;
			s.close();
		}
	}

	void write_have(piece_index_t const piece)
	{
		char* ptr = write_buf_proto;
		write_uint32(5, ptr);
		write_uint8(4, ptr);
		write_uint32(static_cast<int>(piece), ptr);
		boost::asio::async_write(s, boost::asio::buffer(write_buf_proto, 9), std::bind(&peer_conn::on_sent, this, _1, _2, "ERROR SENT HAVE"));
	}
};

[[noreturn]] void print_usage()
{
	std::fprintf(stderr, "usage: connection_tester command [options]\n\n"
		"command is one of:\n"
		"  gen-torrent        generate a test torrent\n"
		"    options for this command:\n"
		"    -s <size>          the size of the torrent in megabytes\n"
		"    -n <num-files>     the number of files in the test torrent\n"
		"    -t <file>          the file to save the .torrent file to\n"
		"  gen-data             generate the data file(s) for the test torrent\n"
		"    options for this command:\n"
		"    -t <file>          the torrent file that was previously generated\n"
		"    -P <path>          the path to where the data should be stored\n\n"
		"  gen-test-torrents    generate many test torrents (cannot be used for up/down tests)\n"
		"    options for this command:\n"
		"    -N <num-torrents>  number of torrents to generate\n"
		"    -n <num-files>     number of files in each torrent\n"
		"    -t <name>          base name of torrent files (index is appended)\n\n"
		"    -T <URL>           add the specified tracker URL to each torrent\n"
		"                       this option may appear multiple times\n\n"
		"  upload               start an uploader test\n"
		"  download             start a downloader test\n"
		"  dual                 start a download and upload test\n"
		"    options for these commands:\n"
		"    -c <num-conns>     the number of connections to make to the target\n"
		"    -d <dst>           the IP address of the target\n"
		"    -p <dst-port>      the port the target listens on\n"
		"    -t <torrent-file>  the torrent file previously generated by gen-torrent\n"
		"    -C                 send corrupt pieces sometimes (applies to upload and dual)\n"
		"    -r <reconnects>    churn - number of reconnects per second\n\n"
		"examples:\n\n"
		"connection_tester gen-torrent -s 1024 -n 4 -t test.torrent\n"
		"connection_tester upload -c 200 -d 127.0.0.1 -p 6881 -t test.torrent\n"
		"connection_tester download -c 200 -d 127.0.0.1 -p 6881 -t test.torrent\n"
		"connection_tester dual -c 200 -d 127.0.0.1 -p 6881 -t test.torrent\n");
	exit(1);
}

void hasher_thread(lt::aux::vector<sha1_hash, piece_index_t>* output
	, lt::file_storage const& fs
	, piece_index_t const start_piece
	, piece_index_t const end_piece
	, bool print)
{
	if (print) std::fprintf(stderr, "\n");
	std::uint32_t piece[0x4000 / 4];
	int const piece_size = fs.piece_length();

	std::vector<file_slice> files = fs.map_block(start_piece, 0
		, std::min(static_cast<int>(end_piece - start_piece) * std::int64_t(piece_size)
			, fs.total_size() - static_cast<int>(start_piece) * std::int64_t(piece_size)));

	for (piece_index_t i = start_piece; i < end_piece; ++i)
	{
		hasher ph;
		for (int j = 0; j < piece_size; j += 0x4000)
		{
			generate_block(piece, i, j);

			// if any part of this block overlaps with a pad-file, we need to
			// clear those bytes to 0
			for (int k = 0; k < 0x4000; )
			{
				if (files.empty())
				{
					TORRENT_ASSERT(i == prev(end_piece));
					TORRENT_ASSERT(k > 0);
					TORRENT_ASSERT(k < 0x4000);
					// this is the last piece of the torrent, and the piece
					// extends a bit past the end of the last file. This part
					// should be truncated
					ph.update(reinterpret_cast<char*>(piece), k);
					goto out;
				}
				auto& f = files.front();
				int const range = int(std::min(std::int64_t(0x4000 - k), f.size));
				if (fs.pad_file_at(f.file_index))
					std::memset(reinterpret_cast<char*>(piece) + k, 0, std::size_t(range));

				f.offset += range;
				f.size -= range;
				k += range;
				if (f.size == 0) files.erase(files.begin());
			}
			ph.update(reinterpret_cast<char*>(piece), 0x4000);
		}
out:
		(*output)[i] = ph.final();
		int const range = static_cast<int>(end_piece) - static_cast<int>(start_piece);
		if (print && (static_cast<int>(i) & 1))
		{
			int const delta_piece = static_cast<int>(i) - static_cast<int>(start_piece);
			std::fprintf(stderr, "\r%.1f %% ", double(delta_piece * 100) / double(range));
		}
	}
	if (print) std::fprintf(stderr, "\n");
}

// size is in megabytes
void generate_torrent(std::vector<char>& buf, int num_pieces, int num_files
	, char const* torrent_name)
{
	file_storage fs;
	// 1 MiB piece size
	const int piece_size = 1024 * 1024;
	const std::int64_t total_size = std::int64_t(piece_size) * num_pieces;

	std::int64_t s = total_size;
	int file_index = 0;
	std::int64_t file_size = total_size / num_files;
	while (s > 0)
	{
		char b[100];
		std::snprintf(b, sizeof(b), "%s/stress_test%d", torrent_name, file_index);
		++file_index;
		fs.add_file(b, std::min(s, file_size));
		s -= file_size;
		file_size += 200;
	}

	lt::create_torrent t(fs, piece_size, lt::create_torrent::v1_only);

	num_pieces = t.num_pieces();

	int const num_threads = std::thread::hardware_concurrency()
		? int(std::thread::hardware_concurrency()) : 4;
	std::printf("hashing in %d threads\n", num_threads);

	std::vector<std::thread> threads;
	threads.reserve(std::size_t(num_threads));
	lt::aux::vector<lt::sha1_hash, piece_index_t> hashes{static_cast<std::size_t>(num_pieces)};
	for (int i = 0; i < num_threads; ++i)
	{
		threads.emplace_back(&hasher_thread, &hashes, t.files()
			, piece_index_t(i * num_pieces / num_threads)
			, piece_index_t((i + 1) * num_pieces / num_threads)
			, i == 0);
	}

	for (auto& i : threads)
		i.join();

	for (auto i : t.piece_range())
		t.set_hash(i, hashes[i]);

	bencode(std::back_inserter(buf), t.generate());
}

void write_handler(file_storage const& fs
	, disk_interface& disk, storage_holder& st
	, piece_index_t& piece, int& offset
	, lt::storage_error const& error)
{
	if (error)
	{
		std::fprintf(stderr, "storage error: %s\n", error.ec.message().c_str());
		return;
	}


	if (static_cast<int>(piece) & 1)
	{
		std::fprintf(stderr, "\r%.1f %% "
			, double(static_cast<int>(piece) * 100) / double(fs.num_pieces()));
	}

	if (piece >= fs.end_piece()) return;
	offset += 0x4000;
	if (offset >= fs.piece_size(piece))
	{
		offset = 0;
		++piece;
	}
	if (piece >= fs.end_piece())
	{
		disk.abort(false);
		return;
	}

	std::uint32_t buffer[0x4000 / 4];
	generate_block(buffer, piece, offset);

	int const left_in_piece = fs.piece_size(piece) - offset;
	if (left_in_piece <= 0) return;

	disk.async_write(st, { piece, offset, std::min(left_in_piece, 0x4000)}
		, reinterpret_cast<char const*>(buffer)
		, std::shared_ptr<disk_observer>()
		, [&](lt::storage_error const& e)
		{ write_handler(fs, disk, st, piece, offset, e); });

	disk.submit_jobs();
}

void generate_data(std::string const path, torrent_info const& ti)
{
	io_context ios;
	counters stats_counters;
	settings_pack sett = default_settings();
	std::unique_ptr<lt::disk_interface> disk = default_disk_io_constructor(ios, sett, stats_counters);

	file_storage const& fs = ti.files();

	aux::vector<download_priority_t, file_index_t> priorities;
	sha1_hash info_hash;
	storage_params params{
		fs,
		nullptr,
		path,
		storage_mode_sparse,
		priorities,
		info_hash
	};

	storage_holder st = disk->new_torrent(params, std::shared_ptr<void>());

	piece_index_t piece(0);
	int offset = 0;

	std::uint32_t buffer[0x4000 / 4];
	generate_block(buffer, piece, offset);

	disk->async_write(st, { piece, offset, std::min(fs.piece_size(piece), 0x4000)}
		, reinterpret_cast<char const*>(buffer)
		, std::shared_ptr<disk_observer>()
		, [&](lt::storage_error const& error)
		{ write_handler(fs, *disk, st, piece, offset, error); });

	// keep 10 writes in flight at all times
	for (int i = 0; i < 10; ++i)
	{
		write_handler(fs, *disk, st, piece, offset, lt::storage_error());
	}

	disk->submit_jobs();

	ios.run();
}

void io_thread(io_context* ios) try
{
	ios->run();
}
catch (std::exception const& e)
{
	std::fprintf(stderr, "ERROR: %s\n", e.what());
}

} // anonymous namespace

int main(int argc, char* argv[])
{
	if (argc <= 1) print_usage();

	char const* command = argv[1];
	int size = 1000;
	int num_files = 10;
	int num_torrents = 1;
	char const* torrent_file = "benchmark.torrent";
	char const* data_path = ".";
	int num_connections = 50;
	char const* destination_ip = "127.0.0.1";
	int destination_port = 6881;
	int churn = 0;
	std::vector<std::string> trackers;

	argv += 2;
	argc -= 2;

	while (argc > 0)
	{
		char const* optname = argv[0];
		++argv;
		--argc;

		if (optname[0] != '-' || strlen(optname) != 2)
		{
			std::fprintf(stderr, "unknown option: %s\n", optname);
			continue;
		}

		// options with no arguments
		switch (optname[1])
		{
			case 'C': test_corruption = true; continue;
		}

		if (argc == 0)
		{
			std::fprintf(stderr, "missing argument for option: %s\n", optname);
			break;
		}

		char const* opt = argv[0];
		++argv;
		--argc;

		switch (optname[1])
		{
			case 's': size = atoi(opt); break;
			case 'n': num_files = atoi(opt); break;
			case 'N': num_torrents = atoi(opt); break;
			case 't': torrent_file = opt; break;
			case 'T': trackers.push_back(opt); break;
			case 'P': data_path = opt; break;
			case 'c': num_connections = atoi(opt); break;
			case 'p': destination_port = atoi(opt); break;
			case 'd': destination_ip = opt; break;
			case 'r': churn = atoi(opt); break;
			default: std::fprintf(stderr, "unknown option: %s\n", optname);
		}
	}

	if (command == "gen-torrent"_sv)
	{
		std::vector<char> tmp;
		std::string name = leaf_path(torrent_file);
		name = name.substr(0, name.find_last_of('.'));
		std::printf("generating torrent: %s\n", name.c_str());
		generate_torrent(tmp, size ? size : 1024, num_files ? num_files : 1
			, name.c_str() );

		FILE* output = stdout;
		if ("-"_sv != torrent_file)
		{
			if( (output = std::fopen(torrent_file, "wb+")) == nullptr)
			{
				std::fprintf(stderr, "Could not open file '%s' for writing: %s\n"
					, torrent_file, std::strerror(errno));
				exit(2);
			}
		}
		std::fprintf(stderr, "writing file to: %s\n", torrent_file);
		fwrite(&tmp[0], 1, tmp.size(), output);
		if (output != stdout)
			std::fclose(output);

		return 0;
	}
	else if (command == "gen-data"_sv)
	{
		error_code ec;
		torrent_info ti(torrent_file, ec);
		if (ec)
		{
			std::fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", ec.message().c_str());
			return 1;
		}
		generate_data(data_path, ti);
		return 0;
	}
	else if (command == "gen-test-torrents"_sv)
	{
		std::vector<char> buf;
		for (int i = 0; i < num_torrents; ++i)
		{
			char torrent_name[100];
			std::snprintf(torrent_name, sizeof(torrent_name), "%s-%d.torrent", torrent_file, i);

			file_storage fs;
			for (int j = 0; j < num_files; ++j)
			{
				char file_name[100];
				std::snprintf(file_name, sizeof(file_name), "%s-%d/file-%d", torrent_file, i, j);
				fs.add_file(file_name, std::int64_t(j + i + 1) * 251);
			}
			// 1 MiB piece size
			const int piece_size = 1024 * 1024;
			lt::create_torrent t(fs, piece_size, lt::create_torrent::v1_only);
			sha1_hash dummy("abcdefghijklmnopqrst");
			for (auto const k : t.piece_range())
				t.set_hash(k, dummy);

			int tier = 0;
			for (auto const& tr : trackers)
				t.add_tracker(tr, tier++);

			buf.clear();
			std::back_insert_iterator<std::vector<char>> out(buf);
			bencode(out, t.generate());
			FILE* f = std::fopen(torrent_name, "w+");
			if (f == nullptr)
			{
				std::fprintf(stderr, "Could not open file '%s' for writing: %s\n"
					, torrent_name, std::strerror(errno));
				return 1;
			}
			size_t ret = fwrite(buf.data(), 1, buf.size(), f);
			if (ret != buf.size())
			{
				std::fprintf(stderr, "write returned: %d (expected %d)\n", int(ret), int(buf.size()));
				std::fclose(f);
				return 1;
			}
			std::printf("wrote %s\n", torrent_name);
			std::fclose(f);
		}
		return 0;
	}
	else if (command == "upload"_sv)
	{
		test_mode = upload_test;
	}
	else if (command == "download"_sv)
	{
		test_mode = download_test;
	}
	else if (command == "dual"_sv)
	{
		test_mode = dual_test;
	}
	else
	{
		std::fprintf(stderr, "unknown command: %s\n\n", command);
		print_usage();
	}

	error_code ec;
	address_v4 addr = make_address_v4(destination_ip, ec);
	if (ec)
	{
		std::fprintf(stderr, "ERROR RESOLVING %s: %s\n", destination_ip, ec.message().c_str());
		return 1;
	}
	tcp::endpoint ep(addr, std::uint16_t(destination_port));

#if !defined __APPLE__
	// apparently darwin doesn't seems to let you bind to
	// loopback on any other IP than 127.0.0.1
	std::uint32_t const ip = addr.to_uint();
	if ((ip & 0xff000000) == 0x7f000000)
	{
		local_bind = true;
	}
#endif

	torrent_info ti(torrent_file, ec);
	if (ec)
	{
		std::fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", ec.message().c_str());
		return 1;
	}

	std::vector<peer_conn*> conns;
	conns.reserve(std::size_t(num_connections));
	int const num_threads = 2;
	io_context ios[num_threads];
	lt::sha1_hash const ih = ti.info_hash();
	for (int i = 0; i < num_connections; ++i)
	{
		bool corrupt = test_corruption && (i & 1) == 0;
		bool seed = false;
		if (test_mode == upload_test) seed = true;
		else if (test_mode == dual_test) seed = (i & 1);
		conns.push_back(new peer_conn(ios[i % num_threads], ti.num_pieces(), ti.piece_length() / 16 / 1024
			, ep, ih.data(), seed, churn, corrupt));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		ios[i % num_threads].poll_one();
	}

	std::thread t1(&io_thread, &ios[0]);
	std::thread t2(&io_thread, &ios[1]);

	t1.join();
	t2.join();

	double up = 0.0;
	double down = 0.0;
	std::int64_t total_sent = 0;
	std::int64_t total_received = 0;

	for (peer_conn* p : conns)
	{
		int time = int(total_milliseconds(p->end_time - p->start_time));
		if (time == 0) time = 1;
		total_sent += p->blocks_sent;
		total_received += p->blocks_received;
		up += (std::int64_t(p->blocks_sent) * 0x4000) / time / 1000.0;
		down += (std::int64_t(p->blocks_received) * 0x4000) / time / 1000.0;
		delete p;
	}

	std::printf("=========================\n"
		"suggests: %d suggested-requests: %d\n"
		"total sent: %.1f %% received: %.1f %%\n"
		"rate sent: %.1f MB/s received: %.1f MB/s\n"
		, int(num_suggest), int(num_suggested_requests)
		, total_sent * 0x4000 * 100.0 / double(ti.total_size())
		, total_received * 0x4000 * 100.0 / double(ti.total_size())
		, up, down);

	return 0;
}
