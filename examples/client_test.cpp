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

#include <iterator>

#include "libtorrent/config.hpp"

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _mkdir and _getcwd
#include <sys/types.h> // for _stat
#include <sys/stat.h>
#endif

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>
#include <boost/unordered_set.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/extensions/ut_pex.hpp"
#include "libtorrent/extensions/smart_ban.hpp"

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

#include "torrent_view.hpp"
#include "session_view.hpp"
#include "print.hpp"

using boost::bind;
using libtorrent::total_milliseconds;

void sleep_ms(int milliseconds)
{
#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
	Sleep(milliseconds);
#elif defined TORRENT_BEOS
	snooze_until(system_time() + boost::int64_t(milliseconds) * 1000, B_SYSTEM_TIMEBASE);
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

#include <stdio.h> // for snprintf
#include <stdlib.h> // for atoi

#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>

struct set_keypress
{
	set_keypress()
	{
		termios new_settings;
		tcgetattr(0,&stored_settings);
		new_settings = stored_settings;
		// Disable canonical mode, and set buffer size to 1 byte
		new_settings.c_lflag &= (~ICANON);
		new_settings.c_cc[VTIME] = 0;
		new_settings.c_cc[VMIN] = 1;
		tcsetattr(0,TCSANOW,&new_settings);
	}
	~set_keypress() { tcsetattr(0,TCSANOW,&stored_settings); }
	termios stored_settings;
};

bool sleep_and_input(int* c, int sleep)
{
	// sets the terminal to single-character mode
	// and resets when destructed
	set_keypress s;
	libtorrent::time_point start = libtorrent::clock_type::now();
	int ret = 0;
retry:
	fd_set set;
	FD_ZERO(&set);
	FD_SET(0, &set);
	timeval tv = {sleep/ 1000, (sleep % 1000) * 1000 };
	ret = select(1, &set, 0, 0, &tv);
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
		fprintf(stderr, "select failed: %s\n", strerror(errno));
		sleep_ms(500);
	}

	return false;
}

#endif

bool print_trackers = false;
bool print_peers = false;
bool print_log = false;
bool print_downloads = false;
bool print_file_progress = false;
bool show_pad_files = false;
bool show_dht_status = false;
bool sequential_download = false;

bool print_ip = true;
bool print_as = false;
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

int load_file(std::string const& filename, std::vector<char>& v
	, libtorrent::error_code& ec, int limit = 8000000)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		ec.assign(errno, boost::system::generic_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	if (s > limit)
	{
		fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = fread(&v[0], 1, v.size(), f);
	if (r < 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	fclose(f);

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
	if (sep == 0) return f;

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
		snprintf(buf, sizeof(buf), "[%s]:%d", addr.to_string(ec).c_str(), ep.port());
	else
#endif
		snprintf(buf, sizeof(buf), "%s:%d", addr.to_string(ec).c_str(), ep.port());
	return buf;
}

struct torrent_entry
{
	torrent_entry(libtorrent::torrent_handle h) : handle(h) {}
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

FILE* g_log_file = 0;

std::string const& piece_bar(libtorrent::bitfield const& p, int width)
{
	const int table_size = 18;
	
	double piece_per_char = p.size() / double(width);
	static std::string bar;
	bar.clear();
	bar.reserve(width * 6);
	bar += "[";
	if (p.size() == 0)
	{
		for (int i = 0; i < width; ++i) bar += ' ';
		bar += "]";
		return bar;
	}

	// the [piece, piece + pieces_per_char) range is the pieces that are represented by each character
	double piece = 0;
	for (int i = 0; i < width; ++i, piece += piece_per_char)
	{
		int num_pieces = 0;
		int num_have = 0;
		int end = (std::max)(int(piece + piece_per_char), int(piece) + 1);
		for (int k = int(piece); k < end; ++k, ++num_pieces)
			if (p[k]) ++num_have;
		int color = int(std::ceil(num_have / float(num_pieces) * (table_size - 1)));
		char buf[10];
		snprintf(buf, 10, "48;5;%d", 232 + color);
		bar += esc(buf);
		bar += " ";
	}
	bar += esc("0");
	bar += "]";
	return bar;
}

int peer_index(libtorrent::tcp::endpoint addr, std::vector<libtorrent::peer_info> const& peers)
{
	using namespace libtorrent;
	std::vector<peer_info>::const_iterator i = std::find_if(peers.begin()
		, peers.end(), boost::bind(&peer_info::ip, _1) == addr);
	if (i == peers.end()) return -1;

	return i - peers.begin();
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
	if (print_send_bufs) out += "rq sndb rcvb   q-bytes ";
	if (print_timers) out += "inactive wait timeout q-time ";
	out += "  v disk ^    rtt  ";
	if (print_block) out += "block-progress ";
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
	out += "country ";
#endif
	if (print_peer_rate) out += "peer-rate est.rec.rate ";
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
			snprintf(str, sizeof(str), "%-30s ", (::print_endpoint(i->ip) +
				(i->flags & peer_info::utp_socket ? " [uTP]" : "") +
				(i->flags & peer_info::i2p_socket ? " [i2p]" : "")
				).c_str());
			out += str;
		}

		char temp[10];
		snprintf(temp, sizeof(temp), "%d/%d"
			, i->download_queue_length
			, i->target_dl_queue_length);
		temp[7] = 0;

		char peer_progress[10];
		snprintf(peer_progress, sizeof(peer_progress), "%.1f%%", i->progress_ppm / 10000.f);
		snprintf(str, sizeof(str)
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
			snprintf(str, sizeof(str), "%3d %3d "
				, i->failcount, i->num_hashfails);
			out += str;
		}
		if (print_send_bufs)
		{
			snprintf(str, sizeof(str), "%2d %6d %6d%5dkB "
				, i->requests_in_buffer, i->used_send_buffer
				, i->used_receive_buffer
				, i->queue_bytes / 1000);
			out += str;
		}
		if (print_timers)
		{
			char req_timeout[20] = "-";
			// timeout is only meaningful if there is at least one outstanding
			// request to the peer
			if (i->download_queue_length > 0)
				snprintf(req_timeout, sizeof(req_timeout), "%d", i->request_timeout);

			snprintf(str, sizeof(str), "%8d %4d %7s %6d "
				, int(total_seconds(i->last_active))
				, int(total_seconds(i->last_request))
				, req_timeout
				, int(total_seconds(i->download_queue_time)));
			out += str;
		}
		snprintf(str, sizeof(str), "%s|%s %5d "
			, add_suffix(i->pending_disk_bytes).c_str()
			, add_suffix(i->pending_disk_read_bytes).c_str()
			, i->rtt);
		out += str;

		if (print_block)
		{
			if (i->downloading_piece_index >= 0)
			{
				char buf[50];
				snprintf(buf, sizeof(buf), "%d:%d", i->downloading_piece_index, i->downloading_block_index);
				out += progress_bar(
					i->downloading_progress * 1000 / i->downloading_total, 14, col_green, '-', '#', buf);
			}
			else
			{
				out += progress_bar(0, 14);
			}
		}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		if (i->country[0] == 0)
		{
			out += " ..";
		}
		else
		{
			snprintf(str, sizeof(str), " %c%c", i->country[0], i->country[1]);
			out += str;
		}
#endif
		if (print_peer_rate)
		{
			bool unchoked = (i->flags & peer_info::choked) == 0;

			snprintf(str, sizeof(str), " %s %s"
				, add_suffix(i->remote_dl_rate, "/s").c_str()
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

int listen_port = 6881;
int allocation_mode = libtorrent::storage_mode_sparse;
std::string save_path(".");
int torrent_upload_limit = 0;
int torrent_download_limit = 0;
std::string monitor_dir;
std::string bind_to_interface = "";
int poll_interval = 5;
int max_connections_per_torrent = 50;
bool seed_mode = false;

bool share_mode = false;
bool disable_storage = false;

bool quit = false;

void signal_handler(int signo)
{
	// make the main loop terminate
	quit = true;
}

void load_torrent(libtorrent::sha1_hash const& ih, std::vector<char>& buf, libtorrent::error_code& ec)
{
	files_t::iterator i = hash_to_filename.find(ih);
	if (i == hash_to_filename.end())
	{
		// for magnet links and torrents downloaded via
		// URL, the metadata is saved in the resume file
		// TODO: pick up metadata from the resume file
		ec.assign(boost::system::errc::no_such_file_or_directory, boost::system::generic_category());
		return;
	}
	load_file(i->second.c_str(), buf, ec);
}

// if non-empty, a peer that will be added to all torrents
std::string peer;

using boost::bind;

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
		getcwd(cwd, sizeof(cwd));
#endif
		f = path_append(cwd, f);
	}

	for (int i = 0; i < f.size(); ++i)
	{
#ifdef TORRENT_WINDOWS
		if (f[i] == '\\') ret.push_back('/');
		else
#endif
		if (std::strchr(unreserved, f[i]) != NULL) ret.push_back(f[i]);
		else
		{
			ret.push_back('%');
			ret.push_back(hex_chars[f[i] >> 4]);
			ret.push_back(hex_chars[f[i] & 0xf]);
		}
	}
	return ret;
}

// monitored_dir is true if this torrent is added because
// it was found in the directory that is monitored. If it
// is, it should be remembered so that it can be removed
// if it's no longer in that directory.
void add_torrent(libtorrent::session& ses
	, handles_t& files
	, std::set<libtorrent::torrent_handle>& non_files
	, std::string torrent
	, int allocation_mode
	, std::string const& save_path
	, bool monitored_dir
	, int torrent_upload_limit
	, int torrent_download_limit)
{
	using namespace libtorrent;
	static int counter = 0;

	printf("[%d] %s\n", counter++, torrent.c_str());

	add_torrent_params p;
	if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
	if (disable_storage) p.storage = disabled_storage_constructor;
	if (share_mode) p.flags |= add_torrent_params::flag_share_mode;

	std::string filename = path_append(save_path, path_append(".resume"
		, leaf_path(torrent) + ".resume"));

	error_code ec;
	load_file(filename.c_str(), p.resume_data, ec);

	p.url = path_to_url(torrent);
	p.save_path = save_path;
	p.storage_mode = (storage_mode_t)allocation_mode;
	p.flags |= add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_duplicate_is_error;
	p.flags |= add_torrent_params::flag_auto_managed;
	p.userdata = (void*)strdup(torrent.c_str());
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
	if (handle == 0)
	{
		ec.assign(errno, boost::system::generic_category());
		return ret;
	}

	struct dirent de;
	dirent* dummy;
	while (readdir_r(handle, &de, &dummy) == 0)
	{
		if (dummy == 0) break;

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
	for (int i = p.size() - 1; i >= 0; --i)
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
	, handles_t& files 
	, std::set<libtorrent::torrent_handle>& non_files
	, int allocation_mode
	, std::string const& save_path
	, int torrent_upload_limit
	, int torrent_download_limit)
{
	std::set<std::string> valid;

	using namespace libtorrent;

	error_code ec;
	std::vector<std::string> ents = list_dir(dir_path, filter_fun, ec);
	if (ec)
	{
		fprintf(stderr, "failed to list directory: (%s : %d) %s\n"
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
		add_torrent(ses, files, non_files, file, allocation_mode
			, save_path, true, torrent_upload_limit, torrent_download_limit);
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
			files.erase(i++);
			continue;
		}
		
		h.auto_managed(false);
		h.pause();
		// the alert handler for save_resume_data_alert
		// will save it to disk
		if (h.need_save_resume_data())
		{
			h.save_resume_data();
			++num_outstanding_resume_data;
		}

		files.erase(i++);
	}
}

char const* timestamp()
{
	time_t t = std::time(0);
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
		fprintf(g_log_file, "[%s] %s\n", timestamp(),  a->message().c_str());
}

int save_file(std::string const& filename, std::vector<char>& v)
{
	FILE* f = fopen(filename.c_str(), "wb");
	if (f == NULL)
		return -1;

	int w = fwrite(&v[0], 1, v.size(), f);
	if (w < 0)
	{
		fclose(f);
		return -1;
	}

	if (w != int(v.size())) return -3;
	fclose(f);
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
		ses_view.update_counters(s->values, s->timestamp);
		return true;
	}
	
#ifndef TORRENT_DISABLE_DHT
	if (dht_stats_alert* p = alert_cast<dht_stats_alert>(a))
	{
		dht_active_requests.swap(p->active_requests);
		dht_routing_table.swap(p->routing_table);
		return true;
	}
#endif

#ifdef TORRENT_USE_OPENSSL
	if (torrent_need_cert_alert* p = alert_cast<torrent_need_cert_alert>(a))
	{
		torrent_handle h = p->handle;
		std::string base_name = path_append("certificates", to_hex(h.info_hash().to_string()));
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
			snprintf(msg, sizeof(msg), "ERROR. could not load certificate %s: %s\n", cert.c_str(), strerror(errno));
			if (g_log_file) fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
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
			snprintf(msg, sizeof(msg), "ERROR. could not load private key %s: %s\n", priv.c_str(), strerror(errno));
			if (g_log_file) fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
			return true;
		}

		char msg[256];
		snprintf(msg, sizeof(msg), "loaded certificate %s and key %s\n", cert.c_str(), priv.c_str());
		if (g_log_file) fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);

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
			|| pd->error == error_code(errors::timed_out_no_handshake
				, get_libtorrent_category()))
			return true;
	}

	if (metadata_received_alert* p = alert_cast<metadata_received_alert>(a))
	{
		// if we have a monitor dir, save the .torrent file we just received in it
		// also, add it to the files map, and remove it from the non_files list
		// to keep the scan dir logic in sync so it's not removed, or added twice
		torrent_handle h = p->handle;
		if (h.is_valid())
		{
			boost::shared_ptr<const torrent_info> ti = h.torrent_file();
			create_torrent ct(*ti);
			entry te = ct.generate();
			std::vector<char> buffer;
			bencode(std::back_inserter(buffer), te);
			sha1_hash hash = ti->info_hash();
			std::string filename = ti->name() + "." + to_hex(hash.to_string()) + ".torrent";
			filename = path_append(monitor_dir, filename);
			save_file(filename, buffer);

			files.insert(std::pair<std::string, libtorrent::torrent_handle>(filename, h));
			hash_to_filename.insert(std::make_pair(hash, filename));
			non_files.erase(h);
		}
	}
	else if (add_torrent_alert* p = alert_cast<add_torrent_alert>(a))
	{
		std::string filename;
		if (p->params.userdata)
		{
			filename = (char*)p->params.userdata;
			free(p->params.userdata);
		}

		if (p->error)
		{
			fprintf(stderr, "failed to add torrent: %s %s\n", filename.c_str()
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
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
			h.resolve_countries(true);
#endif

			// if we have a peer specified, connect to it
			if (!peer.empty())
			{
				char* port = (char*) strrchr((char*)peer.c_str(), ':');
				if (port > 0)
				{
					*port++ = 0;
					char const* ip = peer.c_str();
					int peer_port = atoi(port);
					error_code ec;
					if (peer_port > 0)
						h.connect_peer(tcp::endpoint(address::from_string(ip, ec), peer_port));
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
		TORRENT_ASSERT(p->resume_data);
		if (p->resume_data)
		{
			std::vector<char> out;
			bencode(std::back_inserter(out), *p->resume_data);
			torrent_status st = h.status(torrent_handle::query_save_path);
			save_file(path_append(st.save_path, path_append(".resume", leaf_path(
				hash_to_filename[st.info_hash]) + ".resume")), out);
			if (h.is_valid()
				&& non_files.find(h) == non_files.end()
				&& std::find_if(files.begin(), files.end()
					, boost::bind(&handles_t::value_type::second, _1) == h) == files.end())
				ses.remove_torrent(h);
		}
	}
	else if (save_resume_data_failed_alert* p = alert_cast<save_resume_data_failed_alert>(a))
	{
		--num_outstanding_resume_data;
		torrent_handle h = p->handle;
		if (h.is_valid()
			&& non_files.find(h) == non_files.end()
			&& std::find_if(files.begin(), files.end()
				, boost::bind(&handles_t::value_type::second, _1) == h) == files.end())
			ses.remove_torrent(h);
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
}

void print_piece(libtorrent::partial_piece_info* pp
	, libtorrent::cached_piece_info* cs
	, std::vector<libtorrent::peer_info> const& peers
	, torrent_status const* ts
	, std::string& out)
{
	using namespace libtorrent;

	char str[1024];
	assert(pp == 0 || cs == 0 || cs->piece == pp->piece_index);
	int piece = pp ? pp->piece_index : cs->piece;
	int num_blocks = pp ? pp->blocks_in_piece : cs->blocks.size();

	snprintf(str, sizeof(str), "%5d:[", piece);
	out += str;
	char const* last_color = 0;
	for (int j = 0; j < num_blocks; ++j)
	{
		int index = pp ? peer_index(pp->blocks[j].peer(), peers) % 36 : -1;
		char chr = '+';
		if (index >= 0)
			chr = (index < 10)?'0' + index:'A' + index - 10;
		bool snubbed = index >= 0 ? peers[index].flags & peer_info::snubbed : false;

		char const* color = "";

		if (pp == 0)
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
				chr = '0' + (pp->blocks[j].bytes_progress * 10 / pp->blocks[j].block_size);
			}
			else if (pp->blocks[j].state == block_info::finished) color = esc("32;7");
			else if (pp->blocks[j].state == block_info::writing) color = esc("36;7");
			else if (pp->blocks[j].state == block_info::requested) color = snubbed ? esc("35;7") : esc("0");
			else { color = esc("0"); chr = ' '; }
		}
		if (last_color == 0 || strcmp(last_color, color) != 0)
			snprintf(str, sizeof(str), "%s%c", color, chr);
		else
			out += chr;

		out += str;
	}
	out += esc("0");
	out += "]";
/*
	char const* cache_kind_str[] = {"read", "write", "read-volatile"};
	snprintf(str, sizeof(str), " %3d cache age: %-5.1f state: %s%s\n"
		, cs ? cs->next_to_hash : 0
		, cs ? (total_milliseconds(clock_type::now() - cs->last_use) / 1000.f) : 0.f
		, cs ? cache_kind_str[cs->kind] : "N/A"
		, ts && ts->pieces.size() ? (ts->pieces[piece] ? " have" : " dont-have") : "");
	out += str;
*/
}

int main(int argc, char* argv[])
{
	if (argc == 1)
	{
		fprintf(stderr, "usage: client_test [OPTIONS] [TORRENT|MAGNETURL|URL]\n\n"
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
			"  -E <num-threads>      specify how many hashing threads to use\n"
			"\n BITTORRENT OPTIONS\n"
			"  -c <limit>            sets the max number of connections\n"
			"  -T <limit>            sets the max number of connections per torrent\n"
			"  -U <rate>             sets per-torrent upload rate\n"
			"  -D <rate>             sets per-torrent download rate\n"
			"  -d <rate>             limits the download rate\n"
			"  -u <rate>             limits the upload rate\n"
			"  -S <limit>            limits the upload slots\n"
			"  -A <num pieces>       allowed pieces set size\n"
			"  -H                    Don't start DHT\n"
			"  -X                    Don't start local peer discovery\n"
			"  -n                    announce to trackers in all tiers\n"
			"  -W <num peers>        Set the max number of peers to keep in the peer list\n"
			"  -B <seconds>          sets the peer timeout\n"
			"  -Q                    enables share mode. Share mode attempts to maximize\n"
			"                        share ratio rather than downloading\n"
			"  -K                    enable piece suggestions of read cache\n"
			"  -r <IP:port>          connect to specified peer\n"
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			"  -e                    force encrypted bittorrent connections\n"
#endif
			"\n QUEING OPTIONS\n"
			"  -v <limit>            Set the max number of active downloads\n"
			"  -^ <limit>            Set the max number of active seeds\n"
			"\n NETWORK OPTIONS\n"
			"  -p <port>             sets the listen port\n"
#ifndef TORRENT_NO_DEPRECATE
			"  -o <limit>            limits the number of simultaneous\n"
			"                        half-open TCP connections to the\n"
			"                        given number.\n"
#endif
			"  -w <seconds>          sets the retry time for failed web seeds\n"
			"  -x <file>             loads an emule IP-filter file\n"
			"  -P <host:port>        Use the specified SOCKS5 proxy\n"
			"  -L <user:passwd>      Use the specified username and password for the\n"
			"                        proxy specified by -P\n"
			"  -h                    allow multiple connections from the same IP\n"
			"  -M                    Disable TCP/uTP bandwidth balancing\n"
			"  -N                    Do not attempt to use UPnP and NAT-PMP to forward ports\n"
			"  -Y                    Rate limit local peers\n"
			"  -y                    Disable TCP connections (disable outgoing TCP and reject\n"
			"                        incoming TCP connections)\n"
			"  -J                    Disable uTP connections (disable outgoing uTP and reject\n"
			"                        incoming uTP connections)\n"
			"  -b <IP>               sets IP of the interface to bind the\n"
			"                        listen socket to\n"
			"  -I <IP>               sets the IP of the interface to bind\n"
			"                        outgoing peer connections to\n"
#if TORRENT_USE_I2P
			"  -i <i2p-host>         the hostname to an I2P SAM bridge to use\n"
#endif
			"  -l <limit>            sets the listen socket queue size\n"
			"\n DISK OPTIONS\n"
			"  -a <mode>             sets the allocation mode. [sparse|allocate]\n"
			"  -R <num blocks>       number of blocks per read cache line\n"
			"  -C <limit>            sets the max cache size. Specified in 16kB blocks\n"
			"  -j                    disable disk read-ahead\n"
			"  -z                    disable piece hash checks (used for benchmarking)\n"
			"  -Z <file>             mmap the disk cache to the specified file, should be an SSD\n"
			"  -0                    disable disk I/O, read garbage and don't flush to disk\n"
			"\n\n"
			"TORRENT is a path to a .torrent file\n"
			"MAGNETURL is a magnet link\n"
			"URL is a url to a torrent file\n"
			"\n"
			"Example for running benchmark:\n\n"
			"  client_test -k -z -N -h -H -M -l 2000 -S 1000 -T 1000 -c 1000 test.torrent\n");
			;
		return 0;
	}

	using namespace libtorrent;
	namespace lt = libtorrent;

	settings_pack settings;
	settings.set_int(settings_pack::active_loaded_limit, 20);
	settings.set_int(settings_pack::choking_algorithm, settings_pack::rate_based_choker);

	int refresh_delay = 500;
	bool start_dht = true;

	std::deque<std::string> events;

	time_point next_dir_scan = clock_type::now();

	// the string is the filename of the .torrent file, but only if
	// it was added through the directory monitor. It is used to
	// be able to remove torrents that were added via the directory
	// monitor when they're not in the directory anymore.
	handles_t files;

	// torrents that were not added via the monitor dir
	std::set<torrent_handle> non_files;

	libtorrent::session ses(fingerprint("LT", LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0)
		, lt::session::add_default_plugins | lt::session::start_default_features
		, alert::all_categories
			& ~(alert::dht_notification
			+ alert::progress_notification
			+ alert::stats_notification
			+ alert::session_log_notification
			+ alert::torrent_log_notification
			+ alert::peer_log_notification
			));

	ses.set_load_function(&load_torrent);

	std::vector<char> in;
	error_code ec;
	if (load_file(".ses_state", in, ec) == 0)
	{
		bdecode_node e;
		if (bdecode(&in[0], &in[0] + in.size(), e, ec) == 0)
			ses.load_state(e);
	}

	// load the torrents given on the commandline

	std::vector<add_torrent_params> magnet_links;
	std::vector<std::string> torrents;

	for (int i = 1; i < argc; ++i)
	{
		if (argv[i][0] != '-')
		{
			// match it against the <hash>@<tracker> format
			if (strlen(argv[i]) > 45
				&& ::is_hex(argv[i], 40)
				&& (strncmp(argv[i] + 40, "@http://", 8) == 0
					|| strncmp(argv[i] + 40, "@udp://", 7) == 0))
			{
				sha1_hash info_hash;
				from_hex(argv[i], 40, (char*)&info_hash[0]);

				add_torrent_params p;
				if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
				if (disable_storage) p.storage = disabled_storage_constructor;
				if (share_mode) p.flags |= add_torrent_params::flag_share_mode;
				p.trackers.push_back(argv[i] + 41);
				p.info_hash = info_hash;
				p.save_path = save_path;
				p.storage_mode = (storage_mode_t)allocation_mode;
				p.flags |= add_torrent_params::flag_paused;
				p.flags &= ~add_torrent_params::flag_duplicate_is_error;
				p.flags |= add_torrent_params::flag_auto_managed;
				p.flags |= add_torrent_params::flag_pinned;
				magnet_links.push_back(p);
				continue;
			}

			torrents.push_back(argv[i]);
			continue;
		}

		// if there's a flag but no argument following, ignore it
		if (argc == i) continue;
		char const* arg = argv[i+1];
		if (arg == NULL) arg = "";

		switch (argv[i][1])
		{
			case 'f': g_log_file = fopen(arg, "w+"); break;
#ifndef TORRENT_NO_DEPRECATE
			case 'o': settings.set_int(settings_pack::half_open_limit, atoi(arg)); break;
#endif
			case 'h': settings.set_bool(settings_pack::allow_multiple_connections_per_ip, true); --i; break;
			case 'p': listen_port = atoi(arg); break;
			case 'k': high_performance_seed(settings); --i; break;
			case 'j': settings.set_bool(settings_pack::use_disk_read_ahead, false); --i; break;
			case 'z': settings.set_bool(settings_pack::disable_hash_checks, true); --i; break;
			case 'K': settings.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache); --i; break;
			case 'B': settings.set_int(settings_pack::peer_timeout, atoi(arg)); break;
			case 'n': settings.set_bool(settings_pack::announce_to_all_tiers, true); --i; break;
			case 'G': seed_mode = true; --i; break;
			case 'E': settings.set_int(settings_pack::hashing_threads, atoi(arg)); break;
			case 'd': settings.set_int(settings_pack::download_rate_limit, atoi(arg) * 1000); break;
			case 'u': settings.set_int(settings_pack::upload_rate_limit, atoi(arg) * 1000); break;
			case 'S':
				settings.set_int(settings_pack::unchoke_slots_limit, atoi(arg));
				settings.set_int(settings_pack::choking_algorithm, settings_pack::fixed_slots_choker);
				break;
			case 'a':
				if (strcmp(arg, "allocate") == 0) allocation_mode = storage_mode_allocate;
				else if (strcmp(arg, "full") == 0) allocation_mode = storage_mode_allocate;
				else if (strcmp(arg, "sparse") == 0) allocation_mode = storage_mode_sparse;
				break;
			case 's': save_path = arg; break;
			case 'U': torrent_upload_limit = atoi(arg) * 1000; break;
			case 'D': torrent_download_limit = atoi(arg) * 1000; break;
			case 'm': monitor_dir = arg; break;
			case 'Q': share_mode = true; --i; break;
			case 'b': bind_to_interface = arg; break;
			case 'w': settings.set_int(settings_pack::urlseed_wait_retry, atoi(arg)); break;
			case 't': poll_interval = atoi(arg); break;
			case 'F': refresh_delay = atoi(arg); break;
			case 'H':
				start_dht = false;
				settings.set_bool(settings_pack::enable_dht, false);
				--i;
				break;
			case 'l': settings.set_int(settings_pack::listen_queue_size, atoi(arg)); break;
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			case 'e':
				{
					settings.set_int(settings_pack::out_enc_policy, settings_pack::pe_forced);
					settings.set_int(settings_pack::in_enc_policy, settings_pack::pe_forced);
					settings.set_int(settings_pack::allowed_enc_level, settings_pack::pe_rc4);
					settings.set_bool(settings_pack::prefer_rc4, true);
					--i;
					break;
				}
#endif
			case 'W':
				settings.set_int(settings_pack::max_peerlist_size, atoi(arg));
				settings.set_int(settings_pack::max_paused_peerlist_size, atoi(arg) / 2);
				break;
			case 'x':
				{
					FILE* filter = fopen(arg, "r");
					if (filter)
					{
						ip_filter fil;
						unsigned int a,b,c,d,e,f,g,h, flags;
						while (fscanf(filter, "%u.%u.%u.%u - %u.%u.%u.%u %u\n", &a, &b, &c, &d, &e, &f, &g, &h, &flags) == 9)
						{
							address_v4 start((a << 24) + (b << 16) + (c << 8) + d);
							address_v4 last((e << 24) + (f << 16) + (g << 8) + h);
							if (flags <= 127) flags = ip_filter::blocked;
							else flags = 0;
							fil.add_rule(start, last, flags);
						}
						ses.set_ip_filter(fil);
						fclose(filter);
					}
				}
				break;
			case 'c': settings.set_int(settings_pack::connections_limit, atoi(arg)); break;
			case 'T': max_connections_per_torrent = atoi(arg); break;
#if TORRENT_USE_I2P
			case 'i':
				{
					settings.set_str(settings_pack::i2p_hostname, arg);
					settings.set_int(settings_pack::i2p_port, 7650);
					settings.set_int(settings_pack::proxy_type, settings_pack::i2p_proxy);
					break;
				}
#endif // TORRENT_USE_I2P
			case 'C':
				settings.set_int(settings_pack::cache_size, atoi(arg));
				settings.set_bool(settings_pack::use_read_cache, atoi(arg) > 0);
				settings.set_int(settings_pack::cache_buffer_chunk_size, atoi(arg) / 100);
				break;
			case 'A': settings.set_int(settings_pack::allowed_fast_set_size, atoi(arg)); break;
			case 'R': settings.set_int(settings_pack::read_cache_line_size, atoi(arg)); break;
			case 'M': settings.set_int(settings_pack::mixed_mode_algorithm, settings_pack::prefer_tcp); --i; break;
			case 'y':
				settings.set_bool(settings_pack::enable_outgoing_tcp, false);
				settings.set_bool(settings_pack::enable_incoming_tcp, false);
				--i;
				break;
			case 'J':
				settings.set_bool(settings_pack::enable_outgoing_utp, false);
				settings.set_bool(settings_pack::enable_incoming_utp, false);
				--i;
				break;
			case 'r': peer = arg; break;
			case 'P':
				{
					char* port = (char*) strrchr(arg, ':');
					if (port == 0)
					{
						fprintf(stderr, "invalid proxy hostname, no port found\n");
						break;
					}
					*port++ = 0;
					settings.set_str(settings_pack::proxy_hostname, arg);
					settings.set_int(settings_pack::proxy_port, atoi(port));
					if (atoi(port) == 0) {
						fprintf(stderr, "invalid proxy port\n");
						break;
					}
					if (settings.get_int(settings_pack::proxy_type) == settings_pack::none)
						settings.set_int(settings_pack::proxy_type, settings_pack::socks5);
				}
				break;
			case 'L':
				{
					char* pw = (char*) strchr(arg, ':');
					if (pw == 0)
					{
						fprintf(stderr, "invalid proxy username and password specified\n");
						break;
					}
					*pw++ = 0;
					settings.set_str(settings_pack::proxy_username, arg);
					settings.set_str(settings_pack::proxy_password, pw);
					settings.set_int(settings_pack::proxy_type, settings_pack::socks5_pw);
				}
				break;
			case 'I': settings.set_str(settings_pack::outgoing_interfaces, arg); break;
			case 'N':
				settings.set_bool(settings_pack::enable_upnp, false);
				settings.set_bool(settings_pack::enable_natpmp, false);
				--i;
				break;
			case 'Y':
				{
					--i;
					ip_filter pcf;
					// 1 is the global peer class. This should be done properly in the future
					pcf.add_rule(address_v4::from_string("0.0.0.0")
						, address_v4::from_string("255.255.255.255"), 1);
#if TORRENT_USE_IPV6
					pcf.add_rule(address_v6::from_string("::")
						, address_v6::from_string("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 1);
#endif
					ses.set_peer_class_filter(pcf);
					break;
				}
			case 'X': settings.set_bool(settings_pack::enable_lsd, false); --i; break;
			case 'Z':
				settings.set_str(settings_pack::mmap_cache, arg);
				settings.set_bool(settings_pack::contiguous_recv_buffer, false);
				break;
			case 'v': settings.set_int(settings_pack::active_downloads, atoi(arg));
				settings.set_int(settings_pack::active_limit, atoi(arg) * 2);
				break;
			case '^':
				settings.set_int(settings_pack::active_seeds, atoi(arg));
				settings.set_int(settings_pack::active_limit, atoi(arg) * 2);
				break;
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
		fprintf(stderr, "failed to create resume file directory: (%d) %s\n"
			, errno, strerror(errno));

	if (bind_to_interface.empty()) bind_to_interface = "0.0.0.0";
	char iface_str[100];
	snprintf(iface_str, sizeof(iface_str), "%s:%d", bind_to_interface.c_str()
		, listen_port);
	settings.set_str(settings_pack::listen_interfaces, iface_str);

#ifndef TORRENT_DISABLE_DHT
	dht_settings dht;
	dht.privacy_lookups = true;
	ses.set_dht_settings(dht);

	if (start_dht)
	{
		settings.set_bool(settings_pack::use_dht_as_fallback, false);

		ses.add_dht_router(std::make_pair(
			std::string("router.bittorrent.com"), 6881));
		ses.add_dht_router(std::make_pair(
			std::string("router.utorrent.com"), 6881));
		ses.add_dht_router(std::make_pair(
			std::string("router.bitcomet.com"), 6881));
	}
#endif

	settings.set_str(settings_pack::user_agent, "client_test/" LIBTORRENT_VERSION);

	ses.apply_settings(settings);

	for (std::vector<add_torrent_params>::iterator i = magnet_links.begin()
		, end(magnet_links.end()); i != end; ++i)
	{
		ses.async_add_torrent(*i);
	}

	for (std::vector<std::string>::iterator i = torrents.begin()
		, end(torrents.end()); i != end; ++i)
	{
		if (std::strstr(i->c_str(), "http://") == i->c_str()
			|| std::strstr(i->c_str(), "https://") == i->c_str()
			|| std::strstr(i->c_str(), "magnet:") == i->c_str())
		{
			add_torrent_params p;
			if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
			if (disable_storage) p.storage = disabled_storage_constructor;
			if (share_mode) p.flags |= add_torrent_params::flag_share_mode;
			p.save_path = save_path;
			p.storage_mode = (storage_mode_t)allocation_mode;
			p.url = *i;

			std::vector<char> buf;
			if (std::strstr(i->c_str(), "magnet:") == i->c_str())
			{
				add_torrent_params tmp;
				ec.clear();
				parse_magnet_uri(*i, tmp, ec);

				if (ec) continue;

				std::string filename = path_append(save_path, path_append(".resume"
					, to_hex(tmp.info_hash.to_string()) + ".resume"));

				load_file(filename.c_str(), p.resume_data, ec);
			}

			printf("adding URL: %s\n", i->c_str());
			ses.async_add_torrent(p);
			continue;
		}

		// if it's a torrent file, open it as usual
		add_torrent(ses, files, non_files, i->c_str()
			, allocation_mode, save_path, false
			, torrent_upload_limit, torrent_download_limit);
	}

	// main loop
	std::vector<peer_info> peers;
	std::vector<partial_piece_info> queue;

	int tick = 0;

#ifndef WIN32
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
#ifdef WIN32
					int c = _getch();
#else
					int c = getc(stdin);
					if (c == EOF) { break; }
					if (c != '[') continue;
					c = getc(stdin);
#endif
					if (c == EOF) break;
					if (c == LEFT_ARROW)
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
					else if (c == RIGHT_ARROW)
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
					else if (c == UP_ARROW)
					{
						// arrow up
						view.arrow_up();
						h = view.get_active_handle();
					}
					else if (c == DOWN_ARROW)
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
					scanf("%4096s", url);

					add_torrent_params p;
					if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
					if (disable_storage) p.storage = disabled_storage_constructor;
					if (share_mode) p.flags |= add_torrent_params::flag_share_mode;
					p.save_path = save_path;
					p.storage_mode = (storage_mode_t)allocation_mode;
					p.url = url;

					std::vector<char> buf;
					if (std::strstr(url, "magnet:") == url)
					{
						add_torrent_params tmp;
						parse_magnet_uri(url, tmp, ec);

						if (ec) continue;

						std::string filename = path_append(save_path, path_append(".resume"
								, to_hex(tmp.info_hash.to_string()) + ".resume"));

						load_file(filename.c_str(), p.resume_data, ec);
					}

					printf("adding URL: %s\n", url);
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
					printf("\n\nARE YOU SURE YOU WANT TO DELETE THE FILES FOR '%s'. THIS OPERATION CANNOT BE UNDONE. (y/N)"
						, st.name.c_str());
					char response = 'n';
					scanf("%c", &response);
					if (response == 'y')
					{
						// also delete the .torrent file from the torrent directory
						handles_t::iterator i = std::find_if(files.begin(), files.end()
							, boost::bind(&handles_t::value_type::second, _1) == st.handle);
						if (i != files.end())
						{
							error_code ec;
							std::string path;
							if (is_absolute_path(i->first)) path = i->first;
							else path = path_append(monitor_dir, i->first);
							if (::remove(path.c_str()) < 0)
								printf("failed to delete .torrent file: %s\n", ec.message().c_str());
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
					std::vector<torrent_status> torrents;
					ses.get_torrent_status(&torrents, &yes, 0);
					for (std::vector<torrent_status>::iterator i = torrents.begin()
						, end(torrents.end()); i != end; ++i)
					{
						if (i->need_save_resume)
						{
							i->handle.save_resume_data();
							++num_outstanding_resume_data;
						}
					}
				}

				if (c == 'o' && h.is_valid())
				{
					torrent_status const& ts = view.get_active_torrent();
					int num_pieces = ts.num_pieces;
					if (num_pieces > 300) num_pieces = 300;
					for (int i = 0; i < num_pieces; ++i)
					{
						h.set_piece_deadline(i, (i+5) * 1000, torrent_handle::alert_when_available);
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
				if (c == 'f') print_file_progress = !print_file_progress;
				if (c == 'P') show_pad_files = !show_pad_files;
				if (c == 'g') show_dht_status = !show_dht_status;
				if (c == 'u') ses_view.print_utp_stats(!ses_view.print_utp_stats());
				if (c == 'x') print_disk_stats = !print_disk_stats;
				// toggle columns
				if (c == '1') print_ip = !print_ip;
				if (c == '2') print_as = !print_as;
				if (c == '3') print_timers = !print_timers;
				if (c == '4') print_block = !print_block;
				if (c == '5') print_peer_rate = !print_peer_rate;
				if (c == '6') print_fails = !print_fails;
				if (c == '7') print_send_bufs = !print_send_bufs;
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
						"[p] pause/unpause selected torrent\n"
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
						"[t] show trackers                               [l] show alert log\n"
						"[P] show pad files (in file list)\n"
						"\n"
						"COLUMN OPTIONS\n"
						"[1] toggle IP column                            [2] toggle AS column\n"
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
		std::deque<alert*> alerts;
		ses.pop_alerts(&alerts);
		std::string now = timestamp();
		for (std::deque<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			TORRENT_TRY
			{
				if (!::handle_alert(ses, *i, files, non_files))
				{
					// if we didn't handle the alert, print it to the log
					std::string event_string;
					print_alert(*i, event_string);
					events.push_back(event_string);
					if (events.size() >= 20) events.pop_front();
				}
			} TORRENT_CATCH(std::exception& e) {}
			delete *i;
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
			snprintf(str, sizeof(str), "DHT nodes: %d DHT cached nodes: %d "
				"total DHT size: %" PRId64 " total observers: %d\n"
				, sess_stat.dht_nodes, sess_stat.dht_node_cache, sess_stat.dht_global_nodes
				, sess_stat.dht_total_allocations);
			out += str;
*/

			int bucket = 0;
			for (std::vector<dht_routing_bucket>::iterator i = dht_routing_table.begin()
				, end(dht_routing_table.end()); i != end; ++i, ++bucket)
			{
				char const* progress_bar =
					"################################"
					"################################"
					"################################"
					"################################";
				char const* short_progress_bar = "--------";
				snprintf(str, sizeof(str)
					, "%3d [%3d, %d] %s%s\x1b[K\n"
					, bucket, i->num_nodes, i->num_replacements
					, progress_bar + (128 - i->num_nodes)
					, short_progress_bar + (8 - (std::min)(8, i->num_replacements)));
				out += str;
				pos += 1;
			}

			for (std::vector<dht_lookup>::iterator i = dht_active_requests.begin()
				, end(dht_active_requests.end()); i != end; ++i)
			{
				snprintf(str, sizeof(str)
					, "  %10s [limit: %2d] "
					"in-flight: %-2d "
					"left: %-3d "
					"1st-timeout: %-2d "
					"timeouts: %-2d "
					"responses: %-2d "
					"last_sent: %-2d\x1b[K\n"
					, i->type
					, i->branch_factor
					, i->outstanding_requests
					, i->nodes_left
					, i->first_timeout
					, i->timeouts
					, i->responses
					, i->last_sent);
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
				std::vector<announce_entry> tr = h.trackers();
				time_point now = clock_type::now();
				for (std::vector<announce_entry>::iterator i = tr.begin()
					, end(tr.end()); i != end; ++i)
				{
					if (pos + 1 >= terminal_height) break;
					snprintf(str, sizeof(str), "%2d %-55s fails: %-3d (%-3d) %s %s %5d \"%s\" %s\x1b[K\n"
						, i->tier, i->url.c_str(), i->fails, i->fail_limit, i->verified?"OK ":"-  "
						, i->updating?"updating"
							:to_string(int(total_seconds(i->next_announce - now)), 8).c_str()
						, int(i->min_announce > now ? total_seconds(i->min_announce - now) : 0)
						, i->last_error ? i->last_error.message().c_str() : ""
						, i->message.c_str());
					out += str;
					pos += 1;
				}
			}

			if (print_downloads)
			{
				h.get_download_queue(queue);

				std::sort(queue.begin(), queue.end(), boost::bind(&partial_piece_info::piece_index, _1)
					< boost::bind(&partial_piece_info::piece_index, _2));

				std::sort(cs.pieces.begin(), cs.pieces.end(), boost::bind(&cached_piece_info::piece, _1)
					> boost::bind(&cached_piece_info::piece, _2));

				int p = 0; // this is horizontal position
				for (std::vector<cached_piece_info>::iterator i = cs.pieces.begin();
					i != cs.pieces.end(); ++i)
				{
					if (pos + 3 >= terminal_height) break;

					partial_piece_info* pp = 0;
					partial_piece_info tmp;
					tmp.piece_index = i->piece;
					std::vector<partial_piece_info>::iterator ppi
						= std::lower_bound(queue.begin(), queue.end(), tmp
						, boost::bind(&partial_piece_info::piece_index, _1)
						< boost::bind(&partial_piece_info::piece_index, _2));
					if (ppi != queue.end() && ppi->piece_index == i->piece) pp = &*ppi;

					print_piece(pp, &*i, peers, &s, out);

					int num_blocks = pp ? pp->blocks_in_piece : i->blocks.size();
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

					print_piece(&*i, 0, peers, &s, out);

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

				snprintf(str, sizeof(str), "%s %s read cache | %s %s downloading | %s %s cached | %s %s flushed | %s %s snubbed\x1b[K\n"
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
				std::vector<boost::int64_t> file_progress;
				h.file_progress(file_progress);
				std::vector<pool_file_status> file_status;
			 	h.file_status(file_status);
				std::vector<int> file_prio = h.file_priorities();
				std::vector<pool_file_status>::iterator f = file_status.begin();
				boost::shared_ptr<const torrent_info> ti = h.torrent_file();

				int p = 0; // this is horizontal position
				for (int i = 0; i < ti->num_files(); ++i)
				{
					if (pos + 1 >= terminal_height) break;

					bool pad_file = ti->files().pad_file_at(i);
					if (pad_file)
					{
						if (show_pad_files)
						{
							snprintf(str, sizeof(str), "\x1b[34m%-70s %s\x1b[0m\x1b[K\n"
								, ti->files().file_name(i).c_str()
								, add_suffix(ti->files().file_size(i)).c_str());
							out += str;
							pos += 1;
						}
						continue;
					}

					int progress = ti->files().file_size(i) > 0
						?file_progress[i] * 1000 / ti->files().file_size(i):1000;

					bool complete = file_progress[i] == ti->files().file_size(i);

					std::string title = ti->files().file_name(i);
					if (!complete)
					{
						snprintf(str, sizeof(str), " (%.1f%%)", progress / 10.f);
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

					snprintf(str, sizeof(str), "%s %7s p: %d ",
						progress_bar(progress, file_progress_width, complete ? col_green : col_yellow, '-', '#'
							, title.c_str()).c_str()
						, add_suffix(file_progress[i]).c_str()
						, file_prio[i]);

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

		fflush(stdout);

		if (!monitor_dir.empty()
			&& next_dir_scan < clock_type::now())
		{
			scan_dir(monitor_dir, ses, files, non_files
				, allocation_mode, save_path, torrent_upload_limit
				, torrent_download_limit);
			next_dir_scan = clock_type::now() + seconds(poll_interval);
		}
	}

	// keep track of the number of resume data
	// alerts to wait for
	int num_paused = 0;
	int num_failed = 0;

	ses.pause();
	printf("saving resume data\n");
	std::vector<torrent_status> temp;
 	ses.get_torrent_status(&temp, &yes, 0);
	for (std::vector<torrent_status>::iterator i = temp.begin();
		i != temp.end(); ++i)
	{
		torrent_status& st = *i;
		if (!st.handle.is_valid())
		{
			printf("  skipping, invalid handle\n");
			continue;
		}
		if (!st.has_metadata)
		{
			printf("  skipping %s, no metadata\n", st.name.c_str());
			continue;
		}
		if (!st.need_save_resume)
		{
			printf("  skipping %s, resume file up-to-date\n", st.name.c_str());
			continue;
		}

		// save_resume_data will generate an alert when it's done
		st.handle.save_resume_data();
		++num_outstanding_resume_data;
		printf("\r%d  ", num_outstanding_resume_data);
	}
	printf("\nwaiting for resume data [%d]\n", num_outstanding_resume_data);

	while (num_outstanding_resume_data > 0)
	{
		alert const* a = ses.wait_for_alert(seconds(10));
		if (a == 0) continue;

		std::deque<alert*> alerts;
		ses.pop_alerts(&alerts);
		std::string now = timestamp();
		for (std::deque<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			// make sure to delete each alert
			std::auto_ptr<alert> a(*i);

			torrent_paused_alert const* tp = alert_cast<torrent_paused_alert>(*i);
			if (tp)
			{
				++num_paused;
				printf("\rleft: %d failed: %d pause: %d "
					, num_outstanding_resume_data, num_failed, num_paused);
				continue;
			}

			if (alert_cast<save_resume_data_failed_alert>(*i))
			{
				++num_failed;
				--num_outstanding_resume_data;
				printf("\rleft: %d failed: %d pause: %d "
					, num_outstanding_resume_data, num_failed, num_paused);
				continue;
			}

			save_resume_data_alert const* rd = alert_cast<save_resume_data_alert>(*i);
			if (!rd) continue;
			--num_outstanding_resume_data;
			printf("\rleft: %d failed: %d pause: %d "
				, num_outstanding_resume_data, num_failed, num_paused);

			if (!rd->resume_data) continue;

			torrent_handle h = rd->handle;
			torrent_status st = h.status(torrent_handle::query_save_path);
			std::vector<char> out;
			bencode(std::back_inserter(out), *rd->resume_data);
			save_file(path_append(st.save_path, path_append(".resume"
				, leaf_path(hash_to_filename[st.info_hash]) + ".resume")), out);
		}
	}

	if (g_log_file) fclose(g_log_file);
	printf("\nsaving session state\n");
	{
		entry session_state;
		ses.save_state(session_state);

		std::vector<char> out;
		bencode(std::back_inserter(out), session_state);
		save_file(".ses_state", out);
	}

	printf("closing session");

	return 0;
}

