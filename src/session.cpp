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

#include <ctime>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>
#include <algorithm>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/url_handler.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isprint;
};
#endif

namespace libtorrent
{
	namespace detail
	{
		void checker_impl::operator()()
		{
			eh_initializer();
			for (;;)
			{
				piece_checker_data* t;
				{
					boost::mutex::scoped_lock l(m_mutex);

					// if the job queue is empty and
					// we shouldn't abort
					// wait for a signal
					if (m_torrents.empty() && !m_abort)
						m_cond.wait(l);

					if (m_abort) return;

					assert(!m_torrents.empty());
					
					t = &m_torrents.front();
					if (t->abort)
					{
						m_torrents.pop_front();
						continue;
					}
				}

				try
				{
					t->torrent_ptr->allocate_files(t, m_mutex, t->save_path);
					// lock the session to add the new torrent

					boost::mutex::scoped_lock l(m_mutex);
					if (!t->abort)
					{
						boost::mutex::scoped_lock l(m_ses->m_mutex);

						m_ses->m_torrents.insert(
							std::make_pair(t->info_hash, t->torrent_ptr)).first;
					}
				}
				catch(...)
				{
#ifndef NDEBUG
					std::cerr << "error while checking files\n";
#endif
				}

				// remove ourself from the 'checking'-list
				// (we're no longer in the checking state)
				boost::mutex::scoped_lock l(m_mutex);
				m_torrents.pop_front();
			}
		}

		detail::piece_checker_data* checker_impl::find_torrent(const sha1_hash& info_hash)
		{
			for (std::deque<piece_checker_data>::iterator i
				= m_torrents.begin();
				i != m_torrents.end();
				++i)
			{
				if (i->info_hash == info_hash) return &(*i);
			}
			return 0;
		}

		session_impl::session_impl(int listen_port,
			const std::string& cl_fprint)
			: m_abort(false)
			, m_tracker_manager(m_settings)
			, m_listen_port(listen_port)
			, m_upload_rate(-1)
		{

			// ---- generate a peer id ----

			std::srand(std::time(0));

			const int len1 = std::min(cl_fprint.length(), (std::size_t)7);
			const int len2 = 12 - len1;

			// the client's fingerprint
			std::copy(cl_fprint.begin(), cl_fprint.begin()+len2, m_peer_id.begin());

			// the zeros
			std::fill(m_peer_id.begin()+len1, m_peer_id.begin()+len1+len2, 0);
			assert(len1 + len2 == 12);

			// the random number
			for (unsigned char* i = m_peer_id.begin()+len1+len2;
				i != m_peer_id.end();
				++i)
			{
				*i = rand();
			}
		}


		void session_impl::operator()()
		{
			eh_initializer();
#ifndef NDEBUG
			m_logger = create_log("main session");
#endif

			boost::shared_ptr<socket> listener(new socket(socket::tcp, false));
			int max_port = m_listen_port + 9;


			// create listener socket

			for(;;)
			{
				try
				{
					listener->listen(m_listen_port, 5);
				}
				catch(network_error&)
				{
					if (m_listen_port > max_port)
						throw;
					m_listen_port++;
					continue;
				}
				break;
			}

#ifndef NDEBUG
			(*m_logger) << "listening on port: " << m_listen_port << "\n";
#endif
			m_selector.monitor_readability(listener);
			m_selector.monitor_errors(listener);

		/*
			// temp
			const peer& p = *m_peer_list.begin();
			boost::shared_ptr<libtorrent::socket> s(new socket(socket::tcp, false));
			address a(p.ip, p.port);
			s->connect(a);

			m_connections.insert(std::make_pair(s, peer_connection(this, s, p.id)));
			m_selector.monitor_readability(s);
			m_selector.monitor_errors(s);
			// ~temp
		*/


			std::vector<boost::shared_ptr<socket> > readable_clients;
			std::vector<boost::shared_ptr<socket> > writable_clients;
			std::vector<boost::shared_ptr<socket> > error_clients;
			boost::posix_time::ptime timer = boost::posix_time::second_clock::local_time();

			for(;;)
			{

#ifndef NDEBUG
				for (connection_map::iterator i = m_connections.begin();
					i != m_connections.end();
					++i)
				{
					assert(i->second->has_data() == m_selector.is_writability_monitored(i->first));
				}
#endif


				// if nothing happens within 500000 microseconds (0.5 seconds)
				// do the loop anyway to check if anything else has changed
		//		(*m_logger) << "sleeping\n";
				m_selector.wait(500000, readable_clients, writable_clients, error_clients);

				boost::mutex::scoped_lock l(m_mutex);

				// +1 for the listen socket
				assert(m_selector.count_read_monitors() == m_connections.size() + 1);

				if (m_abort)
				{
					m_tracker_manager.abort_all_requests();
					for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i =
							m_torrents.begin();
						i != m_torrents.end();
						++i)
					{
						i->second->abort();
						m_tracker_manager.queue_request(i->second->generate_tracker_request(m_listen_port));
					}
					m_connections.clear();
					m_torrents.clear();
					break;
				}


				// ************************
				// RECEIVE SOCKETS
				// ************************

				// let the readable clients receive data
				for (std::vector<boost::shared_ptr<socket> >::iterator i = readable_clients.begin();
					i != readable_clients.end();
					++i)
				{

					// special case for listener socket
					if (*i == listener)
					{
						boost::shared_ptr<libtorrent::socket> s = (*i)->accept();
						if (s)
						{
							// we got a connection request!
#ifndef NDEBUG
							(*m_logger) << s->sender().as_string() << " <== INCOMING CONNECTION\n";
#endif
							// TODO: the send buffer size should be controllable from the outside
//							s->set_send_bufsize(2048);

							// TODO: add some possibility to filter IP:s
							boost::shared_ptr<peer_connection> c(
								new peer_connection(this, m_selector, s));
							if (m_upload_rate != -1) c->set_send_quota(100);
							m_connections.insert(std::make_pair(s, c));
							m_selector.monitor_readability(s);
							m_selector.monitor_errors(s);
						}
						continue;
					}

					connection_map::iterator p = m_connections.find(*i);
					if(p == m_connections.end())
					{
						m_selector.remove(*i);
					}
					else
					{
						try
						{
			//				(*m_logger) << "readable: " << p->first->sender().as_string() << "\n";
							p->second->receive_data();
						}
						catch(network_error&)
						{
							// the connection wants to disconnect for some reason, remove it
							// from the connection-list
							m_selector.remove(*i);
							m_connections.erase(p);
						}
					}
				}


				// ************************
				// SEND SOCKETS
				// ************************

				// let the writable clients send data
				for (std::vector<boost::shared_ptr<socket> >::iterator i
					= writable_clients.begin();
					i != writable_clients.end();
					++i)
				{
					connection_map::iterator p = m_connections.find(*i);
					// the connection may have been disconnected in the receive phase
					if (p == m_connections.end())
					{
						m_selector.remove(*i);
					}
					else
					{
						try
						{
							assert(m_selector.is_writability_monitored(p->first));
							assert(p->second->has_data());
			//				(*m_logger) << "writable: " << p->first->sender().as_string() << "\n";
							p->second->send_data();
						}
						catch(network_error&)
						{
							// the connection wants to disconnect for some reason, remove it
							// from the connection-list
							m_selector.remove(*i);
							m_connections.erase(p);
						}
					}
				}



				// ************************
				// ERROR SOCKETS
				// ************************


				// disconnect the one we couldn't connect to
				for (std::vector<boost::shared_ptr<socket> >::iterator i = error_clients.begin();
					i != error_clients.end();
					++i)
				{
					connection_map::iterator p = m_connections.find(*i);

					m_selector.remove(*i);
					// the connection may have been disconnected in the receive or send phase
					if (p != m_connections.end()) m_connections.erase(p);
				}

#ifndef NDEBUG
				for (connection_map::iterator i = m_connections.begin();
					i != m_connections.end();
					++i)
				{
					assert(i->second->has_data() == m_selector.is_writability_monitored(i->first));
				}
#endif


				boost::posix_time::time_duration d = boost::posix_time::second_clock::local_time() - timer;
				if (d.seconds() < 1) continue;
				timer = boost::posix_time::second_clock::local_time();


				// ************************
				// THE SECTION BELOW IS EXECUTED ONCE EVERY SECOND
				// ************************

				// distribute the maximum upload rate among the peers
				// TODO: implement an intelligent algorithm that
				// will shift bandwidth from the peers that can't
				// utilize all their assigned bandwidth to the peers
				// that actually can maintain the upload rate.
				if (m_upload_rate != -1 && !m_connections.empty())
				{
					assert(m_upload_rate >= 0);
					int share = m_upload_rate / m_connections.size();
					for (connection_map::iterator i = m_connections.begin();
						i != m_connections.end();
						++i)
					{
						i->second->set_send_quota(share);
					}
				}

				// do the second_tick() on each connection
				// this will update their statistics (download and upload speeds)
				// also purge sockets that have timed out
				// and keep sockets open by keeping them alive.
				for (connection_map::iterator i = m_connections.begin();
					i != m_connections.end();)
				{
					i->second->second_tick();

					connection_map::iterator j = i;
					++i;
					// if this socket has timed out
					// close it.
					if (j->second->has_timed_out())
					{
						m_selector.remove(j->first);
						m_connections.erase(j);
						continue;
					}

					j->second->keep_alive();
				}

				// check each torrent for abortion or
				// tracker updates
				for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i
					= m_torrents.begin();
					i != m_torrents.end();)
				{
					if (i->second->is_aborted())
					{
						m_tracker_manager.queue_request(
							i->second->generate_tracker_request(m_listen_port));
						i->second->close_all_connections();
						std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator j = i;
						++i;
						m_torrents.erase(j);
						continue;
					}
					else if (i->second->should_request())
					{
						m_tracker_manager.queue_request(
							i->second->generate_tracker_request(m_listen_port),
							boost::get_pointer(i->second));
					}
					++i;
				}
				m_tracker_manager.tick();

#ifndef NDEBUG
				(*m_logger) << "peers: " << m_connections.size() << "                           \n";
				for (connection_map::iterator i = m_connections.begin();
					i != m_connections.end();
					++i)
				{
					(*m_logger) << "h: " << i->first->sender().as_string()
						<< " | down: " << i->second->statistics().download_rate()
						<< " b/s | up: " << i->second->statistics().upload_rate()
						<< " b/s             \n";
				}
#endif
			}

			while (!m_tracker_manager.send_finished())
			{
				m_tracker_manager.tick();
				boost::xtime t;
				boost::xtime_get(&t, boost::TIME_UTC);
				t.nsec += 1000000;
				boost::thread::sleep(t);
			}
		}


		// the return value from this function is valid only as long as the
		// session is locked!
		torrent* session_impl::find_torrent(const sha1_hash& info_hash)
		{
			std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i
				= m_torrents.find(info_hash);
			if (i != m_torrents.end()) return boost::get_pointer(i->second);
			return 0;
		}

#ifndef NDEBUG
		boost::shared_ptr<logger> session_impl::create_log(std::string name)
		{
			name = "libtorrent_log_" + name + ".log";
			// current options are file_logger and cout_logger
#if defined(TORRENT_VERBOSE_LOGGING)
			return boost::shared_ptr<logger>(new file_logger(name.c_str()));
#else
			return boost::shared_ptr<logger>(new null_logger());
#endif
		}
#endif

	}

	session::session(int listen_port, const std::string& fingerprint)
		: m_impl(listen_port, fingerprint)
		, m_checker_impl(&m_impl)
		, m_thread(boost::ref(m_impl))
		, m_checker_thread(boost::ref(m_checker_impl))
	{
#ifndef NDEBUG
		boost::function0<void> test = boost::ref(m_impl);
		assert(!test.empty());
#endif
	}


	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session::add_torrent(
		const torrent_info& ti
		, const boost::filesystem::path& save_path)
	{

		{
			// lock the session
			boost::mutex::scoped_lock l(m_impl.m_mutex);

			// is the torrent already active?
			if (m_impl.find_torrent(ti.info_hash()))
				throw duplicate_torrent();
		}

		{
			// lock the checker_thread
			boost::mutex::scoped_lock l(m_checker_impl.m_mutex);

			// is the torrent currently being checked?
			if (m_checker_impl.find_torrent(ti.info_hash()))
				throw duplicate_torrent();
		}

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		// TODO: have a queue of checking torrents instead of
		// having them all run at the same time
		boost::shared_ptr<torrent> torrent_ptr(new torrent(&m_impl, ti));

		detail::piece_checker_data d;
		d.torrent_ptr = torrent_ptr;
		d.save_path = save_path;
		d.info_hash = ti.info_hash();

		// lock the checker thread
		boost::mutex::scoped_lock l(m_checker_impl.m_mutex);

		// add the torrent to the queue to be checked
		m_checker_impl.m_torrents.push_back(d);
		// and notify the thread that it got another
		// job in its queue
		m_checker_impl.m_cond.notify_one();

		return torrent_handle(&m_impl, &m_checker_impl, ti.info_hash());
	}

	void session::remove_torrent(const torrent_handle& h)
	{
		if (h.m_ses != &m_impl) return;
		// TODO: move the code of abort() here instead of calling it
		h.abort();
	}

	void session::set_http_settings(const http_settings& s)
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_settings = s;
	}

	session::~session()
	{
		{
			boost::mutex::scoped_lock l(m_impl.m_mutex);
			m_impl.m_abort = true;
		}

		{
			boost::mutex::scoped_lock l(m_checker_impl.m_mutex);
			// abort the checker thread
			m_checker_impl.m_abort = true;

			// abort the currently checking torrent
			if (!m_checker_impl.m_torrents.empty())
			{
				m_checker_impl.m_torrents.front().abort = true;
			}
			m_checker_impl.m_cond.notify_one();
		}

		m_thread.join();
		m_checker_thread.join();
	}

	void session::set_upload_rate_limit(int bytes_per_second)
	{
		assert(bytes_per_second > 0);
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_upload_rate = bytes_per_second;
		if (m_impl.m_upload_rate != -1 || !m_impl.m_connections.empty())
			return;

		for (detail::session_impl::connection_map::iterator i
			= m_impl.m_connections.begin();
			i != m_impl.m_connections.end();)
		{
			i->second->set_send_quota(-1);
		}
	}

	// TODO: document
	// TODO: if the first 4 charachters are printable
	// maybe they should be considered a fingerprint?
	std::string extract_fingerprint(const peer_id& p)
	{
		std::string ret;
		const unsigned char* c = p.begin();
		while (c != p.end() && *c != 0)
		{
			if (std::isprint(*c))
				ret += *c;
			else if (*c <= 9)
				ret += '0'+ *c;
			else
				return std::string();
			++c;
		}
		if (c == p.end()) return std::string();

		return ret;
	}
}
