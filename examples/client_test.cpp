/*

Copyright (c) 2003-2017, Arvid Norberg
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

#include <cstdio> // for snprintf
#include <cstdlib> // for atoi
#include <cstring>
#include <utility>
#include <deque>
#include <fstream>
#include <regex>
#include <algorithm> // for min()/max()

#include "libtorrent/config.hpp"

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _mkdir and _getcwd
#include <sys/types.h> // for _stat
#include <sys/stat.h>
#endif

#ifdef TORRENT_UTP_LOG_ENABLE
#include "libtorrent/utp_stream.hpp"
#endif

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/disk_interface.hpp" // for open_file_state

#include "torrent_view.hpp"
#include "session_view.hpp"
#include "print.hpp"

using lt::total_milliseconds;
using lt::alert;
using lt::piece_index_t;
using lt::file_index_t;
using lt::torrent_handle;
using lt::add_torrent_params;
using lt::cache_status;
using lt::total_seconds;
using lt::torrent_flags_t;
using lt::seconds;
using lt::operator "" _sv;
using lt::address_v4;
using lt::address_v6;

using std::chrono::duration_cast;
using std::stoi;

#ifdef _WIN32

#include <windows.h>
#include <conio.h>

bool sleep_and_input(int* c, lt::time_duration const sleep)
{
	for (int i = 0; i < 2; ++i)
	{
		if (_kbhit())
		{
			*c = _getch();
			return true;
		}
		std::this_thread::sleep_for(sleep / 2);
	}
	return false;
}

#else

#include <termios.h>
#include <sys/ioctl.h>
#include <csignal>
#include <utility>
#include <dirent.h>

struct set_keypress
{
	enum terminal_mode {
		echo = 1,
		canonical = 2
	};

	explicit set_keypress(std::uint8_t const mode = 0)
	{
		termios new_settings;
		tcgetattr(0, &stored_settings);
		new_settings = stored_settings;
		// Disable canonical mode, and set buffer size to 1 byte
		// and disable echo
		if (mode & echo) new_settings.c_lflag |= ECHO;
		else new_settings.c_lflag &= ~ECHO;

		if (mode & canonical) new_settings.c_lflag |= ICANON;
		else new_settings.c_lflag &= ~ICANON;

		new_settings.c_cc[VTIME] = 0;
		new_settings.c_cc[VMIN] = 1;
		tcsetattr(0,TCSANOW,&new_settings);
	}
	~set_keypress() { tcsetattr(0, TCSANOW, &stored_settings); }
private:
	termios stored_settings;
};

bool sleep_and_input(int* c, lt::time_duration const sleep)
{
	lt::time_point const done = lt::clock_type::now() + sleep;
	int ret = 0;
retry:
	fd_set set;
	FD_ZERO(&set);
	FD_SET(0, &set);
	int const delay = total_milliseconds(done - lt::clock_type::now());
	timeval tv = {delay / 1000, (delay % 1000) * 1000 };
	ret = select(1, &set, nullptr, nullptr, &tv);
	if (ret > 0)
	{
		*c = getc(stdin);
		return true;
	}
	if (errno == EINTR)
	{
		if (lt::clock_type::now() < done)
			goto retry;
		return false;
	}

	if (ret < 0 && errno != 0 && errno != ETIMEDOUT)
	{
		std::fprintf(stderr, "select failed: %s\n", strerror(errno));
		std::this_thread::sleep_for(lt::milliseconds(500));
	}

	return false;
}

#endif

bool print_trackers = false;
bool print_peers = false;
bool print_connecting_peers = false;
bool print_log = false;
bool print_downloads = false;
bool print_matrix = false;
bool print_file_progress = false;
bool show_pad_files = false;
bool show_dht_status = false;
bool sequential_download = false;

bool print_ip = true;
bool print_local_ip = false;
bool print_timers = false;
bool print_block = false;
bool print_fails = false;
bool print_send_bufs = true;
bool print_disk_stats = false;

// the number of times we've asked to save resume data
// without having received a response (successful or failure)
int num_outstanding_resume_data = 0;

#ifndef TORRENT_DISABLE_DHT
std::vector<lt::dht_lookup> dht_active_requests;
std::vector<lt::dht_routing_bucket> dht_routing_table;
#endif

std::string to_hex(lt::sha1_hash const& s)
{
	std::stringstream ret;
	ret << s;
	return ret.str();
}

bool load_file(std::string const& filename, std::vector<char>& v
	, int limit = 8000000)
{
	std::fstream f(filename, std::ios_base::in | std::ios_base::binary);
	f.seekg(0, std::ios_base::end);
	auto const s = f.tellg();
	if (s > limit || s < 0) return false;
	f.seekg(0, std::ios_base::beg);
	v.resize(static_cast<std::size_t>(s));
	if (s == std::fstream::pos_type(0)) return !f.fail();
	f.read(v.data(), v.size());
	return !f.fail();
}

bool is_absolute_path(std::string const& f)
{
	if (f.empty()) return false;
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	int i = 0;
	// match the xx:\ or xx:/ form
	while (f[i] && strchr("abcdefghijklmnopqrstuvxyz", f[i])) ++i;
	if (i < int(f.size()-1) && f[i] == ':' && (f[i+1] == '\\' || f[i+1] == '/'))
		return true;

	// match the \\ form
	if (int(f.size()) >= 2 && f[0] == '\\' && f[1] == '\\')
		return true;
	return false;
#else
	if (f[0] == '/') return true;
	return false;
#endif
}

std::string path_append(std::string const& lhs, std::string const& rhs)
{
	if (lhs.empty() || lhs == ".") return rhs;
	if (rhs.empty() || rhs == ".") return lhs;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR "\\"
	bool need_sep = lhs[lhs.size()-1] != '\\' && lhs[lhs.size()-1] != '/';
#else
#define TORRENT_SEPARATOR "/"
	bool need_sep = lhs[lhs.size()-1] != '/';
#endif
	return lhs + (need_sep?TORRENT_SEPARATOR:"") + rhs;
}

std::string make_absolute_path(std::string const& p)
{
	if (is_absolute_path(p)) return p;
	std::string ret;
#if defined TORRENT_WINDOWS
	char* cwd = ::_getcwd(nullptr, 0);
	ret = path_append(cwd, p);
	std::free(cwd);
#else
	char* cwd = ::getcwd(nullptr, 0);
	ret = path_append(cwd, p);
	std::free(cwd);
#endif
	return ret;
}

std::string print_endpoint(lt::tcp::endpoint const& ep)
{
	using namespace lt;
	lt::error_code ec;
	char buf[200];
	address const& addr = ep.address();
	if (addr.is_v6())
		std::snprintf(buf, sizeof(buf), "[%s]:%d", addr.to_string(ec).c_str(), ep.port());
	else
		std::snprintf(buf, sizeof(buf), "%s:%d", addr.to_string(ec).c_str(), ep.port());
	return buf;
}

using lt::torrent_status;

FILE* g_log_file = nullptr;

int peer_index(lt::tcp::endpoint addr, std::vector<lt::peer_info> const& peers)
{
	using namespace lt;
	auto i = std::find_if(peers.begin(), peers.end()
		, [&addr](peer_info const& pi) { return pi.ip == addr; });
	if (i == peers.end()) return -1;

	return int(i - peers.begin());
}

// returns the number of lines printed
int print_peer_info(std::string& out
	, std::vector<lt::peer_info> const& peers, int max_lines)
{
	using namespace lt;
	int pos = 0;
	if (print_ip) out += "IP                             ";
	if (print_local_ip) out += "local IP                       ";
	out += "progress        down     (total | peak   )  up      (total | peak   ) sent-req tmo bsy rcv flags         dn  up  source  ";
	if (print_fails) out += "fail hshf ";
	if (print_send_bufs) out += "rq sndb (recvb |alloc | wmrk ) q-bytes ";
	if (print_timers) out += "inactive wait timeout q-time ";
	out += "  v disk ^    rtt  ";
	if (print_block) out += "block-progress ";
	out += "client \x1b[K\n";
	++pos;

	char str[500];
	for (std::vector<peer_info>::const_iterator i = peers.begin();
		i != peers.end(); ++i)
	{
		if ((i->flags & (peer_info::handshake | peer_info::connecting)
			&& !print_connecting_peers))
		{
			continue;
		}

		if (print_ip)
		{
			std::snprintf(str, sizeof(str), "%-30s ", (::print_endpoint(i->ip) +
				(i->flags & peer_info::utp_socket ? " [uTP]" : "") +
				(i->flags & peer_info::i2p_socket ? " [i2p]" : "")
				).c_str());
			out += str;
		}
		if (print_local_ip)
		{
			std::snprintf(str, sizeof(str), "%-30s ", ::print_endpoint(i->local_endpoint).c_str());
			out += str;
		}

		char temp[10];
		std::snprintf(temp, sizeof(temp), "%d/%d"
			, i->download_queue_length
			, i->target_dl_queue_length);
		temp[7] = 0;

		char peer_progress[10];
		std::snprintf(peer_progress, sizeof(peer_progress), "%.1f%%", i->progress_ppm / 10000.f);
		std::snprintf(str, sizeof(str)
			, "%s %s%s (%s|%s) %s%s (%s|%s) %s%7s %4d%4d%4d %s%s%s%s%s%s%s%s%s%s%s%s%s %s%s%s %s%s%s %s%s%s%s%s%s "
			, progress_bar(i->progress_ppm / 1000, 15, col_green, '#', '-', peer_progress).c_str()
			, esc("32"), add_suffix(i->down_speed, "/s").c_str()
			, add_suffix(i->total_download).c_str(), add_suffix(i->download_rate_peak, "/s").c_str()
			, esc("31"), add_suffix(i->up_speed, "/s").c_str(), add_suffix(i->total_upload).c_str()
			, add_suffix(i->upload_rate_peak, "/s").c_str(), esc("0")

			, temp // sent requests and target number of outstanding reqs.
			, i->timed_out_requests
			, i->busy_requests
			, i->upload_queue_length

			, color("I", (i->flags & peer_info::interesting)?col_white:col_blue).c_str()
			, color("C", (i->flags & peer_info::choked)?col_white:col_blue).c_str()
			, color("i", (i->flags & peer_info::remote_interested)?col_white:col_blue).c_str()
			, color("c", (i->flags & peer_info::remote_choked)?col_white:col_blue).c_str()
			, color("x", (i->flags & peer_info::supports_extensions)?col_white:col_blue).c_str()
			, color("o", (i->flags & peer_info::local_connection)?col_white:col_blue).c_str()
			, color("p", (i->flags & peer_info::on_parole)?col_white:col_blue).c_str()
			, color("O", (i->flags & peer_info::optimistic_unchoke)?col_white:col_blue).c_str()
			, color("S", (i->flags & peer_info::snubbed)?col_white:col_blue).c_str()
			, color("U", (i->flags & peer_info::upload_only)?col_white:col_blue).c_str()
			, color("e", (i->flags & peer_info::endgame_mode)?col_white:col_blue).c_str()
			, color("E", (i->flags & peer_info::rc4_encrypted)?col_white:(i->flags & peer_info::plaintext_encrypted)?col_cyan:col_blue).c_str()
			, color("h", (i->flags & peer_info::holepunched)?col_white:col_blue).c_str()

			, color("d", (i->read_state & peer_info::bw_disk)?col_white:col_blue).c_str()
			, color("l", (i->read_state & peer_info::bw_limit)?col_white:col_blue).c_str()
			, color("n", (i->read_state & peer_info::bw_network)?col_white:col_blue).c_str()
			, color("d", (i->write_state & peer_info::bw_disk)?col_white:col_blue).c_str()
			, color("l", (i->write_state & peer_info::bw_limit)?col_white:col_blue).c_str()
			, color("n", (i->write_state & peer_info::bw_network)?col_white:col_blue).c_str()

			, color("t", (i->source & peer_info::tracker)?col_white:col_blue).c_str()
			, color("p", (i->source & peer_info::pex)?col_white:col_blue).c_str()
			, color("d", (i->source & peer_info::dht)?col_white:col_blue).c_str()
			, color("l", (i->source & peer_info::lsd)?col_white:col_blue).c_str()
			, color("r", (i->source & peer_info::resume_data)?col_white:col_blue).c_str()
			, color("i", (i->source & peer_info::incoming)?col_white:col_blue).c_str());
		out += str;

		if (print_fails)
		{
			std::snprintf(str, sizeof(str), "%4d %4d "
				, i->failcount, i->num_hashfails);
			out += str;
		}
		if (print_send_bufs)
		{
			std::snprintf(str, sizeof(str), "%2d %6d %6d|%6d|%6d%5dkB "
				, i->requests_in_buffer, i->used_send_buffer
				, i->used_receive_buffer
				, i->receive_buffer_size
				, i->receive_buffer_watermark
				, i->queue_bytes / 1000);
			out += str;
		}
		if (print_timers)
		{
			char req_timeout[20] = "-";
			// timeout is only meaningful if there is at least one outstanding
			// request to the peer
			if (i->download_queue_length > 0)
				std::snprintf(req_timeout, sizeof(req_timeout), "%d", i->request_timeout);

			std::snprintf(str, sizeof(str), "%8d %4d %7s %6d "
				, int(total_seconds(i->last_active))
				, int(total_seconds(i->last_request))
				, req_timeout
				, int(total_seconds(i->download_queue_time)));
			out += str;
		}
		std::snprintf(str, sizeof(str), "%s|%s %5d "
			, add_suffix(i->pending_disk_bytes).c_str()
			, add_suffix(i->pending_disk_read_bytes).c_str()
			, i->rtt);
		out += str;

		if (print_block)
		{
			if (i->downloading_piece_index >= piece_index_t(0))
			{
				char buf[50];
				std::snprintf(buf, sizeof(buf), "%d:%d"
					, static_cast<int>(i->downloading_piece_index), i->downloading_block_index);
				out += progress_bar(
					i->downloading_progress * 1000 / i->downloading_total, 14, col_green, '-', '#', buf);
			}
			else
			{
				out += progress_bar(0, 14);
			}
		}

		out += " ";

		if (i->flags & lt::peer_info::handshake)
		{
			out += esc("31");
			out += " waiting for handshake";
			out += esc("0");
		}
		else if (i->flags & lt::peer_info::connecting)
		{
			out += esc("31");
			out += " connecting to peer";
			out += esc("0");
		}
		else
		{
			out += " ";
			out += i->client;
		}
		out += "\x1b[K\n";
		++pos;
		if (pos >= max_lines) break;
	}
	return pos;
}

lt::storage_mode_t allocation_mode = lt::storage_mode_sparse;
std::string save_path(".");
int torrent_upload_limit = 0;
int torrent_download_limit = 0;
std::string monitor_dir;
int poll_interval = 5;
int max_connections_per_torrent = 50;
bool seed_mode = false;
bool stats_enabled = false;
int cache_size = -1;

bool share_mode = false;
bool disable_storage = false;

bool quit = false;

void signal_handler(int)
{
	// make the main loop terminate
	quit = true;
}

// if non-empty, a peer that will be added to all torrents
std::string peer;

void print_settings(int const start, int const num
	, char const* const fmt)
{
	for (int i = start; i < start + num; ++i)
	{
		char const* name = lt::name_for_setting(i);
		if (!name || name[0] == '\0') continue;
		std::printf(fmt, name);
	}
}

void assign_setting(lt::settings_pack& settings, std::string const& key, char const* value)
{
	int const sett_name = lt::setting_by_name(key);
	if (sett_name < 0)
	{
		std::fprintf(stderr, "unknown setting: \"%s\"\n", key.c_str());
		std::exit(1);
	}

	using lt::settings_pack;

	switch (sett_name & settings_pack::type_mask)
	{
		case settings_pack::string_type_base:
			settings.set_str(sett_name, value);
			break;
		case settings_pack::bool_type_base:
			if (value == "1"_sv || value == "on"_sv || value == "true"_sv)
			{
				settings.set_bool(sett_name, true);
			}
			else if (value == "0"_sv || value == "off"_sv || value == "false"_sv)
			{
				settings.set_bool(sett_name, false);
			}
			else
			{
				std::fprintf(stderr, "invalid value for \"%s\". expected 0 or 1\n"
					, key.c_str());
				std::exit(1);
			}
			break;
		case settings_pack::int_type_base:
			using namespace libtorrent::literals;
			static std::map<lt::string_view, int> const enums = {
				{"no_piece_suggestions"_sv, settings_pack::no_piece_suggestions},
				{"suggest_read_cache"_sv, settings_pack::suggest_read_cache},
				{"fixed_slots_choker"_sv, settings_pack::fixed_slots_choker},
				{"rate_based_choker"_sv, settings_pack::rate_based_choker},
				{"round_robin"_sv, settings_pack::round_robin},
				{"fastest_upload"_sv, settings_pack::fastest_upload},
				{"anti_leech"_sv, settings_pack::anti_leech},
				{"enable_os_cache"_sv, settings_pack::enable_os_cache},
				{"disable_os_cache"_sv, settings_pack::disable_os_cache},
				{"prefer_tcp"_sv, settings_pack::prefer_tcp},
				{"peer_proportional"_sv, settings_pack::peer_proportional},
				{"pe_forced"_sv, settings_pack::pe_forced},
				{"pe_enabled"_sv, settings_pack::pe_enabled},
				{"pe_disabled"_sv, settings_pack::pe_disabled},
				{"pe_plaintext"_sv, settings_pack::pe_plaintext},
				{"pe_rc4"_sv, settings_pack::pe_rc4},
				{"pe_both"_sv, settings_pack::pe_both},
				{"none"_sv, settings_pack::none},
				{"socks4"_sv, settings_pack::socks4},
				{"socks5"_sv, settings_pack::socks5},
				{"socks5_pw"_sv, settings_pack::socks5_pw},
				{"http"_sv, settings_pack::http},
				{"http_pw"_sv, settings_pack::http_pw},
				{"i2p_proxy"_sv, settings_pack::i2p_proxy},
			};

			auto const it = enums.find(lt::string_view(value));
			if (it != enums.end())
			{
				settings.set_int(sett_name, it->second);
				break;
			}

			static std::map<lt::string_view, lt::alert_category_t> const alert_categories = {
				{"error"_sv, lt::alert_category::error},
				{"peer"_sv, lt::alert_category::peer},
				{"port_mapping"_sv, lt::alert_category::port_mapping},
				{"storage"_sv, lt::alert_category::storage},
				{"tracker"_sv, lt::alert_category::tracker},
				{"connect"_sv, lt::alert_category::connect},
				{"status"_sv, lt::alert_category::status},
				{"ip_block"_sv, lt::alert_category::ip_block},
				{"performance_warning"_sv, lt::alert_category::performance_warning},
				{"dht"_sv, lt::alert_category::dht},
				{"session_log"_sv, lt::alert_category::session_log},
				{"torrent_log"_sv, lt::alert_category::torrent_log},
				{"peer_log"_sv, lt::alert_category::peer_log},
				{"incoming_request"_sv, lt::alert_category::incoming_request},
				{"dht_log"_sv, lt::alert_category::dht_log},
				{"dht_operation"_sv, lt::alert_category::dht_operation},
				{"port_mapping_log"_sv, lt::alert_category::port_mapping_log},
				{"picker_log"_sv, lt::alert_category::picker_log},
				{"file_progress"_sv, lt::alert_category::file_progress},
				{"piece_progress"_sv, lt::alert_category::piece_progress},
				{"upload"_sv, lt::alert_category::upload},
				{"block_progress"_sv, lt::alert_category::block_progress},
				{"all"_sv, lt::alert_category::all},
			};

			std::stringstream flags(value);
			std::string f;
			lt::alert_category_t val;
			while (std::getline(flags, f, ',')) try
			{
				auto const it = alert_categories.find(f);
				if (it == alert_categories.end())
					val |= lt::alert_category_t{unsigned(std::stoi(f))};
				else
					val |= it->second;
			}
			catch (std::invalid_argument const&)
			{
				std::fprintf(stderr, "invalid value for \"%s\". expected integer or enum value\n"
					, key.c_str());
				std::exit(1);
			}

			settings.set_int(sett_name, val);
			break;
	}
}

std::string resume_file(lt::sha1_hash const& info_hash)
{
	return path_append(save_path, path_append(".resume"
		, to_hex(info_hash) + ".resume"));
}

void set_torrent_params(lt::add_torrent_params& p)
{
	p.max_connections = max_connections_per_torrent;
	p.max_uploads = -1;
	p.upload_limit = torrent_upload_limit;
	p.download_limit = torrent_download_limit;

	if (seed_mode) p.flags |= lt::torrent_flags::seed_mode;
	if (disable_storage) p.storage = lt::disabled_storage_constructor;
	if (share_mode) p.flags |= lt::torrent_flags::share_mode;
	p.save_path = save_path;
	p.storage_mode = allocation_mode;
}

void add_magnet(lt::session& ses, lt::string_view uri)
{
	lt::error_code ec;
	lt::add_torrent_params p = lt::parse_magnet_uri(uri.to_string(), ec);

	if (ec)
	{
		std::printf("invalid magnet link \"%s\": %s\n"
			, uri.to_string().c_str(), ec.message().c_str());
		return;
	}

	std::vector<char> resume_data;
	if (load_file(resume_file(p.info_hash), resume_data))
	{
		p = lt::read_resume_data(resume_data, ec);
		if (ec) std::printf("  failed to load resume data: %s\n", ec.message().c_str());
	}

	set_torrent_params(p);

	std::printf("adding magnet: %s\n", uri.to_string().c_str());
	ses.async_add_torrent(std::move(p));
}

// return false on failure
bool add_torrent(lt::session& ses, std::string torrent)
{
	using lt::add_torrent_params;
	using lt::storage_mode_t;

	static int counter = 0;

	std::printf("[%d] %s\n", counter++, torrent.c_str());

	lt::error_code ec;
	auto ti = std::make_shared<lt::torrent_info>(torrent, ec);
	if (ec)
	{
		std::printf("failed to load torrent \"%s\": %s\n"
			, torrent.c_str(), ec.message().c_str());
		return false;
	}

	add_torrent_params p;

	std::vector<char> resume_data;
	if (load_file(resume_file(ti->info_hash()), resume_data))
	{
		p = lt::read_resume_data(resume_data, ec);
		if (ec) std::printf("  failed to load resume data: %s\n", ec.message().c_str());
	}

	set_torrent_params(p);

	p.ti = ti;
	p.flags &= ~lt::torrent_flags::duplicate_is_error;
	ses.async_add_torrent(std::move(p));
	return true;
}

std::vector<std::string> list_dir(std::string path
	, bool (*filter_fun)(lt::string_view)
	, lt::error_code& ec)
{
	std::vector<std::string> ret;
#ifdef TORRENT_WINDOWS
	if (!path.empty() && path[path.size()-1] != '\\') path += "\\*";
	else path += "*";

	WIN32_FIND_DATAA fd;
	HANDLE handle = FindFirstFileA(path.c_str(), &fd);
	if (handle == INVALID_HANDLE_VALUE)
	{
		ec.assign(GetLastError(), boost::system::system_category());
		return ret;
	}

	do
	{
		lt::string_view p = fd.cFileName;
		if (filter_fun(p))
			ret.push_back(p.to_string());

	} while (FindNextFileA(handle, &fd));
	FindClose(handle);
#else

	if (!path.empty() && path[path.size()-1] == '/')
		path.resize(path.size()-1);

	DIR* handle = opendir(path.c_str());
	if (handle == nullptr)
	{
		ec.assign(errno, boost::system::system_category());
		return ret;
	}

	struct dirent* de;
	while ((de = readdir(handle)))
	{
		lt::string_view p(de->d_name);
		if (filter_fun(p))
			ret.push_back(p.to_string());
	}
	closedir(handle);
#endif
	return ret;
}

void scan_dir(std::string const& dir_path, lt::session& ses)
{
	using namespace lt;

	error_code ec;
	std::vector<std::string> ents = list_dir(dir_path
		, [](lt::string_view p) { return p.size() > 8 && p.substr(p.size() - 8) == ".torrent"; }, ec);
	if (ec)
	{
		std::fprintf(stderr, "failed to list directory: (%s : %d) %s\n"
			, ec.category().name(), ec.value(), ec.message().c_str());
		return;
	}

	for (auto const& e : ents)
	{
		std::string const file = path_append(dir_path, e);

		// there's a new file in the monitor directory, load it up
		if (add_torrent(ses, file))
		{
			if (::remove(file.c_str()) < 0)
			{
				std::fprintf(stderr, "failed to remove torrent file: \"%s\"\n"
					, file.c_str());
			}
		}
	}
}

char const* timestamp()
{
	time_t t = std::time(nullptr);
	tm* timeinfo = std::localtime(&t);
	static char str[200];
	std::strftime(str, 200, "%b %d %X", timeinfo);
	return str;
}

void print_alert(lt::alert const* a, std::string& str)
{
	using namespace lt;

	if (a->category() & alert_category::error)
	{
		str += esc("31");
	}
	else if (a->category() & (alert_category::peer | alert_category::storage))
	{
		str += esc("33");
	}
	str += "[";
	str += timestamp();
	str += "] ";
	str += a->message();
	str += esc("0");

	if (g_log_file)
		std::fprintf(g_log_file, "[%s] %s\n", timestamp(),  a->message().c_str());
}

int save_file(std::string const& filename, std::vector<char> const& v)
{
	std::fstream f(filename, std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);
	f.write(v.data(), v.size());
	return !f.fail();
}

// returns true if the alert was handled (and should not be printed to the log)
// returns false if the alert was not handled
bool handle_alert(torrent_view& view, session_view& ses_view
	, lt::session&, lt::alert* a)
{
	using namespace lt;

	if (session_stats_alert* s = alert_cast<session_stats_alert>(a))
	{
		ses_view.update_counters(s->counters()
			, duration_cast<microseconds>(s->timestamp().time_since_epoch()).count());
		return !stats_enabled;
	}

#ifndef TORRENT_DISABLE_DHT
	if (dht_stats_alert* p = alert_cast<dht_stats_alert>(a))
	{
		dht_active_requests = p->active_requests;
		dht_routing_table = p->routing_table;
		return true;
	}
#endif

#ifdef TORRENT_USE_OPENSSL
	if (torrent_need_cert_alert* p = alert_cast<torrent_need_cert_alert>(a))
	{
		torrent_handle h = p->handle;
		std::string base_name = path_append("certificates", to_hex(h.info_hash()));
		std::string cert = base_name + ".pem";
		std::string priv = base_name + "_key.pem";

#ifdef TORRENT_WINDOWS
		struct ::_stat st;
		int ret = ::_stat(cert.c_str(), &st);
		if (ret < 0 || (st.st_mode & _S_IFREG) == 0)
#else
		struct ::stat st;
		int ret = ::stat(cert.c_str(), &st);
		if (ret < 0 || (st.st_mode & S_IFREG) == 0)
#endif
		{
			char msg[256];
			std::snprintf(msg, sizeof(msg), "ERROR. could not load certificate %s: %s\n"
				, cert.c_str(), std::strerror(errno));
			if (g_log_file) std::fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
			return true;
		}

#ifdef TORRENT_WINDOWS
		ret = ::_stat(priv.c_str(), &st);
		if (ret < 0 || (st.st_mode & _S_IFREG) == 0)
#else
		ret = ::stat(priv.c_str(), &st);
		if (ret < 0 || (st.st_mode & S_IFREG) == 0)
#endif
		{
			char msg[256];
			std::snprintf(msg, sizeof(msg), "ERROR. could not load private key %s: %s\n"
				, priv.c_str(), std::strerror(errno));
			if (g_log_file) std::fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
			return true;
		}

		char msg[256];
		std::snprintf(msg, sizeof(msg), "loaded certificate %s and key %s\n", cert.c_str(), priv.c_str());
		if (g_log_file) std::fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);

		h.set_ssl_certificate(cert, priv, "certificates/dhparams.pem", "1234");
		h.resume();
	}
#endif

	// don't log every peer we try to connect to
	if (alert_cast<peer_connect_alert>(a)) return true;

	if (peer_disconnected_alert* pd = alert_cast<peer_disconnected_alert>(a))
	{
		// ignore failures to connect and peers not responding with a
		// handshake. The peers that we successfully connect to and then
		// disconnect is more interesting.
		if (pd->op == operation_t::connect
			|| pd->error == errors::timed_out_no_handshake)
			return true;
	}

#ifdef _MSC_VER
// it seems msvc makes the definitions of 'p' escape the if-statement here
#pragma warning(push)
#pragma warning(disable: 4456)
#endif

	if (metadata_received_alert* p = alert_cast<metadata_received_alert>(a))
	{
		torrent_handle h = p->handle;
		h.save_resume_data(torrent_handle::save_info_dict);
		++num_outstanding_resume_data;
	}
	else if (add_torrent_alert* p = alert_cast<add_torrent_alert>(a))
	{
		if (p->error)
		{
			std::fprintf(stderr, "failed to add torrent: %s %s\n"
				, p->params.ti ? p->params.ti->name().c_str() : p->params.name.c_str()
				, p->error.message().c_str());
		}
		else
		{
			torrent_handle h = p->handle;

			h.save_resume_data(torrent_handle::save_info_dict | torrent_handle::only_if_modified);
			++num_outstanding_resume_data;

			// if we have a peer specified, connect to it
			if (!peer.empty())
			{
				char* port = (char*) strrchr((char*)peer.c_str(), ':');
				if (port != nullptr)
				{
					*port++ = 0;
					char const* ip = peer.c_str();
					int peer_port = atoi(port);
					error_code ec;
					if (peer_port > 0)
						h.connect_peer(tcp::endpoint(address::from_string(ip, ec), std::uint16_t(peer_port)));
				}
			}
		}
	}
	else if (torrent_finished_alert* p = alert_cast<torrent_finished_alert>(a))
	{
		p->handle.set_max_connections(max_connections_per_torrent / 2);

		// write resume data for the finished torrent
		// the alert handler for save_resume_data_alert
		// will save it to disk
		torrent_handle h = p->handle;
		h.save_resume_data(torrent_handle::save_info_dict);
		++num_outstanding_resume_data;
	}
	else if (save_resume_data_alert* p = alert_cast<save_resume_data_alert>(a))
	{
		--num_outstanding_resume_data;
		auto const buf = write_resume_data_buf(p->params);
		save_file(resume_file(p->params.info_hash), buf);
	}
	else if (save_resume_data_failed_alert* p = alert_cast<save_resume_data_failed_alert>(a))
	{
		--num_outstanding_resume_data;
		// don't print the error if it was just that we didn't need to save resume
		// data. Returning true means "handled" and not printed to the log
		return p->error == lt::errors::resume_data_not_modified;
	}
	else if (torrent_paused_alert* p = alert_cast<torrent_paused_alert>(a))
	{
		// write resume data for the finished torrent
		// the alert handler for save_resume_data_alert
		// will save it to disk
		torrent_handle h = p->handle;
		h.save_resume_data(torrent_handle::save_info_dict);
		++num_outstanding_resume_data;
	}
	else if (state_update_alert* p = alert_cast<state_update_alert>(a))
	{
		view.update_torrents(std::move(p->status));
		return true;
	}
	else if (torrent_removed_alert* p = alert_cast<torrent_removed_alert>(a))
	{
		view.remove_torrent(std::move(p->handle));
	}
	return false;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}

void pop_alerts(torrent_view& view, session_view& ses_view
	, lt::session& ses, std::deque<std::string>& events)
{
	std::vector<lt::alert*> alerts;
	ses.pop_alerts(&alerts);
	for (auto a : alerts)
	{
		if (::handle_alert(view, ses_view, ses, a)) continue;

		// if we didn't handle the alert, print it to the log
		std::string event_string;
		print_alert(a, event_string);
		events.push_back(event_string);
		if (events.size() >= 20) events.pop_front();
	}
}

void print_piece(lt::partial_piece_info const* pp
	, lt::cached_piece_info const* cs
	, std::vector<lt::peer_info> const& peers
	, std::string& out)
{
	using namespace lt;

	char str[1024];
	assert(pp == nullptr || cs == nullptr || cs->piece == pp->piece_index);
	int piece = static_cast<int>(pp ? pp->piece_index : cs->piece);
	int num_blocks = pp ? pp->blocks_in_piece : int(cs->blocks.size());

	std::snprintf(str, sizeof(str), "%5d:[", piece);
	out += str;
	string_view last_color;
	for (int j = 0; j < num_blocks; ++j)
	{
		int const index = pp ? peer_index(pp->blocks[j].peer(), peers) % 36 : -1;
		char const* chr = " ";
		bool const snubbed = index >= 0 ? bool(peers[index].flags & lt::peer_info::snubbed) : false;

		char const* color = "";

		if (pp == nullptr)
		{
			color = cs->blocks[j] ? esc("34;7") : esc("0");
			chr = " ";
		}
		else
		{
			if (cs && cs->blocks[j] && pp->blocks[j].state != block_info::finished)
				color = esc("36;7");
			else if (pp->blocks[j].bytes_progress > 0
					&& pp->blocks[j].state == block_info::requested)
			{
				if (pp->blocks[j].num_peers > 1) color = esc("0;1");
				else color = snubbed ? esc("0;35") : esc("0;33");

#ifndef TORRENT_WINDOWS
				static char const* const progress[] = {
					"\u2581", "\u2582", "\u2583", "\u2584",
					"\u2585", "\u2586", "\u2587", "\u2588"
				};
				chr = progress[pp->blocks[j].bytes_progress * 8 / pp->blocks[j].block_size];
#else
				static char const* const progress[] = { "\xb0", "\xb1", "\xb2" };
				chr = progress[pp->blocks[j].bytes_progress * 3 / pp->blocks[j].block_size];
#endif
			}
			else if (pp->blocks[j].state == block_info::finished) color = esc("32;7");
			else if (pp->blocks[j].state == block_info::writing) color = esc("36;7");
			else if (pp->blocks[j].state == block_info::requested)
			{
				color = snubbed ? esc("0;35") : esc("0");
				chr = "=";
			}
			else { color = esc("0"); chr = " "; }
		}
		if (last_color != color)
		{
			out += color;
			last_color = color;
		}
		out += chr;
	}
	out += esc("0");
	out += "]";
}

bool is_resume_file(std::string const& s)
{
	static std::string const hex_digit = "0123456789abcdef";
	if (s.size() != 40 + 7) return false;
	if (s.substr(40) != ".resume") return false;
	for (char const c : s.substr(0, 40))
	{
		if (hex_digit.find(c) == std::string::npos) return false;
	}
	return true;
}

int main(int argc, char* argv[])
{
#ifndef _WIN32
	// sets the terminal to single-character mode
	// and resets when destructed
	set_keypress s;
#endif

	if (argc == 1)
	{
		std::fprintf(stderr, R"(usage: client_test [OPTIONS] [TORRENT|MAGNETURL]
OPTIONS:

CLIENT OPTIONS
  -f <log file>         logs all events to the given file
  -s <path>             sets the save path for downloads. This also determines
                        the resume data save directory. Torrents from the resume
                        directory are automatically added to the session on
                        startup.
  -m <path>             sets the .torrent monitor directory. torrent files
                        dropped in the directory are added the session and the
                        resume data directory, and removed from the monitor dir.
  -t <seconds>          sets the scan interval of the monitor dir
  -F <milliseconds>     sets the UI refresh rate. This is the number of
                        milliseconds between screen refreshes.
  -k                    enable high performance settings. This overwrites any other
                        previous command line options, so be sure to specify this first
  -G                    Add torrents in seed-mode (i.e. assume all pieces
                        are present and check hashes on-demand)
  -e <loops>            exit client after the specified number of iterations
                        through the main loop
  -O                    print session stats counters to the log)"
#ifdef TORRENT_UTP_LOG_ENABLE
R"(
  -q                    Enable uTP transport-level verbose logging
)"
#endif
R"(
LIBTORRENT SETTINGS
  --<name-of-setting>=<value>
                        set the libtorrent setting <name> to <value>
  --list-settings       print all libtorrent settings and exit

BITTORRENT OPTIONS
  -T <limit>            sets the max number of connections per torrent
  -U <rate>             sets per-torrent upload rate
  -D <rate>             sets per-torrent download rate
  -Q                    enables share mode. Share mode attempts to maximize
                        share ratio rather than downloading
  -r <IP:port>          connect to specified peer

NETWORK OPTIONS
  -x <file>             loads an emule IP-filter file
  -Y                    Rate limit local peers)"
#if TORRENT_USE_I2P
R"(  -i <i2p-host>         the hostname to an I2P SAM bridge to use
)"
#endif
R"(
DISK OPTIONS
  -a <mode>             sets the allocation mode. [sparse|allocate]
  -0                    disable disk I/O, read garbage and don't flush to disk

TORRENT is a path to a .torrent file
MAGNETURL is a magnet link

alert mask flags:
	error peer port_mapping storage tracker connect status ip_block
	performance_warning dht session_log torrent_log peer_log incoming_request
	dht_log dht_operation port_mapping_log picker_log file_progress piece_progress
	upload block_progress all

examples:
  --alert_mask=error,port_mapping,tracker,connect,session_log
  --alert_mask=error,session_log,torrent_log,peer_log
  --alert_mask=error,dht,dht_log,dht_operation
  --alert_mask=all
)") ;
		return 0;
	}

	using lt::settings_pack;
	using lt::session_handle;

	torrent_view view;
	session_view ses_view;

	lt::session_params params;

#ifndef TORRENT_DISABLE_DHT
	params.dht_settings.privacy_lookups = true;

	std::vector<char> in;
	if (load_file(".ses_state", in))
	{
		lt::error_code ec;
		lt::bdecode_node e = lt::bdecode(in, ec);
		if (!ec) params = read_session_params(e, session_handle::save_dht_state);
	}
#endif

	auto& settings = params.settings;
	settings.set_int(settings_pack::cache_size, cache_size);
	settings.set_int(settings_pack::choking_algorithm, settings_pack::rate_based_choker);

	settings.set_str(settings_pack::user_agent, "client_test/" LIBTORRENT_VERSION);
	settings.set_int(settings_pack::alert_mask
		, lt::alert_category::error
		| lt::alert_category::peer
		| lt::alert_category::port_mapping
		| lt::alert_category::storage
		| lt::alert_category::tracker
		| lt::alert_category::connect
		| lt::alert_category::status
		| lt::alert_category::ip_block
		| lt::alert_category::performance_warning
		| lt::alert_category::dht
		| lt::alert_category::incoming_request
		| lt::alert_category::dht_operation
		| lt::alert_category::port_mapping_log
		| lt::alert_category::file_progress);

	lt::time_duration refresh_delay = lt::milliseconds(500);
	bool rate_limit_locals = false;

	std::deque<std::string> events;
	int loop_limit = -1;

	lt::time_point next_dir_scan = lt::clock_type::now();

	// load the torrents given on the commandline
	std::vector<lt::string_view> torrents;
	lt::ip_filter loaded_ip_filter;

	for (int i = 1; i < argc; ++i)
	{
		if (argv[i][0] != '-')
		{
			torrents.push_back(argv[i]);
			continue;
		}

		if (argv[i] == "--list-settings"_sv)
		{
			// print all libtorrent settings and exit
			print_settings(settings_pack::string_type_base
				, settings_pack::num_string_settings
				, "%s=<string>\n");
			print_settings(settings_pack::bool_type_base
				, settings_pack::num_bool_settings
				, "%s=<bool>\n");
			print_settings(settings_pack::int_type_base
				, settings_pack::num_int_settings
				, "%s=<int>\n");
			return 0;
		}

		// maybe this is an assignment of a libtorrent setting
		if (argv[i][1] == '-' && strchr(argv[i], '=') != nullptr)
		{
			char const* equal = strchr(argv[i], '=');
			char const* start = argv[i]+2;
			// +2 is to skip the --
			std::string const key(start, equal - start);
			char const* value = equal + 1;

			assign_setting(settings, key, value);
			continue;
		}

		// if there's a flag but no argument following, ignore it
		if (argc == i) continue;
		char const* arg = argv[i+1];
		if (arg == nullptr) arg = "";

		switch (argv[i][1])
		{
			case 'f': g_log_file = std::fopen(arg, "w+"); break;
			case 'k': settings = lt::high_performance_seed(); --i; break;
			case 'G': seed_mode = true; --i; break;
			case 's': save_path = make_absolute_path(arg); break;
			case 'O': stats_enabled = true; --i; break;
#ifdef TORRENT_UTP_LOG_ENABLE
			case 'q':
				lt::set_utp_stream_logging(true);
				break;
#endif
			case 'U': torrent_upload_limit = atoi(arg) * 1000; break;
			case 'D': torrent_download_limit = atoi(arg) * 1000; break;
			case 'm': monitor_dir = make_absolute_path(arg); break;
			case 'Q': share_mode = true; --i; break;
			case 't': poll_interval = atoi(arg); break;
			case 'F': refresh_delay = lt::milliseconds(atoi(arg)); break;
			case 'a': allocation_mode = (arg == std::string("sparse"))
				? lt::storage_mode_sparse
				: lt::storage_mode_allocate;
				break;
			case 'x':
				{
					std::fstream filter(arg, std::ios_base::in);
					if (!filter.fail())
					{
						std::regex regex(R"(^\s*([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)\s*-\s*([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)\s+([0-9]+)$)");

						std::string line;
						while (std::getline(filter, line))
						{
							std::smatch m;
							if (std::regex_match(line, m, regex))
							{
								address_v4 start((stoi(m[1]) << 24) | (stoi(m[2]) << 16) | (stoi(m[3]) << 8) | stoi(m[4]));
								address_v4 last((stoi(m[5]) << 24) | (stoi(m[6]) << 16) | (stoi(m[7]) << 8) | stoi(m[8]));
								loaded_ip_filter.add_rule(start, last
									, stoi(m[9]) <= 127 ? lt::ip_filter::blocked : 0);
							}
						}
					}
				}
				break;
			case 'T': max_connections_per_torrent = atoi(arg); break;
			case 'r': peer = arg; break;
			case 'Y':
				{
					--i;
					rate_limit_locals = true;
					break;
				}
			case '0': disable_storage = true; --i; break;
			case 'e':
				{
					loop_limit = atoi(arg);
					break;
				}
		}
		++i; // skip the argument
	}

	// create directory for resume files
#ifdef TORRENT_WINDOWS
	int mkdir_ret = _mkdir(path_append(save_path, ".resume").c_str());
#else
	int mkdir_ret = mkdir(path_append(save_path, ".resume").c_str(), 0777);
#endif
	if (mkdir_ret < 0 && errno != EEXIST)
	{
		std::fprintf(stderr, "failed to create resume file directory: (%d) %s\n"
			, errno, strerror(errno));
	}

	lt::session ses(std::move(params));

	if (rate_limit_locals)
	{
		lt::ip_filter pcf;
		pcf.add_rule(address_v4::from_string("0.0.0.0")
			, address_v4::from_string("255.255.255.255")
			, 1 << static_cast<std::uint32_t>(lt::session::global_peer_class_id));
		pcf.add_rule(address_v6::from_string("::")
			, address_v6::from_string("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 1);
		ses.set_peer_class_filter(pcf);
	}

	ses.set_ip_filter(loaded_ip_filter);

	for (auto const& i : torrents)
	{
		if (i.substr(0, 7) == "magnet:") add_magnet(ses, i);
		else add_torrent(ses, i.to_string());
	}

	std::thread resume_data_loader([&ses]
	{
		// load resume files
		lt::error_code ec;
		std::string const resume_dir = path_append(save_path, ".resume");
		std::vector<std::string> ents = list_dir(resume_dir
			, [](lt::string_view p) { return p.size() > 7 && p.substr(p.size() - 7) == ".resume"; }, ec);
		if (ec)
		{
		std::fprintf(stderr, "failed to list resume directory \"%s\": (%s : %d) %s\n"
			, resume_dir.c_str(), ec.category().name(), ec.value(), ec.message().c_str());
		}
		else
		{
			for (auto const& e : ents)
			{
				// only load resume files of the form <info-hash>.resume
				if (!is_resume_file(e)) continue;
				std::string const file = path_append(resume_dir, e);

				std::vector<char> resume_data;
				if (!load_file(file, resume_data))
				{
					std::printf("  failed to load resume file \"%s\": %s\n"
						, file.c_str(), ec.message().c_str());
					continue;
				}
				add_torrent_params p = lt::read_resume_data(resume_data, ec);
				if (ec)
				{
					std::printf("  failed to parse resume data \"%s\": %s\n"
						, file.c_str(), ec.message().c_str());
					continue;
				}

				ses.async_add_torrent(std::move(p));
			}
		}
	});

	// main loop
	std::vector<lt::peer_info> peers;
	std::vector<lt::partial_piece_info> queue;

#ifndef _WIN32
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
#endif

	while (!quit && loop_limit != 0)
	{
		if (loop_limit > 0) --loop_limit;

		ses.post_torrent_updates();
		ses.post_session_stats();
		ses.post_dht_stats();

		int terminal_width = 80;
		int terminal_height = 50;
		std::tie(terminal_width, terminal_height) = terminal_size();

		// the ratio of torrent-list and details below depend on the number of
		// torrents we have in the session
		int const height = std::min(terminal_height / 2
			, std::max(5, view.num_visible_torrents() + 2));
		view.set_size(terminal_width, height);
		ses_view.set_pos(height);
		ses_view.set_width(terminal_width);

		int c = 0;
		if (sleep_and_input(&c, refresh_delay))
		{

#ifdef _WIN32
			constexpr int escape_seq = 224;
			constexpr int left_arrow = 75;
			constexpr int right_arrow = 77;
			constexpr int up_arrow = 72;
			constexpr int down_arrow = 80;
#else
			constexpr int escape_seq = 27;
			constexpr int left_arrow = 68;
			constexpr int right_arrow = 67;
			constexpr int up_arrow = 65;
			constexpr int down_arrow = 66;
#endif

			torrent_handle h = view.get_active_handle();

			if (c == EOF)
			{
				quit = true;
				break;
			}
			do
			{
				if (c == escape_seq)
				{
					// escape code, read another character
#ifdef _WIN32
					int c2 = _getch();
#else
					int c2 = getc(stdin);
					if (c2 == EOF)
					{
						quit = true;
						break;
					}
					if (c2 != '[') continue;
					c2 = getc(stdin);
#endif
					if (c2 == EOF)
					{
						quit = true;
						break;
					}
					if (c2 == left_arrow)
					{
						int const filter = view.filter();
						if (filter > 0)
						{
							view.set_filter(filter - 1);
							h = view.get_active_handle();
						}
					}
					else if (c2 == right_arrow)
					{
						int const filter = view.filter();
						if (filter < torrent_view::torrents_max - 1)
						{
							view.set_filter(filter + 1);
							h = view.get_active_handle();
						}
					}
					else if (c2 == up_arrow)
					{
						view.arrow_up();
						h = view.get_active_handle();
					}
					else if (c2 == down_arrow)
					{
						view.arrow_down();
						h = view.get_active_handle();
					}
				}

				if (c == ' ')
				{
					if (ses.is_paused()) ses.resume();
					else ses.pause();
				}

				if (c == '[' && h.is_valid())
				{
					h.queue_position_up();
				}

				if (c == ']' && h.is_valid())
				{
					h.queue_position_down();
				}

				// add magnet link
				if (c == 'm')
				{
					char url[4096];
					url[0] = '\0';
					puts("Enter magnet link:\n");

#ifndef _WIN32
					// enable terminal echo temporarily
					set_keypress s(set_keypress::echo | set_keypress::canonical);
#endif
					if (std::scanf("%4095s", url) == 1) add_magnet(ses, url);
					else std::printf("failed to read magnet link\n");
				}

				if (c == 'q')
				{
					quit = true;
					break;
				}

				if (c == 'W' && h.is_valid())
				{
					std::set<std::string> seeds = h.url_seeds();
					for (std::set<std::string>::iterator i = seeds.begin()
						, end(seeds.end()); i != end; ++i)
					{
						h.remove_url_seed(*i);
					}

					seeds = h.http_seeds();
					for (std::set<std::string>::iterator i = seeds.begin()
						, end(seeds.end()); i != end; ++i)
					{
						h.remove_http_seed(*i);
					}
				}

				if (c == 'D' && h.is_valid())
				{
					torrent_status const& st = view.get_active_torrent();
					std::printf("\n\nARE YOU SURE YOU WANT TO DELETE THE FILES FOR '%s'. THIS OPERATION CANNOT BE UNDONE. (y/N)"
						, st.name.c_str());
#ifndef _WIN32
					// enable terminal echo temporarily
					set_keypress s(set_keypress::echo | set_keypress::canonical);
#endif
					char response = 'n';
					int scan_ret = std::scanf("%c", &response);
					if (scan_ret == 1 && response == 'y')
					{
						// also delete the resume file
						std::string const rpath = resume_file(st.info_hash);
						if (::remove(rpath.c_str()) < 0)
							std::printf("failed to delete resume file (\"%s\")\n"
								, rpath.c_str());

						if (st.handle.is_valid())
						{
							ses.remove_torrent(st.handle, lt::session::delete_files);
						}
						else
						{
							std::printf("failed to delete torrent, invalid handle: %s\n"
								, st.name.c_str());
						}
					}
				}

				if (c == 'j' && h.is_valid())
				{
					h.force_recheck();
				}

				if (c == 'r' && h.is_valid())
				{
					h.force_reannounce();
				}

				if (c == 's' && h.is_valid())
				{
					torrent_status const& ts = view.get_active_torrent();
					h.set_flags(~ts.flags, lt::torrent_flags::sequential_download);
				}

				if (c == 'R')
				{
					// save resume data for all torrents
					std::vector<torrent_status> const torr = ses.get_torrent_status(
						[](torrent_status const& st)
						{ return st.need_save_resume; }, {});
					for (torrent_status const& st : torr)
					{
						st.handle.save_resume_data(torrent_handle::save_info_dict);
						++num_outstanding_resume_data;
					}
				}

				if (c == 'o' && h.is_valid())
				{
					torrent_status const& ts = view.get_active_torrent();
					int num_pieces = ts.num_pieces;
					if (num_pieces > 300) num_pieces = 300;
					for (piece_index_t i(0); i < piece_index_t(num_pieces); ++i)
					{
						h.set_piece_deadline(i, (static_cast<int>(i)+5) * 1000
							, torrent_handle::alert_when_available);
					}
				}

				if (c == 'v' && h.is_valid())
				{
					h.scrape_tracker();
				}

				if (c == 'p' && h.is_valid())
				{
					torrent_status const& ts = view.get_active_torrent();
					if ((ts.flags & (lt::torrent_flags::auto_managed
						| lt::torrent_flags::paused)) == lt::torrent_flags::paused)
					{
						h.set_flags(lt::torrent_flags::auto_managed);
					}
					else
					{
						h.unset_flags(lt::torrent_flags::auto_managed);
						h.pause(torrent_handle::graceful_pause);
					}
				}

				// toggle force-start
				if (c == 'k' && h.is_valid())
				{
					torrent_status const& ts = view.get_active_torrent();
					h.set_flags(
						~(ts.flags & lt::torrent_flags::auto_managed),
						lt::torrent_flags::auto_managed);
					if ((ts.flags & lt::torrent_flags::auto_managed)
						&& (ts.flags & lt::torrent_flags::paused))
					{
						h.resume();
					}
				}

				if (c == 'c' && h.is_valid())
				{
					h.clear_error();
				}

				// toggle displays
				if (c == 't') print_trackers = !print_trackers;
				if (c == 'i') print_peers = !print_peers;
				if (c == 'l') print_log = !print_log;
				if (c == 'd') print_downloads = !print_downloads;
				if (c == 'y') print_matrix = !print_matrix;
				if (c == 'f') print_file_progress = !print_file_progress;
				if (c == 'P') show_pad_files = !show_pad_files;
				if (c == 'g') show_dht_status = !show_dht_status;
				if (c == 'x') print_disk_stats = !print_disk_stats;
				// toggle columns
				if (c == '1') print_ip = !print_ip;
				if (c == '2') print_connecting_peers = !print_connecting_peers;
				if (c == '3') print_timers = !print_timers;
				if (c == '4') print_block = !print_block;
				if (c == '6') print_fails = !print_fails;
				if (c == '7') print_send_bufs = !print_send_bufs;
				if (c == '8') print_local_ip = !print_local_ip;
				if (c == 'C')
				{
					cache_size = (cache_size == 0) ? -1 : 0;
					settings_pack p;
					p.set_int(settings_pack::cache_size, cache_size);
					ses.apply_settings(std::move(p));
				}
				if (c == 'h')
				{
					clear_screen();
					set_cursor_pos(0,0);
					print(
R"(HELP SCREEN (press any key to dismiss)

CLIENT OPTIONS

[q] quit client                                 [m] add magnet link

TORRENT ACTIONS
[p] pause/resume selected torrent               [C] toggle disk cache
[s] toggle sequential download                  [j] force recheck
[space] toggle session pause                    [c] clear error
[v] scrape                                      [D] delete torrent and data
[r] force reannounce                            [R] save resume data for all torrents
[o] set piece deadlines (sequential dl)         [P] toggle auto-managed
[k] toggle force-started                        [W] remove all web seeds
 [  move queue position closer to beginning
 ]  move queue position closer to end

DISPLAY OPTIONS
left/right arrow keys: select torrent filter
up/down arrow keys: select torrent
[i] toggle show peers                           [d] toggle show downloading pieces
[P] show pad files (in file list)               [f] toggle show files
[g] show DHT                                    [x] toggle disk cache stats
[t] show trackers                               [l] toggle show log
[y] toggle show piece matrix

COLUMN OPTIONS
[1] toggle IP column                            [2] toggle show peer connection attempts
[3] toggle timers column                        [4] toggle block progress column
                                                [6] toggle failures column
[7] toggle send buffers column                  [8] toggle local IP column
)");
					int tmp;
					while (sleep_and_input(&tmp, lt::milliseconds(500)) == false);
				}

			} while (sleep_and_input(&c, lt::milliseconds(0)));
			if (c == 'q')
			{
				quit = true;
				break;
			}
		}

		pop_alerts(view, ses_view, ses, events);

		std::string out;

		char str[500];

		int pos = view.height() + ses_view.height();
		set_cursor_pos(0, pos);

		int cache_flags = print_downloads ? 0 : lt::session::disk_cache_no_pieces;
		torrent_handle h = view.get_active_handle();

		cache_status cs;
		ses.get_cache_info(&cs, h, cache_flags);

#ifndef TORRENT_DISABLE_DHT
		if (show_dht_status)
		{
			// TODO: 3 expose these counters as performance counters
/*
			std::snprintf(str, sizeof(str), "DHT nodes: %d DHT cached nodes: %d "
				"total DHT size: %" PRId64 " total observers: %d\n"
				, sess_stat.dht_nodes, sess_stat.dht_node_cache, sess_stat.dht_global_nodes
				, sess_stat.dht_total_allocations);
			out += str;
*/

			int bucket = 0;
			for (lt::dht_routing_bucket const& n : dht_routing_table)
			{
				char const* progress_bar =
					"################################"
					"################################"
					"################################"
					"################################";
				char const* short_progress_bar = "--------";
				std::snprintf(str, sizeof(str)
					, "%3d [%3d, %d] %s%s\x1b[K\n"
					, bucket, n.num_nodes, n.num_replacements
					, progress_bar + (128 - n.num_nodes)
					, short_progress_bar + (8 - std::min(8, n.num_replacements)));
				out += str;
				pos += 1;
				++bucket;
			}

			for (lt::dht_lookup const& l : dht_active_requests)
			{
				std::snprintf(str, sizeof(str)
					, "  %10s target: %s "
					"[limit: %2d] "
					"in-flight: %-2d "
					"left: %-3d "
					"1st-timeout: %-2d "
					"timeouts: %-2d "
					"responses: %-2d "
					"last_sent: %-2d "
					"\x1b[K\n"
					, l.type
					, to_hex(l.target).c_str()
					, l.branch_factor
					, l.outstanding_requests
					, l.nodes_left
					, l.first_timeout
					, l.timeouts
					, l.responses
					, l.last_sent);
				out += str;
				pos += 1;
			}
		}
#endif
		lt::time_point const now = lt::clock_type::now();
		if (h.is_valid())
		{
			torrent_status const& s = view.get_active_torrent();

			print((piece_bar(s.pieces, terminal_width - 2) + "\x1b[K\n").c_str());
			pos += 1;

			if ((print_downloads && s.state != torrent_status::seeding)
				|| print_peers)
				h.get_peer_info(peers);

			if (print_peers && !peers.empty())
			{
				using lt::peer_info;
				// sort connecting towards the bottom of the list, and by peer_id
				// otherwise, to keep the list as stable as possible
				std::sort(peers.begin(), peers.end()
					, [](peer_info const& lhs, peer_info const& rhs)
					{
						{
							bool const l = bool(lhs.flags & peer_info::connecting);
							bool const r = bool(rhs.flags & peer_info::connecting);
							if (l != r) return l < r;
						}

						{
							bool const l = bool(lhs.flags & peer_info::handshake);
							bool const r = bool(rhs.flags & peer_info::handshake);
							if (l != r) return l < r;
						}

						return lhs.pid < rhs.pid;
					});
				pos += print_peer_info(out, peers, terminal_height - pos - 2);
			}

			if (print_trackers)
			{
				snprintf(str, sizeof(str), "next_announce: %4" PRId64 " | current tracker: %s\x1b[K\n"
					, std::int64_t(duration_cast<seconds>(s.next_announce).count())
					, s.current_tracker.c_str());
				out += str;
				pos += 1;
				std::vector<lt::announce_entry> tr = h.trackers();
				for (lt::announce_entry const& ae : h.trackers())
				{
					std::snprintf(str, sizeof(str), "%2d %-55s %s\x1b[K\n"
						, ae.tier, ae.url.c_str(), ae.verified?"OK ":"-  ");
					out += str;
					pos += 1;
					int idx = 0;
					for (auto const& ep : ae.endpoints)
					{
						++idx;
						if (!ep.enabled) continue;
						if (pos + 1 >= terminal_height) break;

						std::snprintf(str, sizeof(str), "  [%2d] fails: %-3d (%-3d) %s %5d \"%s\" %s\x1b[K\n"
							, idx
							, ep.fails, ae.fail_limit
							, to_string(int(total_seconds(ep.next_announce - now)), 8).c_str()
							, ep.min_announce > now ? int(total_seconds(ep.min_announce - now)) : 0
							, ep.last_error ? ep.last_error.message().c_str() : ""
							, ep.message.c_str());
						out += str;
						pos += 1;
						// we only need to show this error once, not for every
						// endpoint
						if (ep.last_error == boost::asio::error::host_not_found) break;
					}

					if (pos + 1 >= terminal_height) break;
				}
			}

			if (print_matrix)
			{
				int height_out = 0;
				print(piece_matrix(s.pieces, terminal_width, &height_out).c_str());
				pos += height_out;
			}

			if (print_downloads)
			{
				h.get_download_queue(queue);

				std::sort(queue.begin(), queue.end()
					, [] (lt::partial_piece_info const& lhs, lt::partial_piece_info const& rhs)
					{ return lhs.piece_index < rhs.piece_index; });

				std::sort(cs.pieces.begin(), cs.pieces.end()
					, [](lt::cached_piece_info const& lhs, lt::cached_piece_info const& rhs)
					{ return lhs.piece < rhs.piece; });

				int p = 0; // this is horizontal position
				for (lt::cached_piece_info const& i : cs.pieces)
				{
					if (pos + 3 >= terminal_height) break;

					lt::partial_piece_info* pp = nullptr;
					lt::partial_piece_info tmp;
					tmp.piece_index = i.piece;
					std::vector<lt::partial_piece_info>::iterator ppi
						= std::lower_bound(queue.begin(), queue.end(), tmp
						, [](lt::partial_piece_info const& lhs, lt::partial_piece_info const& rhs)
						{ return lhs.piece_index < rhs.piece_index; });

					if (ppi != queue.end() && ppi->piece_index == i.piece) pp = &*ppi;

					print_piece(pp, &i, peers, out);

					int num_blocks = pp ? pp->blocks_in_piece : int(i.blocks.size());
					p += num_blocks + 8;
					bool continuous_mode = 8 + num_blocks > terminal_width;
					if (continuous_mode)
					{
						while (p > terminal_width)
						{
							p -= terminal_width;
							++pos;
						}
					}
					else if (p + num_blocks + 8 > terminal_width)
					{
						out += "\x1b[K\n";
						pos += 1;
						p = 0;
					}

					if (pp) queue.erase(ppi);
				}

				for (lt::partial_piece_info const& i : queue)
				{
					if (pos + 3 >= terminal_height) break;

					print_piece(&i, nullptr, peers, out);

					int num_blocks = i.blocks_in_piece;
					p += num_blocks + 8;
					bool continuous_mode = 8 + num_blocks > terminal_width;
					if (continuous_mode)
					{
						while (p > terminal_width)
						{
							p -= terminal_width;
							++pos;
						}
					}
					else if (p + num_blocks + 8 > terminal_width)
					{
						out += "\x1b[K\n";
						pos += 1;
						p = 0;
					}
				}
				if (p != 0)
				{
					out += "\x1b[K\n";
					pos += 1;
				}

				std::snprintf(str, sizeof(str), "%s %s read cache | %s %s downloading | %s %s cached | %s %s flushed | %s %s snubbed | = requested\x1b[K\n"
					, esc("34;7"), esc("0") // read cache
					, esc("33;7"), esc("0") // downloading
					, esc("36;7"), esc("0") // cached
					, esc("32;7"), esc("0") // flushed
					, esc("35;7"), esc("0") // snubbed
					);
				out += str;
				pos += 1;
			}

			if (print_file_progress && s.has_metadata)
			{
				std::vector<std::int64_t> file_progress;
				h.file_progress(file_progress);
				std::vector<lt::open_file_state> file_status = h.file_status();
				std::vector<lt::download_priority_t> file_prio = h.get_file_priorities();
				auto f = file_status.begin();
				std::shared_ptr<const lt::torrent_info> ti = h.torrent_file();

				int p = 0; // this is horizontal position
				for (file_index_t i(0); i < file_index_t(ti->num_files()); ++i)
				{
					int const idx = static_cast<int>(i);
					if (pos + 1 >= terminal_height) break;

					bool const pad_file = ti->files().pad_file_at(i);
					if (pad_file && !show_pad_files) continue;

					int const progress = ti->files().file_size(i) > 0
						? int(file_progress[idx] * 1000 / ti->files().file_size(i)) : 1000;
					assert(file_progress[idx] <= ti->files().file_size(i));

					bool const complete = file_progress[idx] == ti->files().file_size(i);

					std::string title = ti->files().file_name(i).to_string();
					if (!complete)
					{
						std::snprintf(str, sizeof(str), " (%.1f%%)", progress / 10.f);
						title += str;
					}

					if (f != file_status.end() && f->file_index == i)
					{
						title += " [ ";
						if ((f->open_mode & lt::file_open_mode::rw_mask) == lt::file_open_mode::read_write) title += "read/write ";
						else if ((f->open_mode & lt::file_open_mode::rw_mask) == lt::file_open_mode::read_only) title += "read ";
						else if ((f->open_mode & lt::file_open_mode::rw_mask) == lt::file_open_mode::write_only) title += "write ";
						if (f->open_mode & lt::file_open_mode::random_access) title += "random_access ";
						if (f->open_mode & lt::file_open_mode::sparse) title += "sparse ";
						title += "]";
						++f;
					}

					const int file_progress_width = pad_file ? 10 : 65;

					// do we need to line-break?
					if (p + file_progress_width + 13 > terminal_width)
					{
						out += "\x1b[K\n";
						pos += 1;
						p = 0;
					}

					std::snprintf(str, sizeof(str), "%s %7s p: %d ",
						progress_bar(progress, file_progress_width
							, pad_file ? col_blue
							: complete ? col_green : col_yellow
							, '-', '#', title.c_str()).c_str()
						, add_suffix(file_progress[idx]).c_str()
						, static_cast<std::uint8_t>(file_prio[idx]));

					p += file_progress_width + 13;
					out += str;
				}

				if (p != 0)
				{
					out += "\x1b[K\n";
					pos += 1;
				}
			}
		}

		if (print_log)
		{
			for (auto const& e : events)
			{
				if (pos + 1 >= terminal_height) break;
				out += e;
				out += "\x1b[K\n";
				pos += 1;
			}
		}

		// clear rest of screen
		out += "\x1b[J";
		print(out.c_str());

		std::fflush(stdout);

		if (!monitor_dir.empty() && next_dir_scan < now)
		{
			scan_dir(monitor_dir, ses);
			next_dir_scan = now + seconds(poll_interval);
		}
	}

	resume_data_loader.join();

	ses.pause();
	std::printf("saving resume data\n");

	// get all the torrent handles that we need to save resume data for
	std::vector<torrent_status> const temp = ses.get_torrent_status(
		[](torrent_status const& st)
		{
			if (!st.handle.is_valid() || !st.has_metadata || !st.need_save_resume)
				return false;
			return true;
		}, {});

	int idx = 0;
	for (auto const& st : temp)
	{
		// save_resume_data will generate an alert when it's done
		st.handle.save_resume_data(torrent_handle::save_info_dict);
		++num_outstanding_resume_data;
		++idx;
		if ((idx % 32) == 0)
		{
			std::printf("\r%d  ", num_outstanding_resume_data);
			pop_alerts(view, ses_view, ses, events);
		}
	}
	std::printf("\nwaiting for resume data [%d]\n", num_outstanding_resume_data);

	while (num_outstanding_resume_data > 0)
	{
		alert const* a = ses.wait_for_alert(seconds(10));
		if (a == nullptr) continue;
		pop_alerts(view, ses_view, ses, events);
	}

	if (g_log_file) std::fclose(g_log_file);

	// we're just saving the DHT state
#ifndef TORRENT_DISABLE_DHT
	std::printf("\nsaving session state\n");
	{
		lt::entry session_state;
		ses.save_state(session_state, lt::session::save_dht_state);

		std::vector<char> out;
		bencode(std::back_inserter(out), session_state);
		save_file(".ses_state", out);
	}
#endif

	std::printf("closing session\n");

	return 0;
}

