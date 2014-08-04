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

#include "setup_transfer.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/thread.hpp"

#include "libtorrent/thread.hpp"
#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>

#include "test.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/socket_io.hpp" // print_endpoint
#include "libtorrent/socket_type.hpp"
#include "libtorrent/instantiate_connection.hpp"

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/context.hpp>
#endif

#include <boost/detail/atomic_count.hpp>

#ifndef _WIN32
#include <spawn.h>
#include <signal.h>
#endif

#define DEBUG_WEB_SERVER 0

#define DLOG if (DEBUG_WEB_SERVER) fprintf

using namespace libtorrent;

static int tests_failure = 0;
static std::vector<std::string> failure_strings;

void report_failure(char const* err, char const* file, int line)
{
	char buf[500];
	snprintf(buf, sizeof(buf), "\x1b[41m***** %s:%d \"%s\" *****\x1b[0m\n", file, line, err);
	fprintf(stderr, "\n%s\n", buf);
	failure_strings.push_back(buf);
	++tests_failure;
}

int print_failures()
{
	if (tests_failure == 0)
		fprintf(stderr, "\n\n\x1b[42;30m   == %d ALL TESTS PASSED ==\x1b[0m\n\n\n", tests_failure);
	else
		fprintf(stderr, "\n\n\x1b[41m   == %d TEST(S) FAILED ==\x1b[0m\n\n\n", tests_failure);
	return tests_failure;
}

std::auto_ptr<alert> wait_for_alert(session& ses, int type, char const* name)
{
	std::auto_ptr<alert> ret;
	ptime end = time_now() + seconds(10);
	while (!ret.get())
	{
		ptime now = time_now();
		if (now > end) return std::auto_ptr<alert>();

		ses.wait_for_alert(end - now);
		std::deque<alert*> alerts;
		ses.pop_alerts(&alerts);
		for (std::deque<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			fprintf(stderr, "%s: %s: [%s] %s\n", time_now_string(), name
				, (*i)->what(), (*i)->message().c_str());
			if (!ret.get() && (*i)->type() == type)
			{
				ret = std::auto_ptr<alert>(*i);
			}
			else
				delete *i;
		}
	}
	return ret;
}

int load_file(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		ec.assign(errno, boost::system::get_generic_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
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
		ec.assign(errno, boost::system::get_generic_category());
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
		ec.assign(errno, boost::system::get_generic_category());
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

bool print_alerts(libtorrent::session& ses, char const* name
	, bool allow_disconnects, bool allow_no_torrents, bool allow_failed_fastresume
	, bool (*predicate)(libtorrent::alert*), bool no_output)
{
	bool ret = false;
	std::vector<torrent_handle> handles = ses.get_torrents();
	TEST_CHECK(!handles.empty() || allow_no_torrents);
	torrent_handle h;
	if (!handles.empty()) h = handles[0];
	std::deque<alert*> alerts;
	ses.pop_alerts(&alerts);
	for (std::deque<alert*>::iterator i = alerts.begin(); i != alerts.end(); ++i)
	{
		if (predicate && predicate(*i)) ret = true;
		if (peer_disconnected_alert* p = alert_cast<peer_disconnected_alert>(*i))
		{
			fprintf(stderr, "%s: %s: [%s] (%s): %s\n", time_now_string(), name, (*i)->what(), print_endpoint(p->ip).c_str(), p->message().c_str());
		}
		else if ((*i)->message() != "block downloading"
			&& (*i)->message() != "block finished"
			&& (*i)->message() != "piece finished"
			&& !no_output)
		{
			fprintf(stderr, "%s: %s: [%s] %s\n", time_now_string(), name, (*i)->what(), (*i)->message().c_str());
		}

		TEST_CHECK(alert_cast<fastresume_rejected_alert>(*i) == 0 || allow_failed_fastresume);
/*
		peer_error_alert* pea = alert_cast<peer_error_alert>(*i);
		if (pea)
		{
			fprintf(stderr, "%s: peer error: %s\n", time_now_string(), pea->error.message().c_str());
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
		delete *i;
	}
	return ret;
}

bool listen_done = false;
bool listen_alert(libtorrent::alert* a)
{
	if (alert_cast<listen_failed_alert>(a)
		|| alert_cast<listen_succeeded_alert>(a))
		listen_done = true;
	return true;
}

void wait_for_listen(libtorrent::session& ses, char const* name)
{
	listen_done = false;
	alert const* a = 0;
	do
	{
		print_alerts(ses, name, true, true, true, &listen_alert, false);
		if (listen_done) break;
		a = ses.wait_for_alert(milliseconds(500));
	} while (a);
	// we din't receive a listen alert!
	TEST_CHECK(listen_done);
}

void print_ses_rate(float time
	, libtorrent::torrent_status const* st1
	, libtorrent::torrent_status const* st2
	, libtorrent::torrent_status const* st3)
{
	if (st1)
	{
		fprintf(stderr, "%3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st1->download_payload_rate / 1000)
			, int(st1->upload_payload_rate / 1000)
			, int(st1->progress * 100)
			, st1->num_peers
			, st1->connect_candidates
			, st1->error.empty() ? "" : (" [" + st1->error + "]").c_str());
	}
	if (st2)
		fprintf(stderr, " : %3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st2->download_payload_rate / 1000)
			, int(st2->upload_payload_rate / 1000)
			, int(st2->progress * 100)
			, st2->num_peers
			, st2->connect_candidates
			, st2->error.empty() ? "" : (" [" + st2->error + "]").c_str());
	if (st3)
		fprintf(stderr, " : %3.1fs | %dkB/s %dkB/s %d%% %d cc:%d%s", time
			, int(st3->download_payload_rate / 1000)
			, int(st3->upload_payload_rate / 1000)
			, int(st3->progress * 100)
			, st3->num_peers
			, st3->connect_candidates
			, st3->error.empty() ? "" : (" [" + st3->error + "]").c_str());

	fprintf(stderr, "\n");
}

void test_sleep(int millisec)
{
	libtorrent::sleep(millisec);
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
	fprintf(stderr, "stopping proxy on port %d\n", port);
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
		fprintf(stderr, "failed (%d) %s\n", error, error_code(error, get_system_category()).message().c_str());
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
	TerminateProcess(proc, 138);
	CloseHandle(proc);
#else
	printf("killing pid: %d\n", p);
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

int start_proxy(int proxy_type)
{
	using namespace libtorrent;

	std::map<int, proxy_t> :: iterator i = running_proxies.begin();
	for (; i != running_proxies.end(); ++i)
	{
		if (i->second.type == proxy_type) { return i->first; }
	}

	unsigned int seed = total_microseconds(time_now_hires() - min_time()) & 0xffffffff;
	printf("random seed: %u\n", seed);
	std::srand(seed);

	int port = 5000 + (rand() % 55000);
	char const* type = "";
	char const* auth = "";
	char const* cmd = "";

	switch (proxy_type)
	{
		case proxy_settings::socks4:
			type = "socks4";
			auth = " --allow-v4";
			cmd = "python ../socks.py";
			break;
		case proxy_settings::socks5:
			type = "socks5";
			cmd = "python ../socks.py";
			break;
		case proxy_settings::socks5_pw:
			type = "socks5";
			auth = " --username testuser --password testpass";
			cmd = "python ../socks.py";
			break;
		case proxy_settings::http:
			type = "http";
			cmd = "python ../http.py";
			break;
		case proxy_settings::http_pw:
			type = "http";
			auth = " --username testuser --password testpass";
			cmd = "python ../http.py";
			break;
	}
	char buf[512];
	snprintf(buf, sizeof(buf), "%s --port %d%s", cmd, port, auth);

	fprintf(stderr, "%s starting proxy on port %d (%s %s)...\n", time_now_string(), port, type, auth);
	fprintf(stderr, "%s\n", buf);
	pid_type r = async_run(buf);
	if (r == 0) exit(1);
	proxy_t t = { r, proxy_type };
	running_proxies.insert(std::make_pair(port, t));
	fprintf(stderr, "%s launched\n", time_now_string());
	test_sleep(500);
	return port;
}

using namespace libtorrent;

template <class T>
boost::intrusive_ptr<T> clone_ptr(boost::intrusive_ptr<T> const& ptr)
{
	return boost::intrusive_ptr<T>(new T(*ptr));
}

unsigned char random_byte()
{ return std::rand() & 0xff; }

void create_random_files(std::string const& path, const int file_sizes[], int num_files)
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
		create_directory(full_path, ec);
		full_path = combine_path(full_path, filename);

		int to_write = file_sizes[i];
		file f(full_path, file::write_only, ec);
		if (ec) fprintf(stderr, "failed to create file \"%s\": (%d) %s\n"
			, full_path.c_str(), ec.value(), ec.message().c_str());
		size_type offset = 0;
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

boost::intrusive_ptr<torrent_info> create_torrent(std::ostream* file, int piece_size
	, int num_pieces, bool add_tracker, std::string ssl_certificate)
{
	char const* tracker_url = "http://non-existent-name.com/announce";
	// excercise the path when encountering invalid urls
	char const* invalid_tracker_url = "http:";
	char const* invalid_tracker_protocol = "foo://non/existent-name.com/announce";
	
	file_storage fs;
	int total_size = piece_size * num_pieces;
	fs.add_file("temporary", total_size);
	libtorrent::create_torrent t(fs, piece_size);
	if (add_tracker)
	{
		t.add_tracker(tracker_url);
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
	return boost::intrusive_ptr<torrent_info>(new torrent_info(
		&tmp[0], tmp.size(), ec));
}

void update_settings(session_settings& sess_set, bool allow_multiple_ips)
{
	if (allow_multiple_ips) sess_set.allow_multiple_connections_per_ip = true;
	sess_set.ignore_limits_on_local_network = false;
	sess_set.mixed_mode_algorithm = session_settings::prefer_tcp;
	sess_set.max_failcount = 1;
}

boost::tuple<torrent_handle, torrent_handle, torrent_handle>
setup_transfer(session* ses1, session* ses2, session* ses3
	, bool clear_files, bool use_metadata_transfer, bool connect_peers
	, std::string suffix, int piece_size
	, boost::intrusive_ptr<torrent_info>* torrent, bool super_seeding
	, add_torrent_params const* p, bool stop_lsd, bool use_ssl_ports
	, boost::intrusive_ptr<torrent_info>* torrent2)
{
	assert(ses1);
	assert(ses2);

	if (stop_lsd)
	{
		ses1->stop_lsd();
		ses2->stop_lsd();
		if (ses3) ses3->stop_lsd();
	}

	session_settings sess_set = ses1->settings();
	update_settings(sess_set, ses3 != NULL);
	ses1->set_settings(sess_set);

	sess_set = ses2->settings();
	update_settings(sess_set, ses3 != NULL);
	ses2->set_settings(sess_set);

	if (ses3)
	{
		sess_set = ses3->settings();
		update_settings(sess_set, ses3 != NULL);
		ses3->set_settings(sess_set);
	}

	ses1->set_alert_mask(~(alert::progress_notification | alert::stats_notification));
	ses2->set_alert_mask(~(alert::progress_notification | alert::stats_notification));
	if (ses3) ses3->set_alert_mask(~(alert::progress_notification | alert::stats_notification));

	peer_id pid;
	std::generate(&pid[0], &pid[0] + 20, random_byte);
	ses1->set_peer_id(pid);
	std::generate(&pid[0], &pid[0] + 20, random_byte);
	ses2->set_peer_id(pid);
	assert(ses1->id() != ses2->id());
	if (ses3)
	{
		std::generate(&pid[0], &pid[0] + 20, random_byte);
		ses3->set_peer_id(pid);
		assert(ses3->id() != ses2->id());
	}

	boost::intrusive_ptr<torrent_info> t;
	if (torrent == 0)
	{
		error_code ec;
		create_directory("tmp1" + suffix, ec);
		std::ofstream file(combine_path("tmp1" + suffix, "temporary").c_str());
		t = ::create_torrent(&file, piece_size, 9, false);
		file.close();
		if (clear_files)
		{
			remove_all(combine_path("tmp2" + suffix, "temporary"), ec);
			remove_all(combine_path("tmp3" + suffix, "temporary"), ec);
		}
		char ih_hex[41];
		to_hex((char const*)&t->info_hash()[0], 20, ih_hex);
		fprintf(stderr, "generated torrent: %s tmp1%s/temporary\n", ih_hex, suffix.c_str());
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
		param.ti = 0;
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

	assert(ses1->get_torrents().size() == 1);
	assert(ses2->get_torrents().size() == 1);

//	test_sleep(100);

	if (connect_peers)
	{
		std::auto_ptr<alert> a;
/*		do
		{
			a = wait_for_alert(*ses2, state_changed_alert::alert_type, "ses2");
		} while (static_cast<state_changed_alert*>(a.get())->state != torrent_status::downloading);
*/
//		wait_for_alert(*ses1, torrent_finished_alert::alert_type, "ses1");

		error_code ec;
		if (use_ssl_ports)
		{
			fprintf(stderr, "%s: ses1: connecting peer port: %d\n", time_now_string(), int(ses2->ssl_listen_port()));
			tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
				, ses2->ssl_listen_port()));
		}
		else
		{
			fprintf(stderr, "%s: ses1: connecting peer port: %d\n", time_now_string(), int(ses2->listen_port()));
			tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
				, ses2->listen_port()));
		}

		if (ses3)
		{
			// give the other peers some time to get an initial
			// set of pieces before they start sharing with each-other

			if (use_ssl_ports)
			{
				fprintf(stderr, "ses3: connecting peer port: %d\n", int(ses2->ssl_listen_port()));
				tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec)
					, ses2->ssl_listen_port()));
				fprintf(stderr, "ses3: connecting peer port: %d\n", int(ses1->ssl_listen_port()));
				tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec)
					, ses1->ssl_listen_port()));
			}
			else
			{
				fprintf(stderr, "ses3: connecting peer port: %d\n", int(ses2->listen_port()));
				tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec)
					, ses2->listen_port()));
				fprintf(stderr, "ses3: connecting peer port: %d\n", int(ses1->listen_port()));
				tor3.connect_peer(tcp::endpoint(
					address::from_string("127.0.0.1", ec)
					, ses1->listen_port()));
			}
		}
	}

	return boost::make_tuple(tor1, tor2, tor3);
}

pid_type web_server_pid = 0;

int start_web_server(bool ssl, bool chunked_encoding, bool keepalive)
{
	unsigned int seed = total_microseconds(time_now_hires() - min_time()) & 0xffffffff;
	fprintf(stderr, "random seed: %u\n", seed);
	std::srand(seed);
	int port = 5000 + (rand() % 55000);

	char buf[200];
	snprintf(buf, sizeof(buf), "python ../web_server.py %d %d %d %d"
		, port, chunked_encoding , ssl, keepalive);

	fprintf(stderr, "%s starting web_server on port %d...\n", time_now_string(), port);

	fprintf(stderr, "%s\n", buf);
	pid_type r = async_run(buf);
	if (r == 0) exit(1);
	web_server_pid = r;
	fprintf(stderr, "%s launched\n", time_now_string());
	test_sleep(500);
	return port;
}

void stop_web_server()
{
	if (web_server_pid == 0) return;
	fprintf(stderr, "stopping web server\n");
	stop_process(web_server_pid);
	web_server_pid = 0;
}

