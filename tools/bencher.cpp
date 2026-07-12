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
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/span.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

using clock_type = std::chrono::steady_clock;

// calls fun() repeatedly, for at least min_samples calls and at least
// min_duration of wall-clock time, and returns the per-call latency of
// each sample, in nanoseconds
template <typename Fun>
std::vector<double> measure(
	Fun fun, int const min_samples, std::chrono::milliseconds const min_duration)
{
	// warm up, e.g. to avoid attributing first-touch page faults to a
	// real sample
	for (int i = 0; i < 3; ++i)
		fun();

	std::vector<double> samples_ns;
	auto const deadline = clock_type::now() + min_duration;
	while (int(samples_ns.size()) < min_samples || clock_type::now() < deadline)
	{
		auto const start = clock_type::now();
		fun();
		auto const end = clock_type::now();
		// prevents the compiler from reordering or eliding memory writes
		// across this point
		asm volatile("" : : : "memory");
		samples_ns.push_back(std::chrono::duration<double, std::nano>(end - start).count());
	}
	return samples_ns;
}

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

stats analyze(lt::span<double const> samples_ns)
{
	double const mean =
		std::accumulate(samples_ns.begin(), samples_ns.end(), 0.0) / double(samples_ns.size());
	double sq_diff_sum = 0.0;
	for (double const s : samples_ns)
		sq_diff_sum += (s - mean) * (s - mean);
	return {mean, std::sqrt(sq_diff_sum / double(samples_ns.size()))};
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

int main()
try
{
	std::vector<std::pair<char const*, stats>> results;

#if !defined TORRENT_DISABLE_ENCRYPTION
	using namespace lt::aux;

	int const min_samples = 100;
	auto const min_duration = 500ms;

	// key generation: random secret + (2 ^ secret) % prime
	results.emplace_back("dh_key_exchange",
		analyze(measure([] { dh_key_exchange const dh; }, min_samples, min_duration)));

	// a peer's public key to react to, exported the same way it would
	// arrive off the wire
	dh_key_exchange const peer;
	std::array<char, 96> const peer_key = export_key(peer.get_local_key());

	// shared secret: (peer_pubkey ^ secret) % prime. this modexp is the
	// operation that dominates the per-connection DH cost, isolated here
	// from key generation and from hashing the resulting secret
	dh_key_exchange dh;
	results.emplace_back("dh_compute_secret",
		analyze(measure(
			[&] { dh.compute_secret(reinterpret_cast<std::uint8_t const*>(peer_key.data())); },
			min_samples,
			min_duration)));

	// the total DH cost incurred by one side of a PE handshake: generate
	// a local key pair, then compute the shared secret from the peer's
	// key
	results.emplace_back("dh_handshake",
		analyze(measure(
			[&] {
				dh_key_exchange local;
				local.compute_secret(reinterpret_cast<std::uint8_t const*>(peer_key.data()));
			},
			min_samples,
			min_duration)));

	// --- stream cipher throughput: RC4 vs AES-CTR ---
	// Measures the encryption throughput of each cipher by repeatedly
	// encrypting a 16 KB buffer (typical BT message size).
	// The reported latency is the wall-clock time (in nanoseconds)
	// to encrypt 16 KB, i.e. lower = faster.

	std::array<char, 20> const rc4_key = [] {
		std::array<char, 20> k;
		random_bytes(k);
		return k;
	}();

	constexpr int buf_size = 16 * 1024;
	std::vector<char> buf(static_cast<std::size_t>(buf_size));
	random_bytes(buf);

	{
		rc4_handler rc4;
		rc4.set_outgoing_key(rc4_key);
		results.emplace_back("rc4_encrypt_16KB",
			analyze(measure(
				[&] {
					lt::span<char> iov[1] = {lt::span<char>(buf)};
					rc4.encrypt(iov);
					do_not_optimize(buf[0]);
				},
				min_samples,
				min_duration)));
	}

#if TORRENT_HAS_MSE_AES_CTR
	{
		std::array<char, 20> const aes_ctr_key = [] {
			std::array<char, 20> k;
			random_bytes(k);
			return k;
		}();

		std::vector<char> aes_buf(static_cast<std::size_t>(buf_size));
		random_bytes(aes_buf);

		aes_ctr_handler aes_ctr;
		aes_ctr.set_outgoing_key(aes_ctr_key);
		results.emplace_back("aes_ctr_encrypt_16KB",
			analyze(measure(
				[&] {
					lt::span<char> iov[1] = {lt::span<char>(aes_buf)};
					aes_ctr.encrypt(iov);
					do_not_optimize(aes_buf[0]);
				},
				min_samples,
				min_duration)));
	}
#endif

#endif // !defined TORRENT_DISABLE_ENCRYPTION

	for (char const* filename : benchmark_cases)
	{
		std::vector<char> const buf = read_file(fs::path("bench-torrents") / filename);
		stats const s = analyze(measure(
			[&] {
				auto const atp = lt::load_torrent_buffer(buf);
				do_not_optimize(atp);
			},
			10,
			500ms));
		results.emplace_back(filename, s);
	}

	print_bmf(results);
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
	return 1;
}
