/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// benchmark_load_torrent - time how long it takes to parse a .torrent
// file with libtorrent's load_torrent_buffer(), and report peak RSS.
//
// The file is read into memory once (untimed); load_torrent_buffer() is
// then called <iterations> times over the in-memory buffer and the total
// wall-clock time is reported, along with peak RSS from
// getrusage(RUSAGE_SELF). Designed to be driven by
// tools/benchmark_load_torrent.py, which generates "torture" torrents
// and parses the two output lines below.
//
// This is a stripped-down sibling of examples/dump_torrent: no torrent-
// content printing (file list, trackers, info hash, web seeds, ...). The
// optional [sanitize-flags] argument only exists to pin a case to one of
// path_sanitize_flags.hpp's named rulesets regardless of which one
// path_sanitize_flags::default_flags happens to select in a given build,
// since a case's identity (which ruleset, and so which duplicate-
// filename-resolution algorithm, it measures) needs to survive that
// default changing later; every other load_torrent_limits stays at its
// default on purpose.

#include <cinttypes>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include "libtorrent/load_torrent.hpp"
#include "libtorrent/path_sanitize_flags.hpp"
#include "libtorrent/string_view.hpp"

using namespace lt::literals;

#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace {

	[[noreturn]] void print_usage()
	{
		std::cerr << R"(usage: benchmark_load_torrent <torrent-file> <iterations> [sanitize-flags]

Times parsing of a .torrent file. The file is read once;
load_torrent_buffer() is then called <iterations> times over the
in-memory buffer.

[sanitize-flags], if given, pins path_sanitize_flags to one of the
named rulesets from path_sanitize_flags.hpp, regardless of what the
build's default_flags currently selects:
    2.0     path_sanitize_flags::libtorrent_2_0
    2.1     path_sanitize_flags::libtorrent_2_1
    2.2     path_sanitize_flags::libtorrent_2_2
    default path_sanitize_flags::default_flags

Output:
    load time: <total> ms total over <count> iterations (<avg> ms/iter)
    max rss: <bytes> bytes      (POSIX only)
)";
		std::exit(1);
	}

	lt::load_torrent_limits parse_cfg(int const argc, char const* const argv[])
	{
		lt::load_torrent_limits cfg;
		if (argc < 4)
			return cfg;
		lt::string_view const arg = argv[3];
		if (arg == "2.0"_sv)
			cfg.sanitize_flags = lt::path_sanitize_flags::libtorrent_2_0;
		else if (arg == "2.1"_sv)
			cfg.sanitize_flags = lt::path_sanitize_flags::libtorrent_2_1;
		else if (arg == "2.2"_sv)
			cfg.sanitize_flags = lt::path_sanitize_flags::libtorrent_2_2;
		else if (arg == "default"_sv)
			cfg.sanitize_flags = lt::path_sanitize_flags::default_flags;
		else
		{
			std::cerr << "ERROR: unknown [sanitize-flags] value: " << argv[3] << "\n";
			print_usage();
		}
		return cfg;
	}

} // namespace

int main(int argc, char const* argv[])
try
{
	if (argc != 3 && argc != 4)
		print_usage();

	char const* const filename = argv[1];
	int const repeat = std::atoi(argv[2]);
	if (repeat < 1)
	{
		std::cerr << "ERROR: <iterations> must be >= 1\n";
		return 1;
	}
	lt::load_torrent_limits const cfg = parse_cfg(argc, argv);

	// read the file into memory so the timing measures only the parse and
	// not the I/O.
	std::vector<char> buf;
	{
		std::ifstream in(filename, std::ios::binary | std::ios::ate);
		if (!in)
		{
			std::cerr << "ERROR: could not open " << filename << "\n";
			return 1;
		}
		auto const sz = in.tellg();
		if (sz < 0)
		{
			std::cerr << "ERROR: could not size " << filename << "\n";
			return 1;
		}
		in.seekg(0);
		buf.resize(static_cast<std::size_t>(sz));
		if (!in.read(buf.data(), static_cast<std::streamsize>(buf.size())))
		{
			std::cerr << "ERROR: could not read " << filename << "\n";
			return 1;
		}
	}

	// volatile sink fed from each parse: a read-modify-write of volatile
	// memory is an observable side effect, so the chain producing the
	// value (load_torrent_buffer -> add_torrent_params -> torrent_info)
	// must be preserved. Without this, the as-if rule (especially under
	// LTO) lets the compiler prove the call allocates and immediately
	// drops everything, and elide it entirely -- the timing loop would
	// then measure nothing.
	volatile std::int64_t sink = 0;
	auto const t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < repeat; ++i)
	{
		auto const atp = lt::load_torrent_buffer(buf, cfg);
		// explicit load + store rather than `sink +=`, which is deprecated
		// on a volatile lvalue since C++20 (-Wvolatile).
		sink = sink + (atp.ti ? std::int64_t(atp.ti->num_files()) : 0);
	}
	auto const t1 = std::chrono::steady_clock::now();
	double const total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
	std::printf("load time: %.3f ms total over %d iterations (%.3f ms/iter)\n",
		total_ms,
		repeat,
		total_ms / repeat);

#ifndef _WIN32
	// peak RSS the kernel ever observed for this process. ru_maxrss is
	// in KiB on Linux but in bytes on macOS; normalize to bytes here so
	// callers don't have to care.
	struct rusage ru = {};
	if (getrusage(RUSAGE_SELF, &ru) == 0)
	{
#ifdef __APPLE__
		std::int64_t const max_rss_bytes = std::int64_t(ru.ru_maxrss);
#else
		std::int64_t const max_rss_bytes = std::int64_t(ru.ru_maxrss) * 1024;
#endif
		std::printf("max rss: %" PRId64 " bytes\n", max_rss_bytes);
	}
#endif

	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
	return 1;
}
