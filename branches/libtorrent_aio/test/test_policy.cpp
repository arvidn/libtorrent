/*

Copyright (c) 2013, Arvid Norberg
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

#include "libtorrent/policy.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/torrent_handle.hpp"

#include "test.hpp"

using namespace libtorrent;

struct mock_torrent : torrent_interface
{
	bool has_picker() const { return false; }
	piece_picker& picker() { return *((piece_picker*)NULL); }
	int num_peers() const { return 0; }
	aux::session_settings const& settings() const { return m_sett; }
	aux::session_interface& session() { return *((aux::session_interface*)NULL); }
	bool apply_ip_filter() const { return true; }
	torrent_info const& torrent_file() const { return *((torrent_info*)NULL); }
	bool is_paused() const { return false; }
	bool is_finished() const { return false; }
	int max_connections() const { return 100000; }
	void update_want_peers() {}
	void state_updated() {}
	torrent_handle get_handle() { return torrent_handle(); }
#ifndef TORRENT_DISABLE_EXTENSIONS
	void notify_extension_add_peer(tcp::endpoint const& ip, int src, int flags) {}
#endif
	bool connect_to_peer(torrent_peer* peerinfo, bool ignore_limit = false) { return true; }

private:

	aux::session_settings m_sett;
};

int test_main()
{
	mock_torrent t;
	policy p(&t);

// TODO: add tests here

	return 0;
}

