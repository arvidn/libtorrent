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
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/socket.hpp"

using namespace boost::posix_time;
using boost::shared_ptr;
using boost::weak_ptr;
using boost::bind;
using boost::mutex;
using libtorrent::detail::session_impl;

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
		// if we're currently performing a full file check,
		// this is the torrent being processed
		boost::shared_ptr<piece_checker_data> processing;
		boost::shared_ptr<piece_checker_data> t;
		for (;;)
		{
			// temporary torrent used while checking fastresume data
			try
			{
				t.reset();
				{
					boost::mutex::scoped_lock l(m_mutex);

					// if the job queue is empty and
					// we shouldn't abort
					// wait for a signal
					if (m_torrents.empty() && !m_abort && !processing)
						m_cond.wait(l);

					if (m_abort) return;

					if (!m_torrents.empty())
					{
						t = m_torrents.front();
						if (t->abort)
						{
							if (processing->torrent_ptr->num_peers())
							{
								m_ses.m_torrents.insert(std::make_pair(
									t->info_hash, t->torrent_ptr));
								t->torrent_ptr->abort();
							}


							m_torrents.pop_front();
							continue;
						}
					}
				}

				if (t)
				{
					std::string error_msg;
					t->parse_resume_data(t->resume_data, t->torrent_ptr->torrent_file(), error_msg);

					if (!error_msg.empty() && m_ses.m_alerts.should_post(alert::warning))
					{
						session_impl::mutex_t::scoped_lock l2(m_ses.m_mutex);
						m_ses.m_alerts.post_alert(fastresume_rejected_alert(
							t->torrent_ptr->get_handle()
							, error_msg));
					}

					// clear the resume data now that it has been used
					// (the fast resume data is now parsed and stored in t)
					t->resume_data = entry();
					bool up_to_date = t->torrent_ptr->check_fastresume(*t);

					if (up_to_date)
					{
						// lock the session to add the new torrent
						session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
						mutex::scoped_lock l2(m_mutex);

						assert(m_torrents.front() == t);

						t->torrent_ptr->files_checked(t->unfinished_pieces);
						m_torrents.pop_front();
						m_ses.m_torrents.insert(std::make_pair(t->info_hash, t->torrent_ptr));
						if (t->torrent_ptr->is_seed() && m_ses.m_alerts.should_post(alert::info))
						{
							m_ses.m_alerts.post_alert(torrent_finished_alert(
								t->torrent_ptr->get_handle()
								, "torrent is complete"));
						}

						peer_id id;
						std::fill(id.begin(), id.end(), 0);
						for (std::vector<tcp::endpoint>::const_iterator i = t->peers.begin();
								i != t->peers.end(); ++i)
						{
							t->torrent_ptr->get_policy().peer_from_tracker(*i, id);
						}
						continue;
					}

					// lock the checker while we move the torrent from
					// m_torrents to m_processing
					{
						mutex::scoped_lock l(m_mutex);
						assert(m_torrents.front() == t);

						m_torrents.pop_front();
						m_processing.push_back(t);
						if (!processing)
						{
							processing = t;
							processing->processing = true;
						}
					}
				}
			}
			catch(const std::exception& e)
			{
				// This will happen if the storage fails to initialize
				session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
				mutex::scoped_lock l2(m_mutex);

				if (m_ses.m_alerts.should_post(alert::fatal))
				{
					m_ses.m_alerts.post_alert(
						file_error_alert(
							t->torrent_ptr->get_handle()
							, e.what()));
				}
				if (t->torrent_ptr->num_peers())
				{
					m_ses.m_torrents.insert(std::make_pair(
						t->info_hash, t->torrent_ptr));
					t->torrent_ptr->abort();
				}

				assert(!m_torrents.empty());
				m_torrents.pop_front();
			}
			catch(...)
			{
#ifndef NDEBUG
				std::cerr << "error while checking resume data\n";
#endif
				mutex::scoped_lock l(m_mutex);
				assert(!m_torrents.empty());
				m_torrents.pop_front();
				assert(false);
			}

			if (!processing) continue;

			try
			{	
				assert(processing);
	
				float finished = false;
				float progress = 0.f;
				boost::tie(finished, progress) = processing->torrent_ptr->check_files();

				{
					mutex::scoped_lock l(m_mutex);
					processing->progress = progress;
					if (processing->abort)
					{
						assert(!m_processing.empty());
						assert(m_processing.front() == processing);

						if (processing->torrent_ptr->num_peers())
						{
							m_ses.m_torrents.insert(std::make_pair(
								processing->info_hash, processing->torrent_ptr));
							processing->torrent_ptr->abort();
						}

						processing.reset();
						m_processing.pop_front();
						if (!m_processing.empty())
						{
							processing = m_processing.front();
							processing->processing = true;
						}
						continue;
					}
				}
				if (finished)
				{
					// lock the session to add the new torrent
					session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
					mutex::scoped_lock l2(m_mutex);
				
					assert(!m_processing.empty());
					assert(m_processing.front() == processing);

					processing->torrent_ptr->files_checked(processing->unfinished_pieces);
					m_ses.m_torrents.insert(std::make_pair(
						processing->info_hash, processing->torrent_ptr));
					if (processing->torrent_ptr->is_seed()
						&& m_ses.m_alerts.should_post(alert::info))
					{
						m_ses.m_alerts.post_alert(torrent_finished_alert(
							processing->torrent_ptr->get_handle()
							, "torrent is complete"));
					}

					peer_id id;
					std::fill(id.begin(), id.end(), 0);
					for (std::vector<tcp::endpoint>::const_iterator i = processing->peers.begin();
							i != processing->peers.end(); ++i)
					{
						processing->torrent_ptr->get_policy().peer_from_tracker(*i, id);
					}
					processing.reset();
					m_processing.pop_front();
					if (!m_processing.empty())
					{
						processing = m_processing.front();
						processing->processing = true;
					}
				}
			}
			catch(const std::exception& e)
			{
				// This will happen if the storage fails to initialize
				session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
				mutex::scoped_lock l2(m_mutex);

				if (m_ses.m_alerts.should_post(alert::fatal))
				{
					m_ses.m_alerts.post_alert(
						file_error_alert(
							processing->torrent_ptr->get_handle()
							, e.what()));
				}
				assert(!m_processing.empty());

				if (processing->torrent_ptr->num_peers())
				{
					m_ses.m_torrents.insert(std::make_pair(
						processing->info_hash, processing->torrent_ptr));
					processing->torrent_ptr->abort();
				}

				processing.reset();
				m_processing.pop_front();
				if (!m_processing.empty())
				{
					processing = m_processing.front();
					processing->processing = true;
				}
			}
			catch(...)
			{
#ifndef NDEBUG
				std::cerr << "error while checking files\n";
#endif
				mutex::scoped_lock l(m_mutex);
				assert(!m_processing.empty());

				processing.reset();
				m_processing.pop_front();
				if (!m_processing.empty())
				{
					processing = m_processing.front();
					processing->processing = true;
				}

				assert(false);
			}
		}
	}

	detail::piece_checker_data* checker_impl::find_torrent(sha1_hash const& info_hash)
	{
		for (std::deque<boost::shared_ptr<piece_checker_data> >::iterator i
			= m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			if ((*i)->info_hash == info_hash) return i->get();
		}
		for (std::deque<boost::shared_ptr<piece_checker_data> >::iterator i
			= m_processing.begin(); i != m_processing.end(); ++i)
		{
			if ((*i)->info_hash == info_hash) return i->get();
		}

		return 0;
	}

	void checker_impl::remove_torrent(sha1_hash const& info_hash)
	{
		for (std::deque<boost::shared_ptr<piece_checker_data> >::iterator i
			= m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			if ((*i)->info_hash == info_hash)
			{
				assert((*i)->processing == false);
				m_torrents.erase(i);
				return;
			}
		}
		assert(false);
	}

	session_impl::session_impl(
		std::pair<int, int> listen_port_range
		, const fingerprint& cl_fprint
		, const char* listen_interface = 0)
		: m_tracker_manager(m_http_settings)
		, m_listen_port_range(listen_port_range)
		, m_listen_interface(listen_port_range.first)
		, m_abort(false)
		, m_upload_rate(-1)
		, m_download_rate(-1)
		, m_max_uploads(-1)
		, m_max_connections(-1)
		, m_half_open_limit(-1)
		, m_incoming_connection(false)
		, m_timer(m_selector)
	{
		if (listen_interface != 0) m_listen_interface.address(
			address(listen_interface));

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		m_logger = create_log("main_session", false);
		using boost::posix_time::second_clock;
		using boost::posix_time::to_simple_string;
		(*m_logger) << to_simple_string(second_clock::universal_time()) << "\n";
#endif
		std::fill(m_extension_enabled, m_extension_enabled
			+ num_supported_extensions, true);
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
		static char const printable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz-_.!~*'()";

		// the random number
		for (unsigned char* i = m_peer_id.begin() + print.length();
			i != m_peer_id.end(); ++i)
		{
			*i = printable[rand() % (sizeof(printable)-1)];
		}
		// this says that we support the extensions
		std::memcpy(&m_peer_id[17], "ext", 3);
		m_timer.expires_from_now(boost::posix_time::seconds(1));
		m_timer.async_wait(bind(&session_impl::second_tick, this, _1));
	}

	bool session_impl::extensions_enabled() const
	{
		const int n = num_supported_extensions;
		return std::find(m_extension_enabled
			, m_extension_enabled + n, true) != m_extension_enabled + n;
	}
/*
	void session_impl::purge_connections()
	{
		while (!m_disconnect_peer.empty())
		{
			boost::intrusive_ptr<peer_connection>& p = m_disconnect_peer.back();
			assert(p->is_disconnecting());
			if (p->is_connecting())
			{
				// Since this peer is still connecting, will not be
				// in the list of completed connections.
				connection_map::iterator i = m_half_open.find(p->get_socket());
				if (i == m_half_open.end())
				{
					// this connection is not in the half-open list, so it
					// has to be in the queue, waiting to be connected.
					connection_queue::iterator j = std::find(
						m_connection_queue.begin(), m_connection_queue.end(), p);
						
					assert(j != m_connection_queue.end());
					if (j != m_connection_queue.end()) m_connection_queue.erase(j);
				}
				else
				{
					m_half_open.erase(i);
					process_connection_queue();
				}
			}
			else
			{
				connection_map::iterator i = m_connections.find(p->get_socket());
				assert(i != m_connections.end());
				if (i != m_connections.end()) m_connections.erase(i);
			}
			m_disconnect_peer.pop_back();
		}
	}
*/
	void session_impl::open_listen_port()
	{
		try
		{
			// create listener socket
			m_listen_socket = boost::shared_ptr<socket_acceptor>(new socket_acceptor(m_selector));

			for(;;)
			{
				try
				{
					m_listen_socket->open(asio::ipv4::tcp());
					m_listen_socket->bind(m_listen_interface);
					m_listen_socket->listen();
					break;
				}
				catch (asio::error& e)
				{
					// TODO: make sure this is correct
					if (e.code() == asio::error::host_not_found)
					{
						if (m_alerts.should_post(alert::fatal))
						{
							std::string msg = "cannot listen on the given interface '"
								+ m_listen_interface.address().to_string() + "'";
							m_alerts.post_alert(listen_failed_alert(msg));
						}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						std::string msg = "cannot listen on the given interface '"
							+ m_listen_interface.address().to_string() + "'";
						(*m_logger) << msg << "\n";
#endif
						assert(m_listen_socket.unique());
						m_listen_socket.reset();
						break;
					}
					m_listen_interface.port(m_listen_interface.port() + 1);
					if (m_listen_interface.port() > m_listen_port_range.second)
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
		catch (asio::error& e)
		{
			m_alerts.post_alert(listen_failed_alert("failed to open listen port"/*e.what()*/));
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (m_listen_socket)
		{
			(*m_logger) << "listening on port: " << m_listen_interface.port() << "\n";
		}
#endif
		if (m_listen_socket) async_accept();
	}

	void session_impl::process_connection_queue()
	{
		while (!m_connection_queue.empty())
		{
			if ((int)m_half_open.size() >= m_half_open_limit
				&& m_half_open_limit > 0)
				return;

			connection_queue::value_type& c = m_connection_queue.front();
			m_half_open.insert(std::make_pair(c->get_socket(), c));
			assert(c->associated_torrent());
			c->connect();
			m_connection_queue.pop_front();
		}
	}

	void session_impl::async_accept()
	{
		shared_ptr<stream_socket> c(new stream_socket(m_selector));
		m_listen_socket->async_accept(*c
			, bind(&session_impl::on_incoming_connection, this, c
			, weak_ptr<socket_acceptor>(m_listen_socket), _1));
	}

	void session_impl::on_incoming_connection(shared_ptr<stream_socket> const& s
		, weak_ptr<socket_acceptor> const& listen_socket, asio::error const& e)
	{
		async_accept();
		mutex_t::scoped_lock l(m_mutex);
		if (listen_socket.expired()) return;
		assert(listen_socket.lock() == m_listen_socket);
		if (e)
		{
			if (m_alerts.should_post(alert::fatal))
			{
				std::string msg = "cannot listen on the given interface '"
					+ m_listen_interface.address().to_string() + "'";
				m_alerts.post_alert(listen_failed_alert(msg));
			}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::string msg = "cannot listen on the given interface '"
				+ m_listen_interface.address().to_string() + "'";
			(*m_logger) << msg << "\n";
#endif
			assert(m_listen_socket.unique());
			m_listen_socket.reset();
			return;
		}

		// we got a connection request!
		m_incoming_connection = true;
		tcp::endpoint endp = s->remote_endpoint();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << endp << " <== INCOMING CONNECTION\n";
#endif
		if (m_ip_filter.access(endp.address()) & ip_filter::blocked)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << "filtered blocked ip\n";
#endif
			// TODO: issue an info-alert when an ip is blocked
			return;
		}

		boost::intrusive_ptr<peer_connection> c(
			new bt_peer_connection(*this, s));

		m_connections.insert(std::make_pair(s, c));
	}
	
	void session_impl::connection_failed(boost::shared_ptr<stream_socket> const& s
		, tcp::endpoint const& a, char const* message)
	{
		connection_map::iterator p = m_connections.find(s);

		// the connection may have been disconnected in the receive or send phase
		if (p != m_connections.end())
		{
			if (m_alerts.should_post(alert::debug))
			{
				m_alerts.post_alert(
					peer_error_alert(
						a
						, p->second->id()
						, message));
			}

#if defined(TORRENT_VERBOSE_LOGGING)
			(*p->second->m_logger) << "*** CONNECTION FAILED " << message << "\n";
#endif
			p->second->set_failed();
			m_connections.erase(p);
		}
		else
		{
			// the error was not in one of the connected
			// conenctions. Look among the half-open ones.
			p = m_half_open.find(s);
			if (p != m_half_open.end())
			{
				if (m_alerts.should_post(alert::debug))
				{
					m_alerts.post_alert(
						peer_error_alert(
							a
							, p->second->id()
							, message));
				}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				(*m_logger) << "CLOSED: " << a.address().to_string()
					<< " " << message << "\n";
#endif
				p->second->set_failed();
				m_half_open.erase(p);
				process_connection_queue();
			}
		}
	}

	void session_impl::close_connection(boost::intrusive_ptr<peer_connection> const& p)
	{
		assert(p->is_disconnecting());
		if (p->is_connecting())
		{
			// Since this peer is still connecting, will not be
			// in the list of completed connections.
			connection_map::iterator i = m_half_open.find(p->get_socket());
			if (i == m_half_open.end())
			{
				// this connection is not in the half-open list, so it
				// has to be in the queue, waiting to be connected.
				connection_queue::iterator j = std::find(
					m_connection_queue.begin(), m_connection_queue.end(), p);
					
				if (j != m_connection_queue.end()) m_connection_queue.erase(j);
			}
			else
			{
				m_half_open.erase(i);
				process_connection_queue();
			}
		}
		else
		{
			connection_map::iterator i = m_connections.find(p->get_socket());
			if (i != m_connections.end()) m_connections.erase(i);
		}
	}

	void session_impl::second_tick(asio::error const& e)
	{
		if (e)
		{
#if defined(TORRENT_LOGGING)
			(*m_logger) << "*** SECOND TIMER FAILED " << e.what() << "\n";
#endif
			m_abort = true;
			m_selector.interrupt();
			return;
		}
		
		if (m_abort) return;

		m_timer.expires_from_now(boost::posix_time::seconds(1));
		m_timer.async_wait(bind(&session_impl::second_tick, this, _1));
		
		session_impl::mutex_t::scoped_lock l(m_mutex);

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
				tcp::endpoint sender = j->first->remote_endpoint(asio::ignore_error());
				if (m_alerts.should_post(alert::debug))
				{
					m_alerts.post_alert(
						peer_error_alert(
							sender
							, j->second->id()
							, "connection timed out"));
				}
#if defined(TORRENT_VERBOSE_LOGGING)
				(*j->second->m_logger) << "*** CONNECTION TIMED OUT\n";
#endif

				j->second->set_failed();
				m_connections.erase(j);
				continue;
			}

			j->second->keep_alive();
		}

		// check each torrent for tracker updates
		// TODO: do this in a timer-event in each torrent instead
		for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i
			= m_torrents.begin(); i != m_torrents.end();)
		{
			torrent& t = *i->second;
			assert(!t.is_aborted());
			if (t.should_request())
			{
				tracker_request req = t.generate_tracker_request();
				req.listen_port = m_listen_interface.port();
				req.key = m_key;
				m_tracker_manager.queue_request(m_selector, req, t.tracker_login()
					, i->second);

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
	}

	void session_impl::connection_completed(boost::intrusive_ptr<peer_connection> const& p)
	{
		if (m_abort) return;

		connection_map::iterator i = m_half_open.find(p->get_socket());
		assert(i != m_half_open.end());

		m_connections.insert(std::make_pair(p->get_socket(), p));
		m_half_open.erase(i);
		process_connection_queue();
	}

	void session_impl::operator()()
	{
		eh_initializer();

		if (m_listen_port_range.first != 0 && m_listen_port_range.second != 0)
		{
			session_impl::mutex_t::scoped_lock l(m_mutex);
			open_listen_port();
		}

		boost::posix_time::ptime timer = second_clock::universal_time();

		//		for(;;)
		//		{
		try
		{
			m_selector.run();
			assert(m_abort == true);
		}
		catch (std::exception& e)
		{
			std::cerr << e.what() << "\n";
			#ifndef NDEBUG
			std::string err = e.what();
			#endif
			assert(false);
		}

		{
		session_impl::mutex_t::scoped_lock l(m_mutex);

		m_connections.clear();
		
		m_tracker_manager.abort_all_requests();
		for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i =
			m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			i->second->abort();
			if (!i->second->is_paused() || i->second->should_request())
			{
				tracker_request req = i->second->generate_tracker_request();
				req.listen_port = m_listen_interface.port();
				req.key = m_key;
				std::string login = i->second->tracker_login();
				m_tracker_manager.queue_request(m_selector, req, login);
			}
		}
		m_timer.expires_from_now(boost::posix_time::seconds(
			m_http_settings.stop_tracker_timeout));
		m_timer.async_wait(bind(&demuxer::interrupt, &m_selector));
		}

		m_selector.reset();
		m_selector.run();

		m_torrents.clear();

		assert(m_torrents.empty());
		assert(m_connections.empty());
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
	boost::shared_ptr<logger> session_impl::create_log(std::string const& name, bool append)
	{
		// current options are file_logger, cout_logger and null_logger
		return boost::shared_ptr<logger>(new file_logger(name + ".log", append));
	}
#endif

#ifndef NDEBUG
	void session_impl::check_invariant(const char *place)
	{
		assert(place);

		for (connection_map::iterator i = m_half_open.begin();
			i != m_half_open.end(); ++i)
		{
			assert(i->second->is_connecting());
//			assert(m_selector.is_writability_monitored(i->second->get_socket()));
		}

		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			assert(i->second);
			assert(!i->second->is_connecting());
			if (i->second->is_connecting()
/*				|| i->second->can_write() != m_selector.is_writability_monitored(i->first)
				|| i->second->can_read() != m_selector.is_readability_monitored(i->first)*/)
			{
				std::ofstream error_log("error.log", std::ios_base::app);
				boost::intrusive_ptr<peer_connection> p = i->second;
//				error_log << "selector::is_writability_monitored() " << m_selector.is_writability_monitored(i->first) << "\n";
//				error_log << "selector::is_readability_monitored() " << m_selector.is_readability_monitored(i->first) << "\n";
				error_log << "peer_connection::is_connecting() " << p->is_connecting() << "\n";
				error_log << "peer_connection::can_write() " << p->can_write() << "\n";
				error_log << "peer_connection::can_read() " << p->can_read() << "\n";
				error_log << "peer_connection::ul_quota_left " << p->m_ul_bandwidth_quota.left() << "\n";
				error_log << "peer_connection::dl_quota_left " << p->m_dl_bandwidth_quota.left() << "\n";
				error_log << "peer_connection::m_ul_bandwidth_quota.given " << p->m_ul_bandwidth_quota.given << "\n";
				error_log << "peer_connection::get_peer_id " << p->id() << "\n";
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
		// turn off the filename checking in boost.filesystem
		using namespace boost::filesystem;
		path::default_name_check(native);
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
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		std::fill(m_impl.m_extension_enabled, m_impl.m_extension_enabled
			+ num_supported_extensions, false);

		static char const printable[]
			= "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_.!~*'()";

		// remove the 'ext' sufix in the peer_id
		for (unsigned char* i = m_impl.m_peer_id.begin() + 17;
			i != m_impl.m_peer_id.end(); ++i)
		{
			*i = printable[rand() % (sizeof(printable)-1)];
		}
	}

	void session::set_ip_filter(ip_filter const& f)
	{
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (detail::session_impl::connection_map::iterator i
			= m_impl.m_connections.begin(); i != m_impl.m_connections.end();)
		{
			tcp::endpoint sender = i->first->remote_endpoint();
			if (m_impl.m_ip_filter.access(sender.address())
				& ip_filter::blocked)
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*i->second->m_logger) << "*** CONNECTION FILTERED'\n";
#endif
				m_impl.m_connections.erase(i++);
			}
			else ++i;
		}
	}

	void session::set_peer_id(peer_id const& id)
	{
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_peer_id = id;
	}

	void session::set_key(int key)
	{
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_key = key;
	}

	void session::enable_extension(extension_index i)
	{
		assert(i >= 0);
		assert(i < num_supported_extensions);
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_extension_enabled[i] = true;

		// this says that we support the extensions
		std::memcpy(&m_impl.m_peer_id[17], "ext", 3);
	}

	std::vector<torrent_handle> session::get_torrents()
	{
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		mutex::scoped_lock l2(m_checker_impl.m_mutex);
		std::vector<torrent_handle> ret;
		for (std::deque<boost::shared_ptr<detail::piece_checker_data> >::iterator i
			= m_checker_impl.m_torrents.begin()
			, end(m_checker_impl.m_torrents.end()); i != end; ++i)
		{
			if ((*i)->abort) continue;
			ret.push_back(torrent_handle(&m_impl, &m_checker_impl
				, (*i)->info_hash));
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
		torrent_info const& ti
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

		if (ti.begin_files() == ti.end_files())
			throw std::runtime_error("no files in torrent");

		// lock the session and the checker thread (the order is important!)
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		mutex::scoped_lock l2(m_checker_impl.m_mutex);

		if (m_impl.m_abort)
			throw std::runtime_error("session is closing");
		
		// is the torrent already active?
		if (m_impl.find_torrent(ti.info_hash()))
			throw duplicate_torrent();

		// is the torrent currently being checked?
		if (m_checker_impl.find_torrent(ti.info_hash()))
			throw duplicate_torrent();

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(m_impl, m_checker_impl, ti, save_path
				, m_impl.m_listen_interface, compact_mode, block_size));

		boost::shared_ptr<detail::piece_checker_data> d(
			new detail::piece_checker_data);
		d->torrent_ptr = torrent_ptr;
		d->save_path = save_path;
		d->info_hash = ti.info_hash();
		d->resume_data = resume_data;

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
			mutex::scoped_lock l(m_checker_impl.m_mutex);

			// is the torrent currently being checked?
			if (m_checker_impl.find_torrent(info_hash))
				throw duplicate_torrent();
		}

		// lock the session
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);

		// the metadata extension has to be enabled for this to work
		assert(m_impl.m_extension_enabled
			[extended_metadata_message]);

		// is the torrent already active?
		if (m_impl.find_torrent(info_hash))
			throw duplicate_torrent();

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(m_impl, m_checker_impl, tracker_url, info_hash, save_path
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
			session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
			detail::session_impl::torrent_map::iterator i =
				m_impl.m_torrents.find(h.m_info_hash);
			if (i != m_impl.m_torrents.end())
			{
				torrent& t = *i->second;
				t.abort();

				if (!t.is_paused() || t.should_request())
				{
					tracker_request req = t.generate_tracker_request();
					assert(req.event == tracker_request::stopped);
					req.listen_port = m_impl.m_listen_interface.port();
					req.key = m_impl.m_key;
					m_impl.m_tracker_manager.queue_request(m_impl.m_selector, req
						, t.tracker_login());

					if (m_impl.m_alerts.should_post(alert::info))
					{
						m_impl.m_alerts.post_alert(
							tracker_announce_alert(
								t.get_handle(), "tracker announce, event=stopped"));
					}
				}
#ifndef NDEBUG
				sha1_hash i_hash = t.torrent_file().info_hash();
#endif
				m_impl.m_torrents.erase(i);
				assert(m_impl.m_torrents.find(i_hash) == m_impl.m_torrents.end());
				return;
			}
		}

		if (h.m_chk)
		{
			mutex::scoped_lock l(m_checker_impl.m_mutex);

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
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);

		if (m_impl.m_listen_socket)
			m_impl.m_listen_socket.reset();

		m_impl.m_incoming_connection = false;

		m_impl.m_listen_port_range = port_range;
		if (net_interface && std::strlen(net_interface) > 0)
			m_impl.m_listen_interface = tcp::endpoint(port_range.first, net_interface);
		else
			m_impl.m_listen_interface = tcp::endpoint(port_range.first);

		m_impl.open_listen_port();
		return m_impl.m_listen_socket;
	}

	unsigned short session::listen_port() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		return m_impl.m_listen_interface.port();
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
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		return m_impl.m_listen_socket;
	}

	void session::set_http_settings(const http_settings& s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_http_settings = s;
	}

	session::~session()
	{
		{
			// lock the main thread and abort it
			session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
			m_impl.m_abort = true;
			m_impl.m_selector.interrupt();
		}

		{
			mutex::scoped_lock l(m_checker_impl.m_mutex);
			// abort the checker thread
			m_checker_impl.m_abort = true;

			// abort the currently checking torrent
			if (!m_checker_impl.m_torrents.empty())
			{
				m_checker_impl.m_torrents.front()->abort = true;
			}
			m_checker_impl.m_cond.notify_one();
		}

		m_thread.join();
		m_checker_thread.join();
	}

	void session::set_max_uploads(int limit)
	{
		assert(limit > 0 || limit == -1);
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_max_uploads = limit;
	}

	void session::set_max_connections(int limit)
	{
		assert(limit > 0 || limit == -1);
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_max_connections = limit;
	}

	void session::set_max_half_open_connections(int limit)
	{
		assert(limit > 0 || limit == -1);
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_half_open_limit = limit;
	}

	void session::set_upload_rate_limit(int bytes_per_second)
	{
		assert(bytes_per_second > 0 || bytes_per_second == -1);
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
		m_impl.m_upload_rate = bytes_per_second;
	}

	void session::set_download_rate_limit(int bytes_per_second)
	{
		assert(bytes_per_second > 0 || bytes_per_second == -1);
		session_impl::mutex_t::scoped_lock l(m_impl.m_mutex);
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

				std::vector<tcp::endpoint> tmp_peers;
				tmp_peers.reserve(peer_list.size());
				for (entry::list_type::iterator i = peer_list.begin();
					i != peer_list.end(); ++i)
				{
					tcp::endpoint a(
						(unsigned short)(*i)["port"].integer()
						, (*i)["ip"].string().c_str());
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
				, boost::bind<bool>(std::less<int>(), _1, 0)) == tmp_pieces.end())
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
