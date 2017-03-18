/*

Copyright (c) 2003, Arvid Norberg
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

#include "libtorrent/config.hpp"

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _mkdir and _getcwd
#include <sys/types.h> // for _stat
#include <sys/stat.h>
#endif

#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/extensions/ut_pex.hpp"
#include "libtorrent/extensions/smart_ban.hpp"

#include "libtorrent/aux_/max_path.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/string_view.hpp"

#include "torrent_view.hpp"
#include "session_view.hpp"
#include "print.hpp"

using libtorrent::total_milliseconds;

void sleep_ms(int milliseconds)
{
#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
	Sleep(milliseconds);
#elif defined TORRENT_BEOS
	snooze_until(system_time() + std::int64_t(milliseconds) * 1000, B_SYSTEM_TIMEBASE);
#else
	usleep(milliseconds * 1000);
#endif
}

#ifdef _WIN32

#include <windows.h>
#include <conio.h>

bool sleep_and_input(int* c, int sleep)
{
	for (int i = 0; i < 2; ++i)
	{
		if (_kbhit())
		{
			*c = _getch();
			return true;
		}
		Sleep(sleep / 2);
	}
	return false;
};

#else

#include <termios.h>
#include <sys/ioctl.h>
#include <csignal>
#include <utility>

struct set_keypress
{
	set_keypress()
	{
		termios new_settings;
		tcgetattr(0,&stored_settings);
		new_settings = stored_settings;
		// Disable canonical mode, and set buffer size to 1 byte
		// and disable echo
		new_settings.c_lflag &= ~(ICANON | ECHO);
		new_settings.c_cc[VTIME] = 0;
		new_settings.c_cc[VMIN] = 1;
		tcsetattr(0,TCSANOW,&new_settings);
	}
	~set_keypress() { tcsetattr(0,TCSANOW,&stored_settings); }
	termios stored_settings;
};

bool sleep_and_input(int* c, int sleep)
{
	libtorrent::time_point const start = libtorrent::clock_type::now();
	int ret = 0;
retry:
	fd_set set;
	FD_ZERO(&set);
	FD_SET(0, &set);
	timeval tv = {sleep/ 1000, (sleep % 1000) * 1000 };
	ret = select(1, &set, nullptr, nullptr, &tv);
	if (ret > 0)
	{
		*c = getc(stdin);
		return true;
	}
	if (errno == EINTR)
	{
		if (total_milliseconds(libtorrent::clock_type::now() - start) < sleep)
			goto retry;
		return false;
	}

	if (ret < 0 && errno != 0 && errno != ETIMEDOUT)
	{
		std::fprintf(stderr, "select failed: %s\n", strerror(errno));
		sleep_ms(500);
	}

	return false;
}

#endif

bool print_trackers = false;
bool print_peers = false;
bool print_log = false;
bool print_downloads = false;
bool print_matrix = false;
bool print_file_progress = false;
bool show_pad_files = false;
bool show_dht_status = false;
bool sequential_download = false;

bool print_ip = true;
bool print_timers = false;
bool print_block = false;
bool print_peer_rate = false;
bool print_fails = false;
bool print_send_bufs = true;
bool print_disk_stats = false;

// the number of times we've asked to save resume data
// without having received a response (successful or failure)
int num_outstanding_resume_data = 0;

#ifndef TORRENT_DISABLE_DHT
std::vector<libtorrent::dht_lookup> dht_active_requests;
std::vector<libtorrent::dht_routing_bucket> dht_routing_table;
#endif

torrent_view view;
session_view ses_view;

std::string to_hex(lt::sha1_hash const& s)
{
	std::stringstream ret;
	ret << s;
	return ret.str();
}

int load_file(std::string const& filename, std::vector<char>& v
	, libtorrent::error_code& ec, int limit = 8000000)
{
	ec.clear();
	FILE* f = std::fopen(filename.c_str(), "rb");
	if (f == nullptr)
	{
		ec.assign(errno, boost::system::system_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::system_category());
		std::fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::system_category());
		std::fclose(f);
		return -1;
	}

	if (s > limit)
	{
		std::fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::system_category());
		std::fclose(f);
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		std::fclose(f);
		return 0;
	}

	r = int(std::fread(&v[0], 1, v.size(), f));
	if (r < 0)
	{
		ec.assign(errno, boost::system::system_category());
		std::fclose(f);
		return -1;
	}

	std::fclose(f);

	if (r != s) return -3;

	return 0;
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

std::string leaf_path(std::string f)
{
	if (f.empty()) return "";
	char const* first = f.c_str();
	char const* sep = strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	char const* altsep = strrchr(first, '\\');
	if (sep == 0 || altsep > sep) sep = altsep;
#endif
	if (sep == nullptr) return f;

	if (sep - first == int(f.size()) - 1)
	{
		// if the last character is a / (or \)
		// ignore it
		int len = 0;
		while (sep > first)
		{
			--sep;
			if (*sep == '/'
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
				|| *sep == '\\'
#endif
				)
				return std::string(sep + 1, len);
			++len;
		}
		return std::string(first, len);
	}
	return std::string(sep + 1);
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

bool is_hex(char const *in, int len)
{
	for (char const* end = in + len; in < end; ++in)
	{
		if (*in >= '0' && *in <= '9') continue;
		if (*in >= 'A' && *in <= 'F') continue;
		if (*in >= 'a' && *in <= 'f') continue;
		return false;
	}
	return true;
}

std::string print_endpoint(libtorrent::tcp::endpoint const& ep)
{
	using namespace libtorrent;
	error_code ec;
	char buf[200];
	address const& addr = ep.address();
#if TORRENT_USE_IPV6
	if (addr.is_v6())
		std::snprintf(buf, sizeof(buf), "[%s]:%d", addr.to_string(ec).c_str(), ep.port());
	else
#endif
		std::snprintf(buf, sizeof(buf), "%s:%d", addr.to_string(ec).c_str(), ep.port());
	return buf;
}

struct torrent_entry
{
	explicit torrent_entry(libtorrent::torrent_handle h) : handle(std::move(h)) {}
	libtorrent::torrent_handle handle;
	libtorrent::torrent_status status;
};

// maps filenames to torrent_handles
typedef std::map<std::string, libtorrent::torrent_handle> handles_t;
typedef std::map<libtorrent::sha1_hash, std::string> files_t;

files_t hash_to_filename;

using libtorrent::torrent_status;

bool yes(libtorrent::torrent_status const&)
{ return true; }

FILE* g_log_file = nullptr;

int peer_index(libtorrent::tcp::endpoint addr, std::vector<libtorrent::peer_info> const& peers)
{
	using namespace libtorrent;
	std::vector<peer_info>::const_iterator i = std::find_if(peers.begin(), peers.end()
		, [&addr](peer_info const& pi) { return pi.ip == addr; });
	if (i == peers.end()) return -1;

	return int(i - peers.begin());
}

// returns the number of lines printed
int print_peer_info(std::string& out
	, std::vector<libtorrent::peer_info> const& peers, int max_lines)
{
	using namespace libtorrent;
	int pos = 0;
	if (print_ip) out += "IP                             ";
	out += "progress        down     (total | peak   )  up      (total | peak   ) sent-req tmo bsy rcv flags         dn  up  source  ";
	if (print_fails) out += "fail hshf ";
	if (print_send_bufs) out += "rq sndb (recvb |alloc | wmrk ) q-bytes ";
	if (print_timers) out += "inactive wait timeout q-time ";
	out += "  v disk ^    rtt  ";
	if (print_block) out += "block-progress ";
	if (print_peer_rate) out += "est.rec.rate ";
	out += "client \x1b[K\n";
	++pos;

	char str[500];
	for (std::vector<peer_info>::const_iterator i = peers.begin();
		i != peers.end(); ++i)
	{
		if (i->flags & (peer_info::handshake | peer_info::connecting))
			continue;

		if (print_ip)
		{
			std::snprintf(str, sizeof(str), "%-30s ", (::print_endpoint(i->ip) +
				(i->flags & peer_info::utp_socket ? " [uTP]" : "") +
				(i->flags & peer_info::i2p_socket ? " [i2p]" : "")
				).c_str());
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
			std::snprintf(str, sizeof(str), "%3d %3d "
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

		if (print_peer_rate)
		{
			bool unchoked = (i->flags & peer_info::choked) == 0;

			std::snprintf(str, sizeof(str), " %s"
				, unchoked ? add_suffix(i->estimated_reciprocation_rate, "/s").c_str() : "      ");
			out += str;
		}
		out += " ";

		if (i->flags & peer_info::handshake)
		{
			out += esc("31");
			out += " waiting for handshake";
			out += esc("0");
		}
		else if (i->flags & peer_info::connecting)
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

int allocation_mode = libtorrent::storage_mode_sparse;
std::string save_path(".");
int torrent_upload_limit = 0;
int torrent_download_limit = 0;
std::string monitor_dir;
int poll_interval = 5;
int max_connections_per_torrent = 50;
bool seed_mode = false;
int cache_size = 1024;

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

std::string path_to_url(std::string f)
{
	std::string ret = "file://"
#ifdef TORRENT_WINDOWS
		"/"
#endif
		;
	static char const hex_chars[] = "0123456789abcdef";
	static const char unreserved[] =
		"/-_!.~*()ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		"0123456789";

	// make sure the path is an absolute path
	if (!is_absolute_path(f))
	{
		char cwd[TORRENT_MAX_PATH];
#if defined TORRENT_WINDOWS && !defined TORRENT_MINGW
		_getcwd(cwd, sizeof(cwd));
#else
		char const* ret = getcwd(cwd, sizeof(cwd));
		(void)ret; // best effort
#endif
		f = path_append(cwd, f);
	}

	for (int i = 0; i < int(f.size()); ++i)
	{
#ifdef TORRENT_WINDOWS
		if (f[i] == '\\') ret.push_back('/');
		else
#endif
		if (std::strchr(unreserved, f[i]) != nullptr) ret.push_back(f[i]);
		else
		{
			ret.push_back('%');
			ret.push_back(hex_chars[std::uint8_t(f[i]) >> 4]);
			ret.push_back(hex_chars[std::uint8_t(f[i]) & 0xf]);
		}
	}
	return ret;
}

void print_settings(int const start, int const num
	, char const* const fmt)
{
	for (int i = start; i < start + num; ++i)
	{
		char const* name = libtorrent::name_for_setting(i);
		if (!name || name[0] == '\0') continue;
		std::printf(fmt, name);
	}
}

void add_torrent(libtorrent::session& ses
	, handles_t& files
	, std::string torrent)
{
	using namespace libtorrent;
	static int counter = 0;

	std::printf("[%d] %s\n", counter++, torrent.c_str());

	error_code ec;
	add_torrent_params p;

	std::vector<char> resume_data;
	std::string filename = path_append(save_path, path_append(".resume"
		, leaf_path(torrent) + ".resume"));
	load_file(filename, resume_data, ec);
	if (!ec)
	{
		p = read_resume_data(&resume_data[0], int(resume_data.size()), ec);
		if (ec) std::printf("  failed to load resume data: %s\n", ec.message().c_str());
	}
	ec.clear();

	if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
	if (disable_storage) p.storage = disabled_storage_constructor;
	if (share_mode) p.flags |= add_torrent_params::flag_share_mode;

	p.url = path_to_url(torrent);
	p.save_path = save_path;
	p.storage_mode = (storage_mode_t)allocation_mode;
	p.flags &= ~add_torrent_params::flag_duplicate_is_error;
	p.userdata = static_cast<void*>(new std::string(torrent));
	ses.async_add_torrent(p);
	files.insert(std::pair<const std::string, torrent_handle>(torrent, torrent_handle()));
}

std::vector<std::string> list_dir(std::string path
	, bool (*filter_fun)(std::string const&)
	, libtorrent::error_code& ec)
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
		std::string p = fd.cFileName;
		if (filter_fun(p))
			ret.push_back(p);

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

	struct dirent de;
	dirent* dummy;
	while (readdir_r(handle, &de, &dummy) == 0)
	{
		if (dummy == nullptr) break;

		std::string p = de.d_name;
		if (filter_fun(p))
			ret.push_back(p);
	}
	closedir(handle);
#endif
	return ret;
}

bool filter_fun(std::string const& p)
{
	for (int i = int(p.size()) - 1; i >= 0; --i)
	{
		if (p[i] == '/') break;
#ifdef TORRENT_WINDOWS
		if (p[i] == '\\') break;
#endif
		if (p[i] != '.') continue;
		return p.compare(i, 8, ".torrent") == 0;
	}
	return false;
}

void scan_dir(std::string const& dir_path
	, libtorrent::session& ses
	, handles_t& files)
{
	std::set<std::string> valid;

	using namespace libtorrent;

	error_code ec;
	std::vector<std::string> ents = list_dir(dir_path, filter_fun, ec);
	if (ec)
	{
		std::fprintf(stderr, "failed to list directory: (%s : %d) %s\n"
			, ec.category().name(), ec.value(), ec.message().c_str());
		return;
	}

	for (std::vector<std::string>::iterator i = ents.begin()
		, end(ents.end()); i != end; ++i)
	{
		std::string file = path_append(dir_path, *i);

		handles_t::iterator k = files.find(file);
		if (k != files.end())
		{
			valid.insert(file);
			continue;
		}

		// the file has been added to the dir, start
		// downloading it.
		add_torrent(ses, files, file);
		valid.insert(file);
	}

	// remove the torrents that are no longer in the directory

	for (handles_t::iterator i = files.begin(); !files.empty() && i != files.end();)
	{
		if (i->first.empty() || valid.find(i->first) != valid.end())
		{
			++i;
			continue;
		}

		torrent_handle& h = i->second;
		if (!h.is_valid())
		{
			i = files.erase(i);
			continue;
		}

		h.auto_managed(false);
		h.pause();
		// the alert handler for save_resume_data_alert
		// will save it to disk
		h.save_resume_data();
		++num_outstanding_resume_data;

		i = files.erase(i);
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

void print_alert(libtorrent::alert const* a, std::string& str)
{
	using namespace libtorrent;

	if (a->category() & alert::error_notification)
	{
		str += esc("31");
	}
	else if (a->category() & (alert::peer_notification | alert::storage_notification))
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
	FILE* f = std::fopen(filename.c_str(), "wb");
	if (f == nullptr)
		return -1;

	int w = int(std::fwrite(&v[0], 1, v.size(), f));
	std::fclose(f);

	if (w < 0) return -1;
	if (w != int(v.size())) return -3;
	return 0;
}

// returns true if the alert was handled (and should not be printed to the log)
// returns false if the alert was not handled
bool handle_alert(libtorrent::session& ses, libtorrent::alert* a
	, handles_t& files, std::set<libtorrent::torrent_handle>& non_files)
{
	using namespace libtorrent;

	if (session_stats_alert* s = alert_cast<session_stats_alert>(a))
	{
		ses_view.update_counters(s->values.data(), int(s->values.size())
			, duration_cast<microseconds>(s->timestamp().time_since_epoch()).count());
		return true;
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
		if (pd->operation == op_connect
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
		// if we have a monitor dir, save the .torrent file we just received in it
		// also, add it to the files map, and remove it from the non_files list
		// to keep the scan dir logic in sync so it's not removed, or added twice
		torrent_handle h = p->handle;
		if (h.is_valid())
		{
			std::shared_ptr<const torrent_info> ti = h.torrent_file();
			create_torrent ct(*ti);
			entry te = ct.generate();
			std::vector<char> buffer;
			bencode(std::back_inserter(buffer), te);
			sha1_hash hash = ti->info_hash();
			std::string filename = ti->name() + "." + to_hex(hash) + ".torrent";
			filename = path_append(monitor_dir, filename);
			save_file(filename, buffer);

			files.insert(std::pair<std::string, libtorrent::torrent_handle>(filename, h));
			hash_to_filename[hash] = filename;
			non_files.erase(h);
		}
	}
	else if (add_torrent_alert* p = alert_cast<add_torrent_alert>(a))
	{
		std::string filename;
		if (p->params.userdata)
		{
			std::string* f = static_cast<std::string*>(p->params.userdata);
			filename.swap(*f);
			delete f;
		}

		if (p->error)
		{
			std::fprintf(stderr, "failed to add torrent: %s %s\n", filename.c_str()
				, p->error.message().c_str());
		}
		else
		{
			torrent_handle h = p->handle;

			if (!filename.empty())
				files[filename] = h;
			else
				non_files.insert(h);

			h.set_max_connections(max_connections_per_torrent);
			h.set_max_uploads(-1);
			h.set_upload_limit(torrent_upload_limit);
			h.set_download_limit(torrent_download_limit);

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

			sha1_hash info_hash;
			if (p->params.ti)
			{
				info_hash = p->params.ti->info_hash();
			}
			else if (!p->params.info_hash.is_all_zeros())
			{
				info_hash = p->params.info_hash;
			}
			else
			{
				info_hash = h.info_hash();
			}
			hash_to_filename.insert(std::make_pair(info_hash, filename));
		}
	}
	else if (torrent_finished_alert* p = alert_cast<torrent_finished_alert>(a))
	{
		p->handle.set_max_connections(max_connections_per_torrent / 2);

		// write resume data for the finished torrent
		// the alert handler for save_resume_data_alert
		// will save it to disk
		torrent_handle h = p->handle;
		h.save_resume_data();
		++num_outstanding_resume_data;
	}
	else if (save_resume_data_alert* p = alert_cast<save_resume_data_alert>(a))
	{
		--num_outstanding_resume_data;
		torrent_handle h = p->handle;
		auto const buf = write_resume_data_buf(p->params);
		torrent_status st = h.status(torrent_handle::query_save_path);
		save_file(path_append(st.save_path, path_append(".resume", leaf_path(
			hash_to_filename[st.info_hash]) + ".resume")), buf);
		if (h.is_valid()
			&& non_files.find(h) == non_files.end()
			&& std::none_of(files.begin(), files.end()
				, [&h](handles_t::value_type const& hn) { return hn.second == h; }))
		{
			ses.remove_torrent(h);
		}
	}
	else if (save_resume_data_failed_alert* p = alert_cast<save_resume_data_failed_alert>(a))
	{
		--num_outstanding_resume_data;
		torrent_handle h = p->handle;
		if (h.is_valid())
		{
			std::fprintf(stderr, "FAILED TO SAVE RESUME DATA: %s\n"
				, h.status().name.c_str());
		}
		if (h.is_valid()
			&& non_files.find(h) == non_files.end()
			&& std::none_of(files.begin(), files.end()
				, [&h](handles_t::value_type const& hn) { return hn.second == h; }))
		{
			ses.remove_torrent(h);
		}
	}
	else if (torrent_paused_alert* p = alert_cast<torrent_paused_alert>(a))
	{
		// write resume data for the finished torrent
		// the alert handler for save_resume_data_alert
		// will save it to disk
		torrent_handle h = p->handle;
		h.save_resume_data();
		++num_outstanding_resume_data;
	}
	else if (state_update_alert* p = alert_cast<state_update_alert>(a))
	{
		view.update_torrents(p->status);
		return true;
	}
	return false;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}

void print_piece(libtorrent::partial_piece_info* pp
	, libtorrent::cached_piece_info* cs
	, std::vector<libtorrent::peer_info> const& peers
	, std::string& out)
{
	using namespace libtorrent;

	char str[1024];
	assert(pp == nullptr || cs == nullptr || cs->piece == pp->piece_index);
	int piece = static_cast<int>(pp ? pp->piece_index : cs->piece);
	int num_blocks = pp ? pp->blocks_in_piece : int(cs->blocks.size());

	std::snprintf(str, sizeof(str), "%5d:[", piece);
	out += str;
	char const* last_color = nullptr;
	for (int j = 0; j < num_blocks; ++j)
	{
		int index = pp ? peer_index(pp->blocks[j].peer(), peers) % 36 : -1;
		char chr = '+';
		if (index >= 0)
			chr = char((index < 10)?'0' + index:'A' + index - 10);
		bool snubbed = index >= 0 ? ((peers[index].flags & peer_info::snubbed) != 0) : false;

		char const* color = "";

		if (pp == nullptr)
		{
			color = cs->blocks[j] ? esc("34;7") : esc("0");
			chr = ' ';
		}
		else
		{
			if (cs && cs->blocks[j] && pp->blocks[j].state != block_info::finished)
				color = esc("36;7");
			else if (pp->blocks[j].bytes_progress > 0
					&& pp->blocks[j].state == block_info::requested)
			{
				if (pp->blocks[j].num_peers > 1) color = esc("1;7");
				else color = snubbed ? esc("35;7") : esc("33;7");
				chr = char('0' + (pp->blocks[j].bytes_progress * 10 / pp->blocks[j].block_size));
			}
			else if (pp->blocks[j].state == block_info::finished) color = esc("32;7");
			else if (pp->blocks[j].state == block_info::writing) color = esc("36;7");
			else if (pp->blocks[j].state == block_info::requested) color = snubbed ? esc("35;7") : esc("0");
			else { color = esc("0"); chr = ' '; }
		}
		if (last_color == nullptr || std::strcmp(last_color, color) != 0)
		{
			std::snprintf(str, sizeof(str), "%s%c", color, chr);
			out += str;
		}
		else
			out += chr;

		last_color = color;
	}
	out += esc("0");
	out += "]";
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
		std::fprintf(stderr, "usage: client_test [OPTIONS] [TORRENT|MAGNETURL|URL]\n\n"
			"OPTIONS:\n"
			"\n CLIENT OPTIONS\n"
			"  -f <log file>         logs all events to the given file\n"
			"  -s <path>             sets the save path for downloads\n"
			"  -m <path>             sets the .torrent monitor directory\n"
			"  -t <seconds>          sets the scan interval of the monitor dir\n"
			"  -F <milliseconds>     sets the UI refresh rate. This is the number of\n"
			"                        milliseconds between screen refreshes.\n"
			"  -k                    enable high performance settings. This overwrites any other\n"
			"                        previous command line options, so be sure to specify this first\n"
			"  -G                    Add torrents in seed-mode (i.e. assume all pieces\n"
			"                        are present and check hashes on-demand)\n"
			"\n LIBTORRENT SETTINGS\n"
			"  --<name-of-setting>=<value>\n"
			"                        set the libtorrent setting <name> to <value>\n"
			"  --list-settings       print all libtorrent settings and exit\n"
			"\n BITTORRENT OPTIONS\n"
			"  -T <limit>            sets the max number of connections per torrent\n"
			"  -U <rate>             sets per-torrent upload rate\n"
			"  -D <rate>             sets per-torrent download rate\n"
			"  -Q                    enables share mode. Share mode attempts to maximize\n"
			"                        share ratio rather than downloading\n"
			"  -r <IP:port>          connect to specified peer\n"
			"\n NETWORK OPTIONS\n"
			"  -x <file>             loads an emule IP-filter file\n"
			"  -Y                    Rate limit local peers\n"
#if TORRENT_USE_I2P
			"  -i <i2p-host>         the hostname to an I2P SAM bridge to use\n"
#endif
			"\n DISK OPTIONS\n"
			"  -a <mode>             sets the allocation mode. [sparse|allocate]\n"
			"  -0                    disable disk I/O, read garbage and don't flush to disk\n"
			"\n\n"
			"TORRENT is a path to a .torrent file\n"
			"MAGNETURL is a magnet link\n"
			"\n") ;
		return 0;
	}

	using namespace libtorrent;
	namespace lt = libtorrent;

	settings_pack settings;
	settings.set_int(settings_pack::cache_size, cache_size);
	settings.set_int(settings_pack::choking_algorithm, settings_pack::rate_based_choker);

	int refresh_delay = 500;
	bool rate_limit_locals = false;

	std::deque<std::string> events;

	time_point next_dir_scan = clock_type::now();

	// the string is the filename of the .torrent file, but only if
	// it was added through the directory monitor. It is used to
	// be able to remove torrents that were added via the directory
	// monitor when they're not in the directory anymore.
	handles_t files;

	// torrents that were not added via the monitor dir
	std::set<torrent_handle> non_files;

	// load the torrents given on the commandline

	std::vector<add_torrent_params> magnet_links;
	std::vector<std::string> torrents;
	ip_filter loaded_ip_filter;

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

			int const sett_name = setting_by_name(key);
			if (sett_name < 0)
			{
				std::fprintf(stderr, "unknown setting: \"%s\"\n", key.c_str());
				return 1;
			}

			switch (sett_name & settings_pack::type_mask)
			{
				case settings_pack::string_type_base:
					settings.set_str(sett_name, value);
					break;
				case settings_pack::bool_type_base:
					if (value == "0"_sv || value == "1"_sv)
					{
						settings.set_bool(sett_name, atoi(value) != 0);
					}
					else
					{
						std::fprintf(stderr, "invalid value for \"%s\". expected 0 or 1\n"
							, key.c_str());
						return 1;
					}
					break;
				case settings_pack::int_type_base:
					settings.set_int(sett_name, atoi(value));
					break;
			}
			continue;
		}

		// if there's a flag but no argument following, ignore it
		if (argc == i) continue;
		char const* arg = argv[i+1];
		if (arg == nullptr) arg = "";

		switch (argv[i][1])
		{
			case 'f': g_log_file = std::fopen(arg, "w+"); break;
			case 'k': settings = high_performance_seed(); --i; break;
			case 'G': seed_mode = true; --i; break;
			case 's': save_path = arg; break;
			case 'U': torrent_upload_limit = atoi(arg) * 1000; break;
			case 'D': torrent_download_limit = atoi(arg) * 1000; break;
			case 'm': monitor_dir = arg; break;
			case 'Q': share_mode = true; --i; break;
			case 't': poll_interval = atoi(arg); break;
			case 'F': refresh_delay = atoi(arg); break;
			case 'a': allocation_mode = (arg == std::string("sparse"))
				? libtorrent::storage_mode_sparse
				: libtorrent::storage_mode_allocate;
				break;
			case 'x':
				{
					FILE* filter = std::fopen(arg, "r");
					if (filter)
					{
						unsigned int a,b,c,d,e,f,g,h, flags;
						while (std::fscanf(filter, "%u.%u.%u.%u - %u.%u.%u.%u %u\n"
							, &a, &b, &c, &d, &e, &f, &g, &h, &flags) == 9)
						{
							address_v4 start((a << 24) + (b << 16) + (c << 8) + d);
							address_v4 last((e << 24) + (f << 16) + (g << 8) + h);
							if (flags <= 127) flags = ip_filter::blocked;
							else flags = 0;
							loaded_ip_filter.add_rule(start, last, flags);
						}
						std::fclose(filter);
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
			case '0': disable_storage = true; --i;
		}
		++i; // skip the argument
	}

	// create directory for resume files
#ifdef TORRENT_WINDOWS
	int ret = _mkdir(path_append(save_path, ".resume").c_str());
#else
	int ret = mkdir(path_append(save_path, ".resume").c_str(), 0777);
#endif
	if (ret < 0)
		std::fprintf(stderr, "failed to create resume file directory: (%d) %s\n"
			, errno, strerror(errno));

	settings.set_str(settings_pack::user_agent, "client_test/" LIBTORRENT_VERSION);
	settings.set_int(settings_pack::alert_mask, alert::all_categories
		& ~(alert::dht_notification
		+ alert::progress_notification
		+ alert::stats_notification
		+ alert::session_log_notification
		+ alert::torrent_log_notification
		+ alert::peer_log_notification
		+ alert::dht_log_notification
		+ alert::picker_log_notification
		));

	libtorrent::session ses(settings);

	if (rate_limit_locals)
	{
		ip_filter pcf;
		// 1 is the global peer class. This should be done properly in the future
		pcf.add_rule(address_v4::from_string("0.0.0.0")
			, address_v4::from_string("255.255.255.255"), 1);
#if TORRENT_USE_IPV6
		pcf.add_rule(address_v6::from_string("::")
			, address_v6::from_string("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 1);
#endif
		ses.set_peer_class_filter(pcf);
	}

	ses.set_ip_filter(loaded_ip_filter);

	error_code ec;

#ifndef TORRENT_DISABLE_DHT
	dht_settings dht;
	dht.privacy_lookups = true;
	ses.set_dht_settings(dht);

	std::vector<char> in;
	if (load_file(".ses_state", in, ec) == 0)
	{
		bdecode_node e;
		if (bdecode(&in[0], &in[0] + in.size(), e, ec) == 0)
			ses.load_state(e, session::save_dht_state);
	}
#endif

	for (auto const& magnet : magnet_links)
		ses.async_add_torrent(magnet);

	for (auto const& i : torrents)
	{
		if (std::strstr(i.c_str(), "http://") == i.c_str()
			|| std::strstr(i.c_str(), "https://") == i.c_str()
			|| std::strstr(i.c_str(), "magnet:") == i.c_str())
		{
			add_torrent_params p;

			if (std::strstr(i.c_str(), "magnet:") == i.c_str())
			{
				add_torrent_params tmp;
				ec.clear();
				parse_magnet_uri(i, tmp, ec);

				if (ec) continue;

				std::string filename = path_append(save_path, path_append(".resume"
					, to_hex(tmp.info_hash) + ".resume"));

				std::vector<char> resume_data;
				load_file(filename, resume_data, ec);
				if (!ec)
				{
					p = read_resume_data(&resume_data[0], int(resume_data.size()), ec);
					if (ec) std::printf("  failed to load resume data: %s\n", ec.message().c_str());
				}
				ec.clear();
			}

			if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
			if (disable_storage) p.storage = disabled_storage_constructor;
			if (share_mode) p.flags |= add_torrent_params::flag_share_mode;
			p.save_path = save_path;
			p.storage_mode = (storage_mode_t)allocation_mode;
			p.url = i;

			std::printf("adding URL: %s\n", i.c_str());
			ses.async_add_torrent(p);
			continue;
		}

		// if it's a torrent file, open it as usual
		add_torrent(ses, files, i.c_str());
	}

	// main loop
	std::vector<peer_info> peers;
	std::vector<partial_piece_info> queue;

	int tick = 0;

#ifndef _WIN32
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
#endif

	while (!quit)
	{
		++tick;
		ses.post_torrent_updates();
		ses.post_session_stats();
		ses.post_dht_stats();

		int terminal_width = 80;
		int terminal_height = 50;
		terminal_size(&terminal_width, &terminal_height);
		view.set_size(terminal_width, terminal_height / 3);
		ses_view.set_pos(terminal_height / 3);

		int c = 0;
		if (sleep_and_input(&c, refresh_delay))
		{

#ifdef _WIN32
#define ESCAPE_SEQ 224
#define LEFT_ARROW 75
#define RIGHT_ARROW 77
#define UP_ARROW 72
#define DOWN_ARROW 80
#else
#define ESCAPE_SEQ 27
#define LEFT_ARROW 68
#define RIGHT_ARROW 67
#define UP_ARROW 65
#define DOWN_ARROW 66
#endif

			torrent_handle h = view.get_active_handle();

			if (c == EOF) { break; }
			do
			{
				if (c == ESCAPE_SEQ)
				{
					// escape code, read another character
#ifdef _WIN32
					int c2 = _getch();
#else
					int c2 = getc(stdin);
					if (c2 == EOF) { break; }
					if (c2 != '[') continue;
					c2 = getc(stdin);
#endif
					if (c2 == EOF) break;
					if (c2 == LEFT_ARROW)
					{
						// arrow left
						int filter = view.filter();
						if (filter > 0)
						{
							--filter;
							view.set_filter(filter);
							h = view.get_active_handle();
						}
					}
					else if (c2 == RIGHT_ARROW)
					{
						// arrow right
						int filter = view.filter();
						if (filter < torrent_view::torrents_max - 1)
						{
							++filter;
							view.set_filter(filter);
							h = view.get_active_handle();
						}
					}
					else if (c2 == UP_ARROW)
					{
						// arrow up
						view.arrow_up();
						h = view.get_active_handle();
					}
					else if (c2 == DOWN_ARROW)
					{
						// arrow down
						view.arrow_down();
						h = view.get_active_handle();
					}
				}

				if (c == ' ')
				{
					if (ses.is_paused()) ses.resume();
					else ses.pause();
				}

				// add magnet link
				if (c == 'm')
				{
					char url[4096];
					puts("Enter magnet link:\n");
					int ret = std::scanf("%4095s", url);

					add_torrent_params p;
					if (ret == 1 && std::strstr(url, "magnet:") == url)
					{
						add_torrent_params tmp;
						parse_magnet_uri(url, tmp, ec);

						if (ec) continue;

						std::string filename = path_append(save_path, path_append(".resume"
								, to_hex(tmp.info_hash) + ".resume"));

						std::vector<char> resume_data;
						load_file(filename, resume_data, ec);
						if (!ec)
						{
							p = read_resume_data(&resume_data[0], int(resume_data.size()), ec);
							if (ec) std::printf("  failed to load resume data: %s\n", ec.message().c_str());
						}
						ec.clear();
					}

					if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
					if (disable_storage) p.storage = disabled_storage_constructor;
					if (share_mode) p.flags |= add_torrent_params::flag_share_mode;
					p.save_path = save_path;
					p.storage_mode = (storage_mode_t)allocation_mode;
					p.url = url;

					std::printf("adding URL: %s\n", url);
					ses.async_add_torrent(p);
				}

				if (c == 'q') break;

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
					char response = 'n';
					int ret = std::scanf("%c", &response);
					if (ret == 1 && response == 'y')
					{
						// also delete the .torrent file from the torrent directory
						handles_t::iterator i = std::find_if(files.begin(), files.end()
							, [&st] (handles_t::value_type const& hn) { return hn.second == st.handle; });
						if (i != files.end())
						{
							error_code err;
							std::string path;
							if (is_absolute_path(i->first)) path = i->first;
							else path = path_append(monitor_dir, i->first);
							if (::remove(path.c_str()) < 0)
								std::printf("failed to delete .torrent file: %s\n", err.message().c_str());
							files.erase(i);
						}
						if (st.handle.is_valid())
							ses.remove_torrent(st.handle, lt::session::delete_files);
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
					h.set_sequential_download(!ts.sequential_download);
				}

				if (c == 'R')
				{
					// save resume data for all torrents
					std::vector<torrent_status> torr;
					ses.get_torrent_status(&torr, &yes, 0);
					for (torrent_status const& st : torr)
					{
						if (st.need_save_resume)
						{
							st.handle.save_resume_data();
							++num_outstanding_resume_data;
						}
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
					if (!ts.auto_managed && ts.paused)
					{
						h.auto_managed(true);
					}
					else
					{
						h.auto_managed(false);
						h.pause(torrent_handle::graceful_pause);
					}
				}

				// toggle force-start
				if (c == 'k' && h.is_valid())
				{
					torrent_status const& ts = view.get_active_torrent();
					h.auto_managed(!ts.auto_managed);
					if (ts.auto_managed && ts.paused) h.resume();
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
				if (c == 'u') ses_view.print_utp_stats(!ses_view.print_utp_stats());
				if (c == 'x') print_disk_stats = !print_disk_stats;
				// toggle columns
				if (c == '1') print_ip = !print_ip;
				if (c == '3') print_timers = !print_timers;
				if (c == '4') print_block = !print_block;
				if (c == '5') print_peer_rate = !print_peer_rate;
				if (c == '6') print_fails = !print_fails;
				if (c == '7') print_send_bufs = !print_send_bufs;
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
						"HELP SCREEN (press any key to dismiss)\n\n"
						"CLIENT OPTIONS\n"
						"[q] quit client                                 [m] add magnet link\n"
						"\n"
						"TORRENT ACTIONS\n"
						"[p] pause/unpause selected torrent              [C] toggle disk cache\n"
						"[s] toggle sequential download                  [j] force recheck\n"
						"[space] toggle session pause                    [c] clear error\n"
						"[v] scrape                                      [D] delete torrent and data\n"
						"[r] force reannounce                            [R] save resume data for all torrents\n"
						"[o] set piece deadlines (sequential dl)         [P] toggle auto-managed\n"
						"[k] toggle force-started                        [W] remove all web seeds\n"
						"\n"
						"DISPLAY OPTIONS\n"
						"left/right arrow keys: select torrent filter\n"
						"up/down arrow keys: select torrent\n"
						"[i] toggle show peers                           [d] toggle show downloading pieces\n"
						"[u] show uTP stats                              [f] toggle show files\n"
						"[g] show DHT                                    [x] toggle disk cache stats\n"
						"[t] show trackers                               [l] toggle show log\n"
						"[P] show pad files (in file list)               [y] toggle show piece matrix\n"
						"\n"
						"COLUMN OPTIONS\n"
						"[1] toggle IP column                            [2]\n"
						"[3] toggle timers column                        [4] toggle block progress column\n"
						"[5] toggle peer rate column                     [6] toggle failures column\n"
						"[7] toggle send buffers column\n"
						);
					int tmp;
					while (sleep_and_input(&tmp, 500) == false);
				}

			} while (sleep_and_input(&c, 0));
			if (c == 'q') break;
		}

		// loop through the alert queue to see if anything has happened.
		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);
		for (std::vector<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			try
			{
				if (!::handle_alert(ses, *i, files, non_files))
				{
					// if we didn't handle the alert, print it to the log
					std::string event_string;
					print_alert(*i, event_string);
					events.push_back(event_string);
					if (events.size() >= 20) events.pop_front();
				}
			} catch (std::exception const&) {}
		}
		alerts.clear();

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
			for (dht_routing_bucket const& n : dht_routing_table)
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
					, short_progress_bar + (8 - (std::min)(8, n.num_replacements)));
				out += str;
				pos += 1;
			}

			for (dht_lookup const& l : dht_active_requests)
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
		if (h.is_valid())
		{
			torrent_status const& s = view.get_active_torrent();

			print((piece_bar(s.pieces, 126) + "\x1b[K\n").c_str());
			pos += 1;

			if ((print_downloads && s.state != torrent_status::seeding)
				|| print_peers)
				h.get_peer_info(peers);

			if (print_peers && !peers.empty())
				pos += print_peer_info(out, peers, terminal_height - pos - 2);

			if (print_trackers)
			{
				time_point const now = clock_type::now();
				for (announce_entry const& ae : h.trackers())
				{
					if (pos + 1 >= terminal_height) break;
					std::snprintf(str, sizeof(str), "%2d %-55s fails: %-3d (%-3d) %s %s %5d \"%s\" %s\x1b[K\n"
						, ae.tier, ae.url.c_str(), ae.fails, ae.fail_limit, ae.verified?"OK ":"-  "
						, ae.updating?"updating"
							:to_string(int(total_seconds(ae.next_announce - now)), 8).c_str()
						, int(ae.min_announce > now ? total_seconds(ae.min_announce - now) : 0)
						, ae.last_error ? ae.last_error.message().c_str() : ""
						, ae.message.c_str());
					out += str;
					pos += 1;
				}
			}

			if (print_matrix)
			{
				int height = 0;
				print(piece_matrix(s.pieces, terminal_width, &height).c_str());
				pos += height;
			}

			if (print_downloads)
			{
				h.get_download_queue(queue);

				std::sort(queue.begin(), queue.end()
					, [] (partial_piece_info const& lhs, partial_piece_info const& rhs)
					{ return lhs.piece_index < rhs.piece_index; });

				std::sort(cs.pieces.begin(), cs.pieces.end()
					, [](cached_piece_info const& lhs, cached_piece_info const& rhs)
					{ return lhs.piece < rhs.piece; });

				int p = 0; // this is horizontal position
				for (std::vector<cached_piece_info>::iterator i = cs.pieces.begin();
					i != cs.pieces.end(); ++i)
				{
					if (pos + 3 >= terminal_height) break;

					partial_piece_info* pp = nullptr;
					partial_piece_info tmp;
					tmp.piece_index = i->piece;
					std::vector<partial_piece_info>::iterator ppi
						= std::lower_bound(queue.begin(), queue.end(), tmp
						, [](partial_piece_info const& lhs, partial_piece_info const& rhs)
						{ return lhs.piece_index < rhs.piece_index; });

					if (ppi != queue.end() && ppi->piece_index == i->piece) pp = &*ppi;

					print_piece(pp, &*i, peers, out);

					int num_blocks = pp ? pp->blocks_in_piece : int(i->blocks.size());
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

				for (std::vector<partial_piece_info>::iterator i = queue.begin()
					, end(queue.end()); i != end; ++i)
				{
					if (pos + 3 >= terminal_height) break;

					print_piece(&*i, nullptr, peers, out);

					int num_blocks = i->blocks_in_piece;
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

				std::snprintf(str, sizeof(str), "%s %s read cache | %s %s downloading | %s %s cached | %s %s flushed | %s %s snubbed\x1b[K\n"
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
				std::vector<pool_file_status> file_status = h.file_status();
				std::vector<int> file_prio = h.file_priorities();
				std::vector<pool_file_status>::iterator f = file_status.begin();
				std::shared_ptr<const torrent_info> ti = h.torrent_file();

				int p = 0; // this is horizontal position
				for (file_index_t i(0); i < file_index_t(ti->num_files()); ++i)
				{
					int const idx = static_cast<int>(i);
					if (pos + 1 >= terminal_height) break;

					bool pad_file = ti->files().pad_file_at(i);
					if (pad_file)
					{
						if (show_pad_files)
						{
							std::snprintf(str, sizeof(str), "\x1b[34m%-70s %s\x1b[0m\x1b[K\n"
								, ti->files().file_name(i).to_string().c_str()
								, add_suffix(ti->files().file_size(i)).c_str());
							out += str;
							pos += 1;
						}
						continue;
					}

					int progress = ti->files().file_size(i) > 0
						? int(file_progress[idx] * 1000 / ti->files().file_size(i)) : 1000;

					bool complete = file_progress[idx] == ti->files().file_size(i);

					std::string title = ti->files().file_name(i).to_string();
					if (!complete)
					{
						std::snprintf(str, sizeof(str), " (%.1f%%)", progress / 10.f);
						title += str;
					}

					if (f != file_status.end() && f->file_index == i)
					{
						title += " [ ";
						if ((f->open_mode & file::rw_mask) == file::read_write) title += "read/write ";
						else if ((f->open_mode & file::rw_mask) == file::read_only) title += "read ";
						else if ((f->open_mode & file::rw_mask) == file::write_only) title += "write ";
						if (f->open_mode & file::random_access) title += "random_access ";
						if (f->open_mode & file::lock_file) title += "locked ";
						if (f->open_mode & file::sparse) title += "sparse ";
						title += "]";
						++f;
					}

					const int file_progress_width = 65;

					// do we need to line-break?
					if (p + file_progress_width + 13 > terminal_width)
					{
						out += "\x1b[K\n";
						pos += 1;
						p = 0;
					}

					std::snprintf(str, sizeof(str), "%s %7s p: %d ",
						progress_bar(progress, file_progress_width, complete ? col_green : col_yellow, '-', '#'
							, title.c_str()).c_str()
						, add_suffix(file_progress[idx]).c_str()
						, file_prio[idx]);

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
			for (std::deque<std::string>::iterator i = events.begin();
				i != events.end(); ++i)
			{
				if (pos + 1 >= terminal_height) break;
				out += *i;
				out += "\x1b[K\n";
				pos += 1;
			}
		}

		// clear rest of screen
		out += "\x1b[J";
		print(out.c_str());

		std::fflush(stdout);

		if (!monitor_dir.empty()
			&& next_dir_scan < clock_type::now())
		{
			scan_dir(monitor_dir, ses, files);
			next_dir_scan = clock_type::now() + seconds(poll_interval);
		}
	}

	ses.pause();
	std::printf("saving resume data\n");
	std::vector<torrent_status> temp;
	ses.get_torrent_status(&temp, &yes, 0);
	for (std::vector<torrent_status>::iterator i = temp.begin();
		i != temp.end(); ++i)
	{
		torrent_status& st = *i;
		if (!st.handle.is_valid())
		{
			std::printf("  skipping, invalid handle\n");
			continue;
		}
		if (!st.has_metadata)
		{
			std::printf("  skipping %s, no metadata\n", st.name.c_str());
			continue;
		}
		if (!st.need_save_resume)
		{
			std::printf("  skipping %s, resume file up-to-date\n", st.name.c_str());
			continue;
		}

		// save_resume_data will generate an alert when it's done
		st.handle.save_resume_data();
		++num_outstanding_resume_data;
		std::printf("\r%d  ", num_outstanding_resume_data);
	}
	std::printf("\nwaiting for resume data [%d]\n", num_outstanding_resume_data);

	while (num_outstanding_resume_data > 0)
	{
		alert const* a = ses.wait_for_alert(seconds(10));
		if (a == nullptr) continue;

		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);
		std::string const now = timestamp();
		for (std::vector<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			if (!::handle_alert(ses, *i, files, non_files))
			{
				// if we didn't handle the alert, print it to the log
				std::string event_string;
				print_alert(*i, event_string);
			}
		}
	}

	if (g_log_file) std::fclose(g_log_file);

	// we're just saving the DHT state
#ifndef TORRENT_DISABLE_DHT
	std::printf("\nsaving session state\n");
	{
		entry session_state;
		ses.save_state(session_state, session::save_dht_state);

		std::vector<char> out;
		bencode(std::back_inserter(out), session_state);
		save_file(".ses_state", out);
	}
#endif

	std::printf("closing session");

	return 0;
}
