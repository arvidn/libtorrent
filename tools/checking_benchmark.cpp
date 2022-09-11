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
#include <chrono>

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

	std::string const filename = "test_checking_file";

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

void run_test(std::string const& save_path, lt::create_flags_t const flags
	, lt::disk_io_constructor_type disk)
{
	auto const torrent_buf = generate_torrent(7000, save_path, flags);

	std::cout << "drop caches now. e.g. \"echo 1 | sudo tee /proc/sys/vm/drop_caches\"\n";
	std::cout << "press enter to continue\n";

	char dummy;
	std::cin.read(&dummy, 1);

	lt::session_params params;
	params.disk_io_constructor = disk;
	auto& s = params.settings;
	s.set_bool(lt::settings_pack::enable_dht, false);
	s.set_bool(lt::settings_pack::enable_upnp, false);
	s.set_bool(lt::settings_pack::enable_natpmp, false);
	s.set_bool(lt::settings_pack::enable_lsd, false);
	s.set_int(lt::settings_pack::hashing_threads, 1);
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
	std::cout << "\n\nduration: "
		<< (std::chrono::duration_cast<milliseconds>(end - start).count() / 1000.)
		<< "s\n";
}

}

int main(int argc, char const* argv[]) try
{
	std::string save_path = ".";
	if (argc > 1)
		save_path = argv[1];

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
	run_test(save_path, lt::create_torrent::v1_only, lt::mmap_disk_io_constructor);
	std::cout << "v1-only, mmap disk I/O\n\n";
	run_test(save_path, lt::create_torrent::v2_only, lt::mmap_disk_io_constructor);
	std::cout << "v2-only, mmap disk I/O\n\n";
	run_test(save_path, {}, lt::mmap_disk_io_constructor);
	std::cout << "hybrid, mmap disk I/O\n\n";
#endif
	run_test(save_path, lt::create_torrent::v1_only, lt::posix_disk_io_constructor);
	std::cout << "v1-only, posix disk I/O\n\n";
	run_test(save_path, lt::create_torrent::v2_only, lt::posix_disk_io_constructor);
	std::cout << "v2-only, posix disk I/O\n\n";
	run_test(save_path, {}, lt::posix_disk_io_constructor);
	std::cout << "hybrid, posix disk I/O\n\n";
}
catch (lt::system_error const& e)
{
	std::cerr << "Failed: " << e.code().message() << '\n';
}
