/*

Copyright (c) 2006-2022, Arvid Norberg
Copyright (c) 2015-2017, 2020-2021, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2018, Steven Siloti
Copyright (c) 2017, 2020, AllSeeingEyeTolledEweSew
Copyright (c) 2018, d-komarov
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <map>
#include <tuple>
#include <functional>

#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/http_parser.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/aux_/socket_io.hpp" // print_endpoint
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/ip_helpers.hpp"

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

#ifndef _WIN32
#include <spawn.h>
#include <csignal>
#endif

using namespace lt;

#if defined TORRENT_WINDOWS
#include <conio.h>
#endif

#if defined TORRENT_WINDOWS
#define SEPARATOR "\\"
#else
#define SEPARATOR "/"
#endif

std::shared_ptr<torrent_info> generate_torrent(bool const with_files, bool const with_hashes)
{
	if (with_files)
	{
		error_code ec;
		create_directories("test_resume", ec);
		std::vector<char> a(128 * 1024 * 8);
		std::vector<char> b(128 * 1024);
		ofstream("test_resume/tmp1").write(a.data(), std::streamsize(a.size()));
		ofstream("test_resume/tmp2").write(b.data(), std::streamsize(b.size()));
		ofstream("test_resume/tmp3").write(b.data(), std::streamsize(b.size()));
	}
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_resume/tmp1", 128 * 1024 * 8);
	fs.emplace_back("test_resume/tmp2", 128 * 1024);
	fs.emplace_back("test_resume/tmp3", 128 * 1024);
	lt::create_torrent t(std::move(fs), 128 * 1024);

	t.set_comment("test comment");
	t.set_creator("libtorrent test");
	t.add_tracker("http://torrent_file_tracker.com/announce");
	t.add_url_seed("http://torrent_file_url_seed.com/");

	TEST_CHECK(t.num_pieces() > 0);
	if (with_hashes)
	{
		lt::set_piece_hashes(t, "."
			, [] (lt::piece_index_t) {});
	}
	else
	{
		for (auto const i : t.piece_range())
		{
			sha1_hash ph;
			aux::random_bytes(ph);
			t.set_hash(i, ph);
		}

		for (file_index_t f : t.file_range())
		{
			for (auto const i : t.file_piece_range(f))
			{
				sha256_hash ph;
				aux::random_bytes(ph);
				t.set_hash2(f, i, ph);
			}
		}
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	return std::make_shared<torrent_info>(buf, from_span);
}

namespace {
	std::uint32_t g_addr = 0x92343023;
	address_v6::bytes_type g_addr6
		= {0x93, 0x30, 0x2e, 0xf4, 0x1c, 0x01, 0x3d, 0x8a
		, 0x35, 0x3d, 0x69, 0x10, 0x55, 0x82, 0x9d, 0x2f};
}

void init_rand_address()
{
	g_addr = 0x92343023;
	g_addr6 = address_v6::bytes_type{
		{0x93, 0x30, 0x2e, 0xf4, 0x1c, 0x01, 0x3d, 0x8a
		, 0x35, 0x3d, 0x69, 0x10, 0x55, 0x82, 0x9d, 0x2f}};
}

address rand_v4()
{
	address_v4 ret;
	do
	{
		g_addr += 0x3080ca;
		ret = address_v4(g_addr);
	} while (ret.is_unspecified() || aux::is_local(ret) || ret.is_loopback());
	return ret;
}

sha1_hash rand_hash()
{
	sha1_hash ret;
	aux::random_bytes(ret);
	return ret;
}

sha1_hash to_hash(char const* s)
{
	sha1_hash ret;
	aux::from_hex({s, 40}, ret.data());
	return ret;
}

namespace {
void add_mp(span<std::uint8_t> target, span<std::uint8_t const> add)
{
	TORRENT_ASSERT(target.size() == add.size());
	int carry = 0;
	for (int i = int(target.size()) - 1; i >= 0; --i)
	{
		int const res = carry + int(target[i]) + add[i];
		carry = res >> 8;
		target[i] = std::uint8_t(res & 255);
	}
}
}

address rand_v6()
{
	static address_v6::bytes_type const add{
		{0x93, 0x30, 0x2e, 0xf4, 0x1c, 0x01, 0x3d, 0x8a
		, 0x35, 0x3d, 0x69, 0x10, 0x55, 0x82, 0x9d, 0x23}};

	address_v6 ret;
	do
	{
		add_mp(g_addr6, add);
		ret = address_v6(g_addr6);

	} while (ret.is_unspecified() || aux::is_local(ret) || ret.is_loopback());
	return ret;
}

static std::uint16_t g_port = 0;

tcp::endpoint rand_tcp_ep(lt::address(&rand_addr)())
{
	// make sure we don't produce the same "random" port twice
	g_port = (g_port + 1) % 14038;
	return tcp::endpoint(rand_addr(), g_port + 1024);
}

udp::endpoint rand_udp_ep(lt::address(&rand_addr)())
{
	g_port = (g_port + 1) % 14037;
	return udp::endpoint(rand_addr(), g_port + 1024);
}

bool supports_ipv6()
{
#if defined TORRENT_BUILD_SIMULATOR
	return true;
#elif defined TORRENT_WINDOWS
	TORRENT_TRY {
		error_code ec;
		make_address("::1", ec);
		return !ec;
	} TORRENT_CATCH(std::exception const&) { return false; }
#else
	io_context ios;
	tcp::socket test(ios);
	error_code ec;
	test.open(tcp::v6(), ec);
	if (ec) return false;
	error_code ignore;
	test.bind(tcp::endpoint(make_address_v6("::1", ignore), 0), ec);
	return !bool(ec);
#endif
}

std::map<std::string, std::int64_t> get_counters(lt::session& s)
{
	using namespace lt;
	s.post_session_stats();

	std::map<std::string, std::int64_t> ret;
	alert const* a = wait_for_alert(s, session_stats_alert::alert_type
		, "get_counters()");

	TEST_CHECK(a);
	if (!a) return ret;

	session_stats_alert const* sa = alert_cast<session_stats_alert>(a);
	if (!sa) return ret;

	static std::vector<stats_metric> metrics = session_stats_metrics();
	for (auto const& m : metrics)
		ret[m.name] = sa->counters()[m.value_index];
	return ret;
}
namespace {
bool should_print(lt::alert* a)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (auto pla = alert_cast<peer_log_alert>(a))
	{
		if (pla->direction != peer_log_alert::incoming_message
			&& pla->direction != peer_log_alert::outgoing_message
			&& pla->direction != peer_log_alert::info)
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

alert const* wait_for_alert(lt::session& ses, int type, char const* name
	, pop_alerts const p, lt::time_duration timeout)
{
	// we pop alerts in batches, but we wait for individual messages. This is a
	// cache to keep around alerts that came *after* the one we're waiting for.
	// To let subsequent calls to this function be able to pick those up, despite
	// already being popped off the sessions alert queue.
	static std::map<lt::session*, std::vector<alert*>> cache;
	auto& alerts = cache[&ses];

	time_point const end_time = lt::clock_type::now() + timeout;

	while (true)
	{
		time_point const now = clock_type::now();
		if (now > end_time) return nullptr;

		alert const* ret = nullptr;

		if (alerts.empty())
		{
			ses.wait_for_alert(end_time - now);
			ses.pop_alerts(&alerts);
		}
		for (auto i = alerts.begin(); i != alerts.end(); ++i)
		{
			auto a = *i;
			if (should_print(a))
			{
				std::printf("%s: %s: [%s] %s\n", time_now_string().c_str(), name
					, a->what(), a->message().c_str());
			}
			if (a->type() == type)
			{
				ret = a;
				if (p == pop_alerts::pop_all) alerts.clear();
				else alerts.erase(alerts.begin(), std::next(i));
				return ret;
			}
		}
		alerts.clear();
	}
}

int load_file(std::string const& filename, std::vector<char>& v
	, lt::error_code& ec, int limit)
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

	v.resize(static_cast<std::size_t>(s));
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

bool print_alerts(lt::session& ses, char const* name
	, bool allow_no_torrents, bool allow_failed_fastresume
	, std::function<bool(lt::alert const*)> predicate, bool no_output)
{
	TEST_CHECK(!ses.get_torrents().empty() || allow_no_torrents);
	std::vector<alert*> alerts;
	ses.pop_alerts(&alerts);
	for (auto a : alerts)
	{
		if (peer_disconnected_alert const* p = alert_cast<peer_disconnected_alert>(a))
		{
			std::printf("%s: %s: [%s] (%s): %s\n", time_to_string(a->timestamp()).c_str()
				, name, a->what()
				, print_endpoint(p->endpoint).c_str(), p->message().c_str());
		}
		else if (a->type() == invalid_request_alert::alert_type)
		{
			fprintf(stdout, "peer error: %s\n", a->message().c_str());
			TEST_CHECK(false);
		}
		else if (a->type() == fastresume_rejected_alert::alert_type)
		{
			fprintf(stdout, "resume data error: %s\n", a->message().c_str());
			TEST_CHECK(allow_failed_fastresume);
		}
		else if (should_print(a) && !no_output)
		{
			std::printf("%s: %s: [%s] %s\n", time_now_string().c_str(), name, a->what(), a->message().c_str());
		}

		TEST_CHECK(alert_cast<fastresume_rejected_alert>(a) == nullptr || allow_failed_fastresume);

		invalid_request_alert const* ira = alert_cast<invalid_request_alert>(a);
		if (ira)
		{
			std::printf("peer error: %s\n", ira->message().c_str());
			TEST_CHECK(false);
		}
	}
	return predicate && std::any_of(alerts.begin(), alerts.end(), predicate);
}

void wait_for_listen(lt::session& ses, char const* name)
{
	bool listen_done = false;
	alert const* a = nullptr;
	do
	{
		listen_done = print_alerts(ses, name, true, true, [](lt::alert const* al)
			{ return alert_cast<listen_failed_alert>(al) || alert_cast<listen_succeeded_alert>(al); }
			, false);
		if (listen_done) break;
		a = ses.wait_for_alert(milliseconds(500));
	} while (a);
	// we din't receive a listen alert!
	TEST_CHECK(listen_done);
}

void wait_for_downloading(lt::session& ses, char const* name)
{
	time_point start = clock_type::now();
	bool downloading_done = false;
	alert const* a = nullptr;
	do
	{
		downloading_done = print_alerts(ses, name, true, true
			, [](lt::alert const* al)
			{
				state_changed_alert const* sc = alert_cast<state_changed_alert>(al);
				return sc && sc->state == torrent_status::downloading;
			}, false);
		if (downloading_done) break;
		if (clock_type::now() - start > seconds(30)) break;
		a = ses.wait_for_alert(seconds(5));
	} while (a);
	if (!downloading_done)
	{
		std::printf("%s: did not receive a state_changed_alert indicating "
			"the torrent is downloading. waited: %d ms\n"
			, name, int(total_milliseconds(clock_type::now() - start)));
	}
}

void wait_for_seeding(lt::session& ses, char const* name)
{
	time_point start = clock_type::now();
	bool seeding = false;
	alert const* a = nullptr;
	do
	{
		seeding = print_alerts(ses, name, true, true
			, [](lt::alert const* al)
			{
				state_changed_alert const* sc = alert_cast<state_changed_alert>(al);
				return sc && sc->state == torrent_status::seeding;
			}, false);
		if (seeding) break;
		if (clock_type::now() - start > seconds(30)) break;
		a = ses.wait_for_alert(seconds(5));
	} while (a);
	if (!seeding)
	{
		std::printf("%s: did not receive a state_changed_alert indicating "
			"the torrent is seeding. waited: %d ms\n"
			, name, int(total_milliseconds(clock_type::now() - start)));
	}
}

void print_ses_rate(lt::clock_type::time_point const start_time
	, lt::torrent_status const* st1
	, lt::torrent_status const* st2
	, lt::torrent_status const* st3)
{
	auto const d = lt::clock_type::now() - start_time;
	std::printf("%d.%03ds "
		, int(duration_cast<seconds>(d).count())
		, int(duration_cast<milliseconds>(d).count() % 1000));

	if (st1)
	{
		std::printf("| %dkB/s %dkB/s %d%% %d cc:%d%s"
			, int(st1->download_payload_rate / 1000)
			, int(st1->upload_payload_rate / 1000)
			, int(st1->progress * 100)
			, st1->num_peers
			, st1->connect_candidates
			, st1->errc ? (" [" + st1->errc.message() + "]").c_str() : "");
	}
	if (st2)
		std::printf(" | %dkB/s %dkB/s %d%% %d cc:%d%s"
			, int(st2->download_payload_rate / 1000)
			, int(st2->upload_payload_rate / 1000)
			, int(st2->progress * 100)
			, st2->num_peers
			, st2->connect_candidates
			, st2->errc ? (" [" + st1->errc.message() + "]").c_str() : "");
	if (st3)
		std::printf(" | %dkB/s %dkB/s %d%% %d cc:%d%s"
			, int(st3->download_payload_rate / 1000)
			, int(st3->upload_payload_rate / 1000)
			, int(st3->progress * 100)
			, st3->num_peers
			, st3->connect_candidates
			, st3->errc ? (" [" + st1->errc.message() + "]").c_str() : "");

	std::printf("\n");
}

#ifdef _WIN32
using pid_type = DWORD;
#else
using pid_type = pid_t;
#endif

namespace {

// returns 0 on failure, otherwise pid
pid_type async_run(char const* cmdline)
{
#ifdef _WIN32
	char buf[2048];
	std::snprintf(buf, sizeof(buf), "%s", cmdline);

	std::printf("CreateProcess %s\n", buf);
	PROCESS_INFORMATION pi;
	STARTUPINFOA startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	startup.hStdError = GetStdHandle(STD_OUTPUT_HANDLE);
	int const ret = CreateProcessA(nullptr, buf, nullptr, nullptr, TRUE
		, 0, nullptr, nullptr, &startup, &pi);

	if (ret == 0)
	{
		int const error = GetLastError();
		std::printf("ERROR: (%d) %s\n", error, error_code(error, system_category()).message().c_str());
		return 0;
	}

	DWORD len = sizeof(buf);
	if (QueryFullProcessImageNameA(pi.hProcess, PROCESS_NAME_NATIVE, buf, &len) == 0)
	{
		int const error = GetLastError();
		std::printf("ERROR: QueryFullProcessImageName (%d) %s\n", error
			, error_code(error, system_category()).message().c_str());
	}
	else
	{
		std::printf("launched: %s\n", buf);
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
		std::printf("ERROR (%d) %s\n", errno, strerror(errno));
		return 0;
	}
	return p;
#endif
}

void stop_process(pid_type p)
{
#ifdef _WIN32
	HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, p);
	if (proc == nullptr) return;
	TerminateProcess(proc, 138);
	WaitForSingleObject(proc, 5000);
	CloseHandle(proc);
#else
	std::printf("killing pid: %d\n", p);
	kill(p, SIGKILL);
#endif
}

} // anonymous namespace

struct proxy_t
{
	pid_type pid;
	int type;
};

// maps port to proxy type
static std::map<int, proxy_t> running_proxies;

void stop_proxy(int port)
{
	auto const it = running_proxies.find(port);

	if (it == running_proxies.end()) return;

	std::printf("stopping proxy on port %d\n", port);

	stop_process(it->second.pid);
	running_proxies.erase(it);
}

void stop_all_proxies()
{
	std::map<int, proxy_t> proxies = running_proxies;
	running_proxies.clear();
	for (auto const& i : proxies)
	{
		stop_process(i.second.pid);
	}
}

namespace {

#ifdef TORRENT_BUILD_SIMULATOR
void wait_for_port(int) {}
#else
void wait_for_port(int const port)
{
	// wait until the python program is ready to accept connections
	int i = 0;
	io_context ios;
	for (;;)
	{
		tcp::socket s(ios);
		error_code ec;
		s.open(tcp::v4(), ec);
		if (ec)
		{
			std::printf("ERROR opening probe socket: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			return;
		}
		s.connect(tcp::endpoint(make_address("127.0.0.1")
			, std::uint16_t(port)), ec);
		if (ec == boost::system::errc::connection_refused)
		{
			if (i == 100)
			{
				std::printf("ERROR: somehow the python program still hasn't "
					"opened its socket on port %d\n", port);
				return;
			}
			++i;
			std::this_thread::sleep_for(lt::milliseconds(500));
			continue;
		}
		if (ec)
		{
			std::printf("ERROR connecting probe socket: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			return;
		}
		return;
	}
}
#endif

std::vector<std::string> get_python()
{
	std::vector<std::string> ret;
#ifdef _WIN32
	char dummy[1];
	DWORD const req_size = GetEnvironmentVariable("PYTHON_INTERPRETER", dummy, sizeof(dummy));
	if (req_size > 1 && req_size < 4096)
	{
		std::vector<char> buf(req_size);
		DWORD const sz = GetEnvironmentVariable("PYTHON_INTERPRETER", buf.data(), DWORD(buf.size()));
		if (size_t(sz) == buf.size() - 1) ret.emplace_back(buf.data(), buf.size());
	}
#endif
	ret.push_back("python3");
	ret.push_back("python");
	return ret;
}

int find_available_port()
{
	int port = 2000 + (std::int64_t(::getpid()) + ::unit_test::g_test_idx + std::rand()) % 60000;
	error_code ec;
	io_context ios;

	// make sure the port we pick is free
	do {
		++port;
		tcp::socket s(ios);
		s.open(tcp::v4(), ec);
		if (ec) break;
		s.bind(tcp::endpoint(make_address("127.0.0.1")
			, std::uint16_t(port)), ec);
	} while (ec);
	return port;
}
} // anonymous namespace

// returns a port on success and -1 on failure
int start_proxy(int proxy_type)
{
	using namespace lt;

	std::map<int, proxy_t> :: iterator i = running_proxies.begin();
	for (; i != running_proxies.end(); ++i)
	{
		if (i->second.type == proxy_type) { return i->first; }
	}

	int const port = find_available_port();

	char const* type = "";
	char const* auth = "";
	char const* cmd = "";

	switch (proxy_type)
	{
		case settings_pack::socks4:
			type = "socks4";
			auth = " --allow-v4";
			cmd = ".." SEPARATOR "socks.py";
			break;
		case settings_pack::socks5:
			type = "socks5";
			cmd = ".." SEPARATOR "socks.py";
			break;
		case settings_pack::socks5_pw:
			type = "socks5";
			auth = " --username testuser --password testpass";
			cmd = ".." SEPARATOR "socks.py";
			break;
		case settings_pack::http:
			type = "http";
			cmd = ".." SEPARATOR "http_proxy.py";
			break;
		case settings_pack::http_pw:
			type = "http";
			auth = " --basic-auth testuser:testpass";
			cmd = ".." SEPARATOR "http_proxy.py";
			break;
	}
	std::vector<std::string> python_exes = get_python();
	for (auto const& python_exe : python_exes)
	{
		char buf[1024];
		std::snprintf(buf, sizeof(buf), "%s %s --port %d%s", python_exe.c_str(), cmd, port, auth);

		std::printf("%s starting proxy on port %d (%s %s)...\n", time_now_string().c_str(), port, type, auth);
		std::printf("%s\n", buf);
		pid_type r = async_run(buf);
		if (r == 0) continue;
		proxy_t t = { r, proxy_type };
		running_proxies.insert(std::make_pair(port, t));
		std::printf("%s launched\n", time_now_string().c_str());
		std::this_thread::sleep_for(lt::milliseconds(500));
		wait_for_port(port);
		return port;
	}
	abort();
}

using namespace lt;

std::vector<char> generate_piece(piece_index_t const idx, int const piece_size)
{
	using namespace lt;
	std::vector<char> ret(static_cast<std::size_t>(piece_size));

	std::mt19937 rng(static_cast<std::uint32_t>(static_cast<int>(idx)));
	std::uniform_int_distribution<int> rand(-128, 127);
	for (char& c : ret)
	{
		c = static_cast<char>(rand(rng));
	}
	return ret;
}

lt::file_storage make_file_storage(span<const int> const file_sizes
	, int const piece_size, std::string base_name)
{
	using namespace lt;
	file_storage fs;
	for (std::ptrdiff_t i = 0; i != file_sizes.size(); ++i)
	{
		char filename[200];
		std::snprintf(filename, sizeof(filename), "test%d", int(i));
		char dirname[200];
		std::snprintf(dirname, sizeof(dirname), "%s%d", base_name.c_str()
			, int(i) / 5);
		std::string full_path = combine_path(dirname, filename);

		fs.add_file(full_path, file_sizes[i]);
	}

	fs.set_piece_length(piece_size);
	fs.set_num_pieces(aux::calc_num_pieces(fs));

	return fs;
}

std::shared_ptr<lt::torrent_info> make_torrent(std::vector<lt::create_file_entry> files
	, int piece_size,  lt::create_flags_t const flags)
{
	lt::create_torrent ct(std::move(files), piece_size, flags);

	piece_size = ct.piece_length();

	aux::vector<sha256_hash> tree(merkle_num_nodes(piece_size / default_block_size));

	std::int64_t file_offset = 0;
	std::int64_t const total_size = ct.total_size();
	for (auto const f : ct.file_range())
	{
		if (ct.file_at(f).flags & lt::file_storage::flag_pad_file)
		{
			file_offset += ct.file_at(f).size;
			continue;
		}
		lt::piece_index_t const first_piece(int(file_offset / piece_size));
		std::int64_t piece_offset = static_cast<int>(first_piece) * std::int64_t(piece_size);
		bool const aligned = piece_offset == file_offset;
		file_offset += ct.file_at(f).size;
		lt::piece_index_t const end_piece(int((file_offset + piece_size - 1) / piece_size));
		for (auto piece = first_piece; piece < end_piece; ++piece, piece_offset += piece_size)
		{
			auto const this_piece_size = int(std::min(std::int64_t(piece_size), total_size - piece_offset));
			auto const piece_size2 = int(std::min(std::int64_t(piece_size), file_offset - piece_offset));
			auto const blocks_in_piece = (piece_size2 + lt::default_block_size - 1)
				/ lt::default_block_size;

			std::vector<char> piece_buf = generate_piece(piece, this_piece_size);
			if (aligned
				&& piece_offset + this_piece_size > file_offset
				&& ct.file_at(next(f)).flags & file_storage::flag_pad_file)
			{
				// this piece spans the next file. if it's a pad file, we need
				// to set that part to zeros
				int const pad_start = int(file_offset - piece_offset);
				std::size_t const pad_size = std::size_t(this_piece_size - pad_start);
				TORRENT_ASSERT(pad_start >= 0);
				TORRENT_ASSERT(pad_start < this_piece_size);
				TORRENT_ASSERT(pad_start < this_piece_size);
				std::memset(piece_buf.data() + pad_start, 0, pad_size);
				TORRENT_ASSERT(ct.file_at(next(f)).size + pad_start == this_piece_size);
				TORRENT_ASSERT(ct.file_at(next(f)).size == std::int64_t(pad_size));
			}

			if (!(flags & lt::create_torrent::v1_only))
			{
				for (int j = 0; j < piece_size2; j += default_block_size)
				{
					tree[tree.end_index() - blocks_in_piece + j / default_block_size]
						= hasher256(piece_buf.data() + j, std::min(default_block_size, piece_size2 - j)).final();
				}

				merkle_fill_tree(tree, blocks_in_piece);
				ct.set_hash2(f, piece - first_piece, tree[0]);
			}
			if (!(flags & lt::create_torrent::v2_only))
			{
				ct.set_hash(piece, hasher(piece_buf).final());
			}
		}
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), ct.generate());
	return std::make_shared<torrent_info>(buf, from_span);
}

std::vector<lt::create_file_entry> create_random_files(std::string const& path, span<const int> file_sizes)
{
	std::vector<create_file_entry> fs;
	error_code ec;
	aux::vector<char> random_data(300000);
	for (std::ptrdiff_t i = 0; i != file_sizes.size(); ++i)
	{
		aux::random_bytes(random_data);
		char filename[200];
		std::snprintf(filename, sizeof(filename), "test%d", int(i));
		char dirname[200];
		std::snprintf(dirname, sizeof(dirname), "test_dir%d", int(i) / 5);

		std::string full_path = combine_path(path, dirname);
		lt::create_directories(full_path, ec);
		if (ec) std::printf("create_directory(%s) failed: (%d) %s\n"
			, full_path.c_str(), ec.value(), ec.message().c_str());

		full_path = combine_path(full_path, filename);
		std::printf("creating file: %s\n", full_path.c_str());

		int to_write = file_sizes[i];
		fs.emplace_back(full_path, to_write);
		ofstream f(full_path.c_str());
		while (to_write > 0)
		{
			int const s = std::min(to_write, static_cast<int>(random_data.size()));
			f.write(random_data.data(), s);
			to_write -= s;
		}
	}
	return fs;
}

std::shared_ptr<torrent_info> create_torrent(std::ostream* file
	, char const* name, int piece_size
	, int num_pieces, bool add_tracker, lt::create_flags_t const flags
	, std::string ssl_certificate)
{
	// exercise the path when encountering invalid urls
	char const* invalid_tracker_url = "http:";
	char const* invalid_tracker_protocol = "foo://non/existent-name.com/announce";

	std::vector<lt::create_file_entry> fs;
	int total_size = piece_size * num_pieces;
	fs.emplace_back(name, total_size);
	lt::create_torrent t(std::move(fs), piece_size, flags);
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
			std::printf("failed to load SSL certificate: %s\n", ec.message().c_str());
		}
		else
		{
			std::string pem;
			std::copy(file_buf.begin(), file_buf.end(), std::back_inserter(pem));
			t.set_root_cert(pem);
		}
	}

	aux::vector<char> piece(static_cast<std::size_t>(piece_size));
	for (int i = 0; i < piece.end_index(); ++i)
		piece[i] = (i % 26) + 'A';

	if (!(flags & create_torrent::v2_only))
	{
		// calculate the hash for all pieces
		sha1_hash ph = hasher(piece).final();
		for (auto const i : t.piece_range())
			t.set_hash(i, ph);
	}

	if (!(flags & create_torrent::v1_only))
	{
		int const blocks_in_piece = piece_size / default_block_size;
		aux::vector<sha256_hash> v2tree(merkle_num_nodes(merkle_num_leafs(blocks_in_piece)));
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			sha256_hash const block_hash = hasher256(span<char>(piece).subspan(i * default_block_size, default_block_size)).final();
			v2tree[v2tree.end_index() - merkle_num_leafs(blocks_in_piece) + i] = block_hash;
		}
		merkle_fill_tree(v2tree, merkle_num_leafs(blocks_in_piece));

		for (piece_index_t i(0); i < t.end_piece(); ++i)
			t.set_hash2(file_index_t{ 0 }, i - 0_piece, v2tree[0]);
	}

	if (file)
	{
		while (total_size > 0)
		{
			file->write(piece.data(), std::min(piece.end_index(), total_size));
			total_size -= piece.end_index();
		}
	}

	entry tor = t.generate();

	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), tor);
	error_code ec;
	return std::make_shared<torrent_info>(tmp, ec, from_span);
}

std::tuple<torrent_handle, torrent_handle, torrent_handle>
setup_transfer(lt::session* ses1, lt::session* ses2, lt::session* ses3
	, bool clear_files, bool use_metadata_transfer, bool connect_peers
	, std::string suffix, int piece_size
	, std::shared_ptr<torrent_info>* torrent, bool super_seeding
	, add_torrent_params const* p, bool stop_lsd, bool use_ssl_ports
	, std::shared_ptr<torrent_info>* torrent2, create_flags_t const flags)
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
	f.add_rule(make_address_v4("0.0.0.0")
		, make_address_v4("255.255.255.255")
		, 1 << static_cast<std::uint32_t>(lt::session::global_peer_class_id));
	ses1->set_peer_class_filter(f);
	ses2->set_peer_class_filter(f);
	if (ses3) ses3->set_peer_class_filter(f);

	settings_pack pack;
	if (ses3) pack.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
	pack.set_int(settings_pack::mixed_mode_algorithm, settings_pack::prefer_tcp);
	pack.set_int(settings_pack::max_failcount, 1);
	ses1->apply_settings(pack);
	ses2->apply_settings(pack);
	if (ses3)
	{
		ses3->apply_settings(pack);
	}

	std::shared_ptr<torrent_info> t;
	if (torrent == nullptr)
	{
		error_code ec;
		create_directory("tmp1" + suffix, ec);
		std::string const file_path = combine_path("tmp1" + suffix, "temporary");
		ofstream file(file_path.c_str());
		t = ::create_torrent(&file, "temporary", piece_size, 9, false, flags);
		file.close();
		if (clear_files)
		{
			remove_all(combine_path("tmp2" + suffix, "temporary"), ec);
			remove_all(combine_path("tmp3" + suffix, "temporary"), ec);
		}
		std::printf("generated torrent: %s %s\n", aux::to_hex(t->info_hashes().v2).c_str(), file_path.c_str());
	}
	else
	{
		t = *torrent;
	}

	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	add_torrent_params param;
	param.flags &= ~torrent_flags::paused;
	param.flags &= ~torrent_flags::auto_managed;
	if (p) param = *p;
	param.ti = t;
	param.save_path = "tmp1" + suffix;
	param.flags |= torrent_flags::seed_mode;
	error_code ec;
	torrent_handle tor1 = ses1->add_torrent(param, ec);
	if (ec)
	{
		std::printf("ses1.add_torrent: %s\n", ec.message().c_str());
		return std::make_tuple(torrent_handle(), torrent_handle(), torrent_handle());
	}
	if (super_seeding)
	{
		tor1.set_flags(torrent_flags::super_seeding);
	}

	// the downloader cannot use seed_mode
	param.flags &= ~torrent_flags::seed_mode;

	TEST_CHECK(!ses1->get_torrents().empty());

	torrent_handle tor2;
	torrent_handle tor3;

	if (ses3)
	{
		param.ti = t;
		param.save_path = "tmp3" + suffix;
		tor3 = ses3->add_torrent(param, ec);
		TEST_CHECK(!ses3->get_torrents().empty());
	}

	if (use_metadata_transfer)
	{
		param.ti.reset();
		param.info_hashes = t->info_hashes();
	}
	else if (torrent2)
	{
		param.ti = *torrent2;
	}
	else
	{
		param.ti = t;
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

		int port = 0;
		if (use_ssl_ports)
		{
			port = ses2->ssl_listen_port();
			std::printf("%s: ses2->ssl_listen_port(): %d\n", time_now_string().c_str(), port);
		}

		if (port == 0)
		{
			port = ses2->listen_port();
			std::printf("%s: ses2->listen_port(): %d\n", time_now_string().c_str(), port);
		}

		std::printf("%s: ses1: connecting peer port: %d\n"
			, time_now_string().c_str(), port);
		tor1.connect_peer(tcp::endpoint(make_address("127.0.0.1", ec)
			, std::uint16_t(port)));

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

			std::printf("ses3: connecting peer port: %d\n", port);
			tor3.connect_peer(tcp::endpoint(
					make_address("127.0.0.1", ec), std::uint16_t(port)));
			std::printf("ses3: connecting peer port: %d\n", port2);
				tor3.connect_peer(tcp::endpoint(
					make_address("127.0.0.1", ec)
					, std::uint16_t(port2)));
		}
	}

	return std::make_tuple(tor1, tor2, tor3);
}

namespace {
pid_type web_server_pid = 0;
}

int start_web_server(bool ssl, bool chunked_encoding, bool keepalive, int min_interval)
{
	int const port = find_available_port();

	std::vector<std::string> python_exes = get_python();

	for (auto const& python_exe : python_exes)
	{
		char buf[200];
		std::snprintf(buf, sizeof(buf), "%s .." SEPARATOR "web_server.py %d %d %d %d %d"
			, python_exe.c_str(), port, chunked_encoding, ssl, keepalive, min_interval);

		std::printf("%s starting web_server on port %d...\n", time_now_string().c_str(), port);

		std::printf("%s\n", buf);
		pid_type r = async_run(buf);
		if (r == 0) continue;
		web_server_pid = r;
		std::printf("%s launched\n", time_now_string().c_str());
		std::this_thread::sleep_for(lt::milliseconds(1000));
		wait_for_port(port);
		return port;
	}
	abort();
}

void stop_web_server()
{
	if (web_server_pid == 0) return;
	std::printf("stopping web server\n");
	stop_process(web_server_pid);
	web_server_pid = 0;
}

namespace {
pid_type websocket_server_pid = 0;
}

int start_websocket_server(bool ssl, int min_interval)
{
	int port = 2000 + static_cast<int>(aux::random(6000));
	error_code ec;
	io_context ios;

	// make sure the port we pick is free
	do {
		++port;
		tcp::socket s(ios);
		s.open(tcp::v4(), ec);
		if (ec) break;
		s.bind(tcp::endpoint(make_address("127.0.0.1")
			, std::uint16_t(port)), ec);
	} while (ec);

	std::vector<std::string> python_exes = get_python();

	for (auto const& python_exe : python_exes)
	{
		char buf[200];
		std::snprintf(buf, sizeof(buf), "%s ../websocket_server.py %d %d %d"
			, python_exe.c_str(), port, ssl, min_interval);

		std::printf("%s starting websocket server on port %d...\n", time_now_string().c_str(), port);

		std::printf("%s\n", buf);
		pid_type r = async_run(buf);
		if (r == 0) continue;
		websocket_server_pid = r;
		std::printf("%s launched\n", time_now_string().c_str());
		std::this_thread::sleep_for(lt::milliseconds(1000));
		wait_for_port(port);
		return port;
	}
	abort();
}

void stop_websocket_server()
{
	if (websocket_server_pid == 0) return;
	std::printf("stopping websocket server\n");
	stop_process(websocket_server_pid);
	websocket_server_pid = 0;
}


tcp::endpoint ep(char const* ip, int port)
{
	error_code ec;
	tcp::endpoint ret(make_address(ip, ec), std::uint16_t(port));
	TEST_CHECK(!ec);
	return ret;
}

udp::endpoint uep(char const* ip, int port)
{
	error_code ec;
	udp::endpoint ret(make_address(ip, ec), std::uint16_t(port));
	TEST_CHECK(!ec);
	return ret;
}

lt::address addr(char const* ip)
{
	lt::error_code ec;
	auto ret = lt::make_address(ip, ec);
	TEST_CHECK(!ec);
	return ret;
}

lt::address_v4 addr4(char const* ip)
{
	lt::error_code ec;
	auto ret = lt::make_address_v4(ip, ec);
	TEST_CHECK(!ec);
	return ret;
}

lt::address_v6 addr6(char const* ip)
{
	lt::error_code ec;
	auto ret = lt::make_address_v6(ip, ec);
	TEST_CHECK(!ec);
	return ret;
}
