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

#include <fstream>
#include <map>
#include <tuple>
#include <functional>
#include <random>

#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/socket_io.hpp" // print_endpoint
#include "libtorrent/socket_type.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/broadcast_socket.hpp" // for supports_ipv6()
#include "libtorrent/hex.hpp" // to_hex

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/context.hpp>
#endif

#ifndef _WIN32
#include <spawn.h>
#include <csignal>
#endif

#define DEBUG_WEB_SERVER 0

#define DLOG if (DEBUG_WEB_SERVER) fprintf

using namespace libtorrent;
namespace lt = libtorrent;

#if defined TORRENT_WINDOWS
#include <conio.h>
#endif

std::uint32_t g_addr = 0x92343023;

void init_rand_address()
{
	g_addr = 0x92343023;
}

address rand_v4()
{
	address_v4 ret;
	do
	{
		g_addr += 0x3080ca;
		ret = address_v4(g_addr);
	} while (is_any(ret) || is_local(ret) || is_loopback(ret));
	return ret;
}

sha1_hash rand_hash()
{
	sha1_hash ret;
	for (int i = 0; i < 20; ++i)
		ret[i] = boost::uint8_t(lt::random(0xff));
	return ret;
}

#if TORRENT_USE_IPV6
address rand_v6()
{
	address_v6::bytes_type bytes;
	for (int i = 0; i < int(bytes.size()); ++i)
		bytes[i] = boost::uint8_t(lt::random(0xff));
	return address_v6(bytes);
}
#endif

static std::uint16_t g_port = 0;

tcp::endpoint rand_tcp_ep()
{
	// make sure we don't procude the same "random" port twice
	g_port = (g_port + 1) % 14038;
	return tcp::endpoint(rand_v4(), g_port + 1024);
}

udp::endpoint rand_udp_ep(libtorrent::address(&rand_addr)())
{
	g_port = (g_port + 1) % 14037;
	return udp::endpoint(rand_addr(), g_port + 1024);
}

std::map<std::string, std::int64_t> get_counters(libtorrent::session& s)
{
	using namespace libtorrent;
	s.post_session_stats();

	std::map<std::string, std::int64_t> ret;
	alert const* a = wait_for_alert(s, session_stats_alert::alert_type
		, "get_counters()");

	TEST_CHECK(a);
	if (!a) return ret;

	session_stats_alert const* sa = alert_cast<session_stats_alert>(a);
	if (!sa) return ret;

	static std::vector<stats_metric> metrics = session_stats_metrics();
	for (int i = 0; i < int(metrics.size()); ++i)
		ret[metrics[i].name] = sa->values[metrics[i].value_index];
	return ret;
}
namespace {
bool should_print(lt::alert* a)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (auto pla = alert_cast<peer_log_alert>(a))
	{
		if (pla->direction != peer_log_alert::incoming_message
			&& pla->direction != peer_log_alert::outgoing_message)
			return false;
	}
#endif
	if (alert_cast<session_stats_alert>(a)
		|| alert_cast<piece_finished_alert>(a)
		|| alert_cast<block_finished_alert>(a)
		|| alert_cast<block_downloading_alert>(a))
	{
		return false;
	}
	return true;
}
}
alert const* wait_for_alert(lt::session& ses, int type, char const* name)
{
	time_point end_time = libtorrent::clock_type::now() + seconds(10);
	while (true)
	{
		time_point now = clock_type::now();
		if (now > end_time) return nullptr;

		alert const* ret = nullptr;

		ses.wait_for_alert(end_time - now);
		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);
		for (auto a : alerts)
		{
			if (should_print(a))
			{
				std::fprintf(stderr, "%s: %s: [%s] %s\n", time_now_string(), name
					, a->what(), a->message().c_str());
			}
			if (a->type() == type && !ret)
			{
				ret = a;
			}
		}
		if (ret) return ret;
	}
	return nullptr;
}

int load_file(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == nullptr)
	{
		ec.assign(errno, boost::system::system_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::system_category());
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
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = int(fread(&v[0], 1, v.size(), f));
	if (r < 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	fclose(f);

	if (r != s) return -3;

	return 0;
}

void save_file(char const* filename, char const* data, int size)
{
	error_code ec;
	file out(filename, file::write_only, ec);
	TEST_CHECK(!ec);
	if (ec)
	{
		std::fprintf(stderr, "ERROR opening file '%s': %s\n", filename, ec.message().c_str());
		return;
	}
	file::iovec_t b = { (void*)data, size_t(size) };
	out.writev(0, &b, 1, ec);
	TEST_CHECK(!ec);
	if (ec)
	{
		std::fprintf(stderr, "ERROR writing file '%s': %s\n", filename, ec.message().c_str());
		return;
	}

}

bool print_alerts(lt::session& ses, char const* name
	, bool allow_disconnects, bool allow_no_torrents, bool allow_failed_fastresume
	, bool (*predicate)(libtorrent::alert const*), bool no_output)
{
	bool ret = false;
	std::vector<torrent_handle> handles = ses.get_torrents();
	TEST_CHECK(!handles.empty() || allow_no_torrents);
	torrent_handle h;
	if (!handles.empty()) h = handles[0];
	std::vector<alert*> alerts;
	ses.pop_alerts(&alerts);
	for (auto a : alerts)
	{
		if (predicate && predicate(a)) ret = true;
		if (peer_disconnected_alert const* p = alert_cast<peer_disconnected_alert>(a))
		{
			std::fprintf(stderr, "%s: %s: [%s] (%s): %s\n", time_now_string(), name, (a)->what(), print_endpoint(p->ip).c_str(), p->message().c_str());
		}
		else if (should_print(a)
			&& !no_output)
		{
			std::fprintf(stderr, "%s: %s: [%s] %s\n", time_now_string(), name, (a)->what(), (a)->message().c_str());
		}

		TEST_CHECK(alert_cast<fastresume_rejected_alert>(a) == nullptr || allow_failed_fastresume);
/*
		peer_error_alert const* pea = alert_cast<peer_error_alert>(a);
		if (pea)
		{
			std::fprintf(stderr, "%s: peer error: %s\n", time_now_string(), pea->error.message().c_str());
			TEST_CHECK((!handles.empty() && h.status().is_seeding)
				|| pea->error.message() == "connecting to peer"
				|| pea->error.message() == "closing connection to ourself"
				|| pea->error.message() == "duplicate connection"
				|| pea->error.message() == "duplicate peer-id"
				|| pea->error.message() == "upload to upload connection"
				|| pea->error.message() == "stopping torrent"
				|| (allow_disconnects && pea->error.message() == "Broken pipe")
				|| (allow_disconnects && pea->error.message() == "Connection reset by peer")
				|| (allow_disconnects && pea->error.message() == "no shared cipher")
				|| (allow_disconnects && pea->error.message() == "End of file."));
		}
*/

		invalid_request_alert const* ira = alert_cast<invalid_request_alert>(a);
		if (ira)
		{
			std::fprintf(stderr, "peer error: %s\n", ira->message().c_str());
			TEST_CHECK(false);
		}
	}
	return ret;
}

bool listen_done = false;
bool listen_alert(libtorrent::alert const* a)
{
	if (alert_cast<listen_failed_alert>(a)
		|| alert_cast<listen_succeeded_alert>(a))
		listen_done = true;
	return true;
}

void wait_for_listen(lt::session& ses, char const* name)
{
	listen_done = false;
	alert const* a = nullptr;
	do
	{
		print_alerts(ses, name, true, true, true, &listen_alert, false);
		if (listen_done) break;
		a = ses.wait_for_alert(milliseconds(500));
	} while (a);
	// we din't receive a listen alert!
	TEST_CHECK(listen_done);
}

bool downloading_done = false;
bool downloading_alert(libtorrent::alert const* a)
{
	state_changed_alert const* sc = alert_cast<state_changed_alert>(a);
	if (sc && sc->state == torrent_status::downloading)
		downloading_done = true;
	return true;
}

void wait_for_downloading(lt::session& ses, char const* name)
{
	time_point start = clock_type::now();
	downloading_done = false;
	alert const* a = nullptr;
	do
	{
		print_alerts(ses, name, true, true, true, &downloading_alert, false);
		if (downloading_done) break;
		if (total_seconds(clock_type::now() - start) > 10) break;
		a = ses.wait_for_alert(seconds(2));
	} while (a);
	if (!downloading_done)
	{
		std::fprintf(stderr, "%s: did not receive a state_changed_alert indicating "
			"the torrent is downloading. waited: %d ms\n"
			, name, int(total_milliseconds(clock_type::now() - start)));
	}
}

void print_ses_rate(float time
	, libtorrent::torrent_status const* st1
	, libtorrent::torrent_status const* st2
	, libtorrent::torrent_status const* st3)
{
	if (st1)
	{
		std::fprintf(stderr, "%3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st1->download_payload_rate / 1000)
			, int(st1->upload_payload_rate / 1000)
			, int(st1->progress * 100)
			, st1->num_peers
			, st1->connect_candidates
			, st1->errc ? (" [" + st1->errc.message() + "]").c_str() : "");
	}
	if (st2)
		std::fprintf(stderr, " : %3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st2->download_payload_rate / 1000)
			, int(st2->upload_payload_rate / 1000)
			, int(st2->progress * 100)
			, st2->num_peers
			, st2->connect_candidates
			, st2->errc ? (" [" + st1->errc.message() + "]").c_str() : "");
	if (st3)
		std::fprintf(stderr, " : %3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st3->download_payload_rate / 1000)
			, int(st3->upload_payload_rate / 1000)
			, int(st3->progress * 100)
			, st3->num_peers
			, st3->connect_candidates
			, st3->errc ? (" [" + st1->errc.message() + "]").c_str() : "");

	std::fprintf(stderr, "\n");
}

#ifdef _WIN32
typedef DWORD pid_type;
#else
typedef pid_t pid_type;
#endif

struct proxy_t
{
	pid_type pid;
	int type;
};

// maps port to proxy type
static std::map<int, proxy_t> running_proxies;

void stop_proxy(int port)
{
	std::fprintf(stderr, "stopping proxy on port %d\n", port);
	// don't shut down proxies until the test is
	// completely done. This saves a lot of time.
	// they're closed at the end of main() by
	// calling stop_all_proxies().
}

// returns 0 on failure, otherwise pid
pid_type async_run(char const* cmdline)
{
#ifdef _WIN32
	char buf[2048];
	std::snprintf(buf, sizeof(buf), "%s", cmdline);

	PROCESS_INFORMATION pi;
	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput= GetStdHandle(STD_OUTPUT_HANDLE);
	startup.hStdError = GetStdHandle(STD_INPUT_HANDLE);
	int ret = CreateProcessA(NULL, buf, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &startup, &pi);

	if (ret == 0)
	{
		int error = GetLastError();
		std::fprintf(stderr, "failed (%d) %s\n", error, error_code(error, system_category()).message().c_str());
		return 0;
	}
	return pi.dwProcessId;
#else
	pid_type p;
	char arg_storage[4096];
	char* argp = arg_storage;
	std::vector<char*> argv;
	argv.push_back(argp);
	for (char const* in = cmdline; *in != '\0'; ++in)
	{
		if (*in != ' ')
		{
			*argp++ = *in;
			continue;
		}
		*argp++ = '\0';
		argv.push_back(argp);
	}
	*argp = '\0';
	argv.push_back(nullptr);

	int ret = posix_spawnp(&p, argv[0], nullptr, nullptr, &argv[0], nullptr);
	if (ret != 0)
	{
		std::fprintf(stderr, "failed (%d) %s\n", errno, strerror(errno));
		return 0;
	}
	return p;
#endif
}

void stop_process(pid_type p)
{
#ifdef _WIN32
	HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, p);
	TerminateProcess(proc, 138);
	CloseHandle(proc);
#else
	std::printf("killing pid: %d\n", p);
	kill(p, SIGKILL);
#endif
}

void stop_all_proxies()
{
	std::map<int, proxy_t> proxies = running_proxies;
	for (std::map<int, proxy_t>::iterator i = proxies.begin()
		, end(proxies.end()); i != end; ++i)
	{
		stop_process(i->second.pid);
		running_proxies.erase(i->second.pid);
	}
}

// returns a port on success and -1 on failure
int start_proxy(int proxy_type)
{
	using namespace libtorrent;

	std::map<int, proxy_t> :: iterator i = running_proxies.begin();
	for (; i != running_proxies.end(); ++i)
	{
		if (i->second.type == proxy_type) { return i->first; }
	}

	int port = 2000 + lt::random(6000);
	error_code ec;
	io_service ios;

	// make sure the port we pick is free
	do {
		++port;
		tcp::socket s(ios);
		s.open(tcp::v4(), ec);
		if (ec) break;
		s.bind(tcp::endpoint(address::from_string("127.0.0.1")
			, boost::uint16_t(port)), ec);
	} while (ec);


	char const* type = "";
	char const* auth = "";
	char const* cmd = "";

	switch (proxy_type)
	{
		case settings_pack::socks4:
			type = "socks4";
			auth = " --allow-v4";
			cmd = "python ../socks.py";
			break;
		case settings_pack::socks5:
			type = "socks5";
			cmd = "python ../socks.py";
			break;
		case settings_pack::socks5_pw:
			type = "socks5";
			auth = " --username testuser --password testpass";
			cmd = "python ../socks.py";
			break;
		case settings_pack::http:
			type = "http";
			cmd = "python ../http.py";
			break;
		case settings_pack::http_pw:
			type = "http";
			auth = " --username testuser --password testpass";
			cmd = "python ../http.py";
			break;
	}
	char buf[512];
	std::snprintf(buf, sizeof(buf), "%s --port %d%s", cmd, port, auth);

	std::fprintf(stderr, "%s starting proxy on port %d (%s %s)...\n", time_now_string(), port, type, auth);
	std::fprintf(stderr, "%s\n", buf);
	pid_type r = async_run(buf);
	if (r == 0) abort();
	proxy_t t = { r, proxy_type };
	running_proxies.insert(std::make_pair(port, t));
	std::fprintf(stderr, "%s launched\n", time_now_string());
	std::this_thread::sleep_for(lt::milliseconds(500));
	return port;
}

using namespace libtorrent;

template <class T>
std::shared_ptr<T> clone_ptr(std::shared_ptr<T> const& ptr)
{
	return std::make_shared<T>(*ptr);
}

unsigned char random_byte()
{ return lt::random(0xff); }

std::vector<char> generate_piece(int const idx, int const piece_size)
{
	using namespace libtorrent;
	std::vector<char> ret(piece_size);

	std::mt19937 rng(idx);
	std::uniform_int_distribution<int> rand(-128, 127);
	for (char& c : ret)
	{
		c = static_cast<char>(rand(rng));
	}
	return ret;
}

lt::file_storage make_file_storage(const int file_sizes[], int num_files
	, int const piece_size, std::string base_name)
{
	using namespace libtorrent;
	file_storage fs;
	for (int i = 0; i != num_files; ++i)
	{
		char filename[200];
		std::snprintf(filename, sizeof(filename), "test%d", i);
		char dirname[200];
		std::snprintf(dirname, sizeof(dirname), "%s%d", base_name.c_str()
			, i / 5);
		std::string full_path = combine_path(dirname, filename);

		fs.add_file(full_path, file_sizes[i]);
	}

	fs.set_piece_length(piece_size);
	fs.set_num_pieces(int((fs.total_size() + piece_size - 1) / piece_size));

	return fs;
}

std::shared_ptr<lt::torrent_info> make_torrent(const int file_sizes[]
	, int const num_files, int const piece_size)
{
	using namespace libtorrent;
	file_storage fs = make_file_storage(file_sizes, num_files, piece_size);

	libtorrent::create_torrent ct(fs, piece_size, 0x4000
		, libtorrent::create_torrent::optimize_alignment);

	for (int i = 0; i < fs.num_pieces(); ++i)
	{
		std::vector<char> piece = generate_piece(i, fs.piece_size(i));
		ct.set_hash(i, hasher(piece).final());
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), ct.generate());
	error_code ec;
	return std::make_shared<torrent_info>(&buf[0], int(buf.size()), ec);
}

void create_random_files(std::string const& path, const int file_sizes[], int num_files)
{
	error_code ec;
	char* random_data = (char*)malloc(300000);
	for (int i = 0; i != num_files; ++i)
	{
		std::generate(random_data, random_data + 300000, random_byte);
		char filename[200];
		std::snprintf(filename, sizeof(filename), "test%d", i);
		char dirname[200];
		std::snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);

		std::string full_path = combine_path(path, dirname);
		create_directory(full_path, ec);
		full_path = combine_path(full_path, filename);

		int to_write = file_sizes[i];
		file f(full_path, file::write_only, ec);
		if (ec) std::fprintf(stderr, "failed to create file \"%s\": (%d) %s\n"
			, full_path.c_str(), ec.value(), ec.message().c_str());
		std::int64_t offset = 0;
		while (to_write > 0)
		{
			int s = (std::min)(to_write, 300000);
			file::iovec_t b = { random_data, size_t(s)};
			f.writev(offset, &b, 1, ec);
			if (ec) std::fprintf(stderr, "failed to write file \"%s\": (%d) %s\n"
				, full_path.c_str(), ec.value(), ec.message().c_str());
			offset += s;
			to_write -= s;
		}
	}
	free(random_data);
}

std::shared_ptr<torrent_info> create_torrent(std::ostream* file
	, char const* name, int piece_size
	, int num_pieces, bool add_tracker, std::string ssl_certificate)
{
	// exercise the path when encountering invalid urls
	char const* invalid_tracker_url = "http:";
	char const* invalid_tracker_protocol = "foo://non/existent-name.com/announce";

	file_storage fs;
	int total_size = piece_size * num_pieces;
	fs.add_file(name, total_size);
	libtorrent::create_torrent t(fs, piece_size);
	if (add_tracker)
	{
		t.add_tracker(invalid_tracker_url);
		t.add_tracker(invalid_tracker_protocol);
	}

	if (!ssl_certificate.empty())
	{
		std::vector<char> file_buf;
		error_code ec;
		int res = load_file(ssl_certificate, file_buf, ec);
		if (ec || res < 0)
		{
			std::fprintf(stderr, "failed to load SSL certificate: %s\n", ec.message().c_str());
		}
		else
		{
			std::string pem;
			std::copy(file_buf.begin(), file_buf.end(), std::back_inserter(pem));
			t.set_root_cert(pem);
		}
	}

	std::vector<char> piece(piece_size);
	for (int i = 0; i < int(piece.size()); ++i)
		piece[i] = (i % 26) + 'A';

	// calculate the hash for all pieces
	int num = t.num_pieces();
	sha1_hash ph = hasher(piece).final();
	for (int i = 0; i < num; ++i)
		t.set_hash(i, ph);

	if (file)
	{
		while (total_size > 0)
		{
			file->write(&piece[0], (std::min)(int(piece.size()), total_size));
			total_size -= int(piece.size());
		}
	}

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);

	entry tor = t.generate();

	bencode(out, tor);
	error_code ec;
	return std::make_shared<torrent_info>(
		&tmp[0], int(tmp.size()), std::ref(ec), 0);
}

std::tuple<torrent_handle, torrent_handle, torrent_handle>
setup_transfer(lt::session* ses1, lt::session* ses2, lt::session* ses3
	, bool clear_files, bool use_metadata_transfer, bool connect_peers
	, std::string suffix, int piece_size
	, std::shared_ptr<torrent_info>* torrent, bool super_seeding
	, add_torrent_params const* p, bool stop_lsd, bool use_ssl_ports
	, std::shared_ptr<torrent_info>* torrent2)
{
	TORRENT_ASSERT(ses1);
	TORRENT_ASSERT(ses2);

	if (stop_lsd)
	{
		settings_pack pack;
		pack.set_bool(settings_pack::enable_lsd, false);
		ses1->apply_settings(pack);
		ses2->apply_settings(pack);
		if (ses3) ses3->apply_settings(pack);
	}

	// This has the effect of applying the global
	// rule to all peers, regardless of if they're local or not
	ip_filter f;
	f.add_rule(address_v4::from_string("0.0.0.0")
		, address_v4::from_string("255.255.255.255")
		, 1 << lt::session::global_peer_class_id);
	ses1->set_peer_class_filter(f);
	ses2->set_peer_class_filter(f);
	if (ses3) ses3->set_peer_class_filter(f);

	settings_pack pack;
	pack.set_int(settings_pack::alert_mask, ~(alert::progress_notification | alert::stats_notification));
	if (ses3) pack.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
	pack.set_int(settings_pack::mixed_mode_algorithm, settings_pack::prefer_tcp);
	pack.set_int(settings_pack::max_failcount, 1);
	peer_id pid;
	std::generate(&pid[0], &pid[0] + 20, random_byte);
	pack.set_str(settings_pack::peer_fingerprint, pid.to_string());
	ses1->apply_settings(pack);
	TORRENT_ASSERT(ses1->id() == pid);

	std::generate(&pid[0], &pid[0] + 20, random_byte);
	TORRENT_ASSERT(ses1->id() != pid);
	pack.set_str(settings_pack::peer_fingerprint, pid.to_string());
	ses2->apply_settings(pack);
	TORRENT_ASSERT(ses2->id() == pid);
	if (ses3)
	{
		std::generate(&pid[0], &pid[0] + 20, random_byte);
		TORRENT_ASSERT(ses1->id() != pid);
		TORRENT_ASSERT(ses2->id() != pid);
		pack.set_str(settings_pack::peer_fingerprint, pid.to_string());
		ses3->apply_settings(pack);
		TORRENT_ASSERT(ses3->id() == pid);
	}

	TORRENT_ASSERT(ses1->id() != ses2->id());
	if (ses3) TORRENT_ASSERT(ses3->id() != ses2->id());

	std::shared_ptr<torrent_info> t;
	if (torrent == nullptr)
	{
		error_code ec;
		create_directory("tmp1" + suffix, ec);
		std::ofstream file(combine_path("tmp1" + suffix, "temporary").c_str());
		t = ::create_torrent(&file, "temporary", piece_size, 9, false);
		file.close();
		if (clear_files)
		{
			remove_all(combine_path("tmp2" + suffix, "temporary"), ec);
			remove_all(combine_path("tmp3" + suffix, "temporary"), ec);
		}
		char ih_hex[41];
		aux::to_hex(t->info_hash(), ih_hex);
		std::fprintf(stderr, "generated torrent: %s tmp1%s/temporary\n", ih_hex, suffix.c_str());
	}
	else
	{
		t = *torrent;
	}

	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	add_torrent_params param;
	param.flags &= ~add_torrent_params::flag_paused;
	param.flags &= ~add_torrent_params::flag_auto_managed;
	if (p) param = *p;
	param.ti = clone_ptr(t);
	param.save_path = "tmp1" + suffix;
	param.flags |= add_torrent_params::flag_seed_mode;
	error_code ec;
	torrent_handle tor1 = ses1->add_torrent(param, ec);
	if (ec)
	{
		std::fprintf(stderr, "ses1.add_torrent: %s\n", ec.message().c_str());
		return std::make_tuple(torrent_handle(), torrent_handle(), torrent_handle());
	}
	tor1.super_seeding(super_seeding);

	// the downloader cannot use seed_mode
	param.flags &= ~add_torrent_params::flag_seed_mode;

	TEST_CHECK(!ses1->get_torrents().empty());

	torrent_handle tor2;
	torrent_handle tor3;

	if (ses3)
	{
		param.ti = clone_ptr(t);
		param.save_path = "tmp3" + suffix;
		tor3 = ses3->add_torrent(param, ec);
		TEST_CHECK(!ses3->get_torrents().empty());
	}

	if (use_metadata_transfer)
	{
		param.ti.reset();
		param.info_hash = t->info_hash();
	}
	else if (torrent2)
	{
		param.ti = clone_ptr(*torrent2);
	}
	else
	{
		param.ti = clone_ptr(t);
	}
	param.save_path = "tmp2" + suffix;

	tor2 = ses2->add_torrent(param, ec);
	TEST_CHECK(!ses2->get_torrents().empty());

	TORRENT_ASSERT(ses1->get_torrents().size() == 1);
	TORRENT_ASSERT(ses2->get_torrents().size() == 1);

//	std::this_thread::sleep_for(lt::milliseconds(100));

	if (connect_peers)
	{
		wait_for_downloading(*ses2, "ses2");

		error_code ec;
		int port = 0;
		if (use_ssl_ports)
		{
			port = ses2->ssl_listen_port();
			std::fprintf(stderr, "%s: ses2->ssl_listen_port(): %d\n", time_now_string(), port);
		}

		if (port == 0)
		{
			port = ses2->listen_port();
			std::fprintf(stderr, "%s: ses2->listen_port(): %d\n", time_now_string(), port);
		}

		std::fprintf(stderr, "%s: ses1: connecting peer port: %d\n"
			, time_now_string(), port);
		tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
			, boost::uint16_t(port)));

		if (ses3)
		{
			// give the other peers some time to get an initial
			// set of pieces before they start sharing with each-other

			wait_for_downloading(*ses3, "ses3");

			port = 0;
			int port2 = 0;
			if (use_ssl_ports)
			{
				port = ses2->ssl_listen_port();
				port2 = ses1->ssl_listen_port();
			}

			if (port == 0) port = ses2->listen_port();
			if (port2 == 0) port2 = ses1->listen_port();

			std::fprintf(stderr, "ses3: connecting peer port: %d\n", port);
			tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec), boost::uint16_t(port)));
			std::fprintf(stderr, "ses3: connecting peer port: %d\n", port2);
				tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec)
					, boost::uint16_t(port2)));
		}
	}

	return std::make_tuple(tor1, tor2, tor3);
}

pid_type web_server_pid = 0;

int start_web_server(bool ssl, bool chunked_encoding, bool keepalive)
{
	int port = 2000 + lt::random(6000);
	error_code ec;
	io_service ios;

	// make sure the port we pick is free
	do {
		++port;
		tcp::socket s(ios);
		s.open(tcp::v4(), ec);
		if (ec) break;
		s.bind(tcp::endpoint(address::from_string("127.0.0.1")
			, boost::uint16_t(port)), ec);
	} while (ec);

	char buf[200];
	std::snprintf(buf, sizeof(buf), "python ../web_server.py %d %d %d %d"
		, port, chunked_encoding , ssl, keepalive);

	std::fprintf(stderr, "%s starting web_server on port %d...\n", time_now_string(), port);

	std::fprintf(stderr, "%s\n", buf);
	pid_type r = async_run(buf);
	if (r == 0) abort();
	web_server_pid = r;
	std::fprintf(stderr, "%s launched\n", time_now_string());
	std::this_thread::sleep_for(lt::milliseconds(500));
	return port;
}

void stop_web_server()
{
	if (web_server_pid == 0) return;
	std::fprintf(stderr, "stopping web server\n");
	stop_process(web_server_pid);
	web_server_pid = 0;
}

tcp::endpoint ep(char const* ip, int port)
{
	error_code ec;
	tcp::endpoint ret(address::from_string(ip, ec), boost::uint16_t(port));
	TEST_CHECK(!ec);
	return ret;
}

libtorrent::address addr(char const* ip)
{
	lt::error_code ec;
	auto ret = lt::address::from_string(ip, ec);
	TEST_CHECK(!ec);
	return ret;
}

libtorrent::address_v4 addr4(char const* ip)
{
	lt::error_code ec;
	auto ret = lt::address_v4::from_string(ip, ec);
	TEST_CHECK(!ec);
	return ret;
}

#if TORRENT_USE_IPV6
libtorrent::address_v6 addr6(char const* ip)
{
	lt::error_code ec;
	auto ret = lt::address_v6::from_string(ip, ec);
	TEST_CHECK(!ec);
	return ret;
}
#endif
