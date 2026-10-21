/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Shared helpers for seed_conn: building small torrents backed by real,
// on-disk file content with real piece hashes, so the local session can
// genuinely serve requested pieces, either as a seed_mode torrent (piece
// hashes trusted up front, verified lazily per request) or a fast-resumed
// one (have_pieces resume data).

#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <fcntl.h> // for open(), O_CREAT
#include <sys/file.h> // for flock()
#include <unistd.h> // for close()

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/aux_/path.hpp"

#include "peer_session.hpp"

// Exclusive flock() on `path` for the object's lifetime. Serializes
// build_seed_torrent() across concurrent libFuzzer workers (-jobs/-workers)
// sharing the same working directory and seed-torrent paths, so one can't
// truncate a file another is midway through set_piece_hashes()-ing.
class seed_file_lock
{
public:
	explicit seed_file_lock(std::string const& path)
	{
		lt::error_code ec;
		lt::create_directories(lt::parent_path(path), ec);
		m_fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
		if (m_fd < 0 || ::flock(m_fd, LOCK_EX) != 0)
			fuzz_init_failed("failed to lock seed-torrent directory");
	}
	~seed_file_lock()
	{
		::flock(m_fd, LOCK_UN);
		::close(m_fd);
	}
	seed_file_lock(seed_file_lock const&) = delete;
	seed_file_lock& operator=(seed_file_lock const&) = delete;

private:
	int m_fd;
};

// write_seed_file()'s output is a pure function of (path, size, fill), so a
// size match means a prior run already wrote the bytes this run would,
// safe to reuse as is.
inline bool seed_file_matches(std::string const& path, std::int64_t const size)
{
	lt::error_code ec;
	lt::file_status st;
	lt::stat_file(path, &st, ec);
	return !ec && st.file_size == size;
}

// Creates any missing parent directories of `path`.
inline void write_seed_file(
	std::string const& path, std::int64_t const size, std::uint8_t const fill)
{
	lt::error_code ec;
	lt::create_directories(lt::parent_path(path), ec);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	std::vector<char> buf(static_cast<std::size_t>(std::min<std::int64_t>(size, 0x10000)));
	std::uint8_t b = fill;
	std::int64_t left = size;
	while (left > 0)
	{
		std::size_t const n = static_cast<std::size_t>(
			std::min<std::int64_t>(left, static_cast<std::int64_t>(buf.size())));
		for (std::size_t i = 0; i < n; ++i)
			buf[i] = static_cast<char>(b++);
		f.write(buf.data(), static_cast<std::streamsize>(n));
		left -= static_cast<std::int64_t>(n);
	}
}

// file_sizes[i] becomes file "f<i>"; torrent_flags goes straight to
// create_torrent (e.g. create_torrent::v1_only).
struct seed_torrent_spec
{
	char const* name;
	int piece_size;
	std::vector<std::int64_t> file_sizes;
	lt::create_flags_t torrent_flags;
};

// Path is fixed (not per-pid) so repeated invocations reuse rather than
// accumulate. The seedmode/realseed path segment alone decorrelates a
// spec's two info-hashes (it's part of the bencoded dict, like the piece
// hashes), so `fill` can be one constant for every call; hybrid_spec and
// v1_only_spec are already decorrelated from each other by piece_size.
inline lt::add_torrent_params build_seed_torrent(
	seed_torrent_spec const& spec, std::uint8_t const fill, bool const seed_mode)
{
	std::string const save_path = lt::combine_path("fuzzer_seed_torrent", spec.name);
	std::string const mode_dir = seed_mode ? "seedmode" : "realseed";

	seed_file_lock const lock(lt::combine_path(save_path, mode_dir + ".lock"));

	std::vector<lt::create_file_entry> fs;
	for (std::size_t i = 0; i < spec.file_sizes.size(); ++i)
	{
		std::string const fname = lt::combine_path(mode_dir, "f" + std::to_string(i));
		std::string const fpath = lt::combine_path(save_path, fname);
		if (!seed_file_matches(fpath, spec.file_sizes[i]))
			write_seed_file(fpath, spec.file_sizes[i], fill);
		fs.emplace_back(fname, spec.file_sizes[i]);
	}

	lt::create_torrent t(std::move(fs), spec.piece_size, spec.torrent_flags);
	lt::error_code ec;
	lt::set_piece_hashes(t, save_path, ec);
	if (ec)
		fuzz_init_failed("set_piece_hashes() failed");

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), t.generate());
	lt::add_torrent_params atp = lt::load_torrent_buffer(buf);
	atp.save_path = save_path;
	// default_flags starts every torrent paused and auto managed; clear
	// both so these torrents stay seeding permanently instead of racing
	// the startup queue, or later getting rotated out as "inactive" by the
	// queuing logic since they see no real, sustained upload demand.
	atp.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
	if (seed_mode)
		atp.flags |= lt::torrent_flags::seed_mode;
	else
		// claims every piece already had, taking the fast-resume path
		// instead of a real disk hash check.
		atp.have_pieces = lt::typed_bitfield<lt::piece_index_t>(t.num_pieces(), true);
	return atp;
}

// Waits up to 5s for the TCP listen_succeeded_alert, recording its port in
// `listen_port`. Returns 0 on success, -1 on timeout.
inline int wait_for_listen(lt::session& ses, int& listen_port)
{
	lt::time_point const end_time = lt::clock_type::now() + lt::seconds(5);
	while (listen_port == 0)
	{
		auto const now = lt::clock_type::now();
		if (now > end_time)
			return -1;
		std::vector<lt::alert*> alerts;
		ses.wait_for_alert(end_time - now);
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
		{
			if (auto const* la = lt::alert_cast<lt::listen_succeeded_alert>(a))
			{
				if (la->socket_type == lt::socket_type_t::tcp)
					listen_port = la->port;
			}
		}
	}
	return 0;
}

// Waits up to 5s for `count` torrent_checked_alert instances, one per
// seed_conn torrent add. This fires once a torrent considers itself a fully
// verified seed: immediately, trusting the data, for both a seed_mode
// torrent and a fast-resumed one. Returns 0 on success, -1 on timeout.
inline int wait_for_torrents_checked(lt::session& ses, int count)
{
	lt::time_point const end_time = lt::clock_type::now() + lt::seconds(5);
	while (count > 0)
	{
		auto const now = lt::clock_type::now();
		if (now > end_time)
			return -1;
		std::vector<lt::alert*> alerts;
		ses.wait_for_alert(end_time - now);
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
		{
			if (lt::alert_cast<lt::torrent_checked_alert>(a))
				--count;
		}
	}
	return 0;
}
