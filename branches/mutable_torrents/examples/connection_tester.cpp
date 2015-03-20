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
#include "libtorrent/socket_io.hpp"
#include "libtorrent/file_pool.hpp"
#include <cstring>
#include <boost/bind.hpp>
#include <iostream>
#include <boost/array.hpp>
#include <boost/detail/atomic_count.hpp>

#if BOOST_ASIO_DYN_LINK
#if BOOST_VERSION >= 104500
#include <boost/asio/impl/src.hpp>
#elif BOOST_VERSION >= 104400
#include <boost/asio/impl/src.cpp>
#endif
#endif

using namespace libtorrent;
using namespace libtorrent::detail; // for write_* and read_*

void generate_block(boost::uint32_t* buffer, int piece, int start, int length)
{
	boost::uint32_t fill = (piece << 8) | ((start / 0x4000) & 0xff);
	for (int i = 0; i < length / 4; ++i)
	{
		buffer[i] = fill;
	}
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
static boost::detail::atomic_count num_seeds(0);

// the kind of test to run. Upload sends data to a
// bittorrent client, download requests data from
// a client and dual uploads and downloads from a client
// at the same time (this is presumably the most realistic
// test)
enum test_mode_t{ none, upload_test, download_test, dual_test };
test_mode_t test_mode = none;

// the number of suggest messages received (total across all peers)
boost::detail::atomic_count num_suggest(0);

// the number of requests made from suggested pieces
boost::detail::atomic_count num_suggested_requests(0);

void sleep_ms(int milliseconds)
{
#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
	Sleep(milliseconds);
#elif defined TORRENT_BEOS
	snooze_until(system_time() + boost::int64_t(milliseconds) * 1000, B_SYSTEM_TIMEBASE);
#else
	usleep(milliseconds * 1000);
#endif
}

std::string leaf_path(std::string f)
{
	if (f.empty()) return "";
	char const* first = f.c_str();
	char const* sep = strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	char const* altsep = strrchr(first, '\\');
	if (sep == 0 || altsep > sep) sep = altsep;
#endif
	if (sep == 0) return f;

	if (sep - first == int(f.size()) - 1)
	{
		// if the last character is a / (or \)
		// ignore it
		int len = 0;
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

struct peer_conn
{
	peer_conn(io_service& ios, int num_pieces, int blocks_pp, tcp::endpoint const& ep
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
		, num_pieces(num_pieces)
		, start_time(clock_type::now())
		, churn(churn_)
		, corrupt(corrupt_)
		, endpoint(ep)
		, restarting(false)
	{
		corruption_counter = rand() % 1000;
		if (seed) ++num_seeds;
		pieces.reserve(num_pieces);
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
				close("ERROR OPEN: %s", ec);
				return;
			}
			tcp::endpoint bind_if(address_v4(
				(127 << 24)
				+ ((local_if_counter / 255) << 16)
				+ ((local_if_counter % 255) + 1)), 0);
			++local_if_counter;
			s.bind(bind_if, ec);
			if (ec)
			{
				close("ERROR BIND: %s", ec);
				return;
			}
		}
		restarting = false;
		s.async_connect(endpoint, boost::bind(&peer_conn::on_connect, this, _1));
	}

	stream_socket s;
	char write_buf_proto[100];
	boost::uint32_t write_buffer[17*1024/4];
	boost::uint32_t buffer[17*1024/4];
	int read_pos;
	int corruption_counter;

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
			close("ERROR CONNECT: %s", ec);
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
			close("ERROR SEND HANDSHAKE: %s", ec);
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
			close("ERROR READ HANDSHAKE: %s", ec);
			return;
		}

		// buffer is the full 68 byte handshake
		// look at the extension bits

		fast_extension = ((char*)buffer)[27] & 4;

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
			error_code ec;
			boost::asio::async_write(s, libtorrent::asio::buffer(write_buf_proto, ptr - write_buf_proto)
				, boost::bind(&peer_conn::on_have_all_sent, this, _1, _2));
		}
		else
		{
			// bitfield
			int len = (num_pieces + 7) / 8;
			char* ptr = (char*)buffer;
			write_uint32(len + 1, ptr);
			write_uint8(5, ptr);
			memset(ptr, 255, len);
			ptr += len;
			// unchoke
			write_uint32(1, ptr);
			write_uint8(1, ptr);
			error_code ec;
			boost::asio::async_write(s, libtorrent::asio::buffer((char*)buffer, len + 10)
				, boost::bind(&peer_conn::on_have_all_sent, this, _1, _2));
		}
	
	}

	void on_have_all_sent(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			close("ERROR SEND HAVE ALL: %s", ec);
			return;
		}

		// read message
		boost::asio::async_read(s, asio::buffer((char*)buffer, 4)
			, boost::bind(&peer_conn::on_msg_length, this, _1, _2));
	}

	bool write_request()
	{
		// if we're choked (and there are no allowed-fast pieces left)
		if (choked && allowed_fast.empty() && !current_piece_is_allowed) return false;

		// if there are no pieces left to request
		if (pieces.empty() && suggested_pieces.empty() && current_piece == -1) return false;

		if (current_piece == -1)
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
				TORRENT_ASSERT(false);
			}
		}
		char msg[] = "\0\0\0\xd\x06"
			"    " // piece
			"    " // offset
			"    "; // length
		char* m = (char*)malloc(sizeof(msg));
		memcpy(m, msg, sizeof(msg));
		char* ptr = m + 5;
		write_uint32(current_piece, ptr);
		write_uint32(block * 16 * 1024, ptr);
		write_uint32(16 * 1024, ptr);
		error_code ec;
		boost::asio::async_write(s, libtorrent::asio::buffer(m, sizeof(msg) - 1)
			, boost::bind(&peer_conn::on_req_sent, this, m, _1, _2));

		++outstanding_requests;
		++block;
		if (block == blocks_per_piece)
		{
			block = 0;
			current_piece = -1;
			current_piece_is_allowed = false;
		}
		return true;
	}

	void on_req_sent(char* m, error_code const& ec, size_t bytes_transferred)
	{
		free(m);
		if (ec)
		{
			close("ERROR SEND REQUEST: %s", ec);
			return;
		}

		work_download();
	}

	void close(char const* fmt, error_code const& ec)
	{
		end_time = clock_type::now();
		char tmp[1024];
		snprintf(tmp, sizeof(tmp), fmt, ec.message().c_str());
		int time = total_milliseconds(end_time - start_time);
		if (time == 0) time = 1;
		float up = (boost::int64_t(blocks_sent) * 0x4000) / time / 1000.f;
		float down = (boost::int64_t(blocks_received) * 0x4000) / time / 1000.f;
		error_code e;

		char ep_str[200];
		address const& addr = s.local_endpoint(e).address();
#if TORRENT_USE_IPV6
		if (addr.is_v6())
			snprintf(ep_str, sizeof(ep_str), "[%s]:%d", addr.to_string(e).c_str()
				, s.local_endpoint(e).port());
		else
#endif
			snprintf(ep_str, sizeof(ep_str), "%s:%d", addr.to_string(e).c_str()
				, s.local_endpoint(e).port());
		printf("%s ep: %s sent: %d received: %d duration: %d ms up: %.1fMB/s down: %.1fMB/s\n"
			, tmp, ep_str, blocks_sent, blocks_received, time, up, down);
		if (seed) --num_seeds;
	}

	void work_download()
	{
		if (pieces.empty()
			&& suggested_pieces.empty()
			&& current_piece == -1
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
		boost::asio::async_read(s, asio::buffer((char*)buffer, 4)
			, boost::bind(&peer_conn::on_msg_length, this, _1, _2));
	}

	void on_msg_length(error_code const& ec, size_t bytes_transferred)
	{
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
		char* ptr = (char*)buffer;
		unsigned int length = read_uint32(ptr);
		if (length > sizeof(buffer))
		{
			fprintf(stderr, "len: %d\n", length);
			close("ERROR RECEIVE MESSAGE PREFIX: packet too big", error_code());
			return;
		}
		boost::asio::async_read(s, asio::buffer((char*)buffer, length)
			, boost::bind(&peer_conn::on_message, this, _1, _2));
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
			close("ERROR RECEIVE MESSAGE: %s", ec);
			return;
		}
		char* ptr = (char*)buffer;
		int msg = read_uint8(ptr);

		if (test_mode == dual_test && num_seeds == 0)
		{
			TORRENT_ASSERT(!seed);
			close("NO MORE SEEDS, test done", error_code());
			return;
		}

		//printf("msg: %d len: %d\n", msg, int(bytes_transferred));

		if (seed)
		{
			if (msg == 6)
			{
				if (bytes_transferred != 13)
				{
					close("REQUEST packet has invalid size", error_code());
					return;
				}
				int piece = detail::read_int32(ptr);
				int start = detail::read_int32(ptr);
				int length = detail::read_int32(ptr);
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
			else if (msg == 5) // bitfield
			{
				pieces.reserve(num_pieces);
				int piece = 0;
				for (int i = 0; i < int(bytes_transferred); ++i)
				{
					int mask = 0x80;
					for (int k = 0; k < 8; ++k)
					{
						if (piece > num_pieces) break;
						if (*ptr & mask) pieces.push_back(piece);
						mask >>= 1;
						++piece;
					}
					++ptr;
				}
				std::random_shuffle(pieces.begin(), pieces.end());
			}
			else if (msg == 7) // piece
			{
				if (verify_downloads)
				{
					int piece = read_uint32(ptr);
					int start = read_uint32(ptr);
					int size = bytes_transferred - 9;
					verify_piece(piece, start, ptr, size);
				}
				++blocks_received;
				--outstanding_requests;
				int piece = detail::read_int32(ptr);
				int start = detail::read_int32(ptr);

				if (churn && (blocks_received % churn) == 0) {
					outstanding_requests = 0;
					restarting = true;
					s.close();
					return;
				}
				if (int((start + bytes_transferred) / 0x4000) == blocks_per_piece)
				{
					write_have(piece);
					return;
				}
			}
			else if (msg == 13) // suggest
			{
				int piece = detail::read_int32(ptr);
				std::vector<int>::iterator i = std::find(pieces.begin(), pieces.end(), piece);
				if (i != pieces.end())
				{
					pieces.erase(i);
					suggested_pieces.push_back(piece);
					++num_suggest;
				}
			}
			else if (msg == 16) // reject request
			{
				int piece = detail::read_int32(ptr);
				int start = detail::read_int32(ptr);
				int length = detail::read_int32(ptr);

				// put it back!
				if (current_piece != piece)
				{
					if (pieces.empty() || pieces.back() != piece)
						pieces.push_back(piece);
				}
				else
				{
					block = (std::min)(start / 0x4000, block);
					if (block == 0)
					{
						pieces.push_back(current_piece);
						current_piece = -1;
						current_piece_is_allowed = false;
					}
				}
				--outstanding_requests;
				fprintf(stderr, "REJECT: [ piece: %d start: %d length: %d ]\n", piece, start, length);
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
				int piece = detail::read_int32(ptr);
				std::vector<int>::iterator i = std::find(pieces.begin(), pieces.end(), piece);
				if (i != pieces.end())
				{
					pieces.erase(i);
					allowed_fast.push_back(piece);
				}
			}
			work_download();
		}
	}

	bool verify_piece(int piece, int start, char const* ptr, int size)
	{
		boost::uint32_t* buf = (boost::uint32_t*)ptr;
		boost::uint32_t fill = (piece << 8) | ((start / 0x4000) & 0xff);
		for (int i = 0; i < size / 4; ++i)
		{
			if (buf[i] != fill)
			{
				fprintf(stderr, "received invalid block. piece %d block %d\n", piece, start / 0x4000);
				exit(1);
				return false;
			}
		}
		return true;
	}

	void write_piece(int piece, int start, int length)
	{
		generate_block(write_buffer, piece, start, length);

		if (corrupt)
		{
			--corruption_counter;
			if (corruption_counter == 0)
			{
				corruption_counter = 1000;
				memset(write_buffer, 0, 10);
			}
		}
		char* ptr = write_buf_proto;
		write_uint32(9 + length, ptr);
		assert(length == 0x4000);
		write_uint8(7, ptr);
		write_uint32(piece, ptr);
		write_uint32(start, ptr);
		boost::array<libtorrent::asio::const_buffer, 2> vec;
		vec[0] = libtorrent::asio::buffer(write_buf_proto, ptr - write_buf_proto);
		vec[1] = libtorrent::asio::buffer(write_buffer, length);
		boost::asio::async_write(s, vec, boost::bind(&peer_conn::on_have_all_sent, this, _1, _2));
		++blocks_sent;
		if (churn && (blocks_sent % churn) == 0 && seed) {
			outstanding_requests = 0;
			restarting = true;
			s.close();
		}
	}

	void write_have(int piece)
	{
		char* ptr = write_buf_proto;
		write_uint32(5, ptr);
		write_uint8(4, ptr);
		write_uint32(piece, ptr);
		boost::asio::async_write(s, asio::buffer(write_buf_proto, 9), boost::bind(&peer_conn::on_have_all_sent, this, _1, _2));
	}
};

void print_usage()
{
	fprintf(stderr, "usage: connection_tester command [options]\n\n"
		"command is one of:\n"
		"  gen-torrent        generate a test torrent\n"
		"    options for this command:\n"
		"    -s <size>          the size of the torrent in megabytes\n"
		"    -n <num-files>     the number of files in the test torrent\n"
		"    -t <file>          the file to save the .torrent file to\n"
		"    -T <name>          the name of the torrent (and directory\n"
		"                       its files are saved in)\n\n"
		"  gen-data             generate the data file(s) for the test torrent\n"
		"    options for this command:\n"
		"    -t <file>          the torrent file that was previously generated\n"
		"    -P <path>          the path to where the data should be stored\n\n"
		"  gen-test-torrents    generate many test torrents (cannot be used for up/down tests)\n"
		"    options for this command:\n"
		"    -N <num-torrents>  number of torrents to generate\n"
		"    -n <num-files>     number of files in each torrent\n"
		"    -t <name>          base name of torrent files (index is appended)\n\n"
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

void hasher_thread(libtorrent::create_torrent* t, int start_piece, int end_piece, int piece_size, bool print)
{
	if (print) fprintf(stderr, "\n");
	boost::uint32_t piece[0x4000 / 4];
	for (int i = start_piece; i < end_piece; ++i)
	{
		hasher ph;
		for (int j = 0; j < piece_size; j += 0x4000)
		{
			generate_block(piece, i, j, 0x4000);
			ph.update((char*)piece, 0x4000);
		}
		t->set_hash(i, ph.final());
		if (print && (i & 1)) fprintf(stderr, "\r%.1f %% ", float((i-start_piece) * 100) / float(end_piece-start_piece));
	}
	if (print) fprintf(stderr, "\n");
}

// size is in megabytes
void generate_torrent(std::vector<char>& buf, int size, int num_files
	, char const* torrent_name)
{
	file_storage fs;
	// 1 MiB piece size
	const int piece_size = 1024 * 1024;
	const int num_pieces = size;
	const boost::int64_t total_size = boost::int64_t(piece_size) * num_pieces;

	boost::int64_t s = total_size;
	int i = 0;
	boost::int64_t file_size = total_size / num_files;
	while (s > 0)
	{
		char b[100];
		snprintf(b, sizeof(b), "%s/stress_test%d", torrent_name, i);
		++i;
		fs.add_file(b, (std::min)(s, boost::int64_t(file_size)));
		s -= file_size;
		file_size += 200;
	}

//	fs.add_file("stress_test_file", total_size);
	libtorrent::create_torrent t(fs, piece_size);

	// generate the hashes in 4 threads
	thread t1(boost::bind(&hasher_thread, &t, 0, 1 * num_pieces / 4, piece_size, false));
	thread t2(boost::bind(&hasher_thread, &t, 1 * num_pieces / 4, 2 * num_pieces / 4, piece_size, false));
	thread t3(boost::bind(&hasher_thread, &t, 2 * num_pieces / 4, 3 * num_pieces / 4, piece_size, false));
	thread t4(boost::bind(&hasher_thread, &t, 3 * num_pieces / 4, 4 * num_pieces / 4, piece_size, true));

	t1.join();
	t2.join();
	t3.join();
	t4.join();

	std::back_insert_iterator<std::vector<char> > out(buf);

	bencode(out, t.generate());
}

void generate_data(char const* path, torrent_info const& ti)
{
	file_storage const& fs = ti.files();

	file_pool fp;

	storage_params params;
	params.files = &const_cast<file_storage&>(fs);
	params.mapped_files = NULL;
	params.path = path;
	params.pool = &fp;
	params.mode = storage_mode_sparse;

	boost::scoped_ptr<storage_interface> st(default_storage_constructor(params));

	storage_error error;
	st->initialize(error);

	boost::uint32_t piece[0x4000 / 4];
	for (int i = 0; i < ti.num_pieces(); ++i)
	{
		for (int j = 0; j < ti.piece_size(i); j += 0x4000)
		{
			generate_block(piece, i, j, 0x4000);
			file::iovec_t b = { piece, 0x4000};
			storage_error error;
			st->writev(&b, 1, i, j, 0, error);
			if (error)
				fprintf(stderr, "storage error: %s\n", error.ec.message().c_str());
		}
		if (i & 1) fprintf(stderr, "\r%.1f %% ", float(i * 100) / float(ti.num_pieces()));
	}
}

void io_thread(io_service* ios)
{
	error_code ec;
	ios->run(ec);
	if (ec) fprintf(stderr, "ERROR: %s\n", ec.message().c_str());
}

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

	argv += 2;
	argc -= 2;

	while (argc > 0)
	{
		char const* optname = argv[0];
		++argv;
		--argc;

		if (optname[0] != '-' || strlen(optname) != 2)
		{
			fprintf(stderr, "unknown option: %s\n", optname);
			continue;
		}

		// options with no arguments
		switch (optname[1])
		{
			case 'C': test_corruption = true; continue;
		}

		if (argc == 0)
		{
			fprintf(stderr, "missing argument for option: %s\n", optname);
			break;
		}

		char const* optarg = argv[0];
		++argv;
		--argc;

		switch (optname[1])
		{
			case 's': size = atoi(optarg); break;
			case 'n': num_files = atoi(optarg); break;
			case 'N': num_torrents = atoi(optarg); break;
			case 't': torrent_file = optarg; break;
			case 'P': data_path = optarg; break;
			case 'c': num_connections = atoi(optarg); break;
			case 'p': destination_port = atoi(optarg); break;
			case 'd': destination_ip = optarg; break;
			case 'r': churn = atoi(optarg); break;
			default: fprintf(stderr, "unknown option: %s\n", optname);
		}
	}

	if (strcmp(command, "gen-torrent") == 0)
	{
		std::vector<char> tmp;
		std::string name = leaf_path(torrent_file);
		name = name.substr(0, name.find_last_of('.'));
		printf("generating torrent: %s\n", name.c_str());
		generate_torrent(tmp, size ? size : 1024, num_files ? num_files : 1
			, name.c_str());

		FILE* output = stdout;
		if (strcmp("-", torrent_file) != 0)
		{
			if( (output = fopen(torrent_file, "wb+")) == 0)
			{
				fprintf(stderr, "Could not open file '%s' for writing: %s\n", torrent_file, strerror(errno));
				exit(2);
			}
		}
		fprintf(stderr, "writing file to: %s\n", torrent_file);
		fwrite(&tmp[0], 1, tmp.size(), output);
		if (output != stdout)
			fclose(output);

		return 0;
	}
	else if (strcmp(command, "gen-data") == 0)
	{
		error_code ec;
		torrent_info ti(torrent_file, ec);
		if (ec)
		{
			fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", ec.message().c_str());
			return 1;
		}
		generate_data(data_path, ti);
		return 0;
	}
	else if (strcmp(command, "gen-test-torrents") == 0)
	{
		std::vector<char> buf;
		for (int i = 0; i < num_torrents; ++i)
		{
			char torrent_name[100];
			snprintf(torrent_name, sizeof(torrent_name), "%s-%d.torrent", torrent_file, i);

			file_storage fs;
			for (int j = 0; j < num_files; ++j)
			{
				char file_name[100];
				snprintf(file_name, sizeof(file_name), "%s-%d/file-%d", torrent_file, i, j);
				fs.add_file(file_name, boost::int64_t(j + i + 1) * 251);
			}
			// 1 MiB piece size
			const int piece_size = 1024 * 1024;
			libtorrent::create_torrent t(fs, piece_size);
			sha1_hash zero(0);
			for (int i = 0; i < fs.num_pieces(); ++i)
				t.set_hash(i, zero);


			buf.clear();
			std::back_insert_iterator<std::vector<char> > out(buf);
			bencode(out, t.generate());
			FILE* f = fopen(torrent_name, "w+");
			if (f == 0)
			{
				fprintf(stderr, "Could not open file '%s' for writing: %s\n", torrent_name, strerror(errno));
				return 1;
			}
			size_t ret = fwrite(&buf[0], 1, buf.size(), f);
			if (ret != buf.size())
			{
				fprintf(stderr, "write returned: %d (expected %d)\n", int(ret), int(buf.size()));
				return 1;
			}
			printf("wrote %s\n", torrent_name);
			fclose(f);
		}
		return 0;
	}
	else if (strcmp(command, "upload") == 0)
	{
		test_mode = upload_test;
	}
	else if (strcmp(command, "download") == 0)
	{
		test_mode = download_test;
	}
	else if (strcmp(command, "dual") == 0)
	{
		test_mode = dual_test;
	}
	else
	{
		fprintf(stderr, "unknown command: %s\n\n", command);
		print_usage();
	}

	error_code ec;
	address_v4 addr = address_v4::from_string(destination_ip, ec);
	if (ec)
	{
		fprintf(stderr, "ERROR RESOLVING %s: %s\n", destination_ip, ec.message().c_str());
		return 1;
	}
	tcp::endpoint ep(addr, destination_port);
	
#if !defined __APPLE__
	// apparently darwin doesn't seems to let you bind to
	// loopback on any other IP than 127.0.0.1
	unsigned long ip = addr.to_ulong();
	if ((ip & 0xff000000) == 0x7f000000)
	{
		local_bind = true;
	}
#endif

	torrent_info ti(torrent_file, ec);
	if (ec)
	{
		fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", ec.message().c_str());
		return 1;
	}

	std::vector<peer_conn*> conns;
	conns.reserve(num_connections);
	const int num_threads = 2;
	io_service ios[num_threads];
	for (int i = 0; i < num_connections; ++i)
	{
		bool corrupt = test_corruption && (i & 1) == 0;
		bool seed = false;
		if (test_mode == upload_test) seed = true;
		else if (test_mode == dual_test) seed = (i & 1);
		conns.push_back(new peer_conn(ios[i % num_threads], ti.num_pieces(), ti.piece_length() / 16 / 1024
			, ep, (char const*)&ti.info_hash()[0], seed, churn, corrupt));
		sleep_ms(1);
		ios[i % num_threads].poll_one(ec);
		if (ec)
		{
			fprintf(stderr, "ERROR: %s\n", ec.message().c_str());
			break;
		}
	}

	thread t1(boost::bind(&io_thread, &ios[0]));
	thread t2(boost::bind(&io_thread, &ios[1]));
 
	t1.join();
	t2.join();

	float up = 0.f;
	float down = 0.f;
	boost::uint64_t total_sent = 0;
	boost::uint64_t total_received = 0;
	
	for (std::vector<peer_conn*>::iterator i = conns.begin()
		, end(conns.end()); i != end; ++i)
	{
		peer_conn* p = *i;
		int time = total_milliseconds(p->end_time - p->start_time);
		if (time == 0) time = 1;
		total_sent += p->blocks_sent;
		up += (boost::int64_t(p->blocks_sent) * 0x4000) / time / 1000.f;
		down += (boost::int64_t(p->blocks_received) * 0x4000) / time / 1000.f;
		delete p;
	}

	printf("=========================\n"
		"suggests: %d suggested-requests: %d\n"
		"total sent: %.1f %% received: %.1f %%\n"
		"rate sent: %.1f MB/s received: %.1f MB/s\n"
		, int(num_suggest), int(num_suggested_requests)
		, total_sent * 0x4000 * 100.f / float(ti.total_size())
		, total_received * 0x4000 * 100.f / float(ti.total_size())
		, up, down);

	return 0;
}
