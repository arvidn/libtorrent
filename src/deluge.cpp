/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include <deque>
#include <getopt.h> // for getopt_long
#include <stdlib.h> // for daemon()
#include <syslog.h>
#include <boost/bind.hpp>

#include "libtorrent/session.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/puff.hpp"
#include "deluge.hpp"

using namespace libtorrent;

namespace io = libtorrent::detail;

deluge::deluge(session& s, std::string pem_path)
	: m_ses(s)
	, m_listen_socket(NULL)
	, m_accept_thread(NULL)
	, m_context(m_ios, boost::asio::ssl::context::sslv23)
	, m_shutdown(false)
{
	m_context.set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);
	error_code ec;
//	m_context.set_password_callback(boost::bind(&server::get_password, this));
	m_context.use_certificate_chain_file(pem_path.c_str(), ec);
	if (ec)
	{
		fprintf(stderr, "use cert: %s\n", ec.message().c_str());
		return;
	}
	m_context.use_private_key_file(pem_path.c_str(), boost::asio::ssl::context::pem, ec);
	if (ec)
	{
		fprintf(stderr, "use key: %s\n", ec.message().c_str());
		return;
	}
//	m_context.use_tmp_dh_file("dh512.pem");
}

deluge::~deluge()
{
}

void deluge::accept_thread(int port)
{
	socket_acceptor socket(m_ios);
	m_listen_socket = &socket;

	error_code ec;
	socket.open(tcp::v4(), ec);
	if (ec)
	{
		fprintf(stderr, "open: %s\n", ec.message().c_str());
		return;
	}
	socket.set_option(socket_acceptor::reuse_address(true), ec);
	if (ec)
	{
		fprintf(stderr, "reuse address: %s\n", ec.message().c_str());
		return;
	}
	socket.bind(tcp::endpoint(address_v4::any(), port), ec);
	if (ec)
	{
		fprintf(stderr, "bind: %s\n", ec.message().c_str());
		return;
	}
	socket.listen(5, ec);
	if (ec)
	{
		fprintf(stderr, "listen: %s\n", ec.message().c_str());
		return;
	}

	TORRENT_ASSERT(m_threads.empty());
	for (int i = 0; i < 5; ++i)
		m_threads.push_back(new thread(boost::bind(&deluge::connection_thread, this)));

	do_accept();
	m_ios.run();

	for (std::vector<thread*>::iterator i = m_threads.begin()
		, end(m_threads.end()); i != end; ++i)
	{
		(*i)->join();
		delete *i;
	}

	mutex::scoped_lock l(m_mutex);
	m_threads.clear();

	for (std::vector<ssl_socket*>::iterator i = m_jobs.begin()
		, end(m_jobs.end()); i != end; ++i)
	{
		delete *i;
	}
	m_jobs.clear();
}

void deluge::do_accept()
{
	TORRENT_ASSERT(!m_shutdown);
	ssl_socket* sock = new ssl_socket(m_ios, m_context);
	m_listen_socket->async_accept(sock->lowest_layer(), boost::bind(&deluge::on_accept, this, _1, sock));
}

void deluge::on_accept(error_code const& ec, ssl_socket* sock)
{
	if (ec)
	{
		delete sock;
		do_stop();
		return;
	}

	fprintf(stderr, "accepted connection\n");
	mutex::scoped_lock l(m_mutex);
	m_jobs.push_back(sock);
	m_cond.signal(l);
	l.unlock();

	do_accept();
}

void deluge::connection_thread()
{
	mutex::scoped_lock l(m_mutex);
	while (!m_shutdown)
	{
		l.lock();
		while (!m_shutdown && m_jobs.empty())
			m_cond.wait(l);

		if (m_shutdown) return;

		fprintf(stderr, "connection thread woke up: %d\n", int(m_jobs.size()));
		
		ssl_socket* sock = m_jobs.front();
		m_jobs.erase(m_jobs.begin());
		l.unlock();

		error_code ec;
		sock->handshake(boost::asio::ssl::stream_base::server, ec);
		if (ec)
		{
			fprintf(stderr, "ssl handshake: %s\n", ec.message().c_str());
			sock->lowest_layer().close(ec);
			delete sock;
			continue;
		}
		fprintf(stderr, "SSL handshake done\n");

		std::vector<char> buffer;
		std::vector<char> inflated;
		do
		{
			buffer.resize(1024);
			int buffer_use = 0;
			error_code ec;

			int ret = 0;
			boost::uint32_t inlen = 0;
			boost::uint32_t outlen = 0;
read_some_more:
			ret = sock->read_some(asio::buffer(&buffer[buffer_use], buffer.size() - buffer_use), ec);
			fprintf(stderr, "read %d bytes\n", ret);
			if (ec)
			{
				fprintf(stderr, "read: %s\n", ec.message().c_str());
				break;
			}
			TORRENT_ASSERT(ret > 0);

			buffer_use += ret;

			inlen = buffer.size();
			inflated.resize(inlen * 5);
			outlen = inflated.size();
			
			ret = puff((unsigned char*)&inflated[0], &outlen, (unsigned char*)&buffer[0], &inlen);

			// 2 means the input data wasn't terminated
			// properly. We need to read more from the socket
			if (ret == 2)
			{
				if (buffer_use + 512 > buffer.size())
				{
					// don't let the client send infinitely
					// big messages
					if (buffer_use > 1024 * 1024)
					{
						fprintf(stderr, "compressed message size exceeds 1 MB\n");
						break;
					}
					// make sure we have enough space in the
					// incoming buffer.
					buffer.resize(buffer_use + buffer_use / 2);
				}
				goto read_some_more;
			}

			if (ret == 1)
			{
				fprintf(stderr, "inflated message size is greater than %d bytes\n"
					, int(buffer.size() * 5));
				break;
			}

			if (ret != 0)
			{
				fprintf(stderr, "inflate failed: %d\n", ret);
				break;
			}

			fprintf(stderr, "incoming message: %d bytes\n", inlen);

			fprintf(stderr, "message: %s\n", &inflated[0]);

			// TODO: rdecode the message
			// TODO: pass on message to handler

		} while (!m_shutdown);

		fprintf(stderr, "closing connection\n");
		sock->shutdown(ec);
		sock->lowest_layer().close(ec);
		delete sock;
	}

}

void deluge::start(int port)
{
	if (m_accept_thread)
		stop();

	m_accept_thread = new thread(boost::bind(&deluge::accept_thread, this, port));
}

void deluge::do_stop()
{
	mutex::scoped_lock l(m_mutex);
	m_shutdown = true;
	m_cond.signal_all(l);
	if (m_listen_socket)
	{
		m_listen_socket->close();
		m_listen_socket = NULL;
	}
}

void deluge::stop()
{
	m_ios.post(boost::bind(&deluge::do_stop, this));

	m_accept_thread->join();
	delete m_accept_thread;
	m_accept_thread = NULL;
}

