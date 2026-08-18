/*

Copyright (c) 2006-2009, 2011, 2013-2021, Arvid Norberg
Copyright (c) 2015, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, d-komarov
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef SETUP_TRANSFER_HPP
#define SETUP_TRANSFER_HPP

#include <memory>
#include <tuple>
#include "test.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/flags.hpp"

EXPORT lt::add_torrent_params generate_torrent(bool with_files = false, bool with_hashes = false);

EXPORT int load_file(std::string const& filename, std::vector<char>& v
	, lt::error_code& ec, int limit = 8000000);

EXPORT lt::address rand_v4();
EXPORT lt::address rand_v6();
EXPORT lt::tcp::endpoint rand_tcp_ep(lt::address(&rand_addr)() = rand_v4);
EXPORT lt::udp::endpoint rand_udp_ep(lt::address(&rand_addr)() = rand_v4);

// determines if the operating system supports IPv6
EXPORT bool supports_ipv6();

EXPORT lt::sha1_hash rand_hash();
EXPORT lt::sha1_hash to_hash(char const* s);

EXPORT std::map<std::string, std::int64_t> get_counters(lt::session& s);

enum class pop_alerts { pop_all, cache_alerts };

EXPORT lt::alert const* wait_for_alert(
	lt::session& ses, int type, char const* name = ""
	, pop_alerts const p = pop_alerts::pop_all
	, lt::time_duration timeout = lt::seconds(10));

EXPORT void print_ses_rate(lt::clock_type::time_point start_time
	, lt::torrent_status const* st1
	, lt::torrent_status const* st2
	, lt::torrent_status const* st3 = nullptr);

EXPORT bool print_alerts(lt::session& ses, char const* name
	, bool allow_no_torrents = false
	, bool allow_failed_fastresume = false
	, std::function<bool(lt::alert const*)> predicate
		= std::function<bool(lt::alert const*)>()
	, bool no_output = false);

// waits until 'predicate' returns true for one of the session's alerts, or
// until 'timeout' elapses. returns whether the predicate was satisfied. all
// alerts are printed (via print_alerts()) while waiting.
EXPORT bool wait_for_alert(lt::session& ses,
	char const* name,
	std::function<bool(lt::alert const*)> predicate,
	lt::time_duration timeout);

EXPORT void wait_for_listen(lt::session& ses, char const* name);
EXPORT void wait_for_downloading(lt::session& ses, char const* name);
EXPORT void wait_for_seeding(lt::session& ses, char const* name);
EXPORT void wait_for_disconnect(lt::session& ses, char const* name);

EXPORT std::vector<char> generate_piece(lt::piece_index_t idx, int piece_size = 0x4000);
EXPORT lt::file_storage make_file_storage(lt::span<const int> file_sizes
	, int const piece_size, std::string base_name = "test_dir-");
EXPORT lt::add_torrent_params make_torrent(std::vector<lt::create_file_entry> files, int piece_size, lt::create_flags_t flags = {});
EXPORT std::vector<lt::create_file_entry> create_random_files(std::string const& path, lt::span<const int> file_sizes);

EXPORT lt::add_torrent_params create_torrent(std::ostream* file = nullptr
	, char const* name = "temporary", int piece_size = 16 * 1024, int num_pieces = 13
	, bool add_tracker = true, lt::create_flags_t flags = {}, std::string ssl_certificate = ""
	, bool bad_v1_hashes = false);

using setup_flags_t = lt::flags::bitfield_flag<std::uint32_t, struct setup_flags_tag>;

namespace setup_flags {
using lt::operator""_bit;

// give the downloading session (ses2) only the info hash, requiring it to
// fetch metadata over ut_metadata rather than being handed the torrent_info up front
constexpr setup_flags_t use_metadata_transfer = 0_bit;
// have ses1 (and ses3, if given) connect_peer() the downloader once it starts downloading
constexpr setup_flags_t connect = 1_bit;
constexpr setup_flags_t super_seeding = 2_bit;
// leave local service discovery enabled on all sessions (by default it's disabled)
constexpr setup_flags_t start_lsd = 3_bit;
}

EXPORT std::tuple<lt::torrent_handle, lt::torrent_handle, lt::torrent_handle> setup_transfer(
	lt::session* ses1,
	lt::session* ses2,
	lt::session* ses3,
	setup_flags_t flags = setup_flags::use_metadata_transfer | setup_flags::connect,
	std::string suffix = "",
	int piece_size = 16 * 1024,
	lt::add_torrent_params const* atp = nullptr,
	std::shared_ptr<lt::torrent_info>* torrent2 = nullptr,
	lt::create_flags_t create_flags = {},
	lt::torrent_flags_t seeder_extra_flags = {});

EXPORT int start_web_server(bool ssl = false,
	bool chunked = false,
	bool keepalive = true,
	int min_interval = 30,
	bool expect_host_header = false);

EXPORT void stop_web_server();

// path where web_server.py records the stopped announces it receives. Derived
// from the server's port so it is unique across concurrently running servers.
EXPORT std::string web_server_stopped_announces_path(int port);

EXPORT int start_websocket_server(bool ssl = false, int min_interval = 30);
EXPORT void stop_websocket_server();

EXPORT int start_proxy(int type);
EXPORT void stop_proxy(int port);
EXPORT void stop_all_proxies();

EXPORT lt::tcp::endpoint ep(char const* ip, int port);
EXPORT lt::udp::endpoint uep(char const* ip, int port);
EXPORT lt::address addr(char const* ip);
EXPORT lt::address_v4 addr4(char const* ip);
EXPORT lt::address_v6 addr6(char const* ip);

#endif
