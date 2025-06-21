/*

Copyright (c) 2007-2008, 2010, 2013-2015, 2017, 2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
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

// based on test_torrent.cpp
#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/path.hpp" // for combine_path, current_working_directory
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/random.hpp"
#include "settings.hpp"
#include <tuple>
#include <iostream>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

using namespace lt;

// test_exact_source
// based on test_torrent.cpp TORRENT_TEST(added_peers)
TORRENT_TEST(exact_source_1)
{
	/*
  file_storage fs;

	fs.add_file("test_torrent_dir4/tmp1", 1024);

	lt::create_torrent t(fs, 1024);
	t.set_hash(0_piece, sha1_hash::max());
	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), t.generate());
	auto info = std::make_shared<torrent_info>(tmp, from_span);
  */

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	pack.set_int(settings_pack::max_retry_port_bind, 10);
	lt::session ses(pack);

  std::stringstream magnet_uri;
  magnet_uri << "magnet:";
  // Rambo.Movie.Collection.German.microHD.x264-RAIST
  magnet_uri << "?xt=urn:btih:dfa2cf03468dcbc24b977de94e54b2772b8d1ceb";
  // magnet_uri << "&x.pe=127.0.0.1:6885";
  magnet_uri << "&cas=http://127.0.0.1/cas/";
  // magnet:?xt=urn:btih:dfa2cf03468dcbc24b977de94e54b2772b8d1ceb&cas=http://127.0.0.1/cas/

  add_torrent_params p = parse_magnet_uri(magnet_uri.str());
	// p.ti = info;
  /*
	p.info_hashes = info_hash_t{};
#if TORRENT_ABI_VERSION < 3
	p.info_hash = sha1_hash{};
#endif
  */
	p.save_path = ".";

	torrent_handle h = ses.add_torrent(std::move(p));

  torrent_status st = h.status();

  st = h.status();

  // void pop_alerts (std::vector<alert*>* alerts);

  wait_for_alert(ses, torrent_finished_alert::alert_type, "ses");

  /*
  TEST_EQUAL(h.status().save_path, complete("save_path"));

  h.read_piece(piece_index_t{-1});
	alert const* a = wait_for_alert(ses, read_piece_alert::alert_type, "read_piece_alert");
	TEST_CHECK(a);
	if (auto* rp = alert_cast<read_piece_alert>(a))
	{
		TEST_CHECK(rp->error == error_code(lt::errors::no_metadata, lt::libtorrent_category()));
	}

  wait_for_alert(ses, torrent_checked_alert::alert_type, "torrent_checked_alert");
	std::string const f = combine_path(combine_path(work_dir, "Some.framework"), "SDL2");
	TEST_CHECK(aux::get_file_attributes(f) & file_storage::flag_symlink);
	TEST_EQUAL(aux::get_symlink_path(f), "Versions/A/SDL2");

  wait_for_downloading(ses, "");
	h.add_piece(0_piece, piece_data);
	h.set_piece_deadline(0_piece, 0, torrent_handle::alert_when_available);
	h.prioritize_pieces(std::vector<lt::download_priority_t>(std::size_t(num_pieces), lt::dont_download));
	h.add_piece(0_piece, std::move(piece_data));
	std::this_thread::sleep_for(lt::seconds(2));
  */

	/*
  h.save_resume_data();
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra) TEST_EQUAL(ra->params.peers.size(), 2);
  */
}
