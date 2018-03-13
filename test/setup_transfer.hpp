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
#include "test.hpp"

namespace libtorrent
{
	class alert;
	struct add_torrent_params;
	class file_storage;
}

EXPORT int print_failures();
EXPORT unsigned char random_byte();

EXPORT int load_file(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit = 8000000);
EXPORT void save_file(char const* filename, char const* data, int size);

EXPORT void report_failure(char const* err, char const* file, int line);

EXPORT void init_rand_address();
EXPORT libtorrent::address rand_v4();
#if TORRENT_USE_IPV6
EXPORT libtorrent::address rand_v6();
#endif
EXPORT libtorrent::tcp::endpoint rand_tcp_ep();
EXPORT libtorrent::udp::endpoint rand_udp_ep();

EXPORT libtorrent::sha1_hash rand_hash();

EXPORT std::map<std::string, boost::int64_t> get_counters(libtorrent::session& s);

EXPORT libtorrent::alert const* wait_for_alert(
	libtorrent::session& ses, int type, char const* name = ""
	, int num = 1
	, lt::time_duration timeout = lt::seconds(10));

EXPORT void print_ses_rate(float time
	, libtorrent::torrent_status const* st1
	, libtorrent::torrent_status const* st2
	, libtorrent::torrent_status const* st3 = NULL);

EXPORT bool print_alerts(libtorrent::session& ses, char const* name
	, bool allow_disconnects = false
	, bool allow_no_torrents = false
	, bool allow_failed_fastresume = false
	, boost::function<bool(libtorrent::alert const*)> const& predicate
		= boost::function<bool(libtorrent::alert const*)>()
	, bool no_output = false);

EXPORT void wait_for_listen(libtorrent::session& ses, char const* name);
EXPORT void wait_for_downloading(libtorrent::session& ses, char const* name);
EXPORT void test_sleep(int millisec);

EXPORT void create_random_files(std::string const& path, const int file_sizes[]
	, int num_files, libtorrent::file_storage* fs = NULL);

EXPORT boost::shared_ptr<libtorrent::torrent_info> create_torrent(std::ostream* file = 0
	, char const* name = "temporary", int piece_size = 16 * 1024, int num_pieces = 13
	, bool add_tracker = true, std::string ssl_certificate = "");

EXPORT boost::tuple<libtorrent::torrent_handle
	, libtorrent::torrent_handle
	, libtorrent::torrent_handle>
setup_transfer(libtorrent::session* ses1, libtorrent::session* ses2
	, libtorrent::session* ses3, bool clear_files, bool use_metadata_transfer = true
	, bool connect = true, std::string suffix = "", int piece_size = 16 * 1024
	, boost::shared_ptr<libtorrent::torrent_info>* torrent = 0, bool super_seeding = false
	, libtorrent::add_torrent_params const* p = 0, bool stop_lsd = true, bool use_ssl_ports = false
	, boost::shared_ptr<libtorrent::torrent_info>* torrent2 = 0);

EXPORT int start_web_server(bool ssl = false, bool chunked = false
	, bool keepalive = true);

EXPORT void stop_web_server();
EXPORT int start_proxy(int type);
EXPORT void stop_proxy(int port);
EXPORT void stop_all_proxies();

EXPORT libtorrent::tcp::endpoint ep(char const* ip, int port);

#endif

