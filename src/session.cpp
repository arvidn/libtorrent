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
};
#endif

/*

DESIGN OVERVIEW AND RATIONALE

The main goal of this library is to be efficient, primarily memory-wise
but also CPU-wise. This goal has a number of implications:

* It must handle multiple torrents (multiple processes uses much more memory)
* It relies on a well working disk chache, since it will download directly to disk. This is
  to scale better when having many peer connections.
*





*/

namespace
{
	using namespace libtorrent;

	peer_id generate_peer_id()
	{
		peer_id ret;
		std::srand(std::time(0));
//		unsigned char fingerprint[] = "h\0\0\0\0\0\0\0\0\0";
		unsigned char fingerprint[] = "h\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
		const int len = sizeof(fingerprint)-1;
		std::copy(fingerprint, fingerprint+len, ret.begin());
		for (unsigned char* i = ret.begin()+len; i != ret.end(); ++i) *i = rand();
		return ret;
	}

}

namespace libtorrent
{
	namespace detail
	{
		void session_impl::run(int listen_port)
		{
#ifndef NDEBUG
			m_logger = boost::shared_ptr<logger>(
				m_log_spawner->create_logger("main session"));
#endif

			m_peer_id = generate_peer_id();

			boost::shared_ptr<socket> listener(new socket(socket::tcp, false));
			int max_port = listen_port + 9;


			// create listener socket

			for(;;)
			{
				try
				{
					listener->listen(listen_port, 5);
				}
				catch(network_error&)
				{
					if (listen_port > max_port) throw;
					listen_port++;
					continue;
				}
				break;
			}

			std::cout << "listening on port: " << listen_port << "\n";   
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
				// if nothing happens within 500000 microseconds (0.5 seconds)
				// do the loop anyway to check if anything else has changed
		//		(*m_logger) << "sleeping\n";
				m_selector.wait(500000, readable_clients, writable_clients, error_clients);

				boost::mutex::scoped_lock l(m_mutex);

				if (m_abort)
				{
					m_tracker_manager.abort_all_requests();
					for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i =
							m_torrents.begin();
						i != m_torrents.end();
						++i)
					{
						i->second->abort();
						m_tracker_manager.queue_request(i->second->generate_tracker_request(listen_port));
					}
					m_connections.clear();
					m_torrents.clear();
					break;
				}

				// ************************
				// RECEIVE SOCKETS
				// ************************

				// TODO: once every second or so, all sockets should receive_data() to purge connections
				// that has been closed. Otherwise we have to wait 2 minutes for their timeout

				// let the readable clients receive data
				for (std::vector<boost::shared_ptr<socket> >::iterator i = readable_clients.begin(); i != readable_clients.end(); ++i)
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
							s->set_send_bufsize(2048);

							// TODO: add some possibility to filter IP:s
							boost::shared_ptr<peer_connection> c(new peer_connection(this, s));
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
				for (std::vector<boost::shared_ptr<socket> >::iterator i = writable_clients.begin(); i != writable_clients.end(); ++i)
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
				for (std::vector<boost::shared_ptr<socket> >::iterator i = error_clients.begin(); i != error_clients.end(); ++i)
				{
					connection_map::iterator p = m_connections.find(*i);

					m_selector.remove(*i);
					// the connection may have been disconnected in the receive or send phase
					if (p != m_connections.end()) m_connections.erase(p);
				}


				// clear all writablility monitors and add
				// the ones who still has data to send
				m_selector.clear_writable();


				// ************************
				// BUILD WRITER LIST
				// ************************


				// loop over all clients and purge the ones that has timed out
				// and check if they have pending data to be sent
				for (connection_map::iterator i = m_connections.begin(); i != m_connections.end();)
				{
					connection_map::iterator j = i;
					++i;
					if (j->second->has_timed_out())
					{
						m_selector.remove(j->first);
						m_connections.erase(j);
					}
					else
					{
						j->second->keep_alive();
						if (j->second->has_data())
						{
		//					(*m_logger) << j->first->sender().as_string() << " has data\n";
							m_selector.monitor_writability(j->first);
						}
						else
						{
		//					(*m_logger) << j->first->sender().as_string() << " has NO data\n";
						}
					}
				}

		//      (*m_logger) << "time: " << std::clock()-timer << "\n"; 
				boost::posix_time::time_duration d = boost::posix_time::second_clock::local_time() - timer;
				if (d.seconds() < 1) continue;
				timer = boost::posix_time::second_clock::local_time();


				// ************************
				// THE SECTION BELOW IS EXECUTED ONCE EVERY SECOND
				// ************************


				// do the second_tick() on each connection
				// this will update their statistics (download and upload speeds)
				for (connection_map::iterator i = m_connections.begin(); i != m_connections.end(); ++i)
				{
					i->second->second_tick();
				}

				// check each torrent for abortion or
				// tracker updates
				for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i = m_torrents.begin();
					i != m_torrents.end();)
				{
					if (i->second->is_aborted())
					{
						m_tracker_manager.queue_request(i->second->generate_tracker_request(listen_port));
						i->second->close_all_connections();
						std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator j = i;
						++i;
						m_torrents.erase(j);
						continue;
					}
					else if (i->second->should_request())
					{
						m_tracker_manager.queue_request(
							i->second->generate_tracker_request(listen_port),
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

				m_logger->clear();
#else
				std::cout << "peers: " << m_connections.size() << "                           \n";
				for (connection_map::iterator i = m_connections.begin();
					i != m_connections.end();
					++i)
				{
					std::cout << "h: " << i->first->sender().as_string()
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

		torrent* session_impl::find_torrent(const sha1_hash& info_hash)
		{
			std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i = m_torrents.find(info_hash);
			if (i == m_torrents.end()) return 0;
			return boost::get_pointer(i->second);
		}


		void piece_check_thread::operator()()
		{
			// TODO: implement a way to abort a file check and
			// to get feedback on how much of the data that has
			// been checked and how much of the file we have
			// (which should be about the same thing with the
			// new allocation model)
			try
			{
				m_data->torrent_ptr->allocate_files(m_data->save_path);
			}
			catch(...)
			{
				std::cout << "error while checking files\n";
			}

			// lock the session to add the new torrent
			session_impl* ses = m_data->ses;
			boost::mutex::scoped_lock l(ses->m_mutex);

			ses->m_torrents.insert(
				std::make_pair(m_data->info_hash, m_data->torrent_ptr)).first;

			// remove ourself from the 'checking'-list
			// (we're no longer in the checking state)
			assert(ses->m_checkers.find(m_data->info_hash) != ses->m_checkers.end());
			ses->m_checkers.erase(ses->m_checkers.find(m_data->info_hash));
		}

	}

	torrent_handle session::add_torrent(const torrent_info& ti, const std::string& save_path)
	{
		// lock the session
		boost::mutex::scoped_lock l(m_impl.m_mutex);

		// is the torrent already active?
		// TODO: Maybe this should throw?
		if (m_impl.m_torrents.find(ti.info_hash()) != m_impl.m_torrents.end())
			return torrent_handle(&m_impl, ti.info_hash());

		// is the torrent currently being checked?
		if (m_impl.m_checkers.find(ti.info_hash()) != m_impl.m_checkers.end())
			return torrent_handle(&m_impl, ti.info_hash());

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(new torrent(&m_impl, ti));

		boost::shared_ptr<detail::piece_checker_data> d(new detail::piece_checker_data);
		d->torrent_ptr = torrent_ptr;
		d->save_path = save_path;
		d->ses = &m_impl;
		d->info_hash = ti.info_hash();

		m_impl.m_checkers.insert(std::make_pair(ti.info_hash(), d));
		m_impl.m_checker_threads.create_thread(detail::piece_check_thread(d));

		return torrent_handle(&m_impl, ti.info_hash());
	}

	void session::set_http_settings(const http_settings& s)
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_tracker_manager.set_settings(s);
	}

	session::~session()
	{
		{
			boost::mutex::scoped_lock l(m_impl.m_mutex);
			m_impl.m_abort = true;
		}

		m_thread.join();
	}


	float torrent_handle::progress() const
	{
		if (m_ses == 0) return 0.f;
		boost::mutex::scoped_lock l(m_ses->m_mutex);

		std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i = m_ses->m_torrents.find(m_info_hash);
		if (i == m_ses->m_torrents.end()) return 0.f;
		return i->second->progress();
	}

	void torrent_handle::get_peer_info(std::vector<peer_info>& v)
	{
		v.clear();
		if (m_ses == 0) return;
		boost::mutex::scoped_lock l(m_ses->m_mutex);
		
		std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i = m_ses->m_torrents.find(m_info_hash);
		if (i == m_ses->m_torrents.end()) return;

		const torrent* t = boost::get_pointer(i->second);

		for (std::vector<peer_connection*>::const_iterator i = t->begin();
			i != t->end();
			++i)
		{
			peer_connection* peer = *i;
			v.push_back(peer_info());
			peer_info& p = v.back();

			const stat& statistics = peer->statistics();
			p.down_speed = statistics.download_rate();
			p.up_speed = statistics.upload_rate();
			p.id = peer->get_peer_id();
			p.ip = peer->get_socket()->sender();

			p.flags = 0;
			if (peer->is_interesting()) p.flags |= peer_info::interesting;
			if (peer->has_choked()) p.flags |= peer_info::choked;
			if (peer->is_peer_interested()) p.flags |= peer_info::remote_interested;
			if (peer->has_peer_choked()) p.flags |= peer_info::remote_choked;

			p.pieces = peer->get_bitfield();
		}
	}

	void torrent_handle::abort()
	{
		if (m_ses == 0) return;
		boost::mutex::scoped_lock l(m_ses->m_mutex);

		std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i = m_ses->m_torrents.find(m_info_hash);
		if (i == m_ses->m_torrents.end()) return;
		i->second->abort();
		m_ses = 0;
	}

}
