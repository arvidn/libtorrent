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

using namespace libtorrent;

bool tests_failure = false;

void report_failure(char const* err, char const* file, int line)
{
#ifdef TORRENT_WINDOWS
	HANDLE console = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, 0, CONSOLE_TEXTMODE_BUFFER, 0);
	SetConsoleTextAttribute(console, FOREGROUND_RED);
	fprintf(stderr, "\n**** %s:%d \"%s\" ****\n\n", file, line, err);
	CloseHandle(console);
#else
	fprintf(stderr, "\033[31m %s:%d \"%s\"\033[0m\n", file, line, err);
#endif
	tests_failure = true;
}

bool print_alerts(libtorrent::session& ses, char const* name
	, bool allow_disconnects, bool allow_no_torrents, bool allow_failed_fastresume
	, bool (*predicate)(libtorrent::alert*))
{
	bool ret = false;
	std::vector<torrent_handle> handles = ses.get_torrents();
	TEST_CHECK(!handles.empty() || allow_no_torrents);
	torrent_handle h;
	if (!handles.empty()) h = handles[0];
	std::auto_ptr<alert> a;
	a = ses.pop_alert();
	while (a.get())
	{
		if (predicate && predicate(a.get())) ret = true;
		if (peer_disconnected_alert* p = alert_cast<peer_disconnected_alert>(a.get()))
		{
			fprintf(stderr, "%s(%s): %s\n", name, print_endpoint(p->ip).c_str(), p->message().c_str());
		}
		else if (a->message() != "block downloading"
			&& a->message() != "block finished"
			&& a->message() != "piece finished")
		{
			fprintf(stderr, "%s: %s\n", name, a->message().c_str());
		}
		TEST_CHECK(alert_cast<fastresume_rejected_alert>(a.get()) == 0 || allow_failed_fastresume);

		TEST_CHECK(alert_cast<peer_error_alert>(a.get()) == 0
			|| (!handles.empty() && h.is_seed())
			|| a->message() == "connecting to peer"
			|| a->message() == "closing connection to ourself"
			|| a->message() == "duplicate connection"
			|| a->message() == "duplicate peer-id, connection closed"
			|| (allow_disconnects && a->message() == "Broken pipe")
			|| (allow_disconnects && a->message() == "Connection reset by peer")
			|| (allow_disconnects && a->message() == "End of file."));
		a = ses.pop_alert();
	}
	return ret;
}

void test_sleep(int millisec)
{
	libtorrent::sleep(millisec);
}

void stop_proxy(int port)
{
	char buf[100];
	snprintf(buf, sizeof(buf), "delegated -P%d -Fkill", port);
	system(buf);
}

void start_proxy(int port, int proxy_type)
{
	using namespace libtorrent;

	stop_proxy(port);

	char const* type = "";
	char const* auth = "";

	switch (proxy_type)
	{
		case proxy_settings::socks4:
			type = "socks4";
			break;
		case proxy_settings::socks5:
			type = "socks5";
			break;
		case proxy_settings::socks5_pw:
			type = "socks5";
			auth = "AUTHORIZER=-list{testuser:testpass}";
			break;
		case proxy_settings::http:
			type = "http";
			break;
		case proxy_settings::http_pw:
			type = "http";
			auth = "AUTHORIZER=-list{testuser:testpass}";
			break;
	}

	char buf[512];
	// we need to echo n since dg will ask us to configure it
	snprintf(buf, sizeof(buf), "echo n | delegated -P%d ADMIN=test@test.com "
		"PERMIT=\"*:*:localhost\" REMITTABLE=+,https RELAY=proxy,delegate "
		"SERVER=%s %s"
		, port, type, auth);

	fprintf(stderr, "starting delegated proxy...\n");
	system(buf);
	fprintf(stderr, "launched\n");
	// apparently delegate takes a while to open its listen port
	test_sleep(1000);
}

using namespace libtorrent;

template <class T>
boost::intrusive_ptr<T> clone_ptr(boost::intrusive_ptr<T> const& ptr)
{
	return boost::intrusive_ptr<T>(new T(*ptr));
}

boost::intrusive_ptr<torrent_info> create_torrent(std::ostream* file, int piece_size
	, int num_pieces, bool add_tracker)
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

	bencode(out, t.generate());
	error_code ec;
	return boost::intrusive_ptr<torrent_info>(new torrent_info(
		&tmp[0], tmp.size(), ec));
}

boost::tuple<torrent_handle, torrent_handle, torrent_handle>
setup_transfer(session* ses1, session* ses2, session* ses3
	, bool clear_files, bool use_metadata_transfer, bool connect_peers
	, std::string suffix, int piece_size
	, boost::intrusive_ptr<torrent_info>* torrent, bool super_seeding
	, add_torrent_params const* p)
{
	assert(ses1);
	assert(ses2);

	session_settings sess_set = ses1->settings();
	sess_set.allow_multiple_connections_per_ip = true;
	sess_set.ignore_limits_on_local_network = false;
	ses1->set_settings(sess_set);
	ses2->set_settings(sess_set);
	if (ses3) ses3->set_settings(sess_set);
	ses1->set_alert_mask(~alert::progress_notification);
	ses2->set_alert_mask(~alert::progress_notification);
	if (ses3) ses3->set_alert_mask(~alert::progress_notification);

	std::srand(time(0));
	peer_id pid;
	std::generate(&pid[0], &pid[0] + 20, std::rand);
	ses1->set_peer_id(pid);
	std::generate(&pid[0], &pid[0] + 20, std::rand);
	ses2->set_peer_id(pid);
	assert(ses1->id() != ses2->id());
	if (ses3)
	{
		std::generate(&pid[0], &pid[0] + 20, std::rand);
		ses3->set_peer_id(pid);
		assert(ses3->id() != ses2->id());
	}

	boost::intrusive_ptr<torrent_info> t;
	if (torrent == 0)
	{
		error_code ec;
		create_directory("./tmp1" + suffix, ec);
		std::ofstream file(("./tmp1" + suffix + "/temporary").c_str());
		t = ::create_torrent(&file, piece_size, 19, true);
		file.close();
		if (clear_files)
		{
			remove_all("./tmp2" + suffix + "/temporary", ec);
			remove_all("./tmp3" + suffix + "/temporary", ec);
		}
		char ih_hex[41];
		to_hex((char const*)&t->info_hash()[0], 20, ih_hex);
		fprintf(stderr, "generated torrent: %s ./tmp1%s/temporary\n", ih_hex, suffix.c_str());
	}
	else
	{
		t = *torrent;
	}

	// they should not use the same save dir, because the
	// file pool will complain if two torrents are trying to
	// use the same files
	sha1_hash info_hash = t->info_hash();
	add_torrent_params param;
	if (p) param = *p;
	param.ti = clone_ptr(t);
	param.save_path = "./tmp1" + suffix;
	error_code ec;
	torrent_handle tor1 = ses1->add_torrent(param, ec);
	tor1.super_seeding(super_seeding);
	TEST_CHECK(!ses1->get_torrents().empty());
	torrent_handle tor2;
	torrent_handle tor3;

	// the downloader cannot use seed_mode
	param.seed_mode = false;

	if (ses3)
	{
		param.ti = clone_ptr(t);
		param.save_path = "./tmp3" + suffix;
		tor3 = ses3->add_torrent(param, ec);
		TEST_CHECK(!ses3->get_torrents().empty());
	}

  	if (use_metadata_transfer)
	{
		param.ti = 0;
		param.info_hash = t->info_hash();
	}
	else
	{
		param.ti = clone_ptr(t);
	}
	param.save_path = "./tmp2" + suffix;

	tor2 = ses2->add_torrent(param, ec);
	TEST_CHECK(!ses2->get_torrents().empty());

	assert(ses1->get_torrents().size() == 1);
	assert(ses2->get_torrents().size() == 1);

	test_sleep(100);

	if (connect_peers)
	{
		fprintf(stderr, "connecting peer\n");
		error_code ec;
		tor1.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
			, ses2->listen_port()));

		if (ses3)
		{
			// give the other peers some time to get an initial
			// set of pieces before they start sharing with each-other
			tor3.connect_peer(tcp::endpoint(
				address::from_string("127.0.0.1", ec)
				, ses2->listen_port()));
			tor3.connect_peer(tcp::endpoint(
				address::from_string("127.0.0.1", ec)
				, ses1->listen_port()));
		}
	}

	return boost::make_tuple(tor1, tor2, tor3);
}

boost::asio::io_service* tracker_ios = 0;
boost::shared_ptr<libtorrent::thread> tracker_server;
libtorrent::mutex tracker_lock;
libtorrent::condition tracker_initialized;

bool udp_failed = false;

void stop_tracker()
{
	if (tracker_server && tracker_ios)
	{
		tracker_ios->stop();
		tracker_server->join();
		tracker_server.reset();
		tracker_ios = 0;
	}
}

void udp_tracker_thread(int* port);

int start_tracker()
{
	stop_tracker();

	{
		mutex::scoped_lock l(tracker_lock);
		tracker_initialized.clear(l);
	}

	int port = 0;

	tracker_server.reset(new libtorrent::thread(boost::bind(&udp_tracker_thread, &port)));

	{
		mutex::scoped_lock l(tracker_lock);
		tracker_initialized.wait(l);
	}
	test_sleep(100);
	return port;
}

void on_udp_receive(error_code const& ec, size_t bytes_transferred, udp::endpoint const* from, char* buffer, udp::socket* sock)
{
	if (ec)
	{
		fprintf(stderr, "UDP tracker, read failed: %s\n", ec.message().c_str());
		return;
	}

	udp_failed = false;

	if (bytes_transferred < 16)
	{
		fprintf(stderr, "UDP message too short\n");
		return;
	}
	char* ptr = buffer;
	detail::read_uint64(ptr);
	boost::uint32_t action = detail::read_uint32(ptr);
	boost::uint32_t transaction_id = detail::read_uint32(ptr);

	error_code e;

	switch (action)
	{
		case 0: // connect

			ptr = buffer;
			detail::write_uint32(0, ptr); // action = connect
			detail::write_uint32(transaction_id, ptr); // transaction_id
			detail::write_uint64(10, ptr); // connection_id
			sock->send_to(asio::buffer(buffer, 16), *from, 0, e);
			break;

		case 1: // announce

			ptr = buffer;
			detail::write_uint32(1, ptr); // action = announce
			detail::write_uint32(transaction_id, ptr); // transaction_id
			detail::write_uint32(1800, ptr); // interval
			detail::write_uint32(1, ptr); // incomplete
			detail::write_uint32(1, ptr); // complete
			// 0 peers
			sock->send_to(asio::buffer(buffer, 20), *from, 0, e);
			break;
		default: // ignore scrapes
			break;
	}
}

void udp_tracker_thread(int* port)
{
	io_service ios;
	udp::socket acceptor(ios);
	error_code ec;
	acceptor.open(udp::v4(), ec);
	if (ec)
	{
		fprintf(stderr, "Error opening listen UDP socket: %s\n", ec.message().c_str());
		mutex::scoped_lock l(tracker_lock);
		tracker_initialized.signal(l);
		return;
	}
	acceptor.bind(udp::endpoint(address_v4::any(), 0), ec);
	if (ec)
	{
		fprintf(stderr, "Error binding UDP socket to port 0: %s\n", ec.message().c_str());
		mutex::scoped_lock l(tracker_lock);
		tracker_initialized.signal(l);
		return;
	}
	*port = acceptor.local_endpoint().port();

	tracker_ios = &ios;

	fprintf(stderr, "UDP tracker initialized on port %d\n", *port);

	{
		mutex::scoped_lock l(tracker_lock);
		tracker_initialized.signal(l);
	}

	char buffer[2000];

	for (;;)
	{
		error_code ec;
		udp::endpoint from;
		udp_failed = true;
		acceptor.async_receive_from(
			asio::buffer(buffer, sizeof(buffer)), from, boost::bind(
				&on_udp_receive, _1, _2, &from, &buffer[0], &acceptor));
		ios.run_one(ec);
		if (udp_failed) return;

		if (ec)
		{
			fprintf(stderr, "Error receiving on UDP socket: %s\n", ec.message().c_str());
			mutex::scoped_lock l(tracker_lock);
			tracker_initialized.signal(l);
			return;
		}
		ios.reset();
	}
}

boost::asio::io_service* web_ios = 0;
boost::shared_ptr<libtorrent::thread> web_server;
libtorrent::mutex web_lock;
libtorrent::condition web_initialized;

void stop_web_server()
{
	if (web_server && web_ios)
	{
		web_ios->stop();
		web_server->join();
		web_server.reset();
		web_ios = 0;
	}
}

void web_server_thread(int* port, bool ssl);

int start_web_server(bool ssl)
{
	stop_web_server();

	{
		mutex::scoped_lock l(web_lock);
		web_initialized.clear(l);
	}

	int port = 0;

	web_server.reset(new libtorrent::thread(boost::bind(&web_server_thread, &port, ssl)));

	{
		mutex::scoped_lock l(web_lock);
		web_initialized.wait(l);
	}

	// create this directory so that the path
	// "relative/../test_file" can resolve
	error_code ec;
	create_directory("relative", ec);
	test_sleep(100);
	return port;
}

void send_response(stream_socket& s, error_code& ec
	, int code, char const* status_message, char const* extra_header
	, int len)
{
	char msg[400];
	int pkt_len = snprintf(msg, sizeof(msg), "HTTP/1.0 %d %s\r\n"
		"content-length: %d\r\n"
		"%s"
		"\r\n"
		, code, status_message, len
		, extra_header ? extra_header : "");
//	fprintf(stderr, ">> %s\n", msg);
	write(s, boost::asio::buffer(msg, pkt_len), boost::asio::transfer_all(), ec);
}

bool accept_done = false;

void on_accept(error_code const& ec)
{
	if (ec)
	{
		fprintf(stderr, "Error accepting socket: %s\n", ec.message().c_str());
		accept_done = false;
	}
	else
	{
		fprintf(stderr, "accepting connection\n");
		accept_done = true;
	}
}

void web_server_thread(int* port, bool ssl)
{
	// TODO: support SSL

	io_service ios;
	socket_acceptor acceptor(ios);
	error_code ec;
	acceptor.open(tcp::v4(), ec);
	if (ec)
	{
		fprintf(stderr, "Error opening listen socket: %s\n", ec.message().c_str());
		mutex::scoped_lock l(web_lock);
		web_initialized.signal(l);
		return;
	}
	acceptor.set_option(socket_acceptor::reuse_address(true), ec);
	if (ec)
	{
		fprintf(stderr, "Error setting listen socket to reuse addr: %s\n", ec.message().c_str());
		mutex::scoped_lock l(web_lock);
		web_initialized.signal(l);
		return;
	}
	acceptor.bind(tcp::endpoint(address_v4::any(), 0), ec);
	if (ec)
	{
		fprintf(stderr, "Error binding listen socket to port 0: %s\n", ec.message().c_str());
		mutex::scoped_lock l(web_lock);
		web_initialized.signal(l);
		return;
	}
	*port = acceptor.local_endpoint().port();
	acceptor.listen(10, ec);
	if (ec)
	{
		fprintf(stderr, "Error listening on socket: %s\n", ec.message().c_str());
		mutex::scoped_lock l(web_lock);
		web_initialized.signal(l);
		return;
	}

	web_ios = &ios;

	fprintf(stderr, "web server initialized on port %d\n", *port);

	{
		mutex::scoped_lock l(web_lock);
		web_initialized.signal(l);
	}

	char buf[10000];
	int len = 0;
	int offset = 0;
	stream_socket s(ios);

	for (;;)
	{
		s.close(ec);

		len = 0;
		offset = 0;
		accept_done = false;
		acceptor.async_accept(s, &on_accept);
		ios.reset();
		ios.run_one();
		if (!accept_done)
		{
			fprintf(stderr, "accept failed\n");
			return;
		}

		if (!s.is_open()) continue;

		http_parser p;
		bool failed = false;

		do
		{
			p.reset();
			bool error = false;

			p.incoming(buffer::const_interval(buf + offset, buf + len), error);

			TEST_CHECK(error == false);
			if (error)
			{
				fprintf(stderr, "parse failed\n");
				failed = true;
				break;
			}

			while (!p.finished())
			{
				size_t received = s.read_some(boost::asio::buffer(&buf[len]
					, sizeof(buf) - len), ec);

				if (ec || received <= 0)
				{
					fprintf(stderr, "read failed: %s received: %d\n", ec.message().c_str(), int(received));
					failed = true;
					break;
				}
				len += received;
	
	
				p.incoming(buffer::const_interval(buf + offset, buf + len), error);
				TEST_CHECK(error == false);
				if (error)
				{
					fprintf(stderr, "parse failed\n");
					failed = true;
					break;
				}
			}
//			fprintf(stderr, "%s", std::string(buf + offset, p.body_start()).c_str());

			if (failed) break;

			offset += p.body_start() + p.content_length();

			if (p.method() != "get" && p.method() != "post")
			{
				fprintf(stderr, "incorrect method: %s\n", p.method().c_str());
				break;
			}

			std::string path = p.path();

			if (path == "/redirect")
			{
				send_response(s, ec, 301, "Moved Permanently", "Location: /test_file\r\n", 0);
				break;
			}

			if (path == "/infinite_redirect")
			{
				send_response(s, ec, 301, "Moved Permanently", "Location: /infinite_redirect\r\n", 0);
				break;
			}

			if (path == "/relative/redirect")
			{
				send_response(s, ec, 301, "Moved Permanently", "Location: ../test_file\r\n", 0);
				break;
			}

			if (path.substr(0, 9) == "/announce")
			{
				entry announce;
				announce["interval"] = 1800;
				announce["complete"] = 1;
				announce["incomplete"] = 1;
				announce["peers"].string();
				std::vector<char> buf;
				bencode(std::back_inserter(buf), announce);
			
				send_response(s, ec, 200, "OK", 0, buf.size());
				write(s, boost::asio::buffer(&buf[0], buf.size()), boost::asio::transfer_all(), ec);
			}

//			fprintf(stderr, ">> serving file %s\n", path.c_str());
			std::vector<char> file_buf;
			// remove the / from the path
			path = path.substr(1);
			int res = load_file(path, file_buf);
			if (res == -1)
			{
				send_response(s, ec, 404, "Not Found", 0, 0);
				continue;
			}

			if (res != 0)
			{
				// this means the file was either too big or couldn't be read
				send_response(s, ec, 503, "Internal Error", 0, 0);
				continue;
			}

			// serve file

			char const* extra_header = 0;

			if (extension(path) == ".gz")
			{
				extra_header = "Content-Encoding: gzip\r\n";
			}

			if (!p.header("range").empty())
			{
				std::string range = p.header("range");
				int start, end;
				sscanf(range.c_str(), "bytes=%d-%d", &start, &end);
				char eh[200];
				snprintf(eh, sizeof(eh), "%sContent-Range: bytes %d-%d\r\n"
						, extra_header ? extra_header : "", start, end);
				send_response(s, ec, 206, "Partial", eh, end - start + 1);
				if (!file_buf.empty())
				{
					write(s, boost::asio::buffer(&file_buf[0] + start, end - start + 1)
						, boost::asio::transfer_all(), ec);
				}
//				fprintf(stderr, "send %d bytes of payload\n", end - start + 1);
			}
			else
			{
				send_response(s, ec, 200, "OK", extra_header, file_buf.size());
				write(s, boost::asio::buffer(&file_buf[0], file_buf.size()), boost::asio::transfer_all(), ec);
			}
//			fprintf(stderr, "%d bytes left in receive buffer. offset: %d\n", len - offset, offset);
		} while (offset < len);
	}
	fprintf(stderr, "exiting web server thread\n");
}


