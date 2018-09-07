/*

Copyright (c) 2016, Arvid Norberg
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

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix
#include "libtorrent/announce_entry.hpp"

#include <cstring>


// make sure creating a torrent from an existing handle preserves the
// info-dictionary verbatim, so as to not alter the info-hash
TORRENT_TEST(create_verbatim_torrent)
{
	char const test_torrent[] = "d4:infod4:name6:foobar6:lengthi12345e"
		"12:piece lengthi65536e6:pieces20:ababababababababababee";

	lt::torrent_info info(test_torrent, lt::from_span);

	info.add_tracker("http://test.com");
	info.add_tracker("http://test.com");
	TEST_EQUAL(info.trackers().size(), 1);

	lt::create_torrent t(info);

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());

	// now, make sure the info dictionary was unchanged
	buffer.push_back('\0');
	char const* dest_info = std::strstr(&buffer[0], "4:info");

	TEST_CHECK(dest_info != nullptr);

	// +1 and -2 here is to strip the outermost dictionary from the source
	// torrent, since create_torrent may have added items next to the info dict
	TEST_CHECK(memcmp(dest_info, test_torrent + 1, sizeof(test_torrent)-3) == 0);
}

TORRENT_TEST(piece_size)
{
	std::int64_t const kiB = 1024;
	std::int64_t const MiB = 1024 * 1024;
	std::int64_t const GiB = 1024 * 1024 * 1024;
	std::array<std::pair<std::int64_t, std::int64_t>, 11> samples{{
		{100LL,     16 * kiB},
		{3 * MiB,   32 * kiB},
		{11 * MiB,  64 * kiB},
		{43 * MiB,  128 * kiB},
		{172 * MiB, 256 * kiB},
		{688 * MiB, 512 * kiB},
		{3 * GiB,   1 * MiB},
		{11 * GiB,  2 * MiB},
		{44 * GiB,  4 * MiB},
		{176 * GiB, 8 * MiB},
		{704 * GiB, 16 * MiB},
	}};

	for (auto const& t : samples)
	{
		lt::file_storage fs;
		fs.add_file("a", t.first);
		lt::create_torrent ct(fs, 0);
		TEST_CHECK(ct.piece_length() == static_cast<int>(t.second));
	}
}

TORRENT_TEST(create_torrent_round_trip)
{
	char const test_torrent[] = "d8:announce26:udp://testurl.com/announce7:comment22:this is a test comment13:creation datei1337e4:infod6:lengthi12345e4:name6:foobar12:piece lengthi65536e6:pieces20:ababababababababababee";
	lt::torrent_info info1(test_torrent, lt::from_span);
	TEST_EQUAL(info1.comment(), "this is a test comment");
	TEST_EQUAL(info1.trackers().size(), 1);
	TEST_EQUAL(info1.trackers().front().url, "udp://testurl.com/announce");

	lt::create_torrent t(info1);

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	lt::torrent_info info2(buffer, lt::from_span);

	TEST_EQUAL(info2.comment(), "this is a test comment");
	TEST_EQUAL(info2.trackers().size(), 1);
	TEST_EQUAL(info2.trackers().front().url, "udp://testurl.com/announce");
	TEST_CHECK(info1.info_hash() == info2.info_hash());
}

