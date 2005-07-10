/*

Copyright (c) 2003, Arvid Norberg, Magnus Jonsson
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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/limits.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/allocate_resources.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/ip_filter.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isprint;
};
#endif

using namespace boost::posix_time;

namespace libtorrent { namespace detail
{

	std::string generate_auth_string(std::string const& user
		, std::string const& passwd)
	{
		if (user.empty()) return std::string();
		return user + ":" + passwd;
	}
	
	// This is the checker thread
	// it is looping in an infinite loop
	// until the session is aborted. It will
	// normally just block in a wait() call,
	// waiting for a signal from session that
	// there's a new torrent to check.

	void checker_impl::operator()()
	{
		eh_initializer();
		for (;;)
		{
			piece_checker_data* t = 0;
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
				t->processing = true;
			}

			try
			{
				assert(t != 0);
				std::string error_msg;
				t->parse_resume_data(t->resume_data, t->torrent_ptr->torrent_file(), error_msg);

				// clear the resume data now that it has been used
				// (the fast resume data is now parsed and stored in t)
				t->resume_data = entry();
				t->torrent_ptr->check_files(*t, m_mutex);
				// lock the session to add the new torrent

				boost::mutex::scoped_lock l(m_mutex);
				if (t->abort)
				{
					m_torrents.pop_front();
					continue;
				}
				boost::mutex::scoped_lock l2(m_ses.m_mutex);

				if (!error_msg.empty() && m_ses.m_alerts.should_post(alert::warning))
				{
					m_ses.m_alerts.post_alert(fastresume_rejected_alert(
						t->torrent_ptr->get_handle()
						, error_msg));
					
				}
				
				m_ses.m_torrents.insert(std::make_pair(t->info_hash, t->torrent_ptr));
				if (t->torrent_ptr->is_seed() && m_ses.m_alerts.should_post(alert::info))
				{
					m_ses.m_alerts.post_alert(torrent_finished_alert(
						t->torrent_ptr->get_handle()
						, "torrent is complete"));
				}

				peer_id id;
				std::fill(id.begin(), id.end(), 0);
				for (std::vector<address>::const_iterator i = t->peers.begin();
					i != t->peers.end(); ++i)
				{
					t->torrent_ptr->get_policy().peer_from_tracker(*i, id);
				}
				m_torrents.pop_front();
			}
			catch(const std::exception& e)
			{
				// This will happen if the storage fails to initialize
				boost::mutex::scoped_lock l(m_mutex);
				boost::mutex::scoped_lock l2(m_ses.m_mutex);
				if (m_ses.m_alerts.should_post(alert::fatal))
				{
					m_ses.m_alerts.post_alert(
						file_error_alert(
							t->torrent_ptr->get_handle()
							, e.what()));
				}
				m_torrents.pop_front();
			}
			catch(...)
			{
#ifndef NDEBUG
				std::cerr << "error while checking files\n";
#endif
				assert(false);
				boost::mutex::scoped_lock l(m_mutex);
				m_torrents.pop_front();
			}
		}
	}

	detail::piece_checker_data* checker_impl::find_torrent(sha1_hash const& info_hash)
	{
		for (std::deque<piece_checker_data>::iterator i
			= m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			if (i->info_hash == info_hash) return &(*i);
		}
		return 0;
	}

	void checker_impl::remove_torrent(sha1_hash const& info_hash)
	{
		for (std::deque<piece_checker_data>::iterator i
			= m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			if (i->info_hash == info_hash)
			{
				m_torrents.erase(i);
				return;
			}
		}
	}

	session_impl::session_impl(
		std::pair<int, int> listen_port_range
		, const fingerprint& cl_fprint
		, const char* listen_interface = 0)
		: m_tracker_manager(m_settings)
		, m_listen_port_range(listen_port_range)
		, m_listen_interface(listen_interface, listen_port_range.first)
		, m_abort(false)
		, m_upload_rate(-1)
		, m_download_rate(-1)
		, m_max_uploads(-1)
		, m_max_connections(-1)
		, m_incoming_connection(false)
	{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		m_logger = create_log("main_session");
#endif
		std::fill(m_extension_enabled, m_extension_enabled
			+ peer_connection::num_supported_extensions, true);
		// ---- generate a peer id ----

		std::srand((unsigned int)std::time(0));

		m_key = rand() + (rand() << 15) + (rand() << 30);
		std::string print = cl_fprint.to_string();
		assert(print.length() <= 20);

		// the client's fingerprint
		std::copy(
			print.begin()
			, print.begin() + print.length()
			, m_peer_id.begin());

		// http-accepted characters:
		static char const printable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_.!~*'()";

		// the random number
		for (unsigned char* i = m_peer_id.begin() + print.length();
			i != m_peer_id.end();
			++i)
		{
			*i = printable[rand() % (sizeof(printable)-1)];
		}
	}

	bool session_impl::extensions_enabled() const
	{
		const int n = peer_connection::num_supported_extensions;
		return std::find(m_extension_enabled
			, m_extension_enabled + n, true) != m_extension_enabled + n;
	}

	void session_impl::purge_connections()
	{
		while (!m_disconnect_peer.empty())
		{
			m_connections.erase(m_disconnect_peer.back());
			m_disconnect_peer.pop_back();
		}
	}

	void session_impl::open_listen_port()
	{
		try
		{
			// create listener socket
			m_selector.remove(m_listen_socket);
			m_listen_socket = boost::shared_ptr<socket>(new socket(socket::tcp, false));

			for(;;)
			{
				try
				{
					m_listen_socket->listen(m_listen_interface, 10);
					break;
				}
				catch (network_error& e)
				{
					if (e.error_code() == socket::address_not_available)
					{
						if (m_alerts.should_post(alert::fatal))
						{
							std::string msg = "cannot listen on the given interface '" + m_listen_interface.as_string() + "'";
							m_alerts.post_alert(listen_failed_alert(msg));
						}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						std::string msg = "cannot listen on the given interface '" + m_listen_interface.as_string() + "'";
						(*m_logger) << msg << "\n";
#endif
						assert(m_listen_socket.unique());
						m_listen_socket.reset();
						break;
					}
					m_listen_interface.port++;
					if (m_listen_interface.port > m_listen_port_range.second)
					{
						std::stringstream msg;
						msg << "none of the ports in the range ["
							<< m_listen_port_range.first
							<< ", " << m_listen_port_range.second
							<< "] could be opened for listening";
						m_alerts.post_alert(listen_failed_alert(msg.str()));
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						(*m_logger) << msg.str() << "\n";
#endif
						m_listen_socket.reset();
						break;
					}
				}
			}
		}
		catch (network_error& e)
		{
			m_alerts.post_alert(listen_failed_alert(e.what()));
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (m_listen_socket)
		{
			(*m_logger) << "listening on port: " << m_listen_interface.port << "\n";
		}
#endif
		if (m_listen_socket)
		{
			m_selector.monitor_readability(m_listen_socket);
			m_selector.monitor_errors(m_listen_socket);
		}
	}


	void session_impl::operator()()
	{
		eh_initializer();

		if (m_listen_port_range.first != 0 && m_listen_port_range.second != 0)
		{
			boost::mutex::scoped_lock l(m_mutex);
			open_listen_port();
		}

		std::vector<boost::shared_ptr<socket> > readable_clients;
		std::vector<boost::shared_ptr<socket> > writable_clients;
		std::vector<boost::shared_ptr<socket> > error_clients;
		boost::posix_time::ptime timer = second_clock::universal_time();

#ifndef NDEBUG
		int loops_per_second = 0;
#endif
		for(;;)
		{
		try
		{

#ifndef NDEBUG
			{
				boost::mutex::scoped_lock l(m_mutex);
				check_invariant("before SELECT");
			}
			loops_per_second++;
#endif


			// if nothing happens within 500000 microseconds (0.5 seconds)
			// do the loop anyway to check if anything else has changed
			m_selector.wait(500000, readable_clients, writable_clients, error_clients);

#ifndef NDEBUG
			{
				boost::mutex::scoped_lock l(m_mutex);
				check_invariant("after SELECT");
			}

			for (std::vector<boost::shared_ptr<libtorrent::socket> >::iterator i =
				writable_clients.begin(); i != writable_clients.end();
				++i)
			{
				assert((*i)->is_writable());
			}

			for (std::vector<boost::shared_ptr<libtorrent::socket> >::iterator i =
				readable_clients.begin(); i != readable_clients.end(); ++i)
			{
				assert((*i)->is_readable());
			}
#endif
			boost::mutex::scoped_lock l(m_mutex);
#ifndef NDEBUG
			check_invariant("before abort");
#endif
			purge_connections();

			if (m_abort)
			{
				m_tracker_manager.abort_all_requests();
				for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i =
					m_torrents.begin(); i != m_torrents.end(); ++i)
				{
					i->second->abort();
					tracker_request req = i->second->generate_tracker_request();
					req.listen_port = m_listen_interface.port;
					req.key = m_key;
					m_tracker_manager.queue_request(req, i->second->tracker_login());
				}
				m_connections.clear();
				m_torrents.clear();
				break;
			}

#ifndef NDEBUG
			check_invariant("before SEND SOCKETS");
#endif

			// ************************
			// SEND SOCKETS
			// ************************

			// let the writable connections send data
			for (std::vector<boost::shared_ptr<socket> >::iterator i
				= writable_clients.begin(); i != writable_clients.end(); ++i)
			{
				assert((*i)->is_writable());
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
						assert(p->second->can_write());
						assert(p->second->get_socket()->is_writable());
						p->second->send_data();
					}
					catch (file_error& e)
					{
						torrent* t = p->second->associated_torrent();
						assert(t != 0);

						if (m_alerts.should_post(alert::fatal))
						{
							m_alerts.post_alert(
								file_error_alert(
								t->get_handle()
								, e.what()));
						}

						// pause the torrent
						t->pause();
					}
					catch (std::exception& e)
					{
						// the connection wants to disconnect for some reason,
						// remove it from the connection-list
						if (m_alerts.should_post(alert::debug))
						{
							m_alerts.post_alert(
								peer_error_alert(
									p->first->sender()
									, p->second->id()
									, e.what()));
						}

						p->second->set_failed();
						m_selector.remove(*i);
						m_connections.erase(p);
					}
				}
			}
			purge_connections();

#ifndef NDEBUG
			check_invariant("after SEND SOCKETS");
#endif
			// ************************
			// RECEIVE SOCKETS
			// ************************

			// let the readable clients receive data
			for (std::vector<boost::shared_ptr<socket> >::iterator i = readable_clients.begin();
				i != readable_clients.end(); ++i)
			{
				// special case for m_listen_socket socket
				if (*i == m_listen_socket)
				{
					assert(m_listen_socket);
					boost::shared_ptr<libtorrent::socket> s;
					try
					{
						s = (*i)->accept();
					}
					catch(std::exception& e)
					{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						(*m_logger) << "accept failed: " << e.what() << "\n";
#endif
					}
					if (s)
					{
						s->set_blocking(false);
						// we got a connection request!
						m_incoming_connection = true;
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						(*m_logger) << s->sender().as_string() << " <== INCOMING CONNECTION\n";
#endif
						if (m_ip_filter.access(s->sender()) & ip_filter::blocked)
						{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
							(*m_logger) << "filtered blocked ip\n";
#endif
							// TODO: issue an info-alert when an ip is blocked
							continue;
						}

						boost::shared_ptr<peer_connection> c(
							new peer_connection(*this, m_selector, s));

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
						p->second->receive_data();
					}
					catch (file_error& e)
					{
						torrent* t = p->second->associated_torrent();
						assert(t != 0);

						if (m_alerts.should_post(alert::fatal))
						{
							m_alerts.post_alert(
								file_error_alert(
								t->get_handle()
								, e.what()));
						}

						t->pause();
					}
					catch (std::exception& e)
					{
						if (m_alerts.should_post(alert::debug))
						{
							m_alerts.post_alert(
								peer_error_alert(
									p->first->sender()
									, p->second->id()
									, e.what()));
						}
						// the connection wants to disconnect for some reason, remove it
						// from the connection-list
						p->second->set_failed();
						m_selector.remove(*i);
						m_connections.erase(p);
					}
				}
			}
			purge_connections();
#ifndef NDEBUG
			check_invariant("after RECEIVE SOCKETS");
#endif

			// ************************
			// ERROR SOCKETS
			// ************************


			// disconnect the one we couldn't connect to
			for (std::vector<boost::shared_ptr<socket> >::iterator i = error_clients.begin();
				i != error_clients.end();
				++i)
			{
				connection_map::iterator p = m_connections.find(*i);
				if (p != m_connections.end())
				{
					if (m_alerts.should_post(alert::debug))
					{
						m_alerts.post_alert(
							peer_error_alert(
								p->first->sender()
								, p->second->id()
								, "connection closed"));
					}
				}

				m_selector.remove(*i);
				// the connection may have been disconnected in the receive or send phase
				if (p != m_connections.end())
				{
					p->second->set_failed();
					m_connections.erase(p);
				}
				else if (*i == m_listen_socket)
				{
					if (m_alerts.should_post(alert::fatal))
					{
						std::string msg = "cannot listen on the given interface '" + m_listen_interface.as_string() + "'";
						m_alerts.post_alert(listen_failed_alert(msg));
					}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
					std::string msg = "cannot listen on the given interface '" + m_listen_interface.as_string() + "'";
					(*m_logger) << msg << "\n";
#endif
					assert(m_listen_socket.unique());
					m_listen_socket.reset();
				}
			}

#ifndef NDEBUG
			check_invariant("after ERROR SOCKETS");
#endif

			boost::posix_time::time_duration d = second_clock::universal_time() - timer;
			if (d.seconds() < 1) continue;
			timer = second_clock::universal_time();

			// ************************
			// THE SECTION BELOW IS EXECUTED ONCE EVERY SECOND
			// ************************

#ifndef NDEBUG
			if (loops_per_second > 800) std::cout << "\n\nloops: " << loops_per_second << "\n";
			loops_per_second = 0;
#endif

			// do the second_tick() on each connection
			// this will update their statistics (download and upload speeds)
			// also purge sockets that have timed out
			// and keep sockets open by keeping them alive.
			for (connection_map::iterator i = m_connections.begin();
				i != m_connections.end();)
			{
				connection_map::iterator j = i;
				++i;
				// if this socket has timed out
				// close it.
				if (j->second->has_timed_out())
				{
					if (m_alerts.should_post(alert::debug))
					{
						m_alerts.post_alert(
							peer_error_alert(
								j->first->sender()
								, j->second->id()
								, "connection timed out"));
					}
					j->second->set_failed();
					m_selector.remove(j->first);
					m_connections.erase(j);
					continue;
				}

				j->second->keep_alive();
			}

			// check each torrent for abortion or
			// tracker updates
			for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i
				= m_torrents.begin(); i != m_torrents.end();)
			{
				torrent& t = *i->second;
				if (t.is_aborted())
				{
					tracker_request req = t.generate_tracker_request();
					assert(req.event == tracker_request::stopped);
					req.listen_port = m_listen_interface.port;
					req.key = m_key;
					m_tracker_manager.queue_request(req, t.tracker_login());

					if (m_alerts.should_post(alert::info))
					{
						m_alerts.post_alert(
							tracker_announce_alert(
								t.get_handle(), "tracker announce, event=stopped"));
					}
	
#ifndef NDEBUG
					sha1_hash i_hash = t.torrent_file().info_hash();
#endif
					m_torrents.erase(i++);
					assert(m_torrents.find(i_hash) == m_torrents.end());
					continue;
				}
				else if (t.should_request())
				{
					tracker_request req = t.generate_tracker_request();
					req.listen_port = m_listen_interface.port;
					req.key = m_key;
					m_tracker_manager.queue_request(req, t.tracker_login(), i->second);

					if (m_alerts.should_post(alert::info))
					{
						m_alerts.post_alert(
							tracker_announce_alert(
								t.get_handle(), "tracker announce"));
					}
				}

				// tick() will set the used upload quota
				t.second_tick(m_stat);
				++i;
			}
			purge_connections();

			m_stat.second_tick();

			// distribute the maximum upload rate among the torrents

			allocate_resources(m_upload_rate == -1
				? std::numeric_limits<int>::max()
				: m_upload_rate
				, m_torrents
				, &torrent::m_ul_bandwidth_quota);

			allocate_resources(m_download_rate == -1
				? std::numeric_limits<int>::max()
				: m_download_rate
				, m_torrents
				, &torrent::m_dl_bandwidth_quota);

			allocate_resources(m_max_uploads == -1
				? std::numeric_limits<int>::max()
				: m_max_uploads
				, m_torrents
				, &torrent::m_uploads_quota);

			allocate_resources(m_max_connections == -1
				? std::numeric_limits<int>::max()
				: m_max_connections
				, m_torrents
				, &torrent::m_connections_quota);

			for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i
				= m_torrents.begin(); i != m_torrents.end(); ++i)
			{
				i->second->distribute_resources();
			}

			m_tracker_manager.tick();

			}
			catch (std::bad_cast& e)
			{
				std::cerr << e.what() << "\n";
				assert(false);
			}
			catch (std::exception& e)
			{
				std::cerr << e.what() << "\n";
				assert(false);
			}
			catch (...)
			{
				std::cerr << "error!\n";
				assert(false);
			}
		}

		while (!m_tracker_manager.send_finished())
		{
			m_tracker_manager.tick();
			boost::xtime t;
			boost::xtime_get(&t, boost::TIME_UTC);
			t.nsec += 100000000;
			boost::thread::sleep(t);
		}

	}


	// the return value from this function is valid only as long as the
	// session is locked!
	torrent* session_impl::find_torrent(const sha1_hash& info_hash)
	{
		std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i
			= m_torrents.find(info_hash);
#ifndef NDEBUG
		for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator j
			= m_torrents.begin(); j != m_torrents.end(); ++j)
		{
			torrent* p = boost::get_pointer(j->second);
			assert(p);
		}
#endif
		if (i != m_torrents.end()) return boost::get_pointer(i->second);
		return 0;
	}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
	boost::shared_ptr<logger> session_impl::create_log(std::string const& name)
	{
		// current options are file_logger, cout_logger and null_logger
		return boost::shared_ptr<logger>(new file_logger(name + ".log"));
	}
#endif

#ifndef NDEBUG
	void session_impl::check_invariant(const char *place)
	{
		assert(place);

		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			if (i->second->can_write() != m_selector.is_writability_monitored(i->first)
				|| i->second->can_read() != m_selector.is_readability_monitored(i->first))
			{
				std::ofstream error_log("error.log", std::ios_base::app);
				boost::shared_ptr<peer_connection> p = i->second;
				error_log << "selector::is_writability_monitored() " << m_selector.is_writability_monitored(i->first) << "\n";
				error_log << "selector::is_readability_monitored() " << m_selector.is_readability_monitored(i->first) << "\n";
				error_log << "peer_connection::can_write() " << p->can_write() << "\n";
				error_log << "peer_connection::can_read() " << p->can_read() << "\n";
				error_log << "peer_connection::ul_quota_left " << p->m_ul_bandwidth_quota.left() << "\n";
				error_log << "peer_connection::dl_quota_left " << p->m_dl_bandwidth_quota.left() << "\n";
				error_log << "peer_connection::m_ul_bandwidth_quota.given " << p->m_ul_bandwidth_quota.given << "\n";
				error_log << "peer_connection::get_peer_id " << p->get_peer_id() << "\n";
				error_log << "place: " << place << "\n";
				error_log.flush();
				assert(false);
			}
			if (i->second->associated_torrent())
			{
				assert(i->second->associated_torrent()
					->get_policy().has_connection(boost::get_pointer(i->second)));
			}
		}
	}
#endif

}}

namespace libtorrent
{

	session::session(
		fingerprint const& id
		, std::pair<int, int> listen_port_range
		, char const* listen_interface)
		: m_impl(listen_port_range, id, listen_interface)
		, m_checker_impl(m_impl)
		, m_thread(boost::ref(m_impl))
		, m_checker_thread(boost::ref(m_checker_impl))
	{
		assert(listen_port_range.first > 0);
		assert(listen_port_range.first < listen_port_range.second);
#ifndef NDEBUG
		// this test was added after it came to my attention
		// that devstudios managed c++ failed to generate
		// correct code for boost.function
		boost::function0<void> test = boost::ref(m_impl);
		assert(!test.empty());
#endif
	}

	session::session(fingerprint const& id)
		: m_impl(std::make_pair(0, 0), id)
		, m_checker_impl(m_impl)
		, m_thread(boost::ref(m_impl))
		, m_checker_thread(boost::ref(m_checker_impl))
	{
#ifndef NDEBUG
		boost::function0<void> test = boost::ref(m_impl);
		assert(!test.empty());
#endif
	}

	void session::disable_extensions()
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		std::fill(m_impl.m_extension_enabled, m_impl.m_extension_enabled
			+ peer_connection::num_supported_extensions, false);
	}

	void session::set_ip_filter(ip_filter const& f)
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_ip_filter = f;
	}

	void session::set_peer_id(peer_id const& id)
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_peer_id = id;
	}

	void session::set_key(int key)
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_key = key;
	}

	void session::enable_extension(peer_connection::extension_index i)
	{
		assert(i >= 0);
		assert(i < peer_connection::num_supported_extensions);
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_extension_enabled[i] = true;
	}

	std::vector<torrent_handle> session::get_torrents()
	{
		boost::mutex::scoped_lock l(m_checker_impl.m_mutex);
		boost::mutex::scoped_lock l2(m_impl.m_mutex);
		std::vector<torrent_handle> ret;
		for (std::deque<detail::piece_checker_data>::iterator i
			= m_checker_impl.m_torrents.begin()
			, end(m_checker_impl.m_torrents.end()); i != end; ++i)
		{
			if (i->abort) continue;
			ret.push_back(torrent_handle(&m_impl, &m_checker_impl
				, i->info_hash));
		}

		for (detail::session_impl::torrent_map::iterator i
			= m_impl.m_torrents.begin(), end(m_impl.m_torrents.end());
			i != end; ++i)
		{
			if (i->second->is_aborted()) continue;
			ret.push_back(torrent_handle(&m_impl, &m_checker_impl
				, i->first));
		}
		return ret;
	}

	// TODO: add a check to see if filenames are accepted on the
	// current platform.
	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session::add_torrent(
		entry const& metadata
		, boost::filesystem::path const& save_path
		, entry const& resume_data
		, bool compact_mode
		, int block_size)
	{
		// make sure the block_size is an even power of 2
#ifndef NDEBUG
		for (int i = 0; i < 32; ++i)
		{
			if (block_size & (1 << i))
			{
				assert((block_size & ~(1 << i)) == 0);
				break;
			}
		}
#endif
	
			  
		assert(!save_path.empty());
		torrent_info ti(metadata);

		if (ti.begin_files() == ti.end_files())
			throw std::runtime_error("no files in torrent");

		{
			// lock the session
			boost::mutex::scoped_lock l(m_impl.m_mutex);

			// is the torrent already active?
			if (m_impl.find_torrent(ti.info_hash()))
				throw duplicate_torrent();
		}

		// lock the checker_thread
		boost::mutex::scoped_lock l(m_checker_impl.m_mutex);

		// is the torrent currently being checked?
		if (m_checker_impl.find_torrent(ti.info_hash()))
			throw duplicate_torrent();

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(m_impl, metadata, save_path, m_impl.m_listen_interface
				, compact_mode, block_size));

		detail::piece_checker_data d;
		d.torrent_ptr = torrent_ptr;
		d.save_path = save_path;
		d.info_hash = ti.info_hash();
		d.resume_data = resume_data;

		// add the torrent to the queue to be checked
		m_checker_impl.m_torrents.push_back(d);
		// and notify the thread that it got another
		// job in its queue
		m_checker_impl.m_cond.notify_one();

		return torrent_handle(&m_impl, &m_checker_impl, ti.info_hash());
	}

	torrent_handle session::add_torrent(
		char const* tracker_url
		, sha1_hash const& info_hash
		, boost::filesystem::path const& save_path
		, entry const&
		, bool compact_mode
		, int block_size)
	{
		// make sure the block_size is an even power of 2
#ifndef NDEBUG
		for (int i = 0; i < 32; ++i)
		{
			if (block_size & (1 << i))
			{
				assert((block_size & ~(1 << i)) == 0);
				break;
			}
		}
#endif
	
		// TODO: support resume data in this case
		assert(!save_path.empty());
		{
			// lock the checker_thread
			boost::mutex::scoped_lock l(m_checker_impl.m_mutex);

			// is the torrent currently being checked?
			if (m_checker_impl.find_torrent(info_hash))
				throw duplicate_torrent();
		}

		// lock the session
		boost::mutex::scoped_lock l(m_impl.m_mutex);

		// the metadata extension has to be enabled for this to work
		assert(m_impl.m_extension_enabled
			[peer_connection::extended_metadata_message]);

		// is the torrent already active?
		if (m_impl.find_torrent(info_hash))
			throw duplicate_torrent();

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(m_impl, tracker_url, info_hash, save_path
			, m_impl.m_listen_interface, compact_mode, block_size));

		m_impl.m_torrents.insert(
			std::make_pair(info_hash, torrent_ptr)).first;

		return torrent_handle(&m_impl, &m_checker_impl, info_hash);
	}

	void session::remove_torrent(const torrent_handle& h)
	{
		if (h.m_ses != &m_impl) return;
		assert(h.m_chk == &m_checker_impl || h.m_chk == 0);
		assert(h.m_ses != 0);

		{
			boost::mutex::scoped_lock l(m_impl.m_mutex);
			torrent* t = m_impl.find_torrent(h.m_info_hash);
			if (t != 0)
			{
				t->abort();
				return;
			}
		}

		if (h.m_chk)
		{
			boost::mutex::scoped_lock l(m_checker_impl.m_mutex);

			detail::piece_checker_data* d = m_checker_impl.find_torrent(h.m_info_hash);
			if (d != 0)
			{
				if (d->processing) d->abort = true;
				else m_checker_impl.remove_torrent(h.m_info_hash);
				return;
			}
		}
	}

	bool session::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface)
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);

		if (m_impl.m_listen_socket)
		{
			m_impl.m_selector.remove(m_impl.m_listen_socket);
			m_impl.m_listen_socket.reset();
		}

		m_impl.m_incoming_connection = false;

		m_impl.m_listen_port_range = port_range;
		m_impl.m_listen_interface = address(net_interface, port_range.first);
		m_impl.open_listen_port();
		return m_impl.m_listen_socket;
	}

	unsigned short session::listen_port() const
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		return m_impl.m_listen_interface.port;
	}

	session_status session::status() const
	{
		session_status s;
		s.has_incoming_connections = m_impl.m_incoming_connection;
		s.num_peers = (int)m_impl.m_connections.size();

		s.download_rate = m_impl.m_stat.download_rate();
		s.upload_rate = m_impl.m_stat.upload_rate();

		s.payload_download_rate = m_impl.m_stat.download_payload_rate();
		s.payload_upload_rate = m_impl.m_stat.upload_payload_rate();

		s.total_download = m_impl.m_stat.total_protocol_download()
			+ m_impl.m_stat.total_payload_download();

		s.total_upload = m_impl.m_stat.total_protocol_upload()
			+ m_impl.m_stat.total_payload_upload();

		s.total_payload_download = m_impl.m_stat.total_payload_download();
		s.total_payload_upload = m_impl.m_stat.total_payload_upload();

		return s;
	}

	bool session::is_listening() const
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		return m_impl.m_listen_socket;
	}

	void session::set_http_settings(const http_settings& s)
	{
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_settings = s;
	}

	session::~session()
	{
		{
			// lock the main thread and abort it
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

	void session::set_max_uploads(int limit)
	{
		assert(limit > 0 || limit == -1);
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_max_uploads = limit;
	}

	void session::set_max_connections(int limit)
	{
		assert(limit > 0 || limit == -1);
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_max_connections = limit;
	}

	void session::set_upload_rate_limit(int bytes_per_second)
	{
		assert(bytes_per_second > 0 || bytes_per_second == -1);
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_upload_rate = bytes_per_second;
	}

	void session::set_download_rate_limit(int bytes_per_second)
	{
		assert(bytes_per_second > 0 || bytes_per_second == -1);
		boost::mutex::scoped_lock l(m_impl.m_mutex);
		m_impl.m_download_rate = bytes_per_second;
	}

	std::auto_ptr<alert> session::pop_alert()
	{
		if (m_impl.m_alerts.pending())
			return m_impl.m_alerts.get();
		else
			return std::auto_ptr<alert>(0);
	}

	void session::set_severity_level(alert::severity_t s)
	{
		m_impl.m_alerts.set_severity(s);
	}

	void detail::piece_checker_data::parse_resume_data(
		const entry& resume_data
		, const torrent_info& info
		, std::string& error)
	{
		// if we don't have any resume data, return
		if (resume_data.type() == entry::undefined_t) return;

		entry rd = resume_data;

		try
		{
			if (rd["file-format"].string() != "libtorrent resume file")
			{
				error = "missing file format tag";
				return;
			}

			if (rd["file-version"].integer() > 1)
			{
				error = "incompatible file version "
					+ boost::lexical_cast<std::string>(rd["file-version"].integer());
				return;
			}

			// verify info_hash
			const std::string &hash = rd["info-hash"].string();
			std::string real_hash((char*)info.info_hash().begin(), (char*)info.info_hash().end());
			if (hash != real_hash)
			{
				error = "mismatching info-hash: " + hash;
				return;
			}

			// the peers

			if (rd.find_key("peers"))
			{
				entry::list_type& peer_list = rd["peers"].list();

				std::vector<address> tmp_peers;
				tmp_peers.reserve(peer_list.size());
				for (entry::list_type::iterator i = peer_list.begin();
					i != peer_list.end(); ++i)
				{
					address a(
						(*i)["ip"].string().c_str()
						, (unsigned short)(*i)["port"].integer());
					tmp_peers.push_back(a);
				}

				peers.swap(tmp_peers);
			}

			// read piece map
			const entry::list_type& slots = rd["slots"].list();
			if ((int)slots.size() > info.num_pieces())
			{
				error = "file has more slots than torrent (slots: "
					+ boost::lexical_cast<std::string>(slots.size()) + " size: "
					+ boost::lexical_cast<std::string>(info.num_pieces()) + " )";
				return;
			}

			std::vector<int> tmp_pieces;
			tmp_pieces.reserve(slots.size());
			for (entry::list_type::const_iterator i = slots.begin();
				i != slots.end(); ++i)
			{
				int index = (int)i->integer();
				if (index >= info.num_pieces() || index < -2)
				{
					error = "too high index number in slot map (index: "
						+ boost::lexical_cast<std::string>(index) + " size: "
						+ boost::lexical_cast<std::string>(info.num_pieces()) + ")";
					return;
				}
				tmp_pieces.push_back(index);
			}

			// only bother to check the partial pieces if we have the same block size
			// as in the fast resume data. If the blocksize has changed, then throw
			// away all partial pieces.
			std::vector<piece_picker::downloading_piece> tmp_unfinished;
			int num_blocks_per_piece = (int)rd["blocks per piece"].integer();
			if (num_blocks_per_piece == info.piece_length() / torrent_ptr->block_size())
			{
				// the unfinished pieces

				entry::list_type& unfinished = rd["unfinished"].list();

				tmp_unfinished.reserve(unfinished.size());
				for (entry::list_type::iterator i = unfinished.begin();
					i != unfinished.end(); ++i)
				{
					piece_picker::downloading_piece p;
	
					p.index = (int)(*i)["piece"].integer();
					if (p.index < 0 || p.index >= info.num_pieces())
					{
						error = "invalid piece index in unfinished piece list (index: "
							+ boost::lexical_cast<std::string>(p.index) + " size: "
							+ boost::lexical_cast<std::string>(info.num_pieces()) + ")";
						return;
					}

					const std::string& bitmask = (*i)["bitmask"].string();

					const int num_bitmask_bytes = std::max(num_blocks_per_piece / 8, 1);
					if ((int)bitmask.size() != num_bitmask_bytes)
					{
						error = "invalid size of bitmask (" + boost::lexical_cast<std::string>(bitmask.size()) + ")";
						return;
					}
					for (int j = 0; j < num_bitmask_bytes; ++j)
					{
						unsigned char bits = bitmask[j];
						for (int k = 0; k < 8; ++k)
						{
							const int bit = j * 8 + k;
							if (bits & (1 << k))
								p.finished_blocks[bit] = true;
						}
					}

					if (p.finished_blocks.count() == 0) continue;
	
					std::vector<int>::iterator slot_iter
						= std::find(tmp_pieces.begin(), tmp_pieces.end(), p.index);
					if (slot_iter == tmp_pieces.end())
					{
						// this piece is marked as unfinished
						// but doesn't have any storage
						error = "piece " + boost::lexical_cast<std::string>(p.index) + " is "
							"marked as unfinished, but doesn't have any storage";
						return;
					}

					assert(*slot_iter == p.index);
					int slot_index = static_cast<int>(slot_iter - tmp_pieces.begin());
					unsigned long adler
						= torrent_ptr->filesystem().piece_crc(
							slot_index
							, torrent_ptr->block_size()
							, p.finished_blocks);

					const entry& ad = (*i)["adler32"];
	
					// crc's didn't match, don't use the resume data
					if (ad.integer() != adler)
					{
						error = "checksum mismatch on piece " + boost::lexical_cast<std::string>(p.index);
						return;
					}

					tmp_unfinished.push_back(p);
				}
			}

			// verify file sizes

			std::vector<std::pair<size_type, std::time_t> > file_sizes;
			entry::list_type& l = rd["file sizes"].list();

			for (entry::list_type::iterator i = l.begin();
				i != l.end(); ++i)
			{
				file_sizes.push_back(std::pair<size_type, std::time_t>(
					i->list().front().integer()
					, i->list().back().integer()));
			}

			if ((int)tmp_pieces.size() == info.num_pieces()
				&& std::find_if(tmp_pieces.begin(), tmp_pieces.end()
				, boost::bind(std::less<int>(), _1, 0)) == tmp_pieces.end())
			{
				if (info.num_files() != (int)file_sizes.size())
				{
					error = "the number of files does not match the torrent (num: "
						+ boost::lexical_cast<std::string>(file_sizes.size()) + " actual: "
						+ boost::lexical_cast<std::string>(info.num_files()) + ")";
					return;
				}

				std::vector<std::pair<size_type, std::time_t> >::iterator
					fs = file_sizes.begin();
				// the resume data says we have the entire torrent
				// make sure the file sizes are the right ones
				for (torrent_info::file_iterator i = info.begin_files()
					, end(info.end_files()); i != end; ++i, ++fs)
				{
					if (i->size != fs->first)
					{
						error = "file size for '" + i->path.native_file_string() + "' was expected to be "
							+ boost::lexical_cast<std::string>(i->size) + " bytes";
						return;
					}
				}
			}


			if (!match_filesizes(info, save_path, file_sizes, &error))
				return;

			piece_map.swap(tmp_pieces);
			unfinished_pieces.swap(tmp_unfinished);
		}
		catch (invalid_encoding)
		{
			return;
		}
		catch (type_error)
		{
			return;
		}
		catch (file_error)
		{
			return;
		}
	}
}
