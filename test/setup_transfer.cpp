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

#include <array>
#include <map>
#include <tuple>
#include <functional>
#include <thread>
#include <future>
#include <string>
#include <string_view>
#include <cstdlib>
#include <charconv>
#include <stdexcept>

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
#include "libtorrent/load_torrent.hpp"

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

#ifndef _WIN32
#include <spawn.h>
#include <csignal>
#include <unistd.h>
#include <poll.h>
#endif

using namespace lt;
using namespace std::chrono_literals;

#if defined TORRENT_WINDOWS
#include <conio.h>
#include <io.h> // for _write, _fileno
#endif

#if defined TORRENT_WINDOWS
#define SEPARATOR "\\"
#else
#define SEPARATOR "/"
#endif

lt::add_torrent_params generate_torrent(bool const with_files, bool const with_hashes)
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
	t.add_tracker("http://torrent-file-tracker.com/announce");
	t.add_url_seed("http://torrent-file-url-seed.com/");

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

	return load_torrent_buffer(bencode(t.generate()));
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
	try
	{
		error_code ec;
		make_address("::1", ec);
		return !ec;
	}
	catch (std::exception const&) { return false; }
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

namespace {

std::string print_endpoint(peer_endpoint_t const& ep)
{
	if (auto i = std::get_if<peer_alert::ip_endpoint>(&ep))
	{
		return print_endpoint(*i);
	}
	else if (auto i2p = std::get_if<peer_alert::i2p_endpoint>(&ep))
	{
		return lt::aux::to_hex(*i2p);
	}
	TORRENT_ASSERT_FAIL();
	return {};
}
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
				, print_endpoint(p->ep).c_str(), p->message().c_str());
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

bool wait_for_alert(lt::session& ses,
	char const* name,
	std::function<bool(lt::alert const*)> predicate,
	lt::time_duration const timeout)
{
	time_point const end_time = clock_type::now() + timeout;
	for (;;)
	{
		if (print_alerts(ses, name, true, true, predicate, false)) return true;
		time_point const now = clock_type::now();
		if (now >= end_time) return false;
		ses.wait_for_alert(end_time - now);
	}
}

void wait_for_listen(lt::session& ses, char const* name)
{
	bool const listen_done = wait_for_alert(
		ses,
		name,
		[](lt::alert const* al) {
			return alert_cast<listen_failed_alert>(al) || alert_cast<listen_succeeded_alert>(al);
		},
		30s);
	// we didn't receive a listen alert!
	TEST_CHECK(listen_done);
}

void wait_for_downloading(lt::session& ses, char const* name)
{
	time_point const start = clock_type::now();
	bool const downloading_done = wait_for_alert(
		ses,
		name,
		[](lt::alert const* al) {
			state_changed_alert const* sc = alert_cast<state_changed_alert>(al);
			return sc && sc->state == torrent_status::downloading;
		},
		30s);
	if (!downloading_done)
	{
		std::printf("%s: did not receive a state_changed_alert indicating "
			"the torrent is downloading. waited: %d ms\n"
			, name, int(total_milliseconds(clock_type::now() - start)));
	}
}

// bounded wait for the session to notice a peer's socket close, so a final
// alert drain right after isn't racing the peer_disconnected_alert.
void wait_for_disconnect(lt::session& ses, char const* name)
{
	time_point const start = clock_type::now();
	bool const disconnected = wait_for_alert(
		ses,
		name,
		[](lt::alert const* al) { return alert_cast<peer_disconnected_alert>(al) != nullptr; },
		2s);
	if (!disconnected)
	{
		std::printf("%s: did not receive a peer_disconnected_alert. waited: %d ms\n",
			name,
			int(total_milliseconds(clock_type::now() - start)));
	}
}

void wait_for_seeding(lt::session& ses, char const* name)
{
	time_point const start = clock_type::now();
	bool const seeding = wait_for_alert(
		ses,
		name,
		[](lt::alert const* al) {
			state_changed_alert const* sc = alert_cast<state_changed_alert>(al);
			return sc && sc->state == torrent_status::seeding;
		},
		30s);
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
using native_handle = HANDLE;
#else
using pid_type = pid_t;
using native_handle = int;
#endif

namespace {

// reads a chunk into buf. returns 0 on EOF or error
std::size_t native_read(native_handle const h, span<char> const buf)
{
#ifdef _WIN32
	DWORD n = 0;
	if (!ReadFile(h, buf.data(), DWORD(buf.size()), &n, nullptr))
		return 0;
	return std::size_t(n);
#else
	ssize_t n;
	do
	{
		n = ::read(h, buf.data(), static_cast<std::size_t>(buf.size()));
	}
	while (n < 0 && errno == EINTR);
	if (n <= 0)
		return 0;
	return std::size_t(n);
#endif
}

// best-effort write to this process' current stdout, ignores errors and
// gives up (dropping the remainder of buf) rather than blocking forever if
// stdout isn't draining. the destination is resolved on every call (via the
// CRT/kernel fd table) rather than captured once, so it tracks whichever
// redirection is currently in effect.
void write_stdout(span<char const> buf)
{
	while (!buf.empty())
	{
		std::size_t written = 0;
#ifdef _WIN32
		int const n = ::_write(_fileno(stdout), buf.data(), static_cast<unsigned>(buf.size()));
		if (n <= 0)
			return;
		written = std::size_t(n);
#else
		// a regular file (the common case; test/main.cpp redirects stdout to
		// one per test) is always reported writable here, so this only
		// bounds the wait when stdout is a pipe/socket to a slow consumer
		pollfd pfd{};
		pfd.fd = STDOUT_FILENO;
		pfd.events = POLLOUT;
		if (::poll(&pfd, 1, 5000) <= 0 || !(pfd.revents & POLLOUT))
			return;
		ssize_t n;
		do
		{
			n = ::write(STDOUT_FILENO, buf.data(), static_cast<std::size_t>(buf.size()));
		}
		while (n < 0 && errno == EINTR);
		if (n <= 0)
			return;
		written = std::size_t(n);
#endif
		buf = buf.subspan(std::ptrdiff_t(written));
	}
}

void native_close(native_handle const h)
{
#ifdef _WIN32
	CloseHandle(h);
#else
	::close(h);
#endif
}

// reads lines from read_end until EOF. a line of the form
// "LISTENING_PORT <n>" reports the child's bound port through
// port_promise and is not forwarded; every other line is forwarded to
// this process' current stdout via write_stdout(), which tracks whichever
// test is currently running. that's correct because test/main.cpp
// guarantees every proxy/web/websocket server is stopped (and this thread
// joined) before the owning test's iteration ends, so a server's entire
// lifetime, and hence all its output, falls within that test's own stdout
// redirection window. closes read_end before returning.
void stdout_drain(native_handle const read_end, std::promise<int> port_promise)
{
	static constexpr std::string_view port_prefix = "LISTENING_PORT ";

	bool port_reported = false;
	std::string pending;
	std::array<char, 4096> buf;
	for (;;)
	{
		std::size_t const n = native_read(read_end, buf);
		if (n == 0)
			break;
		pending.append(buf.data(), n);
		std::string::size_type pos;
		while ((pos = pending.find('\n')) != std::string::npos)
		{
			std::string const line = pending.substr(0, pos + 1);
			pending.erase(0, pos + 1);
			if (!port_reported && line.rfind(port_prefix, 0) == 0)
			{
				char const* const num_begin = line.data() + port_prefix.size();
				// exclude the trailing '\n', and a '\r' before it: python's
				// stdout is line-buffered text mode on windows, which
				// translates '\n' to "\r\n" even when redirected to a pipe
				char const* num_end = line.data() + line.size() - 1;
				if (num_end > num_begin && *(num_end - 1) == '\r')
					--num_end;
				int port = -1;
				auto const result = std::from_chars(num_begin, num_end, port);
				port_reported = true;
				if (result.ec == std::errc() && result.ptr == num_end && port >= 0)
				{
					port_promise.set_value(port);
					continue;
				}
				// a malformed port report is a hard error, not something to
				// silently ignore in the hope of a later, well-formed one
				port_promise.set_exception(std::make_exception_ptr(std::runtime_error(
					"malformed LISTENING_PORT line: \"" + line.substr(0, line.size() - 1) + "\"")));
			}
			write_stdout(line);
		}
	}
	if (!pending.empty())
		write_stdout(pending);
	if (!port_reported)
		port_promise.set_value(-1);
	native_close(read_end);
}

struct spawned_process
{
	pid_type pid = 0;
	int port = -1;
	// keeps running for the child's entire lifetime, not just until the
	// port is reported. see the comment where it's started, in async_run().
	std::thread stdout_reader;
};

void stop_and_join(pid_type pid, std::thread& reader);

// spawns cmdline and waits (with a timeout) for the child to report the
// port it bound to, by printing "LISTENING_PORT <port>" to stdout before
// doing anything else useful. returns pid == 0 on failure to spawn, or
// port == -1 if the child exited or timed out without reporting one. throws
// std::runtime_error if the child reports a malformed port.
spawned_process async_run(char const* cmdline)
{
	std::promise<int> port_promise;
	std::future<int> port_future = port_promise.get_future();
	spawned_process proc;

#ifdef _WIN32
	char buf[2048];
	std::snprintf(buf, sizeof(buf), "%s", cmdline);

	std::printf("CreateProcess %s\n", buf);

	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE read_end = nullptr;
	HANDLE write_end = nullptr;
	if (!CreatePipe(&read_end, &write_end, &sa, 0))
	{
		std::printf("ERROR: CreatePipe failed (%d)\n", int(GetLastError()));
		return {};
	}
	// the parent's end of the pipe must not be inherited by the child
	SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

	PROCESS_INFORMATION pi;
	STARTUPINFOA startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = write_end;
	startup.hStdError = GetStdHandle(STD_OUTPUT_HANDLE);
	int const ret = CreateProcessA(nullptr, buf, nullptr, nullptr, TRUE
		, 0, nullptr, nullptr, &startup, &pi);
	CloseHandle(write_end);

	if (ret == 0)
	{
		int const error = GetLastError();
		std::printf("ERROR: (%d) %s\n", error, error_code(error, system_category()).message().c_str());
		CloseHandle(read_end);
		return {};
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
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	proc.pid = pi.dwProcessId;
#else
	pid_type pid;
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

	int pipefd[2];
	if (::pipe(pipefd) != 0)
	{
		std::printf("ERROR (%d) %s\n", errno, strerror(errno));
		return {};
	}
	native_handle const read_end = pipefd[0];
	native_handle const write_end = pipefd[1];

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, write_end, STDOUT_FILENO);
	posix_spawn_file_actions_addclose(&actions, read_end);
	posix_spawn_file_actions_addclose(&actions, write_end);

	int const ret = posix_spawnp(&pid, argv[0], &actions, nullptr, &argv[0], nullptr);
	posix_spawn_file_actions_destroy(&actions);
	::close(write_end);

	if (ret != 0)
	{
		std::printf("ERROR (%d) %s\n", errno, strerror(errno));
		::close(read_end);
		return {};
	}

	proc.pid = pid;
#endif

	// this thread must keep running for as long as the child does, not
	// just until it reports its port: something has to keep draining the
	// pipe, or the OS pipe buffer fills up once the child produces more
	// output (e.g. web_server.py logs every request), and the child then
	// blocks on its next write to stdout, hanging the server and the test.
	proc.stdout_reader = std::thread(stdout_drain, read_end, std::move(port_promise));

	if (port_future.wait_for(30s) != std::future_status::ready)
	{
		std::printf("ERROR: timed out waiting for the process to report its listening port\n");
		return proc;
	}
	try
	{
		proc.port = port_future.get();
	}
	catch (...)
	{
		// proc.stdout_reader is still joinable at this point; its destructor
		// would call std::terminate() if we let this exception unwind past
		// it without joining first
		stop_and_join(proc.pid, proc.stdout_reader);
		throw;
	}
	if (proc.port < 0)
	{
		std::printf("ERROR: process exited without reporting a listening port\n");
	}
	return proc;
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

void stop_and_join(pid_type const pid, std::thread& reader)
{
	stop_process(pid);
	if (reader.joinable())
		reader.join();
}

} // anonymous namespace

struct proxy_t
{
	pid_type pid;
	int type;
	std::thread stdout_reader;
};

// maps port to proxy type
static std::map<int, proxy_t> running_proxies;

void stop_proxy(int port)
{
	auto const it = running_proxies.find(port);

	if (it == running_proxies.end()) return;

	std::printf("stopping proxy on port %d\n", port);

	stop_and_join(it->second.pid, it->second.stdout_reader);
	running_proxies.erase(it);
}

void stop_all_proxies()
{
	std::map<int, proxy_t> proxies = std::move(running_proxies);
	running_proxies.clear();
	for (auto& i : proxies)
	{
		stop_and_join(i.second.pid, i.second.stdout_reader);
	}
}

namespace {

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

// stops and joins proc if it failed to spawn or never reported a listening
// port, in which case the caller should try the next python interpreter.
bool spawn_failed(spawned_process& proc)
{
	if (proc.pid == 0)
		return true;
	if (proc.port < 0)
	{
		stop_and_join(proc.pid, proc.stdout_reader);
		return true;
	}
	return false;
}

// tries cmdlines in order until one reports a listening port. throws
// std::runtime_error, naming label, if none of them do.
spawned_process spawn_with_retry(std::vector<std::string> const& cmdlines, char const* label)
{
	for (std::string const& cmdline : cmdlines)
	{
		std::printf("%s starting %s...\n", time_now_string().c_str(), label);
		std::printf("%s\n", cmdline.c_str());
		spawned_process proc = async_run(cmdline.c_str());
		if (!spawn_failed(proc))
		{
			std::printf(
				"%s launched, listening on port %d\n", time_now_string().c_str(), proc.port);
			return proc;
		}
	}
	throw std::runtime_error(
		std::string("failed to spawn ") + label + ": exhausted all python interpreters");
}

} // anonymous namespace

// returns a port on success, or throws std::runtime_error if no python
// interpreter could spawn the proxy
int start_proxy(int proxy_type)
{
	using namespace lt;

	std::map<int, proxy_t> :: iterator i = running_proxies.begin();
	for (; i != running_proxies.end(); ++i)
	{
		if (i->second.type == proxy_type)
		{
			// every current caller stops its own proxy before returning
			// (test/main.cpp's per-test cleanup backstops that too), so a
			// proxy of this type should never already be running here
			std::string const msg = "proxy type " + std::to_string(proxy_type)
				+ " is already running on port " + std::to_string(i->first);
			TORRENT_ASSERT_FAIL_VAL(msg);
			return i->first;
		}
	}

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
	std::vector<std::string> cmdlines;
	for (auto const& python_exe : get_python())
	{
		char buf[1024];
		std::snprintf(buf, sizeof(buf), "%s %s%s", python_exe.c_str(), cmd, auth);
		cmdlines.emplace_back(buf);
	}
	std::string const label = std::string(type) + " proxy";
	spawned_process proc = spawn_with_retry(cmdlines, label.c_str());
	int const port = proc.port;
	running_proxies.emplace(port, proxy_t{proc.pid, proxy_type, std::move(proc.stdout_reader)});
	return port;
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

add_torrent_params make_torrent(std::vector<lt::create_file_entry> files
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

	return load_torrent_buffer(bencode(ct.generate()));
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

add_torrent_params create_torrent(std::ostream* file
	, char const* name, int piece_size
	, int num_pieces, bool add_tracker, lt::create_flags_t const flags
	, std::string ssl_certificate, bool const bad_v1_hashes)
{
	// exercise the path when encountering invalid urls
	char const* invalid_tracker_url = "http:";
	char const* invalid_tracker_protocol = "foo://non/existent-name.com/announce";

	char const* valid_tracker_url = "http://foo.bar";
	char const* valid_tracker_protocol = "udp://foo.bar/announce";

	std::vector<lt::create_file_entry> fs;
	int total_size = piece_size * num_pieces;
	fs.emplace_back(name, total_size);
	lt::create_torrent t(std::move(fs), piece_size, flags);
	if (add_tracker)
	{
		// invalid tracker urls won't be included in the generated magnet link
		t.add_tracker(invalid_tracker_url);
		t.add_tracker(invalid_tracker_protocol);

		// valid tracker urls will be included in the generated magnet link
		t.add_tracker(valid_tracker_url);
		t.add_tracker(valid_tracker_protocol);
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
		sha1_hash const ph = bad_v1_hashes
			? sha1_hash("\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01")
			: hasher(piece).final();
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

		for (piece_index_t i : t.piece_range())
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

	return load_torrent_buffer(bencode(t.generate()));
}

std::tuple<torrent_handle, torrent_handle, torrent_handle>
setup_transfer(lt::session* ses1, lt::session* ses2, lt::session* ses3
	, bool clear_files, bool use_metadata_transfer, bool connect_peers
	, std::string suffix, int piece_size
	, add_torrent_params const* atp
	, bool super_seeding
	, bool stop_lsd, bool use_ssl_ports
	, std::shared_ptr<torrent_info>* torrent2, create_flags_t const flags
	, torrent_flags_t const seeder_extra_flags)
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

	add_torrent_params param;
	if (atp == nullptr || !atp->ti)
	{
		error_code ec;
		create_directory("tmp1" + suffix, ec);
		std::string const file_path = combine_path("tmp1" + suffix, "temporary");
		ofstream file(file_path.c_str());
		if (atp == nullptr)
		{
			param = ::create_torrent(&file, "temporary", piece_size, 9, false, flags);
		}
		else
		{
			// start from the freshly generated add_torrent_params, so it
			// keeps everything create_torrent() filled in (ti, merkle_trees,
			// ...); only overlay the field callers actually customize on the
			// atp they passed in, so a field neither side sets here isn't
			// silently dropped.
			param = ::create_torrent(&file, "temporary", piece_size, 9, false, flags);
			param.flags = atp->flags;
		}
		file.close();
		if (clear_files)
		{
			remove_all(combine_path("tmp2" + suffix, "temporary"), ec);
			remove_all(combine_path("tmp3" + suffix, "temporary"), ec);
		}
		std::printf("generated torrent: %s %s\n", aux::to_hex(param.ti->info_hashes().v2).c_str(), file_path.c_str());
	}
	else
	{
		param = *atp;
	}
	auto ti = param.ti;

	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	param.flags &= ~torrent_flags::paused;
	param.flags &= ~torrent_flags::auto_managed;
	param.save_path = "tmp1" + suffix;
	// save flags before ORing in seeder-only bits so we can restore them for the downloader
	auto const downloader_flags = param.flags;
	param.flags |= torrent_flags::seed_mode | seeder_extra_flags;
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

	// the downloader cannot use seed_mode; restore the original flags minus seed_mode
	// (seeder_extra_flags may overlap with downloader flags, so restore rather than mask)
	param.flags = downloader_flags & ~torrent_flags::seed_mode;

	TEST_CHECK(!ses1->get_torrents().empty());

	torrent_handle tor2;
	torrent_handle tor3;

	if (ses3)
	{
		param.save_path = "tmp3" + suffix;
		tor3 = ses3->add_torrent(param, ec);
		TEST_CHECK(!ses3->get_torrents().empty());
	}

	if (use_metadata_transfer)
	{
		param.ti.reset();
		param.info_hashes = ti->info_hashes();
	}
	else if (torrent2)
	{
		param.ti = *torrent2;
	}
	else
	{
		param.ti = ti;
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
std::thread web_server_reader;
}

int start_web_server(
	bool ssl, bool chunked_encoding, bool keepalive, int min_interval, bool expect_host_header)
{
	// only one web_server.py is ever tracked at a time; callers must
	// stop_web_server() before starting another one
	TORRENT_ASSERT(web_server_pid == 0);
	TORRENT_ASSERT(!web_server_reader.joinable());

	std::vector<std::string> cmdlines;
	for (auto const& python_exe : get_python())
	{
		char buf[300];
		std::snprintf(buf,
			sizeof(buf),
			"%s .." SEPARATOR "web_server.py %d %d %d %d %d",
			python_exe.c_str(),
			chunked_encoding,
			ssl,
			keepalive,
			min_interval,
			expect_host_header);
		cmdlines.emplace_back(buf);
	}
	spawned_process proc = spawn_with_retry(cmdlines, "web_server.py");
	int const port = proc.port;
	web_server_pid = proc.pid;
	web_server_reader = std::move(proc.stdout_reader);
	return port;
}

void stop_web_server()
{
	if (web_server_pid == 0) return;
	std::printf("stopping web server\n");
	stop_and_join(web_server_pid, web_server_reader);
	web_server_pid = 0;
}

namespace {
pid_type websocket_server_pid = 0;
std::thread websocket_server_reader;
}

int start_websocket_server(bool ssl, int min_interval)
{
	// only one websocket_server.py is ever tracked at a time; callers must
	// stop_websocket_server() before starting another one
	TORRENT_ASSERT(websocket_server_pid == 0);
	TORRENT_ASSERT(!websocket_server_reader.joinable());

	std::vector<std::string> cmdlines;
	for (auto const& python_exe : get_python())
	{
		char buf[200];
		std::snprintf(buf,
			sizeof(buf),
			"%s ../websocket_server.py %d %d",
			python_exe.c_str(),
			ssl,
			min_interval);
		cmdlines.emplace_back(buf);
	}
	spawned_process proc = spawn_with_retry(cmdlines, "websocket_server.py");
	int const port = proc.port;
	websocket_server_pid = proc.pid;
	websocket_server_reader = std::move(proc.stdout_reader);
	return port;
}

void stop_websocket_server()
{
	if (websocket_server_pid == 0) return;
	std::printf("stopping websocket server\n");
	stop_and_join(websocket_server_pid, websocket_server_reader);
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
