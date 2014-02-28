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

#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/thread.hpp"
#include <boost/tuple/tuple.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/extensions/metadata_transfer.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/extensions/lt_trackers.hpp"
#include "libtorrent/bencode.hpp"

using boost::tuples::ignore;

int test_main()
{
	using namespace libtorrent;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48130, 49000), "0.0.0.0", 0);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49130, 50000), "0.0.0.0", 0);
	ses1.add_extension(create_lt_trackers_plugin);
	ses2.add_extension(create_lt_trackers_plugin);

	add_torrent_params atp;
	atp.info_hash = sha1_hash("12345678901234567890");
	atp.save_path = "./";
	error_code ec;
	torrent_handle tor1 = ses1.add_torrent(atp, ec);
	atp.trackers.push_back("http://test.non-existent.com/announce");
	torrent_handle tor2 = ses2.add_torrent(atp, ec);
	tor2.connect_peer(tcp::endpoint(address_v4::from_string("127.0.0.1"), ses1.listen_port()));

	// trackers are NOT supposed to be exchanged for torrents that we
	// don't have metadata for, since they might be private
	for (int i = 0; i < 10; ++i)
	{
		// make sure this function can be called on
		// torrents without metadata
		print_alerts(ses1, "ses1", false, true);
		print_alerts(ses2, "ses2", false, true);

		if (tor1.trackers().size() != 0) break;
		test_sleep(1000);
	}

	TEST_CHECK(tor1.trackers().size() == 0);

	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name"] = "slightly shorter name, it's kind of sad that people started the trend of incorrectly encoding the regular name field and then adding another one with correct encoding";
	info["name.utf-8"] = "this is a long ass name in order to try to make make_magnet_uri overflow and hopefully crash. Although, by the time you read this that particular bug should have been fixed";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry torrent;
	torrent["info"] = info;

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	boost::intrusive_ptr<torrent_info> ti(new torrent_info(&buf[0], buf.size(), ec));
	TEST_CHECK(!ec);

	atp.ti = ti;
	atp.info_hash.clear();
	atp.save_path = "./";
	atp.trackers.clear();

	tor1 = ses1.add_torrent(atp, ec);
	atp.trackers.push_back("http://test.non-existent.com/announce");
	tor2 = ses2.add_torrent(atp, ec);
	tor2.connect_peer(tcp::endpoint(address_v4::from_string("127.0.0.1"), ses1.listen_port()));

	TEST_CHECK(tor1.trackers().size() == 0);

	for (int i = 0; i < 60; ++i)
	{
		// make sure this function can be called on
		// torrents without metadata
		print_alerts(ses1, "ses1", false, true);
		print_alerts(ses2, "ses2", false, true);

		if (tor1.trackers().size() == 1) break;
		test_sleep(1000);
	}

	TEST_EQUAL(tor1.trackers().size(), 1);

	return 0;
}

