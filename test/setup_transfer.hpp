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

#ifndef SETUP_TRANSFER_HPP
#define SETUP_TRANSFER_HPP

#include "libtorrent/session.hpp"
#include <boost/tuple/tuple.hpp>


void print_alerts(libtorrent::session& ses, char const* name
	, bool allow_disconnects = false
	, bool allow_no_torrents = false
	, bool allow_failed_fastresume = false);
void test_sleep(int millisec);

boost::intrusive_ptr<libtorrent::torrent_info> create_torrent(std::ostream* file = 0, int piece_size = 16 * 1024, int num_pieces = 1024 / 8);

boost::tuple<libtorrent::torrent_handle, libtorrent::torrent_handle
	, libtorrent::torrent_handle>
setup_transfer(libtorrent::session* ses1, libtorrent::session* ses2
	, libtorrent::session* ses3, bool clear_files, bool use_metadata_transfer = true
	, bool connect = true, std::string suffix = "", int piece_size = 16 * 1024
	, boost::intrusive_ptr<libtorrent::torrent_info>* torrent = 0);

void start_web_server(int port, bool ssl = false);
void stop_web_server(int port);
void start_proxy(int port, int type);
void stop_proxy(int port);

#endif

