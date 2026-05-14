/*

Copyright (c) 2010-2022, Arvid Norberg
Copyright (c) 2015, Mike Tzou
Copyright (c) 2016, 2018, 2020-2021, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/peer_id.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/session.hpp" // for default_disk_io_constructor
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/info_hash.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/merkle_tree.hpp"
#include <random>
#include <cstring>
#include <thread>
#include <functional>
#include <iostream>
#include <atomic>
#include <array>
#include <chrono>
#include <map>
#include <memory>

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

// in order to circumvent the restriction of only
// one connection per IP that most clients implement
// all sockets created by this tester are bound to
// unique local IPs in the range (127.0.0.1 - 127.255.255.255)
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
// test). hash_stress_test floods the target with v2
// HASH_REQUEST messages to stress its hash-response path.
enum test_mode_t
{
	none,
	upload_test,
	download_test,
	dual_test,
	hash_stress_test
};
test_mode_t test_mode = none;

// which torrent metadata version to produce in gen-torrent
enum class gen_version_t
{
	v1,
	v2,
	hybrid
};

// when true and the torrent is hybrid, force the v1 sha1 info hash and
// clear the v2 reserved bit in the handshake.
bool force_v1_handshake = false;

// list of file roots used by hash-stress to pick random files to query.
// Parallel-index into file_trees (defined below after the file_merkle_tree
// struct). Built once at startup; read-only afterwards.
std::vector<sha256_hash> file_root_list;

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

// flat merkle tree for a single file, plus dimensions. used by both
// gen-torrent (when computing v2 piece roots) and the seed-side
// HASH_REQUEST responder.
struct file_merkle_tree
{
	aux::vector<sha256_hash> tree;
	int num_blocks = 0;
	int num_leafs = 0;
	int blocks_per_piece = 0;
};

// per-file merkle trees, keyed by the file's merkle root (pieces_root).
// Built once at startup; read-only afterwards.
std::map<sha256_hash, file_merkle_tree> file_trees;

// build a full per-file merkle tree from the deterministic content that
// generate_block() produces. The file's content for absolute piece P at
// 16 KiB offset O is generate_block(buf, P, O).
file_merkle_tree build_file_merkle_tree(piece_index_t const file_first_piece,
	int const file_num_pieces,
	std::int64_t const file_size,
	int const piece_length)
{
	file_merkle_tree out;
	out.num_blocks = int((file_size + default_block_size - 1) / default_block_size);
	out.num_leafs = merkle_num_leafs(out.num_blocks);
	out.blocks_per_piece = piece_length / default_block_size;

	int const num_nodes = merkle_num_nodes(out.num_leafs);
	int const first_leaf = merkle_first_leaf(out.num_leafs);
	out.tree.resize(num_nodes);

	std::uint32_t block_buf[default_block_size / 4];
	int block_idx = 0;
	for (int p = 0; p < file_num_pieces; ++p)
	{
		piece_index_t const abs_piece(static_cast<int>(file_first_piece) + p);
		std::int64_t const piece_offset = std::int64_t(p) * piece_length;
		int const bytes_in_piece =
			int(std::min(std::int64_t(piece_length), file_size - piece_offset));
		int const blocks_in_piece = (bytes_in_piece + default_block_size - 1) / default_block_size;

		for (int b = 0; b < blocks_in_piece; ++b)
		{
			generate_block(block_buf, abs_piece, b * default_block_size);
			int const block_bytes = (b == blocks_in_piece - 1)
				? (bytes_in_piece - b * default_block_size)
				: default_block_size;
			out.tree[first_leaf + block_idx] =
				hasher256(reinterpret_cast<char const*>(block_buf), block_bytes).final();
			++block_idx;
		}
	}
	// remaining leaves are zero (default-constructed sha256_hash), which is
	// the correct merkle padding for tail leaves below the piece layer.
	merkle_fill_tree(out.tree, out.num_leafs);
	return out;
}

// extract the hashes for a HASH_REQUEST response: `count` hashes at the given
// (base, index) followed by the uncle proof chain (`max(0, proof_layers -
// (merkle_num_layers(merkle_num_leafs(count)) - 1))` hashes). Mirrors the
// wire format produced by bt_peer_connection::write_hashes().
std::vector<sha256_hash> get_hashes_for_request(file_merkle_tree const& ft,
	int const base,
	int const index,
	int const count,
	int const proof_layers)
{
	if (count <= 0 || count > 8192) return {};
	int const tree_layers = merkle_num_layers(ft.num_leafs);
	if (base < 0 || base >= tree_layers) return {};
	int const base_layer_idx = tree_layers - base;
	int const base_start_idx = merkle_to_flat_index(base_layer_idx, index);
	if (base_start_idx < 0 || base_start_idx + count > int(ft.tree.size())) return {};

	std::vector<sha256_hash> ret;
	int const base_tree_layers = merkle_num_layers(merkle_num_leafs(count)) - 1;
	int const proof_hashes = std::max(0, proof_layers - base_tree_layers);
	ret.reserve(std::size_t(count + proof_hashes));

	for (int i = 0; i < count; ++i)
		ret.push_back(ft.tree[base_start_idx + i]);

	int proof_idx = base_start_idx;
	for (int i = 0; i < proof_layers; ++i)
	{
		proof_idx = merkle_get_parent(proof_idx);
		if (proof_idx <= 0) break;
		if (i >= base_tree_layers)
		{
			int const sibling = merkle_get_sibling(proof_idx);
			if (sibling < 0 || sibling >= int(ft.tree.size())) return {};
			ret.push_back(ft.tree[sibling]);
		}
	}
	return ret;
}

struct peer_conn
{
	peer_conn(io_context& ios,
		int piece_count,
		int blocks_pp,
		int last_piece_size_,
		tcp::endpoint const& ep,
		sha1_hash const& ih,
		bool v2_,
		bool seed_,
		int churn_,
		bool corrupt_,
		bool flood_hashes_ = false)
		: s(ios)
		, read_pos(0)
		, state(handshaking)
		, choked(true)
		, current_piece(-1)
		, current_piece_is_allowed(false)
		, block(0)
		, blocks_per_piece(blocks_pp)
		, last_piece_blocks((last_piece_size_ + 0x3fff) / 0x4000)
		, last_block_size(last_piece_size_ - ((last_piece_size_ + 0x3fff) / 0x4000 - 1) * 0x4000)
		, info_hash(ih)
		, outstanding_requests(0)
		, v2(v2_)
		, seed(seed_)
		, fast_extension(false)
		, flood_hashes(flood_hashes_)
		, blocks_received(0)
		, blocks_sent(0)
		, hashes_received(0)
		, hashes_rejected(0)
		, hash_requests_sent(0)
		, hashes_sent(0)
		, hash_rejects_sent(0)
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
	// per-connection scratch buffers reused across messages. We rely on each
	// async_write being followed by the next one only from inside its
	// completion handler, so writes on a single socket never overlap and the
	// buffer below is never clobbered while in flight. Adding an overlapping
	// async_write on the same socket would silently corrupt the in-flight
	// payload.
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
	// number of blocks in the final (possibly partial) piece, and the size of
	// the last block within that piece. for regular pieces, every block is
	// `blocks_per_piece` blocks of 16 KiB.
	int last_piece_blocks;
	int last_block_size;
	sha1_hash info_hash;
	int outstanding_requests;
	// when set, this connection sets the v2 protocol bit in the handshake
	// and responds to / drains HASH_REQUEST / HASHES / HASH_REJECT messages.
	bool v2;
	// if this is true, this connection is a seed
	bool seed;
	bool fast_extension;
	// when set, this downloader sends a flood of HASH_REQUEST messages instead
	// of (or in addition to) regular piece requests. Used by hash-stress mode.
	bool flood_hashes;
	int blocks_received;
	int blocks_sent;
	int hashes_received;
	int hashes_rejected;
	int hash_requests_sent;
	int hashes_sent;
	int hash_rejects_sent;
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

		static char const handshake[] = "\x13"
										"BitTorrent protocol\0\0\0\0\0\0\0\x04"
										"                    " // space for info-hash
										"aaaaaaaaaaaaaaaaaaaa" // peer-id
										"\0\0\0\x01\x02"; // interested
		constexpr int handshake_size = int(sizeof(handshake) - 1);
		static_assert(handshake_size <= int(sizeof(write_buf_proto)), "write_buf_proto too small");
		std::memcpy(write_buf_proto, handshake, handshake_size);
		// bit 4 of reserved byte 7 (0x10) advertises the v2 hash-exchange
		// protocol (BEP 52). reserved bytes are at offset 20..27.
		if (v2) write_buf_proto[27] |= 0x10;
		std::memcpy(write_buf_proto + 28, info_hash.data(), 20);
		std::generate(write_buf_proto + 48, write_buf_proto + 68, [] { return char(rand()); });
		// for seeds, don't send the interested message
		boost::asio::async_write(s,
			boost::asio::buffer(write_buf_proto, std::size_t(handshake_size - (seed ? 5 : 0))),
			std::bind(&peer_conn::on_handshake, this, _1, _2));
	}

	void on_handshake(error_code const& ec, size_t)
	{
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

		if (seed)
		{
			// read next message from the peer
			boost::asio::async_read(s,
				boost::asio::buffer(buffer, 4),
				std::bind(&peer_conn::on_msg_length, this, _1, _2));
		}
		else
		{
			// the only write from a downloader that lands here is write_have(),
			// called after the last block of a piece. Run the completion
			// check / pipeline refill instead of just blocking on a read --
			// otherwise if the last block to arrive is the last block of any
			// piece, the connection stalls near 100%.
			work_download();
		}
	}

	bool write_request()
	{
		// if we're choked (and there are no allowed-fast pieces left)
		if (choked && allowed_fast.empty() && !current_piece_is_allowed) return false;

		// if there are no pieces left to request
		if (pieces.empty() && suggested_pieces.empty() && allowed_fast.empty()
			&& current_piece == piece_index_t(-1))
		{
			return false;
		}

		if (current_piece == piece_index_t(-1))
		{
			// pick a new piece. allowed-fast pieces are usable both while
			// choked and while unchoked -- so once the normal `pieces` queue
			// has been drained, fall back to allowed_fast even when unchoked
			// (otherwise the seed's ALLOWED_FAST set is silently leaked and
			// the leech caps at less than 100%).
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
			else if (allowed_fast.size() > 0)
			{
				current_piece = allowed_fast.front();
				allowed_fast.erase(allowed_fast.begin());
				current_piece_is_allowed = true;
			}
			else
			{
				TORRENT_ASSERT_FAIL();
			}
		}
		bool const is_last_piece = (static_cast<int>(current_piece) == num_pieces - 1);
		int const this_blocks_per_piece = is_last_piece ? last_piece_blocks : blocks_per_piece;
		int const this_block_size =
			(is_last_piece && block == last_piece_blocks - 1) ? last_block_size : 16 * 1024;

		char* ptr = write_buf_proto;
		write_uint32(13, ptr); // payload size
		write_uint8(6, ptr); // request
		write_uint32(static_cast<int>(current_piece), ptr);
		write_uint32(block * 16 * 1024, ptr);
		write_uint32(this_block_size, ptr);
		boost::asio::async_write(s,
			boost::asio::buffer(write_buf_proto, 17),
			std::bind(&peer_conn::on_req_sent, this, _1, _2));

		++outstanding_requests;
		++block;
		if (block == this_blocks_per_piece)
		{
			block = 0;
			current_piece = piece_index_t(-1);
			current_piece_is_allowed = false;
		}
		return true;
	}

	void on_req_sent(error_code const& ec, size_t)
	{
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
		double const up = double(std::int64_t(blocks_sent) * 0x4000 / time) / 1000.0;
		double const down = double(std::int64_t(blocks_received) * 0x4000 / time) / 1000.0;
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
		if (flood_hashes)
		{
			if (file_root_list.empty())
			{
				close("HASH_STRESS: no v2 files in torrent", error_code());
				return;
			}
			if (outstanding_requests < 40)
			{
				if (write_flood_hash_request()) return;
			}
			boost::asio::async_read(s,
				boost::asio::buffer(buffer, 4),
				std::bind(&peer_conn::on_msg_length, this, _1, _2));
			return;
		}

		int const total_blocks = (num_pieces - 1) * blocks_per_piece + last_piece_blocks;
		if (pieces.empty() && suggested_pieces.empty() && allowed_fast.empty()
			&& current_piece == piece_index_t(-1) && outstanding_requests == 0
			&& blocks_received >= total_blocks)
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

	// pick a random file and piece-range, send a HASH_REQUEST at the
	// block layer. Returns false only if the file map is empty.
	bool write_flood_hash_request()
	{
		auto const& root = file_root_list[std::size_t(std::rand()) % file_root_list.size()];
		auto it = file_trees.find(root);
		if (it == file_trees.end()) return false;
		file_merkle_tree const& ft = it->second;
		int const tree_layers = merkle_num_layers(ft.num_leafs);
		if (tree_layers < 1 || ft.blocks_per_piece <= 0 || ft.num_blocks <= 0) return false;

		int const num_pieces_in_file =
			std::max(1, (ft.num_blocks + ft.blocks_per_piece - 1) / ft.blocks_per_piece);
		int const piece_idx = std::rand() % num_pieces_in_file;
		int const block_start = piece_idx * ft.blocks_per_piece;
		int const blocks_in_this_piece = std::min(ft.blocks_per_piece, ft.num_blocks - block_start);

		// validate_hash_request() requires proof_layers < num_layers - base.
		// With base == 0, the maximum valid proof_layers is tree_layers - 1
		// (uncle chain from the block layer up to just below the root).
		write_hash_request(root, 0 /*base*/, block_start, blocks_in_this_piece, tree_layers - 1);
		++outstanding_requests;
		return true;
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
			else if (msg == 21) // hash_request
			{
				if (bytes_transferred != 49)
				{
					close("HASH_REQUEST packet has invalid size", error_code());
					return;
				}
				handle_hash_request(ptr);
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
			else if (msg == 22) // hashes
			{
				++hashes_received;
				if (flood_hashes && outstanding_requests > 0) --outstanding_requests;
			}
			else if (msg == 23) // hash_reject
			{
				++hashes_rejected;
				if (flood_hashes && outstanding_requests > 0) --outstanding_requests;
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
		// BT REQUEST is at most 16 KiB but the last block of a (partial)
		// last piece can be smaller. Fill the buffer rounded up to a 4-byte
		// word so all `length` bytes are deterministic.
		int const fill_words = (length + 3) / 4;
		generate_block({write_buffer, fill_words}, piece, start);

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

	// reads a 48-byte HASH_REQUEST/HASH_REJECT payload (file-root + 4 int32s)
	// from `ptr` and produces a HASHES (msg 22) or HASH_REJECT (msg 23) reply
	// from the precomputed merkle tree for `file_root`.
	void handle_hash_request(char const* ptr)
	{
		sha256_hash file_root;
		std::memcpy(file_root.data(), ptr, 32);
		ptr += 32;
		int const base = aux::read_int32(ptr);
		int const index = aux::read_int32(ptr);
		int const count = aux::read_int32(ptr);
		int const proof_layers = aux::read_int32(ptr);

		auto it = file_trees.find(file_root);
		std::vector<sha256_hash> hashes;
		if (it != file_trees.end())
			hashes = get_hashes_for_request(it->second, base, index, count, proof_layers);

		if (hashes.empty())
		{
			++hash_rejects_sent;
			write_hash_reject(file_root, base, index, count, proof_layers);
		}
		else
		{
			++hashes_sent;
			write_hashes(file_root, base, index, count, proof_layers, hashes);
		}
	}

	void write_hashes(sha256_hash const& file_root,
		int const base,
		int const index,
		int const count,
		int const proof_layers,
		std::vector<sha256_hash> const& hashes)
	{
		int const payload_size = 1 + 32 + 4 + 4 + 4 + 4 + int(hashes.size()) * 32;
		int const total = 4 + payload_size;
		// HASHES is the only message big enough that it doesn't fit in
		// write_buf_proto. write_buffer is sized for a 16 KiB block plus
		// header so it easily accommodates any HASHES reply this tester
		// produces (get_hashes_for_request caps count at blocks_per_piece).
		TORRENT_ASSERT(total <= int(sizeof(write_buffer)));
		char* ptr = reinterpret_cast<char*>(write_buffer);
		write_uint32(payload_size, ptr);
		write_uint8(22, ptr); // msg_hashes
		std::memcpy(ptr, file_root.data(), 32);
		ptr += 32;
		write_uint32(base, ptr);
		write_uint32(index, ptr);
		write_uint32(count, ptr);
		write_uint32(proof_layers, ptr);
		for (auto const& h : hashes)
		{
			std::memcpy(ptr, h.data(), 32);
			ptr += 32;
		}
		boost::asio::async_write(s,
			boost::asio::buffer(write_buffer, std::size_t(total)),
			std::bind(&peer_conn::on_hash_sent, this, _1, _2, "ERROR SENT HASHES"));
	}

	void write_hash_reject(sha256_hash const& file_root,
		int const base,
		int const index,
		int const count,
		int const proof_layers)
	{
		constexpr int payload_size = 1 + 32 + 4 + 4 + 4 + 4;
		constexpr int total = 4 + payload_size;
		static_assert(total <= int(sizeof(write_buf_proto)), "write_buf_proto too small");
		char* ptr = write_buf_proto;
		write_uint32(payload_size, ptr);
		write_uint8(23, ptr); // msg_hash_reject
		std::memcpy(ptr, file_root.data(), 32);
		ptr += 32;
		write_uint32(base, ptr);
		write_uint32(index, ptr);
		write_uint32(count, ptr);
		write_uint32(proof_layers, ptr);
		boost::asio::async_write(s,
			boost::asio::buffer(write_buf_proto, std::size_t(total)),
			std::bind(&peer_conn::on_hash_sent, this, _1, _2, "ERROR SENT HASH_REJECT"));
	}

	void write_hash_request(sha256_hash const& file_root,
		int const base,
		int const index,
		int const count,
		int const proof_layers)
	{
		constexpr int payload_size = 1 + 32 + 4 + 4 + 4 + 4;
		constexpr int total = 4 + payload_size;
		static_assert(total <= int(sizeof(write_buf_proto)), "write_buf_proto too small");
		char* ptr = write_buf_proto;
		write_uint32(payload_size, ptr);
		write_uint8(21, ptr); // msg_hash_request
		std::memcpy(ptr, file_root.data(), 32);
		ptr += 32;
		write_uint32(base, ptr);
		write_uint32(index, ptr);
		write_uint32(count, ptr);
		write_uint32(proof_layers, ptr);
		boost::asio::async_write(s,
			boost::asio::buffer(write_buf_proto, std::size_t(total)),
			std::bind(&peer_conn::on_hash_sent, this, _1, _2, "ERROR SENT HASH_REQUEST"));
		++hash_requests_sent;
	}

	void on_hash_sent(error_code const& ec, size_t, char const* msg)
	{
		if (ec)
		{
			close(msg, ec);
			return;
		}
		if (seed)
		{
			// after sending a HASHES / HASH_REJECT, resume reading.
			boost::asio::async_read(s,
				boost::asio::buffer(buffer, 4),
				std::bind(&peer_conn::on_msg_length, this, _1, _2));
		}
		else
		{
			// downloader in flood mode: pipeline another HASH_REQUEST or drain
			// pending responses.
			work_download();
		}
	}
};

[[noreturn]] void print_usage()
{
	std::fprintf(stderr,
		"usage: connection_tester command [options]\n\n"
		"command is one of:\n"
		"  gen-torrent        generate a test torrent\n"
		"    options for this command:\n"
		"    -s <size>          the size of the torrent in megabytes\n"
		"    -n <num-files>     the number of files in the test torrent\n"
		"    -t <file>          the file to save the .torrent file to\n"
		"    -V <version>       torrent format: 1 = v1-only, 2 = v2-only,\n"
		"                       h = hybrid (default)\n"
		"    -U <num>           Add <num> random test tracker URLs\n\n"
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
		"  hash-stress          flood the target with v2 HASH_REQUEST messages\n"
		"                       (requires a v2 or hybrid torrent on the target)\n"
		"    options for these commands:\n"
		"    -c <num-conns>     the number of connections to make to the target\n"
		"    -d <dst>           the IP address of the target\n"
		"    -p <dst-port>      the port the target listens on\n"
		"    -t <torrent-file>  the torrent file previously generated by gen-torrent\n"
		"    -C                 send corrupt pieces sometimes (applies to upload and dual)\n"
		"    -1                 for hybrid torrents, use the v1 info hash in the\n"
		"                       handshake and clear the v2 reserved bit (default is\n"
		"                       to use the v2 info hash and exercise the v2 path)\n"
		"    -r <reconnects>    churn - number of reconnects per second\n\n"
		"examples:\n\n"
		"connection_tester gen-torrent -s 1024 -n 4 -t test.torrent\n"
		"connection_tester upload -c 200 -d 127.0.0.1 -p 6881 -t test.torrent\n"
		"connection_tester download -c 200 -d 127.0.0.1 -p 6881 -t test.torrent\n"
		"connection_tester dual -c 200 -d 127.0.0.1 -p 6881 -t test.torrent\n"
		"connection_tester hash-stress -c 100 -d 127.0.0.1 -p 6881 -t test.torrent\n");
	exit(1);
}

void hasher_thread(lt::aux::vector<sha1_hash, piece_index_t>* output
	, lt::create_torrent const& ct
	, file_slice current_file
	, piece_index_t const start_piece
	, piece_index_t const end_piece
	, bool print)
{
	if (print) std::fprintf(stderr, "\n");
	std::uint32_t piece[0x4000 / 4];
	int const piece_size = ct.piece_length();

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
				while (current_file.size == 0)
				{
					++current_file.file_index;
					if (current_file.file_index >= ct.end_file())
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
					current_file.offset = 0;
					current_file.size = ct.file_at(current_file.file_index).size;
				}
				int const range = int(std::min(std::int64_t(0x4000 - k), current_file.size));
				if (ct.file_at(current_file.file_index).flags & file_storage::flag_pad_file)
					std::memset(reinterpret_cast<char*>(piece) + k, 0, std::size_t(range));

				current_file.offset += range;
				current_file.size -= range;
				k += range;
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

// describes the (canonicalized) file an absolute piece belongs to.
// Only meaningful for v2 and hybrid torrents, where canonicalize() guarantees
// each piece lives within a single file.
struct piece_file_map_entry
{
	file_index_t file{0};
	piece_index_t::diff_type piece_in_file{0};
	bool is_pad = false;
};

// walk t.file_at(fi) in order, attributing each piece to the data file it
// belongs to. Pad files are skipped because they fill the tail of the
// previous data file's last (partial) piece; they own no pieces of their own.
std::vector<piece_file_map_entry> compute_piece_to_file_map(create_torrent const& t)
{
	int const piece_length = t.piece_length();
	int const num_pieces = t.num_pieces();
	std::vector<piece_file_map_entry> result;
	result.resize(std::size_t(num_pieces));

	int abs = 0;
	for (file_index_t fi{0}; fi < t.end_file(); ++fi)
	{
		auto const& fe = t.file_at(fi);
		if (fe.flags & file_storage::flag_pad_file) continue;
		if (fe.size == 0) continue;
		int const file_pieces = int((fe.size + piece_length - 1) / piece_length);
		for (int p = 0; p < file_pieces && abs < num_pieces; ++p, ++abs)
		{
			result[std::size_t(abs)].file = fi;
			result[std::size_t(abs)].piece_in_file = piece_index_t::diff_type(p);
			result[std::size_t(abs)].is_pad = false;
		}
	}
	return result;
}

void v2_hasher_thread(create_torrent const* ct,
	std::vector<piece_file_map_entry> const* piece_map,
	lt::aux::vector<sha256_hash, piece_index_t>* output,
	piece_index_t const start_piece,
	piece_index_t const end_piece)
{
	int const piece_length = ct->piece_length();
	int const blocks_per_piece = piece_length / default_block_size;
	// largest subtree we will ever need: a full piece. For files smaller
	// than a piece the subtree is smaller and we reuse the prefix.
	aux::vector<sha256_hash> subtree(merkle_num_nodes(blocks_per_piece));

	std::uint32_t block_buf[default_block_size / 4];
	for (piece_index_t i = start_piece; i < end_piece; ++i)
	{
		auto const& pm = (*piece_map)[std::size_t(static_cast<int>(i))];
		if (pm.is_pad) continue;
		auto const& fe = ct->file_at(pm.file);
		std::int64_t const piece_offset =
			std::int64_t(static_cast<int>(pm.piece_in_file)) * piece_length;
		int const bytes_in_piece =
			int(std::min(std::int64_t(piece_length), fe.size - piece_offset));
		if (bytes_in_piece <= 0) continue;
		int const blocks_in_piece = (bytes_in_piece + default_block_size - 1) / default_block_size;
		// BEP 52 padding rule: per-piece merkle roots use blocks_per_piece
		// leaves for files at least a piece long (zero-padding the last
		// piece's tail). For files shorter than a piece, pad to next pow-2
		// of the block count. Matches create_torrent.cpp on_hash().
		int const num_leafs =
			(fe.size < piece_length) ? merkle_num_leafs(blocks_in_piece) : blocks_per_piece;
		int const num_nodes = merkle_num_nodes(num_leafs);
		int const first_leaf = merkle_first_leaf(num_leafs);

		for (int n = 0; n < num_nodes; ++n)
			subtree[n] = sha256_hash{};
		for (int b = 0; b < blocks_in_piece; ++b)
		{
			generate_block(block_buf, i, b * default_block_size);
			int const block_bytes = (b == blocks_in_piece - 1)
				? (bytes_in_piece - b * default_block_size)
				: default_block_size;
			subtree[first_leaf + b] =
				hasher256(reinterpret_cast<char const*>(block_buf), block_bytes).final();
		}
		merkle_fill_tree(span<sha256_hash>(subtree).first(num_nodes), num_leafs);
		(*output)[i] = subtree[0];
	}
}

// size is in megabytes
std::vector<char> generate_torrent(int num_pieces,
	int num_files,
	char const* torrent_name,
	int num_trackers,
	gen_version_t const version)
{
	std::vector<lt::create_file_entry> files;
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
		files.push_back({std::string(b), file_size, {}, 0, {}});
		s -= file_size;
		file_size += 200;
	}

	lt::create_flags_t flags{};
	if (version == gen_version_t::v1)
		flags = lt::create_torrent::v1_only;
	else if (version == gen_version_t::v2)
		flags = lt::create_torrent::v2_only;
	// hybrid: no version flag — canonicalize and emit both v1 and v2 metadata

	lt::create_torrent t(std::move(files), piece_size, flags);

	num_pieces = t.num_pieces();
	bool const do_v1 = (version != gen_version_t::v2);
	bool const do_v2 = (version != gen_version_t::v1);

	int const num_threads = std::thread::hardware_concurrency()
		? int(std::thread::hardware_concurrency()) : 4;
	std::printf("hashing in %d threads\n", num_threads);

	if (do_v1)
	{
		std::vector<std::thread> threads;
		threads.reserve(std::size_t(num_threads));
		lt::aux::vector<lt::sha1_hash, piece_index_t> hashes{static_cast<std::size_t>(num_pieces)};
		lt::file_slice current_file;
		current_file.file_index = file_index_t{0};
		current_file.offset = 0;
		current_file.size = t.file_at(current_file.file_index).size;
		std::int64_t offset = 0;
		for (int i = 0; i < num_threads; ++i)
		{
			auto const start_piece = piece_index_t(i * num_pieces / num_threads);
			auto const target_offset = static_cast<int>(start_piece) * t.piece_length();
			while (offset < target_offset)
			{
				while (current_file.size == 0)
				{
					++current_file.file_index;
					current_file.offset = 0;
					current_file.size = t.file_at(current_file.file_index).size;
				}
				std::int64_t const increment = std::min(current_file.size, target_offset - offset);
				current_file.offset += increment;
				current_file.size -= increment;
				offset += increment;
			}
			threads.emplace_back(&hasher_thread,
				&hashes,
				std::cref(t),
				current_file,
				start_piece,
				piece_index_t((i + 1) * num_pieces / num_threads),
				i == 0);
		}

		for (auto& i : threads)
			i.join();

		for (auto i : t.piece_range())
			t.set_hash(i, hashes[i]);
	}

	if (do_v2)
	{
		auto const piece_map = compute_piece_to_file_map(t);
		lt::aux::vector<sha256_hash, piece_index_t> v2_hashes{static_cast<std::size_t>(num_pieces)};

		std::vector<std::thread> threads;
		threads.reserve(std::size_t(num_threads));
		for (int i = 0; i < num_threads; ++i)
		{
			threads.emplace_back(&v2_hasher_thread,
				&t,
				&piece_map,
				&v2_hashes,
				piece_index_t(i * num_pieces / num_threads),
				piece_index_t((i + 1) * num_pieces / num_threads));
		}
		for (auto& th : threads)
			th.join();

		for (piece_index_t p : t.piece_range())
		{
			auto const& pm = piece_map[std::size_t(static_cast<int>(p))];
			if (pm.is_pad) continue;
			if (v2_hashes[p].is_all_zeros()) continue;
			t.set_hash2(pm.file, pm.piece_in_file, v2_hashes[p]);
		}
	}

	for (int i = 0; i < num_trackers; ++i)
	{
		char b[100];
		std::snprintf(b, sizeof(b), "http://test.tracker%d.com/announce", i);
		t.add_tracker(b);
	}

	return t.generate_buf();
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

	file_storage const& fs = ti.layout();

	aux::vector<download_priority_t, file_index_t> priorities;
	sha1_hash info_hash;
	renamed_files rf;
	storage_params params{
		fs,
		rf,
		path,
		{},
		storage_mode_sparse,
		priorities,
		info_hash,
		ti.v1(),
		ti.v2(),
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

// build the per-file merkle trees needed to satisfy HASH_REQUEST messages
// and to drive the hash-stress flood. Populates the globals file_trees and
// file_root_list. No-op for v1-only torrents.
void build_global_file_trees(torrent_info const& ti)
{
	if (!ti.v2()) return;
	file_storage const& fs = ti.layout();
	int const piece_length = fs.piece_length();

	for (file_index_t fi : fs.file_range())
	{
		if (fs.pad_file_at(fi)) continue;
		std::int64_t const file_size = fs.file_size(fi);
		if (file_size == 0) continue;

		piece_index_t const file_first_piece = fs.piece_index_at_file(fi);
		int const file_num_pieces = fs.file_num_pieces(fi);

		file_merkle_tree ft =
			build_file_merkle_tree(file_first_piece, file_num_pieces, file_size, piece_length);
		if (ft.tree.empty()) continue;
		sha256_hash const root = ft.tree[0];
		file_root_list.push_back(root);
		file_trees.emplace(root, std::move(ft));
	}
	std::printf("built %d v2 file merkle trees\n", int(file_trees.size()));
}

} // anonymous namespace

int main(int argc, char* argv[])
{
	if (argc <= 1) print_usage();

	char const* command = argv[1];
	int size = 1000;
	int num_files = 10;
	int num_torrents = 1;
	int num_trackers = 0;
	char const* torrent_file = "benchmark.torrent";
	char const* data_path = ".";
	int num_connections = 50;
	char const* destination_ip = "127.0.0.1";
	int destination_port = 6881;
	int churn = 0;
	std::vector<std::string> trackers;
	gen_version_t gen_version = gen_version_t::hybrid;

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
			case '1':
				force_v1_handshake = true;
				continue;
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
			case 'U': num_trackers = atoi(opt); break;
			case 'P': data_path = opt; break;
			case 'c': num_connections = atoi(opt); break;
			case 'p': destination_port = atoi(opt); break;
			case 'd': destination_ip = opt; break;
			case 'r': churn = atoi(opt); break;
			case 'V':
				if (opt[0] == '1' && opt[1] == 0)
					gen_version = gen_version_t::v1;
				else if (opt[0] == '2' && opt[1] == 0)
					gen_version = gen_version_t::v2;
				else if (opt[0] == 'h' && opt[1] == 0)
					gen_version = gen_version_t::hybrid;
				else
				{
					std::fprintf(stderr, "invalid -V value: %s (expected 1, 2 or h)\n", opt);
					return 1;
				}
				break;
			default: std::fprintf(stderr, "unknown option: %s\n", optname);
		}
	}

	if (command == "gen-torrent"_sv)
	{
		std::string name = leaf_path(torrent_file);
		name = name.substr(0, name.find_last_of('.'));
		std::printf("generating torrent: %s\n", name.c_str());
		std::vector<char> tmp = generate_torrent(
			size ? size : 1024, num_files ? num_files : 1, name.c_str(), num_trackers, gen_version);

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
		try
		{
			add_torrent_params atp = load_torrent_file(torrent_file);
			generate_data(data_path, *atp.ti);
		}
		catch (lt::system_error const& err)
		{
			std::fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", err.code().message().c_str());
			return 1;
		}
		return 0;
	}
	else if (command == "gen-test-torrents"_sv)
	{
		for (int i = 0; i < num_torrents; ++i)
		{
			char torrent_name[100];
			std::snprintf(torrent_name, sizeof(torrent_name), "%s-%d.torrent", torrent_file, i);

			std::vector<create_file_entry> fs;
			for (int j = 0; j < num_files; ++j)
			{
				char file_name[100];
				std::snprintf(file_name, sizeof(file_name), "%s-%d/file-%d", torrent_file, i, j);
				fs.push_back({std::string(file_name), std::int64_t(j + i + 1) * 251, {}, 0, {}});
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

			std::vector<char> buf = t.generate_buf();
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
	else if (command == "hash-stress"_sv)
	{
		test_mode = hash_stress_test;
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

	add_torrent_params atp = load_torrent_file(torrent_file);
	if (ec)
	{
		std::fprintf(stderr, "ERROR LOADING .TORRENT: %s\n", ec.message().c_str());
		return 1;
	}

	info_hash_t const& ihs = atp.ti->info_hashes();
	bool const torrent_has_v2 = ihs.has_v2();
	bool const torrent_has_v1 = ihs.has_v1();
	bool const handshake_uses_v2 = torrent_has_v2 && !(torrent_has_v1 && force_v1_handshake);
	sha1_hash const handshake_info_hash = handshake_uses_v2 ? sha1_hash(ihs.v2.data()) : ihs.v1;

	if (test_mode == hash_stress_test && !torrent_has_v2)
	{
		std::fprintf(stderr, "ERROR: hash-stress requires a v2 or hybrid torrent\n");
		return 1;
	}

	if (test_mode == hash_stress_test && force_v1_handshake)
	{
		std::fprintf(stderr, "ERROR: -1 (force v1 handshake) is incompatible with hash-stress\n");
		return 1;
	}

	// build the per-file merkle trees we need to respond to HASH_REQUEST (as
	// seed) and to drive the hash-stress flood. download-only mode does not
	// touch the trees, so the build is skipped to save startup cost.
	if (torrent_has_v2
		&& (test_mode == upload_test || test_mode == dual_test || test_mode == hash_stress_test))
	{
		build_global_file_trees(*atp.ti);
	}

	bool const flood_hashes_all = (test_mode == hash_stress_test);

	std::vector<peer_conn*> conns;
	conns.reserve(std::size_t(num_connections));
	int const num_threads = 2;
	io_context ios[num_threads];
	int const last_piece_size = atp.ti->piece_size(piece_index_t(atp.ti->num_pieces() - 1));
	for (int i = 0; i < num_connections; ++i)
	{
		bool corrupt = test_corruption && (i & 1) == 0;
		bool seed = false;
		if (test_mode == upload_test) seed = true;
		else if (test_mode == dual_test) seed = (i & 1);
		// hash-stress connections all act as downloaders that flood HASH_REQUEST.
		conns.push_back(new peer_conn(ios[i % num_threads],
			atp.ti->num_pieces(),
			atp.ti->piece_length() / 16 / 1024,
			last_piece_size,
			ep,
			handshake_info_hash,
			handshake_uses_v2,
			seed,
			churn,
			corrupt,
			flood_hashes_all));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		ios[i % num_threads].poll_one();
	}

	std::thread t1(&io_thread, &ios[0]);
	std::thread t2(&io_thread, &ios[1]);

	t1.join();
	t2.join();

	std::int64_t total_sent = 0;
	std::int64_t total_received = 0;
	std::int64_t total_hashes_received = 0;
	std::int64_t total_hashes_rejected = 0;
	std::int64_t total_hash_requests = 0;
	std::int64_t total_hashes_sent = 0;
	std::int64_t total_hash_rejects_sent = 0;

	for (peer_conn* p : conns)
	{
		int time = int(total_milliseconds(p->end_time - p->start_time));
		if (time == 0) time = 1;
		total_sent += p->blocks_sent;
		total_received += p->blocks_received;
		total_hashes_received += p->hashes_received;
		total_hashes_rejected += p->hashes_rejected;
		total_hash_requests += p->hash_requests_sent;
		total_hashes_sent += p->hashes_sent;
		total_hash_rejects_sent += p->hash_rejects_sent;
		delete p;
	}

	std::printf("=========================\n"
				"suggests: %d suggested-requests: %d\n"
				"total sent: %.1f %% received: %.1f %%\n"
				"rate sent: %.1f MB/s received: %.1f MB/s\n"
				"hash-requests: sent=%lld   hashes: sent=%lld received=%lld\n"
				"hash-rejects:  sent=%lld received=%lld\n",
		int(num_suggest),
		int(num_suggested_requests),
		double(total_sent * 0x4000) * 100.0 / double(atp.ti->total_size()),
		double(total_received * 0x4000) * 100.0 / double(atp.ti->total_size()),
		double(total_sent * 0x4000) / 1000000.0,
		double(total_received * 0x4000) / 1000000.0,
		static_cast<long long>(total_hash_requests),
		static_cast<long long>(total_hashes_sent),
		static_cast<long long>(total_hashes_received),
		static_cast<long long>(total_hash_rejects_sent),
		static_cast<long long>(total_hashes_rejected));

	return 0;
}
