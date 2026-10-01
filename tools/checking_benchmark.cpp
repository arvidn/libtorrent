/*

Copyright (c) 2022, Arvid Norberg
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

#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <string>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

#include "libtorrent/create_torrent.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/mmap_disk_io.hpp"
#include "libtorrent/posix_disk_io.hpp"

#include "libtorrent/aux_/path.hpp"

using namespace std::literals::chrono_literals;
using std::chrono::milliseconds;

namespace {

constexpr char const* test_filename = "test_checking_file";

void generate_block_fill(lt::span<char> buf, std::uint64_t& state)
{
	int const tail = buf.size() % 8;
	int offset = 0;
	for (; offset < buf.size() - tail; offset += 8)
	{
		std::memcpy(buf.data() + offset, reinterpret_cast<char const*>(&state), 8);
		++state;
	}
	if (tail > 0)
	{
		std::memcpy(buf.data() + offset, reinterpret_cast<char const*>(&state), tail);
		++state;
	}
}

std::vector<char> generate_torrent(int num_pieces, std::string save_path
	, lt::create_flags_t const flags)
{
	// 1 MiB piece size
	const int piece_size = 1024 * 1024;
	const std::int64_t total_size = std::int64_t(piece_size) * num_pieces + 2356;

	std::string const filename{test_filename};

	std::vector<lt::create_file_entry> fs;
	fs.emplace_back(filename, total_size);

	std::string const filepath = save_path + "/" + filename;

	lt::file_status st;
	lt::error_code ec;
	lt::stat_file(filepath, &st, ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
	{
		std::cerr << "stat() failed: " << ec.message()
			<< " for file: " << filepath << '\n';
		std::exit(1);
	}
	if (st.file_size != total_size)
	{
		std::cout << "writing test file\n";
		std::uint64_t state = 0;
		std::ofstream output(filepath, std::ios_base::binary);
		std::int64_t bytes_left = total_size;
		std::array<char, 100000> buffer;
		while (bytes_left > 0)
		{
			generate_block_fill(buffer, state);
			output.write(buffer.data(), std::min(buffer.size(), std::size_t(bytes_left)));
			bytes_left -= buffer.size();

			std::cout << "\rleft: " << bytes_left << " B  ";
			std::cout.flush();
		}
		std::cout << '\n';
	}

	lt::create_torrent t(std::move(fs), piece_size, flags);

	std::cout << "hashing torrent\n";
	lt::set_piece_hashes(t, save_path);

	std::vector<char> ret;
	bencode(std::back_inserter(ret), t.generate());
	return ret;
}

#ifdef __linux__
void print_filesystem(std::string const& save_path)
{
	constexpr std::uint32_t zfs_magic = 0x2fc12fc1;

	struct fs_magic
	{
		std::uint32_t magic;
		char const* name;
	};
	// these are the f_type values from statfs(2)
	constexpr std::array<fs_magic, 14> known{{{0xef53, "ext2/3/4"},
		{0x9123683e, "btrfs"},
		{0x58465342, "xfs"},
		{0xf2f52010, "f2fs"},
		{0xca451a4e, "bcachefs"},
		{zfs_magic, "zfs"},
		{0x01021994, "tmpfs"},
		{0x794c7630, "overlayfs"},
		{0x6969, "nfs"},
		{0x65735546, "fuse"},
		{0x5346544e, "ntfs"},
		{0x2011bab0, "exfat"},
		{0x4d44, "vfat"},
		{0x73717368, "squashfs"}}};

	struct statfs st = {};
	if (::statfs(save_path.c_str(), &st) != 0)
	{
		std::cerr << "statfs() failed: " << std::strerror(errno) << " for path: " << save_path
				  << '\n';
		return;
	}

	auto const type = static_cast<std::uint32_t>(st.f_type);
	std::cout << "filesystem: ";
	auto const it = std::find_if(
		known.begin(), known.end(), [&](fs_magic const& m) { return m.magic == type; });
	std::cout << (it != known.end() ? it->name : "unknown") << '\n';

	if (type == zfs_magic)
	{
		std::cout << "WARNING: the ZFS ARC is not cleared between runs, results\n"
					 "will reflect a warm cache\n";
	}
}

bool drop_file_cache(std::string const& filepath)
{
	int const fd = ::open(filepath.c_str(), O_RDONLY);
	if (fd < 0)
		return false;
	// POSIX_FADV_DONTNEED does not evict dirty pages
	bool const ok = ::fdatasync(fd) == 0 && ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
	::close(fd);
	return ok;
}
#endif

void drop_caches(std::string const& save_path)
{
#ifdef __linux__
	if (drop_file_cache(save_path + "/" + std::string{test_filename}))
		return;
	std::cout << "failed to drop the file cache automatically\n";
#else
	(void)save_path;
#endif
	std::cout << "drop caches now. e.g. \"echo 1 | sudo tee /proc/sys/vm/drop_caches\"\n";
	std::cout << "press enter to continue\n";

	char dummy;
	std::cin.read(&dummy, 1);
}

milliseconds run_test(std::string const& save_path,
	std::vector<char> const& torrent_buf,
	lt::disk_io_constructor_type disk,
	int const hashing_threads)
{
	drop_caches(save_path);

	lt::session_params params;
	params.disk_io_constructor = disk;
	auto& s = params.settings;
	s.set_bool(lt::settings_pack::enable_dht, false);
	s.set_bool(lt::settings_pack::enable_upnp, false);
	s.set_bool(lt::settings_pack::enable_natpmp, false);
	s.set_bool(lt::settings_pack::enable_lsd, false);
	s.set_int(lt::settings_pack::hashing_threads, hashing_threads);
	s.set_int(lt::settings_pack::alert_mask
		, lt::alert_category::error | lt::alert_category::storage | lt::alert_category::status);
	s.set_str(lt::settings_pack::listen_interfaces, "");

	lt::session ses(params);
	lt::add_torrent_params atp = lt::load_torrent_buffer(torrent_buf);
	atp.save_path = save_path;
	atp.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
	lt::torrent_handle h = ses.add_torrent(atp);
	auto const start = lt::clock_type::now();
	for (;;)
	{
		ses.wait_for_alert(5s);
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);
		for (lt::alert const* a : alerts)
		{
			std::cout << a->message() << '\n';
			if (auto const* sca = lt::alert_cast<lt::state_changed_alert>(a))
			{
				if (sca->state != lt::torrent_status::checking_files
					&& sca->state != lt::torrent_status::checking_resume_data)
					goto done;
			}
		}
	}
done:
	auto const end = lt::clock_type::now();
	return std::chrono::duration_cast<milliseconds>(end - start);
}

struct result
{
	char const* torrent;
	int hashing_threads;
	// one duration per disk I/O backend, in the same order as the table columns
	std::vector<milliseconds> durations;
};

void print_table(std::vector<char const*> const& disk_names, std::vector<result> const& results)
{
	std::cout << "\n| torrent | hashing threads |";
	for (char const* name : disk_names)
		std::cout << ' ' << name << " (s) |";
	std::cout << "\n|---|---:|";
	for (std::size_t i = 0; i < disk_names.size(); ++i)
		std::cout << "---:|";
	std::cout << '\n';

	for (auto const& r : results)
	{
		std::cout << "| " << r.torrent << " | " << r.hashing_threads << " |";
		for (auto const d : r.durations)
		{
			std::cout << ' ' << std::fixed << std::setprecision(3) << (d.count() / 1000.) << " |";
		}
		std::cout << '\n';
	}
}
}

int main(int argc, char const* argv[]) try
{
	std::string save_path = ".";
	if (argc > 1)
		save_path = argv[1];

#ifdef __linux__
	print_filesystem(save_path);
#endif

	struct torrent_config
	{
		char const* name;
		lt::create_flags_t flags;
	};
	struct disk_config
	{
		char const* name;
		lt::disk_io_constructor_type constructor;
	};
	std::array<torrent_config, 3> const torrents{{{"v1-only", lt::create_torrent::v1_only},
		{"v2-only", lt::create_torrent::v2_only},
		{"hybrid", {}}}};
	std::vector<disk_config> const disks{
#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
		{"mmap", lt::mmap_disk_io_constructor},
#endif
		{"posix", lt::posix_disk_io_constructor}};
	constexpr std::array<int, 3> thread_counts{{1, 5, 10}};

	std::vector<char const*> disk_names;
	for (auto const& disk : disks)
		disk_names.push_back(disk.name);

	std::vector<result> results;
	for (auto const& torrent : torrents)
	{
		auto const torrent_buf = generate_torrent(7000, save_path, torrent.flags);
		for (int const threads : thread_counts)
		{
			result r{torrent.name, threads, {}};
			for (auto const& disk : disks)
			{
				r.durations.push_back(run_test(save_path, torrent_buf, disk.constructor, threads));
			}
			results.push_back(std::move(r));
		}
	}

	print_table(disk_names, results);
}
catch (lt::system_error const& e)
{
	std::cerr << "Failed: " << e.code().message() << '\n';
}
