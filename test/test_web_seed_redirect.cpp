/*

Copyright (c) 2008-2014, Arvid Norberg
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

#include "test.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"
#include "settings.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"

using namespace lt;

TORRENT_TEST(web_seed_redirect)
{
	using namespace lt;

	error_code ec;

	file_storage fs;
	int piece_size = 0x4000;

	std::array<char, 16000> random_data;
	aux::random_bytes(random_data);
	file f("test_file", open_mode::write_only, ec);
	if (ec)
	{
		std::printf("failed to create file \"test_file\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
		TEST_ERROR("failed to create file");
		return;
	}
	iovec_t b = random_data;
	f.writev(0, b, ec);
	fs.add_file("test_file", 16000);

	int port = start_web_server();

	// generate a torrent with pad files to make sure they
	// are not requested web seeds
	lt::create_torrent t(fs, piece_size, 0x4000);

	char tmp[512];
	std::snprintf(tmp, sizeof(tmp), "http://127.0.0.1:%d/redirect", port);
	t.add_url_seed(tmp);

	// calculate the hash for all pieces
	set_piece_hashes(t, ".", ec);

	if (ec)
	{
		std::printf("error creating hashes for test torrent: %s\n"
			, ec.message().c_str());
		TEST_ERROR("failed to create hashes");
		return;
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto torrent_file = std::make_shared<torrent_info>(buf, ec, from_span);

	{
		settings_pack p = settings();
		p.set_int(settings_pack::max_queued_disk_bytes, 256 * 1024);
		lt::session ses(p);

		// disable keep-alive because otherwise the test will choke on seeing
		// the disconnect (from the redirect)
		test_transfer(ses, torrent_file, 0, "http", true, false, false, false);
	}

	stop_web_server();
}
