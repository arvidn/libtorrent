/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// bencher - runs libtorrent's small, no-argument benchmarks and prints the
// results as Bencher Metric Format (BMF) JSON on stdout, for use with
// `bencher run --adapter json`. Named "bencher"
// Expects to be run with tools/ as the working directory (this is how
// it's invoked from CI, which cd's into tools/ before running it).

#include "libtorrent/config.hpp"

#if !defined(__GNUC__) && !defined(__clang__)
// cppcheck-suppress preprocessorErrorDirective
#error \
	"do_not_optimize() relies on GNU inline asm to act as a compiler optimization barrier, and is only supported on GCC and clang"
#endif

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/aux_/piece_picker.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/span.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

using clock_type = std::chrono::steady_clock;

// prevents the compiler from proving `value` is unused and eliding the
// computation that produced it. The empty asm block with an input
// constraint forces the compiler to materialize `value` (in a register
// or in memory) at this point, without emitting any real instruction.
// Adapted from the technique used by Google Benchmark's DoNotOptimize.
template <typename T>
inline void do_not_optimize(T const& value)
{
	asm volatile("" : : "r,m"(value) : "memory");
}

struct stats
{
	double mean_ns;
	double stddev_ns;
};

// calls fun() repeatedly, for at least 10 samples and at least 500ms of
// wall-clock time, and returns the mean and standard deviation of the
// per-call latency, in nanoseconds. Samples are folded into the running
// mean and variance as they're taken (Welford's online algorithm), so this
// doesn't need to keep every sample around just to analyze them afterwards.
template <typename Fun>
stats analyze(Fun fun)
{
	int const min_samples = 10;
	auto const min_duration = 500ms;

	// warm up, e.g. to avoid attributing first-touch page faults to a
	// real sample
	for (int i = 0; i < 3; ++i)
		fun();

	int count = 0;
	double mean_ns = 0.0;
	double m2 = 0.0;
	auto const deadline = clock_type::now() + min_duration;
	while (count < min_samples || clock_type::now() < deadline)
	{
		auto const start = clock_type::now();
		fun();
		auto const end = clock_type::now();
		// prevents the compiler from reordering or eliding memory writes
		// across this point
		asm volatile("" : : : "memory");
		double const sample_ns = std::chrono::duration<double, std::nano>(end - start).count();

		++count;
		double const delta = sample_ns - mean_ns;
		mean_ns += delta / count;
		m2 += delta * (sample_ns - mean_ns);
	}
	return {mean_ns, std::sqrt(m2 / count)};
}

// prints a set of named benchmarks as a Bencher Metric Format (BMF)
// JSON document on stdout, for use with `bencher run --adapter json`
void print_bmf(lt::span<std::pair<char const*, stats> const> benchmarks)
{
	std::cout << std::fixed << std::setprecision(3);

	std::cout << "{\n";
	for (int i = 0; i < int(benchmarks.size()); ++i)
	{
		auto const& [name, s] = benchmarks[i];
		std::cout << "  \"" << name
				  << "\": {\n"
					 "    \"latency\": {\n"
					 "      \"value\": "
				  << s.mean_ns
				  << ",\n"
					 "      \"lower_value\": "
				  << (s.mean_ns - s.stddev_ns)
				  << ",\n"
					 "      \"upper_value\": "
				  << (s.mean_ns + s.stddev_ns)
				  << "\n"
					 "    }\n"
					 "  }"
				  << (i + 1 == int(benchmarks.size()) ? "\n" : ",\n");
	}
	std::cout << "}\n";
}

std::vector<char> read_file(fs::path filename)
{
	std::ifstream in(filename, std::ios::binary | std::ios::ate);
	if (!in) throw std::runtime_error("could not open " + filename.string());

	auto const sz = in.tellg();
	if (sz < 0) throw std::runtime_error("could not determine size of " + filename.string());

	in.seekg(0);
	std::vector<char> buf(static_cast<std::size_t>(sz));
	if (!in.read(buf.data(), static_cast<std::streamsize>(buf.size())))
		throw std::runtime_error("could not read " + filename.string());

	return buf;
}

std::array<char const*, 3> const benchmark_cases = {{
	"base-v1.torrent",
	"base-v2.torrent",
	"many-pad-files.torrent",
}};

// piece_picker benchmarks. These target specific worst cases in
// src/piece_picker.cpp where an operation that looks local (touching one
// piece, or a peer with few pieces) actually costs O(num_pieces), so
// regressions there are easy to introduce without noticing in normal use.
// A pointer is used as the peer identity throughout; the picker never
// dereferences it outside of TORRENT_DEBUG_REFCOUNTS (off by default), so
// nullptr is a valid stand-in for a real aux::torrent_peer.
namespace pp_bench {

	using lt::counters;
	using lt::download_priority_t;
	using lt::piece_block;
	using lt::piece_index_t;
	using lt::typed_bitfield;
	using lt::aux::piece_picker;

	constexpr int num_pieces = 100000;
	constexpr int piece_size = 4 * 16 * 1024; // 4 blocks per piece

	piece_picker make_picker()
	{
		return piece_picker(std::int64_t(num_pieces) * piece_size, piece_size);
	}

	// a bitfield with `k` bits set, spread evenly across the whole piece range,
	// rather than clustered, so scan cost doesn't depend on where in the range
	// the peer's pieces happen to fall
	typed_bitfield<piece_index_t> sparse_bits(int const k)
	{
		typed_bitfield<piece_index_t> bm(num_pieces, false);
		if (k <= 0) return bm;
		int const stride = std::max(1, num_pieces / k);
		int count = 0;
		for (int i = 0; i < num_pieces && count < k; i += stride, ++count)
			bm.set_bit(piece_index_t(i));
		return bm;
	}

	void run(std::vector<std::pair<char const*, stats>>& results)
	{
		counters pc;
		std::vector<piece_index_t> const empty_suggested;

		// picks with the defaults shared by every pick_pieces() benchmark below
		// (rarest-first, no suggestions, 20 connected peers). `picked` is
		// cleared rather than freed, so repeated calls across benchmark samples
		// don't pay for a reallocation that has nothing to do with what's
		// actually being measured.
		auto const pick = [&](piece_picker const& picker,
							  typed_bitfield<piece_index_t> const& bits,
							  int const num_blocks,
							  std::vector<piece_block>& picked) {
			picked.clear();
			picker.pick_pieces(bits,
				picked,
				num_blocks,
				0,
				nullptr,
				piece_picker::rarest_first,
				empty_suggested,
				20,
				pc);
		};

		// scenario 1: inc_refcount()/dec_refcount() taking a peer's bitfield try
		// to avoid dirtying the picker by first counting set bits, capped at
		// min(50, bitmask.size()/2). That counting loop only stops early once it
		// *finds* the cap number of set bits, so a peer with fewer set bits than
		// the cap scans the entire bitmask before falling into the cheap
		// per-piece update path. Sweeping the set-bit count from far below to far
		// above the fixed cap of 50 shows the scan cost is flat with respect to
		// that constant -- it's driven by the piece count, not by where the
		// threshold happens to be drawn, so retuning "50" trades one O(n) path
		// for another rather than removing the O(n) cost.
		{
			struct pp_case
			{
				char const* name;
				int bits_set;
			};
			pp_case const cases[] = {
				{"piece picker: refcount bitfield, 1 bit set", 1},
				{"piece picker: refcount bitfield, 10 bits set", 10},
				{"piece picker: refcount bitfield, 49 bits set", 49},
				{"piece picker: refcount bitfield, 50 bits set", 50},
				{"piece picker: refcount bitfield, 200 bits set", 200},
				{"piece picker: refcount bitfield, 5000 bits set", 5000},
			};
			for (auto const& c : cases)
			{
				piece_picker p = make_picker();
				auto const bits = sparse_bits(c.bits_set);
				results.emplace_back(c.name, analyze([&] {
					p.inc_refcount(bits, nullptr);
					p.dec_refcount(bits, nullptr);
				}));
			}
		}

		// scenario 2: pick_pieces()'s rarest-first scan walks the entire
		// pickable-piece list (m_pieces), testing each entry against the peer's
		// bitfield -- it never walks the peer's bitfield directly. A peer that
		// only has one piece therefore costs the same scan as a peer that has
		// almost everything, since the loop can't stop until it either satisfies
		// the block request or exhausts the list.
		{
			piece_picker p = make_picker();
			p.inc_refcount_all(nullptr); // a background seed, so every piece is pickable

			typed_bitfield<piece_index_t> sparse_peer(num_pieces, false);
			sparse_peer.set_bit(piece_index_t(num_pieces - 1));
			typed_bitfield<piece_index_t> const dense_peer(num_pieces, true);

			std::vector<piece_block> picked;
			results.emplace_back("piece picker: pick pieces, sparse peer", analyze([&] {
				pick(p, sparse_peer, 200, picked);
				do_not_optimize(picked);
			}));

			results.emplace_back("piece picker: pick pieces, dense peer", analyze([&] {
				pick(p, dense_peer, 4, picked);
				do_not_optimize(picked);
			}));
		}

		// scenario 3: peers with every piece are tracked as a single m_seeds
		// counter, O(1) to add or remove. A peer that has all but one piece
		// fails that literal all-bits-set check and instead pays the full
		// per-piece bitfield scan from scenario 1 -- so a peer missing a single
		// piece out of num_pieces is far more expensive to account for than one
		// that's actually complete.
		{
			piece_picker p = make_picker();
			results.emplace_back("piece picker: add/remove seed", analyze([&] {
				p.inc_refcount_all(nullptr);
				p.dec_refcount_all(nullptr);
			}));

			typed_bitfield<piece_index_t> near_seed(num_pieces, true);
			near_seed.clear_bit(piece_index_t(0));
			results.emplace_back("piece picker: add/remove near-seed", analyze([&] {
				p.inc_refcount(near_seed, nullptr);
				p.dec_refcount(near_seed, nullptr);
			}));
		}

		// scenario 4: when a peer we've counted as a seed (via m_seeds) sends a
		// DONT_HAVE for one piece, the only way to represent "seed minus one
		// piece" is break_one_seed(), which converts the m_seeds count into real
		// per-piece peer_count increments across the whole piece map.
		{
			piece_picker p = make_picker();
			results.emplace_back("piece picker: break one seed", analyze([&] {
				p.inc_refcount_all(nullptr);
				// piece 0 has peer_count == 0 while a seed is present, so this
				// is what triggers break_one_seed()
				p.dec_refcount(piece_index_t(0), nullptr);
				// restore piece 0's count and drop the now-per-piece "seed"
				// back out, so the next iteration starts from the same state
				p.inc_refcount(piece_index_t(0), nullptr);
				p.dec_refcount_all(nullptr);
			}));
		}

		// scenario 5: m_pieces/m_priority_boundaries are rebuilt lazily -- any
		// bulk change just sets m_dirty, and the full rebuild is deferred to the
		// next pick. There's no incremental rebuild, so that next pick pays for
		// the whole piece list regardless of how small the change that dirtied
		// it was. Comparing a pick on an already-clean picker against a pick
		// immediately following a (cheap) dirtying change isolates that
		// deferred rebuild cost.
		{
			piece_picker p_clean = make_picker();
			p_clean.inc_refcount_all(nullptr);
			typed_bitfield<piece_index_t> const all_pieces(num_pieces, true);
			std::vector<piece_block> picked;
			pick(p_clean, all_pieces, 1, picked); // warm up: build m_pieces once

			results.emplace_back("piece picker: pick pieces, clean", analyze([&] {
				pick(p_clean, all_pieces, 1, picked);
				do_not_optimize(picked);
			}));

			piece_picker p_dirty = make_picker();
			results.emplace_back("piece picker: pick pieces, after dirty", analyze([&] {
				// toggling a seed on/off dirties the picker cheaply (O(1));
				// the pick that follows is what pays for update_pieces()'s
				// O(n) rebuild
				p_dirty.inc_refcount_all(nullptr);
				pick(p_dirty, all_pieces, 1, picked);
				do_not_optimize(picked);
				p_dirty.dec_refcount_all(nullptr);
			}));
		}

		// scenario 6: m_downloads is a vector sorted by piece index. Looking up
		// a piece in it is a binary search, but inserting or removing a
		// downloading piece shifts every trailing element. With many pieces
		// open for download at once, inserting a low-index piece is far more
		// expensive than appending one near the end.
		{
			piece_picker p = make_picker();
			p.inc_refcount_all(nullptr);

			int const open_pieces = 5000;
			for (int i = 1; i <= open_pieces; ++i)
				p.mark_as_downloading(piece_block(piece_index_t(i), 0), nullptr);

			results.emplace_back("piece picker: mark as downloading, low index", analyze([&] {
				p.mark_as_downloading(piece_block(piece_index_t(0), 0), nullptr);
				p.abort_download(piece_block(piece_index_t(0), 0), nullptr);
			}));

			results.emplace_back("piece picker: mark as downloading, high index", analyze([&] {
				p.mark_as_downloading(piece_block(piece_index_t(open_pieces + 1), 0), nullptr);
				p.abort_download(piece_block(piece_index_t(open_pieces + 1), 0), nullptr);
			}));
		}

		// scenario 7: get_availability() and piece_priorities() copy one entry
		// per piece into the caller-provided vector unconditionally. Fine as an
		// occasional bulk query, but worth tracking for torrents with very
		// large piece counts.
		{
			piece_picker p = make_picker();
			p.inc_refcount_all(nullptr);

			// get_availability()/piece_priorities() resize() their out-param to
			// num_pieces on every call, so reusing these across samples avoids
			// a reallocation that isn't part of what's being measured
			lt::aux::vector<int, piece_index_t> avail;
			results.emplace_back("piece picker: get availability", analyze([&] {
				p.get_availability(avail);
				do_not_optimize(avail);
			}));

			std::vector<download_priority_t> prios;
			results.emplace_back("piece picker: piece priorities", analyze([&] {
				p.piece_priorities(prios);
				do_not_optimize(prios);
			}));
		}
	}

} // namespace pp_bench

int main()
try
{
	std::vector<std::pair<char const*, stats>> results;

#if !defined TORRENT_DISABLE_ENCRYPTION
	using namespace lt::aux;

	// key generation: random secret + (2 ^ secret) % prime
	results.emplace_back("dh_key_exchange", analyze([] { dh_key_exchange const dh; }));

	// a peer's public key to react to, exported the same way it would
	// arrive off the wire
	dh_key_exchange const peer;
	std::array<char, 96> const peer_key = export_key(peer.get_local_key());

	// shared secret: (peer_pubkey ^ secret) % prime. this modexp is the
	// operation that dominates the per-connection DH cost, isolated here
	// from key generation and from hashing the resulting secret
	dh_key_exchange dh;
	results.emplace_back("dh_compute_secret", analyze([&] {
		dh.compute_secret(reinterpret_cast<std::uint8_t const*>(peer_key.data()));
	}));

	// the total DH cost incurred by one side of a PE handshake: generate
	// a local key pair, then compute the shared secret from the peer's
	// key
	results.emplace_back("dh_handshake", analyze([&] {
		dh_key_exchange local;
		local.compute_secret(reinterpret_cast<std::uint8_t const*>(peer_key.data()));
	}));

	// RC4 stream cipher throughput, encrypting a single 16 kiB buffer (the
	// size of one block, the unit of transfer in the BitTorrent protocol).
	// decrypt() runs the same underlying rc4_encrypt() core over the same
	// amount of data, just against the peer key schedule, so its cost
	// tracks this benchmark and isn't measured separately.
	lt::sha1_hash const rc4_key = lt::hasher("bencher-rc4-key", 15).final();

	rc4_handler rc4_enc;
	rc4_enc.set_outgoing_key(rc4_key);
	std::vector<char> rc4_buf(16 * 1024);
	results.emplace_back("rc4_encrypt", analyze([&] {
		lt::span<char> iovec(rc4_buf);
		auto const ret = rc4_enc.encrypt(iovec);
		do_not_optimize(ret);
	}));
#endif

	for (char const* filename : benchmark_cases)
	{
		std::vector<char> const buf = read_file(fs::path("bench-torrents") / filename);
		stats const s = analyze([&] {
			auto const atp = lt::load_torrent_buffer(buf);
			do_not_optimize(atp);
		});
		results.emplace_back(filename, s);
	}

	pp_bench::run(results);

	print_bmf(results);
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
	return 1;
}
