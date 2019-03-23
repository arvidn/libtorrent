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

#include <memory>
#include <tuple>
#include "test.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/fwd.hpp"

EXPORT std::shared_ptr<lt::torrent_info> generate_torrent(bool with_files = false);

EXPORT int print_failures();

EXPORT int load_file(std::string const& filename, std::vector<char>& v
	, lt::error_code& ec, int limit = 8000000);

EXPORT void report_failure(char const* err, char const* file, int line);

EXPORT void init_rand_address();
EXPORT lt::address rand_v4();
EXPORT lt::address rand_v6();
EXPORT lt::tcp::endpoint rand_tcp_ep(lt::address(&rand_addr)() = rand_v4);
EXPORT lt::udp::endpoint rand_udp_ep(lt::address(&rand_addr)() = rand_v4);

EXPORT lt::sha1_hash rand_hash();
EXPORT lt::sha1_hash to_hash(char const* s);

EXPORT std::map<std::string, std::int64_t> get_counters(lt::session& s);

enum class pop_alerts { pop_all, cache_alerts };

EXPORT lt::alert const* wait_for_alert(
	lt::session& ses, int type, char const* name = ""
	, pop_alerts const p = pop_alerts::pop_all
	, lt::time_duration timeout = lt::seconds(10));

EXPORT void print_ses_rate(float time
	, lt::torrent_status const* st1
	, lt::torrent_status const* st2
	, lt::torrent_status const* st3 = nullptr);

EXPORT bool print_alerts(lt::session& ses, char const* name
	, bool allow_no_torrents = false
	, bool allow_failed_fastresume = false
	, std::function<bool(lt::alert const*)> predicate
		= std::function<bool(lt::alert const*)>()
	, bool no_output = false);

EXPORT void wait_for_listen(lt::session& ses, char const* name);
EXPORT void wait_for_downloading(lt::session& ses, char const* name);

EXPORT std::vector<char> generate_piece(lt::piece_index_t idx, int piece_size = 0x4000);
EXPORT lt::file_storage make_file_storage(lt::span<const int> file_sizes
	, int const piece_size, std::string base_name = "test_dir-");
EXPORT std::shared_ptr<lt::torrent_info> make_torrent(lt::span<const int> file_sizes
	, int piece_size);
EXPORT void create_random_files(std::string const& path, lt::span<const int> file_sizes
	, libtorrent::file_storage* fs = nullptr);

EXPORT std::shared_ptr<lt::torrent_info> create_torrent(std::ostream* file = nullptr
	, char const* name = "temporary", int piece_size = 16 * 1024, int num_pieces = 13
	, bool add_tracker = true, std::string ssl_certificate = "");

EXPORT std::tuple<lt::torrent_handle
	, lt::torrent_handle
	, lt::torrent_handle>
setup_transfer(lt::session* ses1, lt::session* ses2
	, lt::session* ses3, bool clear_files, bool use_metadata_transfer = true
	, bool connect = true, std::string suffix = "", int piece_size = 16 * 1024
	, std::shared_ptr<lt::torrent_info>* torrent = nullptr
	, bool super_seeding = false
	, lt::add_torrent_params const* p = nullptr
	, bool stop_lsd = true, bool use_ssl_ports = false
	, std::shared_ptr<lt::torrent_info>* torrent2 = nullptr);

EXPORT int start_web_server(bool ssl = false, bool chunked = false
	, bool keepalive = true, int min_interval = 30);

EXPORT void stop_web_server();
EXPORT int start_proxy(int type);
EXPORT void stop_proxy(int port);
EXPORT void stop_all_proxies();

EXPORT lt::tcp::endpoint ep(char const* ip, int port);
EXPORT lt::udp::endpoint uep(char const* ip, int port);
EXPORT lt::address addr(char const* ip);
EXPORT lt::address_v4 addr4(char const* ip);
EXPORT lt::address_v6 addr6(char const* ip);

#endif
