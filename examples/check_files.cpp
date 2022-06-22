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

#include <cstdlib>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>

#include <libtorrent/load_torrent.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/operations.hpp>
#include <libtorrent/file_storage.hpp>

using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::duration_cast;

int main(int argc, char* argv[]) try
{
	if (argc != 4) {
		std::cerr << "usage: ./check_files torrent-file download-dir output-resume-file\n";
		return 1;
	}

	lt::session_params ses_params;
	lt::settings_pack& pack = ses_params.settings;
	// start an off-line session. No listen sockets, no DHT or LSD and no port
	// forwarding
	pack.set_bool(lt::settings_pack::enable_dht, false);
	pack.set_bool(lt::settings_pack::enable_lsd, false);
	pack.set_bool(lt::settings_pack::enable_upnp, false);
	pack.set_bool(lt::settings_pack::enable_natpmp, false);
	pack.set_str(lt::settings_pack::listen_interfaces, "");
	lt::session ses(ses_params);
	lt::add_torrent_params p = lt::load_torrent_file(argv[1]);
	p.save_path = argv[2];

	// stop_when_ready will stop the torrent immediately when it's done
	// checking.
	p.flags |= lt::torrent_flags::stop_when_ready;
	// start the torrent in non-paused mode
	p.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);

	auto const total_size = p.ti->total_size();
	lt::torrent_handle h = ses.add_torrent(std::move(p));

	ses.post_torrent_updates();

	using clock_type = std::chrono::system_clock;
	auto const start_time = clock_type::now();

	std::vector<lt::alert*> alerts;
	for (;;)
	{
		ses.pop_alerts(&alerts);
		for (auto a : alerts)
		{
			if (auto const* su = lt::alert_cast<lt::state_update_alert>(a))
			{
				ses.post_torrent_updates();
				for (lt::torrent_status const& st : su->status)
				{
					if (st.handle != h) continue;

					if (st.state != lt::torrent_status::checking_files
						&& st.state != lt::torrent_status::checking_resume_data)
					{
						h.save_resume_data();
						std::puts("\nrequest resume data");
						goto done_checking;
					}

					// the number of bytes we've checked so far
					auto const bytes_progress = double(st.progress_ppm)
						/ 1000000. * double(total_size);
					auto const now = clock_type::now();
					// the amount of time we've spent so far, in seconds
					auto const duration = duration_cast<milliseconds>(now - start_time) / 1000.;
					auto const rate = bytes_progress / duration.count();

					std::printf("\rchecking %5.2f%% %7.2f MB/s pieces: %-5d ETA: %.1fs   "
						, st.progress_ppm / 10000.0
						, rate / 1000000.
						, st.num_pieces
						, (total_size - bytes_progress) / rate);
					std::fflush(stdout);
				}
			}
			if (auto const* err = lt::alert_cast<lt::file_error_alert>(a))
			{
				std::printf("\nfile error: (%s) %s\nfile: %s\n"
					, operation_name(err->op)
					, err->error.message().c_str()
					, err->filename());
				return 1;
			}
			if (auto const* err = lt::alert_cast<lt::torrent_error_alert>(a))
			{
				std::printf("\ntorrent error: %s\nfile: %s\n"
					, err->error.message().c_str()
					, err->filename());
				return 1;
			}
		}
		std::this_thread::sleep_for(milliseconds(100));
	}
	done_checking:

	for (;;)
	{
		ses.wait_for_alert(seconds(1));
		ses.pop_alerts(&alerts);
		for (auto a : alerts)
		{
			if (auto const* srd = lt::alert_cast<lt::save_resume_data_alert>(a))
			{
				std::printf("saving resume data \"%s\"\n", argv[3]);
				std::ofstream of(argv[3], std::ios_base::binary);
				of.unsetf(std::ios_base::skipws);
				auto const b = lt::write_resume_data_buf(srd->params);
				of.write(b.data(), int(b.size()));
				goto done_saving;
			}
			if (auto const* rdf = lt::alert_cast<lt::save_resume_data_failed_alert>(a))
			{
				std::printf("resume data failed: %s\n", rdf->error.message().c_str());
				return 1;
			}
		}
	}
	done_saving:

	return 0;
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
	return 1;
}
