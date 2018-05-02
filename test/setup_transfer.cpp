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
#include <deque>
#include <map>

#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/socket_io.hpp" // print_endpoint
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/broadcast_socket.hpp" // for supports_ipv6()

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

#ifndef _WIN32
#include <spawn.h>
#include <signal.h>
#endif

#define DEBUG_WEB_SERVER 0

#define DLOG if (DEBUG_WEB_SERVER) fprintf

using namespace libtorrent;
namespace lt = libtorrent;

#if defined TORRENT_WINDOWS
#include <conio.h>
#endif

boost::uint32_t g_addr = 0x92343023;

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
		ret[i] = lt::random();
	return ret;
}

#if TORRENT_USE_IPV6
address rand_v6()
{
	address_v6::bytes_type bytes;
	for (int i = 0; i < int(bytes.size()); ++i) bytes[i] = rand();
	return address_v6(bytes);
}
#endif

static boost::uint16_t g_port = 0;

tcp::endpoint rand_tcp_ep()
{
	// make sure we don't procude the same "random" port twice
	g_port = (g_port + 1) % 14038;
	return tcp::endpoint(rand_v4(), g_port + 1024);
}

udp::endpoint rand_udp_ep()
{
	g_port = (g_port + 1) % 14037;
	return udp::endpoint(rand_v4(), g_port + 1024);
}

std::map<std::string, boost::int64_t> get_counters(libtorrent::session& s)
{
	using namespace libtorrent;
	s.post_session_stats();

	std::map<std::string, boost::int64_t> ret;
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

alert const* wait_for_alert(lt::session& ses, int type, char const* name, int num
	, lt::time_duration timeout)
{
	time_point end = libtorrent::clock_type::now() + timeout;
	while (true)
	{
		time_point now = clock_type::now();
		if (now > end) return NULL;

		alert const* ret = NULL;

		ses.wait_for_alert(end - now);
		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);
		for (std::vector<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			fprintf(stdout, "%s: %s: [%s] %s\n", time_now_string(), name
				, (*i)->what(), (*i)->message().c_str());
			if ((*i)->type() == type)
			{
				ret = *i;
				--num;
			}
		}
		if (num <= 0) return ret;
	}
	return NULL;
}

int load_file(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
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

	r = fread(&v[0], 1, v.size(), f);
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
		fprintf(stderr, "ERROR opening file '%s': %s\n", filename, ec.message().c_str());
		return;
	}
	file::iovec_t b = { (void*)data, size_t(size) };
	out.writev(0, &b, 1, ec);
	TEST_CHECK(!ec);
	if (ec)
	{
		fprintf(stderr, "ERROR writing file '%s': %s\n", filename, ec.message().c_str());
		return;
	}
}

bool print_alerts(lt::session& ses, char const* name
	, bool allow_disconnects, bool allow_no_torrents, bool allow_failed_fastresume
	, boost::function<bool(libtorrent::alert const*)> const& predicate, bool no_output)
{
	TEST_CHECK(!ses.get_torrents().empty() || allow_no_torrents);
	std::vector<alert*> alerts;
	ses.pop_alerts(&alerts);
	for (std::vector<alert*>::iterator i = alerts.begin(); i != alerts.end(); ++i)
	{
		if (peer_disconnected_alert const* p = alert_cast<peer_disconnected_alert>(*i))
		{
			fprintf(stdout, "%s: %s: [%s] (%s): %s\n", time_now_string(), name, (*i)->what(), print_endpoint(p->ip).c_str(), p->message().c_str());
		}
		else if ((*i)->type() == invalid_request_alert::alert_type)
		{
			fprintf(stdout, "peer error: %s\n", (*i)->message().c_str());
			TEST_CHECK(false);
		}
		else if ((*i)->type() == fastresume_rejected_alert::alert_type)
		{
			fprintf(stdout, "resume data error: %s\n", (*i)->message().c_str());
			TEST_CHECK(allow_failed_fastresume);
		}
		else if (!no_output
			&& (*i)->type() != block_downloading_alert::alert_type
			&& (*i)->type() != block_finished_alert::alert_type
			&& (*i)->type() != piece_finished_alert::alert_type)
		{
			fprintf(stdout, "%s: %s: [%s] %s\n", time_now_string(), name, (*i)->what(), (*i)->message().c_str());
		}
/*
		peer_error_alert const* pea = alert_cast<peer_error_alert>(*i);
		if (pea)
		{
			fprintf(stdout, "%s: peer error: %s\n", time_now_string(), pea->error.message().c_str());
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
	}
	return predicate && boost::algorithm::any_of(alerts.begin(), alerts.end(), predicate);
}

bool listen_alert(libtorrent::alert const* a)
{
	return alert_cast<listen_failed_alert>(a) || alert_cast<listen_succeeded_alert>(a);
}

void wait_for_listen(lt::session& ses, char const* name)
{
	alert const* a = 0;
	bool listen_done = false;
	do
	{
		listen_done = print_alerts(ses, name, true, true, true, listen_alert, false);
		if (listen_done) break;
		a = ses.wait_for_alert(milliseconds(500));
	} while (a);
	// we din't receive a listen alert!
	TEST_CHECK(listen_done);
}

bool downloading_alert(libtorrent::alert const* a)
{
	state_changed_alert const* sc = alert_cast<state_changed_alert>(a);
	return sc && sc->state == torrent_status::downloading;
}

void wait_for_downloading(lt::session& ses, char const* name)
{
	time_point const start = clock_type::now();
	bool downloading_done = false;
	alert const* a = 0;
	do
	{
		downloading_done = print_alerts(ses, name, true, true, true, downloading_alert, false);
		if (downloading_done) break;
		if (total_seconds(clock_type::now() - start) > 10) break;
		a = ses.wait_for_alert(seconds(2));
	} while (a);
	if (!downloading_done)
	{
		fprintf(stdout, "%s: did not receive a state_changed_alert indicating "
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
		fprintf(stdout, "%3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st1->download_payload_rate / 1000)
			, int(st1->upload_payload_rate / 1000)
			, int(st1->progress * 100)
			, st1->num_peers
			, st1->connect_candidates
			, st1->errc ? (" [" + st1->errc.message() + "]").c_str() : "");
	}
	if (st2)
		fprintf(stdout, " : %3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st2->download_payload_rate / 1000)
			, int(st2->upload_payload_rate / 1000)
			, int(st2->progress * 100)
			, st2->num_peers
			, st2->connect_candidates
			, st2->errc ? (" [" + st1->errc.message() + "]").c_str() : "");
	if (st3)
		fprintf(stdout, " : %3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st3->download_payload_rate / 1000)
			, int(st3->upload_payload_rate / 1000)
			, int(st3->progress * 100)
			, st3->num_peers
			, st3->connect_candidates
			, st3->errc ? (" [" + st1->errc.message() + "]").c_str() : "");

	fprintf(stdout, "\n");
}

void test_sleep(int milliseconds)
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

// returns 0 on failure, otherwise pid
pid_type async_run(char const* cmdline)
{
#ifdef _WIN32
	char buf[2048];
	snprintf(buf, sizeof(buf), "%s", cmdline);

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
		fprintf(stdout, "failed (%d) %s\n", error, error_code(error, system_category()).message().c_str());
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
	argv.push_back(NULL);

	int ret = posix_spawnp(&p, argv[0], NULL, NULL, &argv[0], NULL);
	if (ret != 0)
	{
		fprintf(stderr, "failed (%d) %s\n", errno, strerror(errno));
		return 0;
	}
	return p;
#endif
}

void stop_process(pid_type p)
{
#ifdef _WIN32
	HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, p);
	if (proc == NULL) return;
	TerminateProcess(proc, 138);
	WaitForSingleObject(proc, 5000);
	CloseHandle(proc);
#else
	printf("killing pid: %d\n", p);
	kill(p, SIGKILL);
#endif
}

void stop_proxy(int port)
{
	std::map<int, proxy_t>::iterator const it = running_proxies.find(port);

	if (it == running_proxies.end()) return;

	fprintf(stdout, "stopping proxy on port %d\n", port);

	stop_process(it->second.pid);
	running_proxies.erase(it);
}

void stop_all_proxies()
{
	std::map<int, proxy_t> proxies = running_proxies;
	running_proxies.clear();
	for (std::map<int, proxy_t>::iterator i = proxies.begin()
		, end = proxies.end(); i != end; ++i)
	{
		stop_process(i->second.pid);
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

	int port = 2000 + (lt::random() % 6000);
	error_code ec;
	io_service ios;

	// make sure the port we pick is free
	do {
		++port;
		tcp::socket s(ios);
		s.open(tcp::v4(), ec);
		if (ec) break;
		s.bind(tcp::endpoint(address::from_string("127.0.0.1"), port), ec);
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
	snprintf(buf, sizeof(buf), "%s --port %d%s", cmd, port, auth);

	fprintf(stdout, "%s starting proxy on port %d (%s %s)...\n", time_now_string(), port, type, auth);
	fprintf(stdout, "%s\n", buf);
	pid_type r = async_run(buf);
	if (r == 0) abort();
	proxy_t t = { r, proxy_type };
	running_proxies.insert(std::make_pair(port, t));
	fprintf(stdout, "%s launched\n", time_now_string());
	test_sleep(500);
	return port;
}

using namespace libtorrent;

template <class T>
boost::shared_ptr<T> clone_ptr(boost::shared_ptr<T> const& ptr)
{
	return boost::make_shared<T>(*ptr);
}

unsigned char random_byte()
{ return std::rand() & 0xff; }

void create_random_files(std::string const& path, const int file_sizes[], int num_files
	, file_storage* fs)
{
	error_code ec;
	char* random_data = (char*)malloc(300000);
	for (int i = 0; i != num_files; ++i)
	{
		std::generate(random_data, random_data + 300000, random_byte);
		char filename[200];
		snprintf(filename, sizeof(filename), "test%d", i);
		char dirname[200];
		snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);

		std::string full_path = combine_path(path, dirname);
		error_code ec;
		lt::create_directories(full_path, ec);
		if (ec) fprintf(stderr, "create_directory(%s) failed: (%d) %s\n"
			, full_path.c_str(), ec.value(), ec.message().c_str());

		full_path = combine_path(full_path, filename);

		int to_write = file_sizes[i];
		if (fs) fs->add_file(full_path, to_write);
		file f(full_path, file::write_only, ec);
		if (ec) fprintf(stderr, "failed to create file \"%s\": (%d) %s\n"
			, full_path.c_str(), ec.value(), ec.message().c_str());
		boost::int64_t offset = 0;
		while (to_write > 0)
		{
			int s = (std::min)(to_write, 300000);
			file::iovec_t b = { random_data, size_t(s)};
			f.writev(offset, &b, 1, ec);
			if (ec) fprintf(stderr, "failed to write file \"%s\": (%d) %s\n"
				, full_path.c_str(), ec.value(), ec.message().c_str());
			offset += s;
			to_write -= s;
		}
	}
	free(random_data);
}

boost::shared_ptr<torrent_info> create_torrent(std::ostream* file
	, char const* name, int piece_size
	, int num_pieces, bool add_tracker, std::string ssl_certificate)
{
	// excercise the path when encountering invalid urls
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
			fprintf(stderr, "failed to load SSL certificate: %s\n", ec.message().c_str());
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
	sha1_hash ph = hasher(&piece[0], piece.size()).final();
	for (int i = 0; i < num; ++i)
		t.set_hash(i, ph);

	if (file)
	{
		while (total_size > 0)
		{
			file->write(&piece[0], (std::min)(int(piece.size()), total_size));
			total_size -= piece.size();
		}
	}

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);

	entry tor = t.generate();

	bencode(out, tor);
	error_code ec;
	return boost::make_shared<torrent_info>(
		&tmp[0], tmp.size(), boost::ref(ec), 0);
}

boost::tuple<torrent_handle, torrent_handle, torrent_handle>
setup_transfer(lt::session* ses1, lt::session* ses2, lt::session* ses3
	, bool clear_files, bool use_metadata_transfer, bool connect_peers
	, std::string suffix, int piece_size
	, boost::shared_ptr<torrent_info>* torrent, bool super_seeding
	, add_torrent_params const* p, bool stop_lsd, bool use_ssl_ports
	, boost::shared_ptr<torrent_info>* torrent2)
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
	ses1->apply_settings(pack);
	ses2->apply_settings(pack);
	if (ses3)
	{
		ses3->apply_settings(pack);
	}

	boost::shared_ptr<torrent_info> t;
	if (torrent == 0)
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
		to_hex((char const*)&t->info_hash()[0], 20, ih_hex);
		fprintf(stdout, "generated torrent: %s tmp1%s/temporary\n", ih_hex, suffix.c_str());
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
		fprintf(stdout, "ses1.add_torrent: %s\n", ec.message().c_str());
		return boost::make_tuple(torrent_handle(), torrent_handle(), torrent_handle());
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

//	test_sleep(100);

	if (connect_peers)
	{
		wait_for_downloading(*ses2, "ses2");

		error_code ec;
		int port = 0;
		if (use_ssl_ports)
		{
			port = ses2->ssl_listen_port();
			fprintf(stdout, "%s: ses2->ssl_listen_port(): %d\n", time_now_string(), port);
		}

		if (port == 0)
		{
			port = ses2->listen_port();
			fprintf(stdout, "%s: ses2->listen_port(): %d\n", time_now_string(), port);
		}

		fprintf(stdout, "%s: ses1: connecting peer port: %d\n"
			, time_now_string(), port);
		tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
			, port));

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

			fprintf(stdout, "ses3: connecting peer port: %d\n", port);
			tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec), port));
			fprintf(stdout, "ses3: connecting peer port: %d\n", port2);
				tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec)
					, port2));
		}
	}

	return boost::make_tuple(tor1, tor2, tor3);
}

pid_type web_server_pid = 0;

int start_web_server(bool ssl, bool chunked_encoding, bool keepalive)
{
	int port = 2000 + (lt::random() % 6000);
	error_code ec;
	io_service ios;

	// make sure the port we pick is free
	do {
		++port;
		tcp::socket s(ios);
		s.open(tcp::v4(), ec);
		if (ec) break;
		s.bind(tcp::endpoint(address::from_string("127.0.0.1"), port), ec);
	} while (ec);

	char buf[200];
	snprintf(buf, sizeof(buf), "python ../web_server.py %d %d %d %d"
		, port, chunked_encoding , ssl, keepalive);

	fprintf(stdout, "%s starting web_server on port %d...\n", time_now_string(), port);

	fprintf(stdout, "%s\n", buf);
	pid_type r = async_run(buf);
	if (r == 0) abort();
	web_server_pid = r;
	fprintf(stdout, "%s launched\n", time_now_string());
	test_sleep(500);
	return port;
}

void stop_web_server()
{
	if (web_server_pid == 0) return;
	fprintf(stdout, "stopping web server\n");
	stop_process(web_server_pid);
	web_server_pid = 0;
}

tcp::endpoint ep(char const* ip, int port)
{
	error_code ec;
	return tcp::endpoint(address::from_string(ip, ec), port);
}
