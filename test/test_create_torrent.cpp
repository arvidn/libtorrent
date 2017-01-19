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
#include "libtorrent/entry.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/escape_string.hpp" // for convert_path_to_posix
#include <boost/make_shared.hpp>
#include <cstring>

namespace lt = libtorrent;

// make sure creating a torrent from an existing handle preserves the
// info-dictionary verbatim, so as to not alter the info-hash
int test_main()
{
	char const test_torrent[] = "d4:infod4:name6:foobar6:lengthi12345e12:piece lengthi65536e6:pieces20:ababababababababababee";

	lt::torrent_info info(test_torrent, sizeof(test_torrent) - 1);

	lt::create_torrent t(info);

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());

	// now, make sure the info dictionary was unchanged
	buffer.push_back('\0');
	char const* dest_info = std::strstr(&buffer[0], "4:info");

	TEST_CHECK(dest_info != NULL);

	// +1 and -2 here is to strip the outermost dictionary from the source
	// torrent, since create_torrent may have added items next to the info dict
	TEST_CHECK(memcmp(dest_info, test_torrent + 1, sizeof(test_torrent)-3) == 0);

	return 0;
}

