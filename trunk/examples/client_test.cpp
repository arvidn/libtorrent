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

#include <iostream>
#include <fstream>
#include <iterator>
#include <exception>

#include "libtorrent/config.hpp"

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/bind.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/extensions/metadata_transfer.hpp"
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

using boost::bind;

#ifdef _WIN32

#if defined(_MSC_VER)
#	define for if (false) {} else for
#endif

#include <windows.h>
#include <conio.h>

bool sleep_and_input(char* c)
{
	Sleep(500);
	if (_kbhit())
	{
		*c = _getch();
		return true;
	}
	return false;
};

void clear_home()
{
	CONSOLE_SCREEN_BUFFER_INFO si;
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(h, &si);
	COORD c = {0, 0};
	DWORD n;
	FillConsoleOutputCharacter(h, ' ', si.dwSize.X * si.dwSize.Y, c, &n);
	SetConsoleCursorPosition(h, c);
}

#else

#include <stdlib.h>
#include <stdio.h>

#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>

#define ANSI_TERMINAL_COLORS

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

bool sleep_and_input(char* c)
{
	// sets the terminal to single-character mode
	// and resets when destructed
	set_keypress s;

	fd_set set;
	FD_ZERO(&set);
	FD_SET(0, &set);
	timeval tv = {0, 500000};
	if (select(1, &set, 0, 0, &tv) > 0)
	{
		*c = getc(stdin);
		return true;
	}
	return false;
}

void clear_home()
{
	std::cout << "\033[2J\033[0;0H";
}

#endif

char const* esc(char const* code)
{
#ifdef ANSI_TERMINAL_COLORS
	// this is a silly optimization
	// to avoid copying of strings
	enum { num_strings = 20 };
	static char buf[num_strings][20];
	static int round_robin = 0;
	char* ret = buf[round_robin];
	++round_robin;
	if (round_robin >= num_strings) round_robin = 0;
	ret[0] = '\033';
	ret[1] = '[';
	int i = 2;
	int j = 0;
	while (code[j]) ret[i++] = code[j++];
	ret[i++] = 'm';
	ret[i++] = 0;
	return ret;
#else
	return "";
#endif
}

std::string to_string(int v, int width)
{
	std::stringstream s;
	s.flags(std::ios_base::right);
	s.width(width);
	s.fill(' ');
	s << v;
	return s.str();
}

std::string const& to_string(float v, int width, int precision = 3)
{
	static std::string ret;
	ret.resize(20);
	int size = std::sprintf(&ret[0], "%*.*f", width, precision, v);
	ret.resize((std::min)(size, width));
	return ret;
}

std::string pos_to_string(float v, int width, int precision = 4)
{
	std::stringstream s;
	s.precision(precision);
	s.flags(std::ios_base::right);
	s.width(width);
	s.fill(' ');
	s << fabs(v);
	return s.str();
}

std::string ratio(float a, float b)
{
	std::stringstream s;
	if (a > b)
	{
		if (b < 0.001f) s << " inf:1";
		else s << pos_to_string(a/b, 4) << ":1";
	}
	else if (a < b)
	{
		if (a < 0.001f) s << " 1:inf";
		else s << "1:" << pos_to_string(b/a, 4);
	}
	else
	{
		s << "   1:1";
	}

	return s.str();
}

std::string const& add_suffix(float val)
{
	static std::string ret;
	const char* prefix[] = {"kB", "MB", "GB", "TB"};
	const int num_prefix = sizeof(prefix) / sizeof(const char*);
	for (int i = 0; i < num_prefix; ++i)
	{
		val /= 1000.f;
		if (fabs(val) < 1000.f)
		{
			ret = to_string(val, 4);
			ret += prefix[i];
			return ret;
		}
	}
	ret = to_string(val, 4);
	ret += "PB";
	return ret;
}

std::string const& piece_bar(std::vector<bool> const& p, int width)
{
	static const char* lookup[] =
	{
		// black, blue, cyan, white
		"40", "44", "46", "47"
	};
	
	double piece_per_char = p.size() / double(width);
	static std::string bar;
	bar.clear();
	bar.reserve(width * 6);
	bar += "[";

	// the [piece, piece + pieces_per_char) range is the pieces that are represented by each character
	double piece = 0;
	for (int i = 0; i < width; ++i, piece += piece_per_char)
	{
		int num_pieces = 0;
		int num_have = 0;
		int end = (std::max)(int(piece + piece_per_char), int(piece) + 1);
		for (int k = int(piece); k < end; ++k, ++num_pieces)
			if (p[k]) ++num_have;
		int color = int(std::ceil(num_have / float(num_pieces) * (sizeof(lookup) / sizeof(lookup[0]) - 1)));
		bar += esc(lookup[color]);
		bar += " ";
	}
	bar += esc("0");
	bar += "]";
	return bar;
}

std::string const& progress_bar(float progress, int width, char const* code = "33")
{
	static std::string bar;
	bar.clear();
	bar.reserve(width + 10);

	int progress_chars = static_cast<int>(progress * width + .5f);
	bar = esc(code);
	std::fill_n(std::back_inserter(bar), progress_chars, '#');
	bar += esc("0");
	std::fill_n(std::back_inserter(bar), width - progress_chars, '-');
	return bar;
}

int peer_index(libtorrent::tcp::endpoint addr, std::vector<libtorrent::peer_info> const& peers)
{
	using namespace libtorrent;
	std::vector<peer_info>::const_iterator i = std::find_if(peers.begin()
		, peers.end(), bind(&peer_info::ip, _1) == addr);
	if (i == peers.end()) return -1;

	return i - peers.begin();
}

void print_peer_info(std::ostream& out, std::vector<libtorrent::peer_info> const& peers)
{
	using namespace libtorrent;
	out << "IP                      down    (total)   up      (total)  sent-req recv flags        source fail hshf sndb         inactive wait disk quota block-progress "
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		"country "
#endif
		"peer-rate client \n";

	for (std::vector<peer_info>::const_iterator i = peers.begin();
		i != peers.end(); ++i)
	{
		if (i->flags & (peer_info::handshake))
			continue;

		out.fill(' ');
		std::stringstream ip;
		ip << i->ip.address().to_string() << ":" << i->ip.port();
		out.width(22);
		out << ip.str() << " ";
		out.width(2);
		out << esc("32") << (i->down_speed > 0 ? add_suffix(i->down_speed) + "/s " : "         ")
			<< "(" << add_suffix(i->total_download) << ") " << esc("0")
			<< esc("31") << (i->up_speed > 0 ? add_suffix(i->up_speed) + "/s ": "         ")
			<< "(" << add_suffix(i->total_upload) << ") " << esc("0")
			<< to_string(i->download_queue_length, 3) << " ("
			<< to_string(i->target_dl_queue_length, 3) << ") "
			<< to_string(i->upload_queue_length, 3) << " "
			<< ((i->flags & peer_info::interesting)?'I':'.')
			<< ((i->flags & peer_info::choked)?'C':'.')
			<< ((i->flags & peer_info::remote_interested)?'i':'.')
			<< ((i->flags & peer_info::remote_choked)?'c':'.')
			<< ((i->flags & peer_info::supports_extensions)?'e':'.')
			<< ((i->flags & peer_info::local_connection)?'l':'r')
			<< ((i->flags & peer_info::seed)?'s':'.')
			<< ((i->flags & peer_info::on_parole)?'p':'.')
			<< ((i->flags & peer_info::optimistic_unchoke)?'O':'.')
			<< ((i->flags & peer_info::reading)?'R':(i->flags & peer_info::waiting_read_quota)?'r':'.')
			<< ((i->flags & peer_info::writing)?'W':(i->flags & peer_info::waiting_write_quota)?'w':'.')
#ifndef TORRENT_DISABLE_ENCRYPTION
			<< ((i->flags & peer_info::rc4_encrypted)?'E':
				(i->flags & peer_info::plaintext_encrypted)?'e':'.')
#else
			<< ".."
#endif
			<< " "
			<< ((i->source & peer_info::tracker)?"T":"_")
			<< ((i->source & peer_info::pex)?"P":"_")
			<< ((i->source & peer_info::dht)?"D":"_")
			<< ((i->source & peer_info::lsd)?"L":"_")
			<< ((i->source & peer_info::resume_data)?"R":"_") << "  "
			<< to_string(i->failcount, 2) << " "
			<< to_string(i->num_hashfails, 2) << " "
			<< to_string(i->used_send_buffer, 6) << " ("<< add_suffix(i->send_buffer_size) << ") "
			<< to_string(total_seconds(i->last_active), 8) << " "
			<< to_string(total_seconds(i->last_request), 4) << " "
			<< add_suffix(i->pending_disk_bytes) << " "
			<< to_string(i->send_quota, 5) << " ";

		if (i->downloading_piece_index >= 0)
		{
			out << progress_bar(
				i->downloading_progress / float(i->downloading_total), 15);
		}
		else
		{
			out << progress_bar(0.f, 15);
		}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		if (i->country[0] == 0)
		{
			out << " ..";
		}
		else
		{
			out << " " << i->country[0] << i->country[1];
		}
#endif
		out << " " << (i->remote_dl_rate > 0 ? add_suffix(i->remote_dl_rate) + "/s ": "         ") << " ";

		if (i->flags & peer_info::handshake)
		{
			out << esc("31") << " waiting for handshake" << esc("0") << "\n";
		}
		else if (i->flags & peer_info::connecting)
		{
			out << esc("31") << " connecting to peer" << esc("0") << "\n";
		}
		else if (i->flags & peer_info::queued)
		{
			out << esc("33") << " queued" << esc("0") << "\n";
		}
		else
		{
			out << " " << i->client << "\n";
		}
	}
}

typedef std::multimap<std::string, libtorrent::torrent_handle> handles_t;

using boost::bind;
using boost::filesystem::path;
using boost::filesystem::exists;
using boost::filesystem::directory_iterator;
using boost::filesystem::extension;


// monitored_dir is true if this torrent is added because
// it was found in the directory that is monitored. If it
// is, it should be remembered so that it can be removed
// if it's no longer in that directory.
void add_torrent(libtorrent::session& ses
	, handles_t& handles
	, std::string const& torrent
	, float preferred_ratio
	, bool compact_mode
	, path const& save_path
	, bool monitored_dir
	, int torrent_upload_limit
	, int torrent_download_limit) try
{
	using namespace libtorrent;

	std::ifstream in(torrent.c_str(), std::ios_base::binary);
	in.unsetf(std::ios_base::skipws);
	entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
	boost::intrusive_ptr<torrent_info> t(new torrent_info(e));

	std::cout << t->name() << "\n";

	entry resume_data;
	try
	{
		std::stringstream s;
		s << t->name() << ".fastresume";
		boost::filesystem::ifstream resume_file(save_path / s.str(), std::ios_base::binary);
		resume_file.unsetf(std::ios_base::skipws);
		resume_data = bdecode(
			std::istream_iterator<char>(resume_file)
			, std::istream_iterator<char>());
	}
	catch (invalid_encoding&) {}
	catch (boost::filesystem::filesystem_error&) {}

	torrent_handle h = ses.add_torrent(t, save_path, resume_data
		, compact_mode ? storage_mode_compact : storage_mode_sparse, false);
	handles.insert(std::make_pair(
		monitored_dir?std::string(torrent):std::string(), h));

	h.set_max_connections(50);
	h.set_max_uploads(-1);
	h.set_ratio(preferred_ratio);
	h.set_sequenced_download_threshold(15);
	h.set_upload_limit(torrent_upload_limit);
	h.set_download_limit(torrent_download_limit);
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
	h.resolve_countries(true);
#endif
}
catch (std::exception&) {};

void scan_dir(path const& dir_path
	, libtorrent::session& ses
	, handles_t& handles
	, float preferred_ratio
	, bool compact_mode
	, path const& save_path
	, int torrent_upload_limit
	, int torrent_download_limit)
{
	std::set<std::string> valid;

	using namespace libtorrent;

	for (directory_iterator i(dir_path), end; i != end; ++i)
	{
		if (extension(*i) != ".torrent") continue;
		std::string file = i->string();

		handles_t::iterator k = handles.find(file);
		if (k != handles.end())
		{
			valid.insert(file);
			continue;
		}

		// the file has been added to the dir, start
		// downloading it.
		add_torrent(ses, handles, file, preferred_ratio, compact_mode
			, save_path, true, torrent_upload_limit, torrent_download_limit);
		valid.insert(file);
	}

	// remove the torrents that are no longer in the directory

	for (handles_t::iterator i = handles.begin(); !handles.empty() && i != handles.end();)
	{
		if (i->first.empty() || valid.find(i->first) != valid.end())
		{
			++i;
			continue;
		}

		torrent_handle& h = i->second;
		if (!h.is_valid())
		{
			handles.erase(i++);
			continue;
		}
		
		h.pause();
		if (h.has_metadata())
		{
			entry data = h.write_resume_data();
			std::stringstream s;
			s << h.get_torrent_info().name() << ".fastresume";
			boost::filesystem::ofstream out(h.save_path() / s.str(), std::ios_base::binary);
			out.unsetf(std::ios_base::skipws);
			bencode(std::ostream_iterator<char>(out), data);
		}
		ses.remove_torrent(h);
		handles.erase(i++);
	}
}

int main(int ac, char* av[])
{
#if BOOST_VERSION < 103400
	using boost::filesystem::no_check;
	path::default_name_check(no_check);
#endif

	int listen_port;
	float preferred_ratio;
	int download_limit;
	int upload_limit;
	int torrent_upload_limit;
	int torrent_download_limit;
	int upload_slots_limit;
	int half_open_limit;
	std::string save_path_str;
	std::string log_level;
	std::string ip_filter_file;
	std::string allocation_mode;
	std::string in_monitor_dir;
	std::string bind_to_interface;
	std::string proxy;
	std::string proxy_login;
	std::string proxy_type;
	int poll_interval;
	int wait_retry;

	namespace po = boost::program_options;
	try
	{

		po::options_description desc("supported options");
		desc.add_options()
		("help,h", "display this help message")
		("port,p", po::value<int>(&listen_port)->default_value(6881)
			, "set listening port")
		("ratio,r", po::value<float>(&preferred_ratio)->default_value(0)
			, "set the preferred upload/download ratio. 0 means infinite. Values "
			"smaller than 1 are clamped to 1.")
		("max-download-rate,d", po::value<int>(&download_limit)->default_value(0)
			, "the maximum download rate given in kB/s. 0 means infinite.")
		("max-upload-rate,u", po::value<int>(&upload_limit)->default_value(0)
			, "the maximum upload rate given in kB/s. 0 means infinite.")
		("max-torrent-upload-rate", po::value<int>(&torrent_upload_limit)->default_value(20)
			, "the maximum upload rate for an individual torrent, given in kB/s. 0 means infinite.")
		("max-torrent-download-rate", po::value<int>(&torrent_download_limit)->default_value(0)
			, "the maximum download rate for an individual torrent, given in kB/s. 0 means infinite.")
		("max-upload-slots", po::value<int>(&upload_slots_limit)->default_value(8)
			, "the maximum number of upload slots. 0 means infinite.")
		("save-path,s", po::value<std::string>(&save_path_str)->default_value("./")
			, "the path where the downloaded file/folder should be placed.")
		("log-level,l", po::value<std::string>(&log_level)->default_value("info")
			, "sets the level at which events are logged [debug | info | warning | fatal].")
		("ip-filter,f", po::value<std::string>(&ip_filter_file)->default_value("")
			, "sets the path to the ip-filter file used to block access from certain "
			"ips. ")
		("allocation-mode,a", po::value<std::string>(&allocation_mode)->default_value("full")
			, "sets mode used for allocating the downloaded files on disk. "
			"Possible options are [full | compact]")
		("input-file,i", po::value<std::vector<std::string> >()
			, "adds an input .torrent file. At least one is required. arguments "
			"without any flag are implicitly an input file. To start a torrentless "
			"download, use <info-hash>@<tracker-url> instead of specifying a file.")
		("monitor-dir,m", po::value<std::string>(&in_monitor_dir)
			, "monitors the given directory, looking for .torrent files and "
			"automatically starts downloading them. It will stop downloading "
			"torrent files that are removed from the directory")
		("poll-interval,t", po::value<int>(&poll_interval)->default_value(2)
			, "if a directory is being monitored, this is the interval (given "
			"in seconds) between two refreshes of the directory listing")
		("wait-retry,w", po::value<int>(&wait_retry)->default_value(30)
			, "if the download of a url seed failes, this is the interval (given "
			"in seconds) to wait until the next retry")
		("half-open-limit,o", po::value<int>(&half_open_limit)->default_value(-1)
			, "Sets the maximum number of simultaneous half-open tcp connections")
		("bind,b", po::value<std::string>(&bind_to_interface)->default_value("")
			, "Sets the local interface to bind outbound and the listen "
			"socket to")
		("proxy-server,x", po::value<std::string>(&proxy)->default_value("")
			, "Sets the http proxy to be used for tracker and web seeds "
			"connections. The string is expected to be on the form: "
			"<hostname>:<port>. If no port is specified, 8080 is assumed")
		("proxy-login,n", po::value<std::string>(&proxy_login)->default_value("")
			, "Sets the username and password used to authenticate with the http "
			"proxy. The string should be given in the form: <username>:<password>")
		("proxy-type", po::value<std::string>(&proxy_type)->default_value("socks5")
			, "Sets the type of proxy to use [socks5 | http] ")
			;

		po::positional_options_description p;
		p.add("input-file", -1);

		po::variables_map vm;
		po::store(po::command_line_parser(ac, av).
			options(desc).positional(p).run(), vm);
		po::notify(vm);    

		// make sure the arguments stays within the usable limits
		path monitor_dir(in_monitor_dir);
		if (listen_port < 0 || listen_port > 65525) listen_port = 6881;
		if (preferred_ratio != 0 && preferred_ratio < 1.f) preferred_ratio = 1.f;
		upload_limit *= 1000;
		torrent_upload_limit *= 1000;
		torrent_download_limit *= 1000;
		download_limit *= 1000;
		if (download_limit <= 0) download_limit = -1;
		if (upload_limit <= 0) upload_limit = -1;
		if (torrent_upload_limit <= 0) torrent_upload_limit = -1;
		if (torrent_download_limit <= 0) torrent_download_limit = -1;
		if (poll_interval < 2) poll_interval = 2;
		if (wait_retry < 0) wait_retry = 0;
		if (half_open_limit < 1) half_open_limit = -1;
		if (upload_slots_limit <= 0) upload_slots_limit = -1;
		if (!monitor_dir.empty() && !exists(monitor_dir))
		{
			std::cerr << "The monitor directory doesn't exist: " << monitor_dir.string() << std::endl;
			return 1;
		}

		if (vm.count("help")
			|| vm.count("input-file") + vm.count("monitor-dir") == 0)
		{
			std::cout << desc << "\n";
			return 1;
		}

		bool compact_allocation_mode = (allocation_mode == "compact");

		using namespace libtorrent;

		std::vector<std::string> input;
		if (vm.count("input-file") > 0)
			input = vm["input-file"].as< std::vector<std::string> >();

		session_settings settings;
		proxy_settings ps;

		if (!proxy.empty())
		{
			try
			{
				std::size_t i = proxy.find(':');
				ps.hostname = proxy.substr(0, i);
				if (i == std::string::npos) ps.port = 8080;
				else ps.port = boost::lexical_cast<int>(
					proxy.substr(i + 1));
				if (proxy_type == "socks5")
					ps.type = proxy_settings::socks5;
				else
					ps.type = proxy_settings::http;
			}
			catch (std::exception&)
			{
				std::cerr << "Proxy hostname did not match the required format: "
					<< proxy << std::endl;
				return 1;
			}

			if (!proxy_login.empty())
			{
				std::size_t i = proxy_login.find(':');
				if (i == std::string::npos)
				{
					std::cerr << "Proxy login did not match the required format: "
					<< proxy_login << std::endl;
					return 1;
				}
				ps.username = proxy_login.substr(0, i);
				ps.password = proxy_login.substr(i + 1);
				if (proxy_type == "socks5")
					ps.type = proxy_settings::socks5_pw;
				else
					ps.type = proxy_settings::http_pw;
			}
		}

		settings.user_agent = "client_test/" LIBTORRENT_VERSION;
		settings.urlseed_wait_retry = wait_retry;

		std::deque<std::string> events;

		ptime next_dir_scan = time_now();

		// the string is the filename of the .torrent file, but only if
		// it was added through the directory monitor. It is used to
		// be able to remove torrents that were added via the directory
		// monitor when they're not in the directory anymore.
		handles_t handles;
		session ses;
		// UPnP port mapping
		ses.start_upnp();
		// NAT-PMP port mapping
		ses.start_natpmp();
		// Local service discovery (finds peers on the local network)
		ses.start_lsd();
		ses.add_extension(&create_metadata_plugin);
		ses.add_extension(&create_ut_pex_plugin);
		ses.add_extension(&create_ut_metadata_plugin);
		ses.add_extension(&create_smart_ban_plugin);

		ses.set_max_uploads(upload_slots_limit);
		ses.set_max_half_open_connections(half_open_limit);
		ses.set_download_rate_limit(download_limit);
		ses.set_upload_rate_limit(upload_limit);
		ses.listen_on(std::make_pair(listen_port, listen_port + 10)
			, bind_to_interface.c_str());
		ses.set_settings(settings);
		ses.set_tracker_proxy(ps);
		ses.set_peer_proxy(ps);
		ses.set_web_seed_proxy(ps);

		if (log_level == "debug")
			ses.set_severity_level(alert::debug);
		else if (log_level == "warning")
			ses.set_severity_level(alert::warning);
		else if (log_level == "fatal")
			ses.set_severity_level(alert::fatal);
		else
			ses.set_severity_level(alert::info);

#ifndef TORRENT_DISABLE_DHT
		settings.use_dht_as_fallback = false;

		boost::filesystem::ifstream dht_state_file(".dht_state"
			, std::ios_base::binary);
		dht_state_file.unsetf(std::ios_base::skipws);
		entry dht_state;
		try
		{
			dht_state = bdecode(
				std::istream_iterator<char>(dht_state_file)
				, std::istream_iterator<char>());
		}
		catch (std::exception&) {}
		ses.start_dht(dht_state);
		ses.add_dht_router(std::make_pair(std::string("router.bittorrent.com")
			, 6881));
		ses.add_dht_router(std::make_pair(std::string("router.utorrent.com")
			, 6881));
		ses.add_dht_router(std::make_pair(std::string("router.bitcomet.com")
			, 6881));
#endif

		// look for ipfilter.dat
		// poor man's parser
		// reads emule ipfilter files.
		// with the following format:
		// 
		// <first-ip> - <last-ip> , <access> , <comment>
		// 
		// first-ip is an ip address that defines the first
		// address of the range
		// last-ip is the last ip address in the range
		// access is a number specifying the access control
		// for this ip-range. Right now values > 127 = allowed
		// and numbers <= 127 = blocked
		// the rest of the line is ignored
		//
		// In the original spec ranges may not overlap, but
		// here ranges may overlap, and it is the last added
		// rule that has precedence for addresses that may fall
		// into more than one range.
		if (!ip_filter_file.empty())
		{
			std::ifstream in(ip_filter_file.c_str());
			ip_filter filter;
			while (in.good())
			{
				char line[300];
				in.getline(line, 300);
				int len = in.gcount();
				if (len <= 0) continue;
				if (line[0] == '#') continue;
				int a, b, c, d;
				char dummy;
				std::stringstream ln(line);
				ln >> a >> dummy >> b >> dummy >> c >> dummy >> d >> dummy;
				address_v4 start((a << 24) + (b << 16) + (c << 8) + d);
				ln >> a >> dummy >> b >> dummy >> c >> dummy >> d >> dummy;
				address_v4 last((a << 24) + (b << 16) + (c << 8) + d);
				int flags;
				ln >> flags;
				if (flags <= 127) flags = ip_filter::blocked;
				else flags = 0;
				if (ln.fail()) break;
				filter.add_rule(start, last, flags);
			}
			ses.set_ip_filter(filter);
		}
		boost::filesystem::path save_path(save_path_str);

		// load the torrents given on the commandline
		boost::regex ex("([0-9A-Fa-f]{40})@(.+)");
		for (std::vector<std::string>::const_iterator i = input.begin();
			i != input.end(); ++i)
		{
			try
			{
				// first see if this is a torrentless download
				if (i->substr(0, 7) == "magnet:")
				{
					std::cout << "adding MANGET link: " << *i << std::endl;
					torrent_handle h = add_magnet_uri(ses, *i, save_path
						, compact_allocation_mode ? storage_mode_compact
						: storage_mode_sparse);

					handles.insert(std::make_pair(std::string(), h));

					h.set_max_connections(50);
					h.set_max_uploads(-1);
					h.set_ratio(preferred_ratio);
					h.set_sequenced_download_threshold(15);
					h.set_upload_limit(torrent_upload_limit);
					h.set_download_limit(torrent_download_limit);
					continue;
				}
				boost::cmatch what;
				if (boost::regex_match(i->c_str(), what, ex))
				{
					sha1_hash info_hash = boost::lexical_cast<sha1_hash>(what[1]);

					torrent_handle h = ses.add_torrent(std::string(what[2]).c_str()
						, info_hash, 0, save_path, entry(), compact_allocation_mode ? storage_mode_compact
						: storage_mode_sparse);
					handles.insert(std::make_pair(std::string(), h));

					h.set_max_connections(50);
					h.set_max_uploads(-1);
					h.set_ratio(preferred_ratio);
					h.set_sequenced_download_threshold(15);
					h.set_upload_limit(torrent_upload_limit);
					h.set_download_limit(torrent_download_limit);
					continue;
				}
				// if it's a torrent file, open it as usual
				add_torrent(ses, handles, i->c_str(), preferred_ratio
					, compact_allocation_mode ? storage_mode_compact : storage_mode_sparse
					, save_path, false, torrent_upload_limit, torrent_download_limit);
			}
			catch (std::exception& e)
			{
				std::cout << e.what() << "\n";
			}
		}

		// main loop
		std::vector<peer_info> peers;
		std::vector<partial_piece_info> queue;

		bool print_peers = false;
		bool print_log = false;
		bool print_downloads = false;
		bool print_piece_bar = false;
		bool print_file_progress = false;

		for (;;)
		{
			char c;
			if (sleep_and_input(&c))
			{
				if (c == 'q')
				{
					for (handles_t::iterator i = handles.begin();
						i != handles.end(); ++i)
					{
						torrent_handle& h = i->second;
						if (!h.is_valid() || !h.has_metadata()) continue;

						h.pause();

						entry data = h.write_resume_data();
						std::stringstream s;
						s << h.get_torrent_info().name() << ".fastresume";
						boost::filesystem::ofstream out(h.save_path() / s.str(), std::ios_base::binary);
						out.unsetf(std::ios_base::skipws);
						bencode(std::ostream_iterator<char>(out), data);
						ses.remove_torrent(h);
					}
					break;
				}

				if(c == 'r')
				{
					// force reannounce on all torrents
					std::for_each(handles.begin(), handles.end()
						, bind(&torrent_handle::force_reannounce
						, bind(&handles_t::value_type::second, _1)));
				}

				if(c == 'p')
				{
					// pause all torrents
					std::for_each(handles.begin(), handles.end()
						, bind(&torrent_handle::pause
						, bind(&handles_t::value_type::second, _1)));
				}

				if(c == 'u')
				{
					// unpause all torrents
					std::for_each(handles.begin(), handles.end()
						, bind(&torrent_handle::resume
						, bind(&handles_t::value_type::second, _1)));
				}

				if (c == 'i') print_peers = !print_peers;
				if (c == 'l') print_log = !print_log;
				if (c == 'd') print_downloads = !print_downloads;
				if (c == 'f') print_file_progress = !print_file_progress;
				if (c == 'a') print_piece_bar = !print_piece_bar;
			}

			int terminal_width = 80;

#ifndef _WIN32
			{
				winsize size;
				ioctl(STDOUT_FILENO, TIOCGWINSZ, (char*)&size);
				terminal_width = size.ws_col;
			}
#endif

			// loop through the alert queue to see if anything has happened.
			std::auto_ptr<alert> a;
			a = ses.pop_alert();
			std::string now = time_now_string();
			while (a.get())
			{
				std::stringstream event_string;
				if (a->severity() == alert::fatal)
					event_string << esc("31"); // red
				else if (a->severity() == alert::warning)
					event_string << esc("33"); // yellow

				event_string << now << ": ";
				if (torrent_finished_alert* p = dynamic_cast<torrent_finished_alert*>(a.get()))
				{
					p->handle.set_max_connections(30);

					// write resume data for the finished torrent
					torrent_handle h = p->handle;
					entry data = h.write_resume_data();
					std::stringstream s;
					s << h.get_torrent_info().name() << ".fastresume";
					boost::filesystem::ofstream out(h.save_path() / s.str(), std::ios_base::binary);
					out.unsetf(std::ios_base::skipws);
					bencode(std::ostream_iterator<char>(out), data);

					event_string << p->handle.get_torrent_info().name() << ": "
						<< a->msg();
				}
				else if (peer_error_alert* p = dynamic_cast<peer_error_alert*>(a.get()))
				{
					event_string << identify_client(p->pid) << ": " << a->msg();
				}
				else if (invalid_request_alert* p = dynamic_cast<invalid_request_alert*>(a.get()))
				{
					event_string << identify_client(p->pid) << ": " << a->msg();
				}
				else if (tracker_warning_alert* p = dynamic_cast<tracker_warning_alert*>(a.get()))
				{
					event_string << "tracker message: " << p->msg();
				}
				else if (tracker_reply_alert* p = dynamic_cast<tracker_reply_alert*>(a.get()))
				{
					event_string << p->msg() << " (" << p->num_peers << ")";
				}
				else if (url_seed_alert* p = dynamic_cast<url_seed_alert*>(a.get()))
				{
					event_string << "web seed '" << p->url << "': " << p->msg();
				}
				else if (peer_blocked_alert* p = dynamic_cast<peer_blocked_alert*>(a.get()))
				{
					event_string << "(" << p->ip << ") " << p->msg();
				}
				else if (torrent_alert* p = dynamic_cast<torrent_alert*>(a.get()))
				{
					std::string name;
					try { name = p->handle.name(); } catch (std::exception&) {}
					event_string << "(" << name << ") " << p->msg();
				}
				else
				{
					event_string << a->msg();
				}
				event_string << esc("0");
				events.push_back(event_string.str());

				if (events.size() >= 20) events.pop_front();
				a = ses.pop_alert();
			}

			session_status sess_stat = ses.status();
			
			std::stringstream out;
			for (handles_t::iterator i = handles.begin();
				i != handles.end();)
			{
				torrent_handle& h = i->second;
				if (!h.is_valid())
				{
					handles.erase(i++);
					continue;
				}
				else
				{
					++i;
				}

				out << "- " << esc("37") << std::setw(40)
					<< std::setiosflags(std::ios::left);
				if (h.has_metadata())
				{
					std::string name = h.get_torrent_info().name();
					if (name.size() > 40) name.resize(40);
					out << name;
				}
				else
				{
					out << "-";
				}
				out << esc("0") << " ";
				torrent_status s = h.status();

				if (s.state != torrent_status::seeding)
				{
					static char const* state_str[] =
						{"queued", "checking", "connecting", "downloading metadata"
						, "downloading", "finished", "seeding", "allocating"};
					out << state_str[s.state] << " ";
				}

				if ((print_downloads && s.state != torrent_status::seeding)
					|| print_peers)
					h.get_peer_info(peers);

				if (s.state != torrent_status::seeding)
				{
					char const* progress_bar_color = "33"; // yellow
					if (s.state == torrent_status::checking_files
						|| s.state == torrent_status::downloading_metadata)
					{
						progress_bar_color = "35"; // magenta
					}
					else if (s.current_tracker.empty())
					{
						progress_bar_color = "31"; // red
					}
					else if (sess_stat.has_incoming_connections)
					{
						progress_bar_color = "32"; // green
					}
					out.precision(4);
					out.width(5);
					out.fill(' ');
					out << (s.progress*100) << "% ";
					out << progress_bar(s.progress, terminal_width - 63, progress_bar_color);
					out << "\n";
					out << "  total downloaded: " << esc("32") << s.total_done << esc("0") << " Bytes ";
					out	<< "peers: " << s.num_peers << " "
						<< "seeds: " << s.num_seeds << " "
						<< "distributed copies: " << s.distributed_copies << "\n"
						<< "  magnet-link: " << make_magnet_uri(h) << "\n"
						<< "  download: " << esc("32") << (s.download_rate > 0 ? add_suffix(s.download_rate) + "/s ": "         ") << esc("0")
						<< "(" << esc("32") << add_suffix(s.total_download) << esc("0") << ") ";
				}
				else
				{
					out << "download: " << "(" << esc("32") << add_suffix(s.total_download) << esc("0") << ") ";
				}
				out << "upload: " << esc("31") << (s.upload_rate > 0 ? add_suffix(s.upload_rate) + "/s ": "         ") << esc("0")
					<< "(" << esc("31") << add_suffix(s.total_upload) << esc("0") << ") "
					<< "ratio: " << ratio(s.total_payload_download, s.total_payload_upload) << "\n";
				if (s.state != torrent_status::seeding)
				{
					boost::posix_time::time_duration t = s.next_announce;
					out << "  next announce: " << esc("37")
						<< to_string(t.hours(),2) << ":"
						<< to_string(t.minutes(),2) << ":"
						<< to_string(t.seconds(), 2) << esc("0") << " ";
					out << "tracker: " << s.current_tracker << "\n";
					if (print_piece_bar && s.progress < 1.f && s.pieces)
						out << piece_bar(*s.pieces, terminal_width - 3) << "\n";
				}

				if (print_peers && !peers.empty())
					print_peer_info(out, peers);

				if (print_downloads && s.state != torrent_status::seeding)
				{
					h.get_download_queue(queue);
					std::sort(queue.begin(), queue.end(), bind(&partial_piece_info::piece_index, _1)
						< bind(&partial_piece_info::piece_index, _2));
					for (std::vector<partial_piece_info>::iterator i = queue.begin();
						i != queue.end(); ++i)
					{
						out << to_string(i->piece_index, 4) << ": [";
						for (int j = 0; j < i->blocks_in_piece; ++j)
						{
							int index = peer_index(i->blocks[j].peer, peers);
							char str[] = "+";
							if (index >= 0)
								str[0] = (index < 10)?'0' + index:'A' + index - 10;

#ifdef ANSI_TERMINAL_COLORS
							if (i->blocks[j].bytes_progress > 0
								&& i->blocks[j].state == block_info::requested)
							{
								if (i->blocks[j].num_peers > 1)
									out << esc("1;7");
								else
									out << esc("33;7");
								out << to_string(i->blocks[j].bytes_progress / float(i->blocks[j].block_size) * 10, 1) << esc("0");
							}
							else if (i->blocks[j].state == block_info::finished) out << esc("32;7") << str << esc("0");
							else if (i->blocks[j].state == block_info::writing) out << esc("35;7") << str << esc("0");
							else if (i->blocks[j].state == block_info::requested) out << str;
							else out << " ";
#else
							if (i->blocks[j].state == block_info::finished) out << "#";
							else if (i->blocks[j].state == block_info::writing) out << "+";
							else if (i->blocks[j].state == block_info::requested) out << str;
							else out << " ";
#endif
						}
						char* piece_state[4] = {"", "slow", "medium", "fast"};
						out << "] " << piece_state[i->piece_state] << "\n";
					}

					out << "___________________________________\n";
				}

				if (print_file_progress
					&& s.state != torrent_status::seeding
					&& h.has_metadata())
				{
					std::vector<float> file_progress;
					h.file_progress(file_progress);
					torrent_info const& info = h.get_torrent_info();
					for (int i = 0; i < info.num_files(); ++i)
					{
						if (file_progress[i] == 1.f)
							out << progress_bar(file_progress[i], 40, "32") << " "
								<< info.file_at(i).path.leaf() << "\n";
						else
							out << progress_bar(file_progress[i], 40, "33") << " "
								<< info.file_at(i).path.leaf() << "\n";
					}

					out << "___________________________________\n";
				}

			}

			out << "==== conns: " << sess_stat.num_peers << " down: " << esc("32") << add_suffix(sess_stat.download_rate) << "/s" << esc("0")
				<< " (" << esc("32") << add_suffix(sess_stat.total_download) << esc("0") << ") "
				" up: " << esc("31") << add_suffix(sess_stat.upload_rate) << "/s " << esc("0")
				<< " (" << esc("31") << add_suffix(sess_stat.total_upload) << esc("0") << ")"
				" unchoked: " << sess_stat.num_unchoked << " / " << sess_stat.allowed_upload_slots
				<< " ====" << std::endl;

			if (print_log)
			{
				for (std::deque<std::string>::iterator i = events.begin();
					i != events.end(); ++i)
				{
					out << *i << "\n";
				}
			}

			clear_home();
			puts(out.str().c_str());

			if (!monitor_dir.empty()
				&& next_dir_scan < time_now())
			{
				scan_dir(monitor_dir, ses, handles, preferred_ratio
					, compact_allocation_mode, save_path, torrent_upload_limit
					, torrent_download_limit);
				next_dir_scan = time_now() + seconds(poll_interval);
			}
		}

#ifndef TORRENT_DISABLE_DHT
		dht_state = ses.dht_state();
		boost::filesystem::ofstream out(".dht_state"
			, std::ios_base::binary);
		out.unsetf(std::ios_base::skipws);
		bencode(std::ostream_iterator<char>(out), dht_state);
#endif
	}
	catch (std::exception& e)
	{
  		std::cout << e.what() << "\n";
	}

	return 0;
}

