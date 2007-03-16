/*

Copyright (c) 2006, Arvid Norberg, Magnus Jonsson
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
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"

using namespace boost::posix_time;
using boost::shared_ptr;
using boost::weak_ptr;
using boost::bind;
using boost::mutex;
using libtorrent::aux::session_impl;

namespace libtorrent { namespace detail
{

	std::string generate_auth_string(std::string const& user
		, std::string const& passwd)
	{
		if (user.empty()) return std::string();
		return user + ":" + passwd;
	}
	

	} namespace aux {
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

					INVARIANT_CHECK;

					// if the job queue is empty and
					// we shouldn't abort
					// wait for a signal
					while (m_torrents.empty() && !m_abort && !processing)
						m_cond.wait(l);

					if (m_abort)
					{
						// no lock is needed here, because the main thread
						// has already been shut down by now
						processing.reset();
						t.reset();
						std::for_each(m_torrents.begin(), m_torrents.end()
							, boost::bind(&torrent::abort
							, boost::bind(&shared_ptr<torrent>::get
							, boost::bind(&piece_checker_data::torrent_ptr, _1))));
						m_torrents.clear();
						std::for_each(m_processing.begin(), m_processing.end()
							, boost::bind(&torrent::abort
							, boost::bind(&shared_ptr<torrent>::get
							, boost::bind(&piece_checker_data::torrent_ptr, _1))));
						m_processing.clear();
						return;
					}

					if (!m_torrents.empty())
					{
						t = m_torrents.front();
						if (t->abort)
						{
							// make sure the locking order is
							// consistent to avoid dead locks
							// we need to lock the session because closing
							// torrents assume to have access to it
							l.unlock();
							session_impl::mutex_t::scoped_lock l2(m_ses.m_mutex);
							l.lock();

							t->torrent_ptr->abort();
							m_torrents.pop_front();
							continue;
						}
					}
				}

				if (t)
				{
					std::string error_msg;
					t->parse_resume_data(t->resume_data, t->torrent_ptr->torrent_file()
						, error_msg);

					if (!error_msg.empty() && m_ses.m_alerts.should_post(alert::warning))
					{
						session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
						m_ses.m_alerts.post_alert(fastresume_rejected_alert(
							t->torrent_ptr->get_handle()
							, error_msg));
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						(*m_ses.m_logger) << "fastresume data for "
							<< t->torrent_ptr->torrent_file().name() << " rejected: "
							<< error_msg << "\n";
#endif
					}

					// lock the session to add the new torrent
					session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
					mutex::scoped_lock l2(m_mutex);
					// clear the resume data now that it has been used
					// (the fast resume data is now parsed and stored in t)
					t->resume_data = entry();
					bool up_to_date = t->torrent_ptr->check_fastresume(*t);

					if (up_to_date)
					{
						INVARIANT_CHECK;

						assert(m_torrents.front() == t);

						t->torrent_ptr->files_checked(t->unfinished_pieces);
						m_torrents.pop_front();

						// we cannot add the torrent if the session is aborted.
						if (!m_ses.is_aborted())
						{
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
						}
						else
						{
							t->torrent_ptr->abort();
						}
						t.reset();
						continue;
					}

					l.unlock();

					// move the torrent from
					// m_torrents to m_processing
					assert(m_torrents.front() == t);

					m_torrents.pop_front();
					m_processing.push_back(t);
					if (!processing)
					{
						processing = t;
						processing->processing = true;
						t.reset();
					}
				}
			}
			catch (const std::exception& e)
			{
				// This will happen if the storage fails to initialize
				// for example if one of the files has an invalid filename.
				session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
				mutex::scoped_lock l2(m_mutex);

				if (m_ses.m_alerts.should_post(alert::fatal))
				{
					m_ses.m_alerts.post_alert(
						file_error_alert(
							t->torrent_ptr->get_handle()
							, e.what()));
				}
				t->torrent_ptr->abort();

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

					INVARIANT_CHECK;

					processing->progress = progress;
					if (processing->abort)
					{
						assert(!m_processing.empty());
						assert(m_processing.front() == processing);

						processing->torrent_ptr->abort();

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

					INVARIANT_CHECK;

					assert(!m_processing.empty());
					assert(m_processing.front() == processing);

					// TODO: factor out the adding of torrents to the session
					// and to the checker thread to avoid duplicating the
					// check for abortion.
					if (!m_ses.is_aborted())
					{
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
					}
					else
					{
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
			}
			catch(std::exception const& e)
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

				processing->torrent_ptr->abort();

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

	aux::piece_checker_data* checker_impl::find_torrent(sha1_hash const& info_hash)
	{
		INVARIANT_CHECK;
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
		INVARIANT_CHECK;
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
		for (std::deque<boost::shared_ptr<piece_checker_data> >::iterator i
			= m_processing.begin(); i != m_processing.end(); ++i)
		{
			if ((*i)->info_hash == info_hash)
			{
				assert((*i)->processing == false);
				m_processing.erase(i);
				return;
			}
		}

		assert(false);
	}

#ifndef NDEBUG
	void checker_impl::check_invariant() const
	{
		for (std::deque<boost::shared_ptr<piece_checker_data> >::const_iterator i
			= m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			assert(*i);
			assert((*i)->torrent_ptr);
		}
		for (std::deque<boost::shared_ptr<piece_checker_data> >::const_iterator i
			= m_processing.begin(); i != m_processing.end(); ++i)
		{
			assert(*i);
			assert((*i)->torrent_ptr);
		}
	}
#endif

	struct seed_random_generator
	{
		seed_random_generator()
		{
			std::srand((unsigned int)(boost::posix_time::microsec_clock::
				universal_time().time_of_day().total_microseconds()));
		}
	};

	session_impl::session_impl(
		std::pair<int, int> listen_port_range
		, fingerprint const& cl_fprint
		, char const* listen_interface)
		: m_strand(m_io_service)
		, m_dl_bandwidth_manager(m_io_service, peer_connection::download_channel)
		, m_ul_bandwidth_manager(m_io_service, peer_connection::upload_channel)
		, m_tracker_manager(m_settings)
		, m_listen_port_range(listen_port_range)
		, m_listen_interface(address::from_string(listen_interface), listen_port_range.first)
		, m_abort(false)
		, m_max_uploads(-1)
		, m_max_connections(-1)
		, m_half_open_limit(-1)
		, m_incoming_connection(false)
		, m_files(40)
		, m_last_tick(microsec_clock::universal_time())
		, m_timer(m_io_service)
		, m_checker_impl(*this)
	{

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		m_logger = create_log("main_session", listen_port(), false);
		using boost::posix_time::second_clock;
		using boost::posix_time::to_simple_string;
		(*m_logger) << to_simple_string(second_clock::universal_time()) << "\n";
		
		m_stats_logger = create_log("session_stats", listen_port(), false);
		(*m_stats_logger) <<
			"1. second\n"
			"2. hard upload quota\n"
			"3. hard download quota\n"
			"\n";
		m_second_counter = 0;
		m_dl_bandwidth_manager.m_ses = this;
		m_ul_bandwidth_manager.m_ses = this;
#endif

		// ---- generate a peer id ----
		static seed_random_generator seeder;

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

		m_timer.expires_from_now(seconds(1));
		m_timer.async_wait(m_strand.wrap(
			bind(&session_impl::second_tick, this, _1)));

		m_thread.reset(new boost::thread(boost::ref(*this)));
		m_checker_thread.reset(new boost::thread(boost::ref(m_checker_impl)));
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void session_impl::add_extension(
		boost::function<boost::shared_ptr<torrent_plugin>(torrent*)> ext)
	{
		m_extensions.push_back(ext);
	}
#endif

#ifndef TORRENT_DISABLE_DHT	
	void session_impl::add_dht_node(udp::endpoint n)
	{
		if (m_dht) m_dht->add_node(n);
	}
#endif

	void session_impl::abort()
	{
		mutex_t::scoped_lock l(m_mutex);
		assert(!m_abort);
		// abort the main thread
		m_abort = true;
		m_io_service.stop();
		l.unlock();

		mutex::scoped_lock l2(m_checker_impl.m_mutex);
		// abort the checker thread
		m_checker_impl.m_abort = true;
	}

	void session_impl::set_ip_filter(ip_filter const& f)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (session_impl::connection_map::iterator i
			= m_connections.begin(); i != m_connections.end();)
		{
			tcp::endpoint sender = i->first->remote_endpoint();
			if (m_ip_filter.access(sender.address()) & ip_filter::blocked)
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*i->second->m_logger) << "*** CONNECTION FILTERED\n";
#endif
				session_impl::connection_map::iterator j = i;
				++i;
				j->second->disconnect();
			}
			else ++i;
		}
	}

	void session_impl::set_settings(session_settings const& s)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_settings = s;
		m_files.resize(m_settings.file_pool_size);
		// replace all occurances of '\n' with ' '.
		std::string::iterator i = m_settings.user_agent.begin();
		while ((i = std::find(i, m_settings.user_agent.end(), '\n'))
			!= m_settings.user_agent.end())
			*i = ' ';
	}

	void session_impl::open_listen_port()
	{
		try
		{
			// create listener socket
			m_listen_socket = boost::shared_ptr<socket_acceptor>(new socket_acceptor(m_io_service));

			for(;;)
			{
				try
				{
					m_listen_socket->open(m_listen_interface.protocol());
					m_listen_socket->bind(m_listen_interface);
					m_listen_socket->listen();
					break;
				}
				catch (asio::system_error& e)
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
					m_listen_socket->close();
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
		catch (asio::system_error& e)
		{
			if (m_alerts.should_post(alert::fatal))
			{
				m_alerts.post_alert(listen_failed_alert(
					std::string("failed to open listen port: ") + e.what()));
			}
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

			connection_queue::value_type c = m_connection_queue.front();

			try
			{
				m_connection_queue.pop_front();
				assert(c->associated_torrent().lock().get());
				c->connect();
				m_half_open.insert(std::make_pair(c->get_socket(), c));
			}
			catch (std::exception& e)
			{
				c->disconnect();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				(*m_logger) << "connect failed [" << c->remote() << "]: "
					<< e.what() << "\n";
#endif
			}
		}
	}

	void session_impl::async_accept()
	{
		shared_ptr<stream_socket> c(new stream_socket(m_io_service));
		m_listen_socket->async_accept(*c
			, bind(&session_impl::on_incoming_connection, this, c
			, weak_ptr<socket_acceptor>(m_listen_socket), _1));
	}

	void session_impl::on_incoming_connection(shared_ptr<stream_socket> const& s
		, weak_ptr<socket_acceptor> const& listen_socket, asio::error_code const& e) try
	{
		if (listen_socket.expired())
			return;
		
		if (e == asio::error::operation_aborted)
			return;

		mutex_t::scoped_lock l(m_mutex);
		assert(listen_socket.lock() == m_listen_socket);

		if (m_abort) return;

		async_accept();
		if (e)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::string msg = "error accepting connection on '"
				+ m_listen_interface.address().to_string() + "'";
			(*m_logger) << msg << "\n";
#endif
			assert(m_listen_socket.unique());
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
			// TODO: issue an info-alert when an ip is blocked!!
			return;
		}

		boost::intrusive_ptr<peer_connection> c(
			new bt_peer_connection(*this, s));
#ifndef NDEBUG
		c->m_in_constructor = false;
#endif

		m_connections.insert(std::make_pair(s, c));
	}
	catch (std::exception& exc)
	{
#ifndef NDEBUG
		std::string err = exc.what();
#endif
	}
	
	void session_impl::connection_failed(boost::shared_ptr<stream_socket> const& s
		, tcp::endpoint const& a, char const* message)
#ifndef NDEBUG
		try
#endif
	{
		mutex_t::scoped_lock l(m_mutex);
		
		connection_map::iterator p = m_connections.find(s);

		// the connection may have been disconnected in the receive or send phase
		if (p != m_connections.end())
		{
			if (m_alerts.should_post(alert::debug))
			{
				m_alerts.post_alert(
					peer_error_alert(
						a
						, p->second->pid()
						, message));
			}

#if defined(TORRENT_VERBOSE_LOGGING)
			(*p->second->m_logger) << "*** CONNECTION FAILED " << message << "\n";
#endif
			p->second->set_failed();
			p->second->disconnect();
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
							, p->second->pid()
							, message));
				}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				(*m_logger) << "CLOSED: " << a.address().to_string()
					<< " " << message << "\n";
#endif
				p->second->set_failed();
				p->second->disconnect();
			}
		}
	}
#ifndef NDEBUG
	catch (...)
	{
		assert(false);
	};
#endif

	void session_impl::close_connection(boost::intrusive_ptr<peer_connection> const& p)
	{
		mutex_t::scoped_lock l(m_mutex);

		assert(p->is_disconnecting());

		if (p->is_connecting())
		{
			assert(p->is_local());
			assert(m_connections.find(p->get_socket()) == m_connections.end());
			// Since this peer is still connecting, will not be
			// in the list of completed connections.
			connection_map::iterator i = m_half_open.find(p->get_socket());
			if (i == m_half_open.end())
			{
				// this connection is not in the half-open list, so it
				// has to be in the queue, waiting to be connected.
				connection_queue::iterator j = std::find(
					m_connection_queue.begin(), m_connection_queue.end(), p);
				
				// if this connection was closed while being connected
				// it has been removed from the connection queue and
				// not yet put into the half-open queue.
				if (j != m_connection_queue.end())
					m_connection_queue.erase(j);
			}
			else
			{
				m_half_open.erase(i);
				process_connection_queue();
			}
		}
		else
		{
			assert(m_half_open.find(p->get_socket()) == m_half_open.end());
			assert(std::find(m_connection_queue.begin()
				, m_connection_queue.end(), p) == m_connection_queue.end());
			connection_map::iterator i = m_connections.find(p->get_socket());
//			assert (i != m_connections.end());
			if (i != m_connections.end())
				m_connections.erase(i);
		}
	}

	void session_impl::set_peer_id(peer_id const& id)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_peer_id = id;
	}

	void session_impl::set_key(int key)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_key = key;
	}

	void session_impl::second_tick(asio::error_code const& e) try
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);

		if (e)
		{
#if defined(TORRENT_LOGGING)
			(*m_logger) << "*** SECOND TIMER FAILED " << e.message() << "\n";
#endif
			m_abort = true;
			m_io_service.stop();
			return;
		}

		if (m_abort) return;
		float tick_interval = (microsec_clock::universal_time()
			- m_last_tick).total_milliseconds() / 1000.f;
		m_last_tick = microsec_clock::universal_time();

		m_timer.expires_from_now(seconds(1));
		m_timer.async_wait(m_strand.wrap(
			bind(&session_impl::second_tick, this, _1)));
		
		// do the second_tick() on each connection
		// this will update their statistics (download and upload speeds)
		// also purge sockets that have timed out
		// and keep sockets open by keeping them alive.
		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			// we need to do like this because j->second->disconnect() will
			// erase the connection from the map we're iterating
			connection_map::iterator j = i;
			++i;
			// if this socket has timed out
			// close it.
			peer_connection& c = *j->second;
			if (c.has_timed_out())
			{
				if (m_alerts.should_post(alert::debug))
				{
					m_alerts.post_alert(
						peer_error_alert(
							c.remote()
							, c.pid()
							, "connection timed out"));
				}
#if defined(TORRENT_VERBOSE_LOGGING)
				(*c.m_logger) << "*** CONNECTION TIMED OUT\n";
#endif

				c.set_failed();
				c.disconnect();
				continue;
			}

			c.keep_alive();
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
				m_tracker_manager.queue_request(m_strand, req, t.tracker_login()
					, m_listen_interface.address(), i->second);

				if (m_alerts.should_post(alert::info))
				{
					m_alerts.post_alert(
						tracker_announce_alert(
							t.get_handle(), "tracker announce"));
				}
			}

			// second_tick() will set the used upload quota
			t.second_tick(m_stat, tick_interval);
			++i;
		}

		m_stat.second_tick(tick_interval);

		// distribute the maximum upload rate among the torrents

		assert(m_max_uploads >= -1);
		assert(m_max_connections >= -1);

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
#ifndef NDEBUG
			i->second->check_invariant();
#endif
			i->second->distribute_resources(tick_interval);
		}
	}
	catch (std::exception& exc)
	{
#ifndef NDEBUG
		std::string err = exc.what();
#endif
	}; // msvc 7.1 seems to require this

	void session_impl::connection_completed(
		boost::intrusive_ptr<peer_connection> const& p)
#ifndef NDEBUG
	try
#endif
	{
		mutex_t::scoped_lock l(m_mutex);

		connection_map::iterator i = m_half_open.find(p->get_socket());
		m_connections.insert(std::make_pair(p->get_socket(), p));
		assert(i != m_half_open.end());
		if (i != m_half_open.end()) m_half_open.erase(i);

		if (m_abort) return;

		process_connection_queue();
	}
#ifndef NDEBUG
	catch (std::exception& e)
	{
		assert(false);
	};
#endif

	void session_impl::operator()()
	{
		eh_initializer();

		if (m_listen_port_range.first != 0 && m_listen_port_range.second != 0)
		{
			session_impl::mutex_t::scoped_lock l(m_mutex);
			open_listen_port();
		}

		boost::posix_time::ptime timer = second_clock::universal_time();

		do
		{
			try
			{
				m_io_service.run();
				assert(m_abort == true);
			}
			catch (std::exception& e)
			{
#ifndef NDEBUG
				std::cerr << e.what() << "\n";
				std::string err = e.what();
#endif
				assert(false);
			}
		}
		while (!m_abort);

		deadline_timer tracker_timer(m_io_service);

		session_impl::mutex_t::scoped_lock l(m_mutex);

		m_tracker_manager.abort_all_requests();
		for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i =
			m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			i->second->abort();
			// generate a tracker request in case the torrent is not paused
			// (in which case it's not currently announced with the tracker)
			// or if the torrent itself thinks we should request. Do not build
			// a request in case the torrent doesn't have any trackers
			if ((!i->second->is_paused() || i->second->should_request())
				&& !i->second->trackers().empty())
			{
				tracker_request req = i->second->generate_tracker_request();
				req.listen_port = m_listen_interface.port();
				req.key = m_key;
				std::string login = i->second->tracker_login();
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				boost::shared_ptr<tracker_logger> tl(new tracker_logger(*this));
				m_tracker_loggers.push_back(tl);
				m_tracker_manager.queue_request(m_strand, req, login, m_listen_interface.address(), tl);
#else
				m_tracker_manager.queue_request(m_strand, req, login, m_listen_interface.address());
#endif
			}
		}

		ptime start(microsec_clock::universal_time());
		l.unlock();

		while (microsec_clock::universal_time() - start < seconds(
			m_settings.stop_tracker_timeout)
			&& !m_tracker_manager.empty())
		{
			tracker_timer.expires_from_now(boost::posix_time::milliseconds(100));
			tracker_timer.async_wait(m_strand.wrap(
				bind(&io_service::stop, &m_io_service)));

			m_io_service.reset();
			m_io_service.run();
		}

		l.lock();
		assert(m_abort);
		m_abort = true;

		m_connection_queue.clear();

		while (!m_half_open.empty())
			m_half_open.begin()->second->disconnect();

		while (!m_connections.empty())
			m_connections.begin()->second->disconnect();

#ifndef NDEBUG
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end(); ++i)
		{
			assert(i->second->num_peers() == 0);
		}
#endif

		m_torrents.clear();

		assert(m_torrents.empty());
		assert(m_connections.empty());
	}


	// the return value from this function is valid only as long as the
	// session is locked!
	boost::weak_ptr<torrent> session_impl::find_torrent(sha1_hash const& info_hash)
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
		if (i != m_torrents.end()) return i->second;
		return boost::weak_ptr<torrent>();
	}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
	boost::shared_ptr<logger> session_impl::create_log(std::string const& name
		, int instance, bool append)
	{
		// current options are file_logger, cout_logger and null_logger
		return boost::shared_ptr<logger>(new logger(name + ".log", instance, append));
	}
#endif

	std::vector<torrent_handle> session_impl::get_torrents()
	{
		mutex_t::scoped_lock l(m_mutex);
		mutex::scoped_lock l2(m_checker_impl.m_mutex);
		std::vector<torrent_handle> ret;
		for (std::deque<boost::shared_ptr<aux::piece_checker_data> >::iterator i
			= m_checker_impl.m_torrents.begin()
			, end(m_checker_impl.m_torrents.end()); i != end; ++i)
		{
			if ((*i)->abort) continue;
			ret.push_back(torrent_handle(this, &m_checker_impl
				, (*i)->info_hash));
		}

		for (std::deque<boost::shared_ptr<aux::piece_checker_data> >::iterator i
			= m_checker_impl.m_processing.begin()
			, end(m_checker_impl.m_processing.end()); i != end; ++i)
		{
			if ((*i)->abort) continue;
			ret.push_back(torrent_handle(this, &m_checker_impl
				, (*i)->info_hash));
		}

		for (session_impl::torrent_map::iterator i
			= m_torrents.begin(), end(m_torrents.end());
			i != end; ++i)
		{
			if (i->second->is_aborted()) continue;
			ret.push_back(torrent_handle(this, &m_checker_impl
				, i->first));
		}
		return ret;
	}

	torrent_handle session_impl::find_torrent_handle(sha1_hash const& info_hash)
	{
		return torrent_handle(this, &m_checker_impl, info_hash);
	}

	torrent_handle session_impl::add_torrent(
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
		mutex_t::scoped_lock l(m_mutex);
		mutex::scoped_lock l2(m_checker_impl.m_mutex);

		if (is_aborted())
			throw std::runtime_error("session is closing");
		
		// is the torrent already active?
		if (!find_torrent(ti.info_hash()).expired())
			throw duplicate_torrent();

		// is the torrent currently being checked?
		if (m_checker_impl.find_torrent(ti.info_hash()))
			throw duplicate_torrent();

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(*this, m_checker_impl, ti, save_path
				, m_listen_interface, compact_mode, block_size
				, settings()));

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent_plugin> tp((*i)(torrent_ptr.get()));
			if (tp) torrent_ptr->add_extension(tp);
		}
#endif

		boost::shared_ptr<aux::piece_checker_data> d(
			new aux::piece_checker_data);
		d->torrent_ptr = torrent_ptr;
		d->save_path = save_path;
		d->info_hash = ti.info_hash();
		d->resume_data = resume_data;

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
		{
			torrent_info::nodes_t const& nodes = ti.nodes();
			std::for_each(nodes.begin(), nodes.end(), bind(
				(void(dht::dht_tracker::*)(std::pair<std::string, int> const&))
				&dht::dht_tracker::add_node
				, boost::ref(m_dht), _1));
		}
#endif

		// add the torrent to the queue to be checked
		m_checker_impl.m_torrents.push_back(d);
		// and notify the thread that it got another
		// job in its queue
		m_checker_impl.m_cond.notify_one();

		return torrent_handle(this, &m_checker_impl, ti.info_hash());
	}

	torrent_handle session_impl::add_torrent(
		char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
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
		session_impl::mutex_t::scoped_lock l(m_mutex);

		// is the torrent already active?
		if (!find_torrent(info_hash).expired())
			throw duplicate_torrent();

		// you cannot add new torrents to a session that is closing down
		assert(!is_aborted());

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(*this, m_checker_impl, tracker_url, info_hash, name
			, save_path, m_listen_interface, compact_mode, block_size
			, settings()));

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent_plugin> tp((*i)(torrent_ptr.get()));
			if (tp) torrent_ptr->add_extension(tp);
		}
#endif

		m_torrents.insert(
			std::make_pair(info_hash, torrent_ptr)).first;

		return torrent_handle(this, &m_checker_impl, info_hash);
	}

	void session_impl::remove_torrent(const torrent_handle& h)
	{
		if (h.m_ses != this) return;
		assert(h.m_chk == &m_checker_impl || h.m_chk == 0);
		assert(h.m_ses != 0);

		mutex_t::scoped_lock l(m_mutex);
		session_impl::torrent_map::iterator i =
			m_torrents.find(h.m_info_hash);
		if (i != m_torrents.end())
		{
			torrent& t = *i->second;
			t.abort();

			if ((!t.is_paused() || t.should_request())
				&& !t.torrent_file().trackers().empty())
			{
				tracker_request req = t.generate_tracker_request();
				assert(req.event == tracker_request::stopped);
				req.listen_port = m_listen_interface.port();
				req.key = m_key;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				boost::shared_ptr<tracker_logger> tl(new tracker_logger(*this));
				m_tracker_loggers.push_back(tl);
				m_tracker_manager.queue_request(m_strand, req
					, t.tracker_login(), m_listen_interface.address(), tl);
#else
				m_tracker_manager.queue_request(m_strand, req
					, t.tracker_login(), m_listen_interface.address());
#endif

				if (m_alerts.should_post(alert::info))
				{
					m_alerts.post_alert(
						tracker_announce_alert(
							t.get_handle(), "tracker announce, event=stopped"));
				}
			}
#ifndef NDEBUG
			sha1_hash i_hash = t.torrent_file().info_hash();
#endif
			m_torrents.erase(i);
			assert(m_torrents.find(i_hash) == m_torrents.end());
			return;
		}
		l.unlock();

		if (h.m_chk)
		{
			mutex::scoped_lock l(m_checker_impl.m_mutex);

			aux::piece_checker_data* d = m_checker_impl.find_torrent(h.m_info_hash);
			if (d != 0)
			{
				if (d->processing) d->abort = true;
				else m_checker_impl.remove_torrent(h.m_info_hash);
				return;
			}
		}
	}

	bool session_impl::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface)
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);

		tcp::endpoint new_interface;
		if (net_interface && std::strlen(net_interface) > 0)
			new_interface = tcp::endpoint(address::from_string(net_interface), port_range.first);
		else
			new_interface = tcp::endpoint(address(), port_range.first);

		m_listen_port_range = port_range;

		// if the interface is the same and the socket is open
		// don't do anything
		if (new_interface == m_listen_interface
			&& m_listen_socket) return true;

		if (m_listen_socket)
			m_listen_socket.reset();
			
#ifndef TORRENT_DISABLE_DHT
		if (m_listen_interface.address() != new_interface.address()
			&& m_dht)
		{
			// the listen interface changed, rebind the dht listen socket as well
			m_dht->rebind(new_interface.address()
				, m_dht_settings.service_port);
		}
#endif

		m_incoming_connection = false;
		m_listen_interface = new_interface;

		open_listen_port();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		m_logger = create_log("main_session", listen_port(), false);
		using boost::posix_time::second_clock;
		using boost::posix_time::to_simple_string;
		(*m_logger) << to_simple_string(second_clock::universal_time()) << "\n";
#endif

		return m_listen_socket;
	}

	unsigned short session_impl::listen_port() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_listen_interface.port();
	}

	session_status session_impl::status() const
	{
		mutex_t::scoped_lock l(m_mutex);
		session_status s;
		s.has_incoming_connections = m_incoming_connection;
		s.num_peers = (int)m_connections.size();

		s.download_rate = m_stat.download_rate();
		s.upload_rate = m_stat.upload_rate();

		s.payload_download_rate = m_stat.download_payload_rate();
		s.payload_upload_rate = m_stat.upload_payload_rate();

		s.total_download = m_stat.total_protocol_download()
			+ m_stat.total_payload_download();

		s.total_upload = m_stat.total_protocol_upload()
			+ m_stat.total_payload_upload();

		s.total_payload_download = m_stat.total_payload_download();
		s.total_payload_upload = m_stat.total_payload_upload();

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
		{
			m_dht->dht_status(s);
		}
		else
		{
			s.dht_nodes = 0;
			s.dht_node_cache = 0;
			s.dht_torrents = 0;
		}
#endif

		return s;
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::start_dht(entry const& startup_state)
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_dht)
		{
			m_dht->stop();
			m_dht = 0;
		}
		m_dht = new dht::dht_tracker(m_io_service
			, m_dht_settings, m_listen_interface.address()
			, startup_state);
	}

	void session_impl::stop_dht()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (!m_dht) return;
		m_dht->stop();
		m_dht = 0;
	}

	void session_impl::set_dht_settings(dht_settings const& settings)
	{
		mutex_t::scoped_lock l(m_mutex);
		if (settings.service_port != m_dht_settings.service_port
			&& m_dht)
		{
			m_dht->rebind(m_listen_interface.address()
				, settings.service_port);
		}
		m_dht_settings = settings;
	}

	entry session_impl::dht_state() const
	{
		assert(m_dht);
		mutex_t::scoped_lock l(m_mutex);
		return m_dht->state();
	}

	void session_impl::add_dht_node(std::pair<std::string, int> const& node)
	{
		assert(m_dht);
		mutex_t::scoped_lock l(m_mutex);
		m_dht->add_node(node);
	}

	void session_impl::add_dht_router(std::pair<std::string, int> const& node)
	{
		assert(m_dht);
		mutex_t::scoped_lock l(m_mutex);
		m_dht->add_router_node(node);
	}

#endif

	void session_impl::set_download_rate_limit(int bytes_per_second)
	{
		assert(bytes_per_second > 0 || bytes_per_second == -1);
		mutex_t::scoped_lock l(m_mutex);
		if (bytes_per_second == -1) bytes_per_second = bandwidth_limit::inf;
		m_dl_bandwidth_manager.throttle(bytes_per_second);
	}

	bool session_impl::is_listening() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_listen_socket;
	}

	session_impl::~session_impl()
	{
#ifndef TORRENT_DISABLE_DHT
		stop_dht();
#endif
		// lock the main thread and abort it
		mutex_t::scoped_lock l(m_mutex);
		m_abort = true;
		m_io_service.stop();
		l.unlock();

		m_thread->join();

		// it's important that the main thread is closed completely before
		// the checker thread is terminated. Because all the connections
		// have to be closed and removed from the torrents before they
		// can be destructed. (because the weak pointers in the
		// peer_connections will be invalidated when the torrents are
		// destructed and then the invariant will be broken).

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

		m_checker_thread->join();

		assert(m_torrents.empty());
		assert(m_connections.empty());
	}

	void session_impl::set_max_uploads(int limit)
	{
		assert(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);
		m_max_uploads = limit;
	}

	void session_impl::set_max_connections(int limit)
	{
		assert(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);
		m_max_connections = limit;
	}

	void session_impl::set_max_half_open_connections(int limit)
	{
		assert(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);
   
		m_half_open_limit = limit;
	}

	void session_impl::set_upload_rate_limit(int bytes_per_second)
	{
		assert(bytes_per_second > 0 || bytes_per_second == -1);
		mutex_t::scoped_lock l(m_mutex);
		if (bytes_per_second == -1) bytes_per_second = bandwidth_limit::inf;
		m_ul_bandwidth_manager.throttle(bytes_per_second);
	}

	int session_impl::num_uploads() const
	{
		int uploads = 0;
		mutex_t::scoped_lock l(m_mutex);
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; i++)
		{
			uploads += i->second->get_policy().num_uploads();
		}
		return uploads;
	}

	int session_impl::num_connections() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return  m_connections.size() + m_half_open.size();
	}


	std::auto_ptr<alert> session_impl::pop_alert()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_alerts.pending())
			return m_alerts.get();
		return std::auto_ptr<alert>(0);
	}

	void session_impl::set_severity_level(alert::severity_t s)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_alerts.set_severity(s);
	}

	int session_impl::upload_rate_limit() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_ul_bandwidth_manager.throttle();
	}

	int session_impl::download_rate_limit() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_dl_bandwidth_manager.throttle();
	}

#ifndef NDEBUG
	void session_impl::check_invariant(const char *place)
	{
		assert(place);

		for (connection_map::iterator i = m_half_open.begin();
			i != m_half_open.end(); ++i)
		{
			assert(i->second->is_connecting());
		}

		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			assert(i->second);
			assert(!i->second->is_connecting());
			if (i->second->is_connecting())
			{
				std::ofstream error_log("error.log", std::ios_base::app);
				boost::intrusive_ptr<peer_connection> p = i->second;
				error_log << "peer_connection::is_connecting() " << p->is_connecting() << "\n";
				error_log << "peer_connection::can_write() " << p->can_write() << "\n";
				error_log << "peer_connection::can_read() " << p->can_read() << "\n";
				error_log << "peer_connection::get_peer_id " << p->pid() << "\n";
				error_log << "place: " << place << "\n";
				error_log.flush();
				assert(false);
			}

			boost::shared_ptr<torrent> t = i->second->associated_torrent().lock();

			if (t)
			{
				assert(t->get_policy().has_connection(boost::get_pointer(i->second)));
			}
		}
	}
#endif

	void piece_checker_data::parse_resume_data(
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
			sha1_hash hash = rd["info-hash"].string();
			if (hash != info.info_hash())
			{
				error = "mismatching info-hash: " + boost::lexical_cast<std::string>(hash);
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
						address::from_string((*i)["ip"].string())
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
					if (ad.integer() != entry::integer_type(adler))
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
}}

