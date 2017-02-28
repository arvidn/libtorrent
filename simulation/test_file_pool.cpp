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

#include "setup_swarm.hpp"
#include "test.hpp"
#include "utils.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/file.hpp"

using namespace libtorrent;

TORRENT_TEST(close_file_interval)
{
	bool ran_to_completion = false;

	// with seed mode
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::close_file_interval, 20);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool
		{
			// terminate after 40 seconds
			if (ticks > 25)
			{
				ran_to_completion = true;
				return true;
			}

			torrent_handle h = ses.get_torrents().front();
			std::vector<pool_file_status> file_status;
			h.file_status(file_status);
			printf("%d: %d files\n", ticks, int(file_status.size()));
			if (ticks > 0 && ticks < 19)
			{
				TEST_EQUAL(file_status.size(), 1);
			}
			else if (ticks > 21)
			{
				// the close file timer shuold have kicked in at 20 seconds
				// and closed the file
				TEST_EQUAL(file_status.size(), 0);
			}
			return false;
		});
	TEST_CHECK(ran_to_completion);
}

