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

#include "libtorrent/pch.hpp"

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
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/enum_net.hpp"

#ifndef TORRENT_DISABLE_ENCRYPTION

#include <openssl/crypto.h>

namespace
{
	// openssl requires this to clean up internal
	// structures it allocates
	struct openssl_cleanup
	{
		~openssl_cleanup() { CRYPTO_cleanup_all_ex_data(); }
	} openssl_global_destructor;
}

#endif
#ifdef _WIN32
// for ERROR_SEM_TIMEOUT
#include <winerror.h>
#endif

using boost::shared_ptr;
using boost::weak_ptr;
using boost::bind;
using boost::mutex;
using libtorrent::aux::session_impl;

namespace libtorrent {

namespace fs = boost::filesystem;

namespace detail
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

					// lock the session to add the new torrent
					session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

					if (!error_msg.empty() && m_ses.m_alerts.should_post(alert::warning))
					{
						m_ses.m_alerts.post_alert(fastresume_rejected_alert(
							t->torrent_ptr->get_handle()
							, error_msg));
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
						(*m_ses.m_logger) << "fastresume data for "
							<< t->torrent_ptr->torrent_file().name() << " rejected: "
							<< error_msg << "\n";
#endif
					}

					mutex::scoped_lock l2(m_mutex);

					if (m_torrents.empty() || m_torrents.front() != t)
					{
						// this means the torrent was removed right after it was
						// added. Abort the checking.
						t.reset();
						continue;
					}
					
					// clear the resume data now that it has been used
					// (the fast resume data is now parsed and stored in t)
					t->resume_data = entry();
					bool up_to_date = t->torrent_ptr->check_fastresume(*t);

					if (up_to_date)
					{
						INVARIANT_CHECK;

						TORRENT_ASSERT(!m_torrents.empty());
						TORRENT_ASSERT(m_torrents.front() == t);

						t->torrent_ptr->files_checked(t->unfinished_pieces);
						m_torrents.pop_front();

						// we cannot add the torrent if the session is aborted.
						if (!m_ses.is_aborted())
						{
							m_ses.m_torrents.insert(std::make_pair(t->info_hash, t->torrent_ptr));
							if (m_ses.m_alerts.should_post(alert::info))
							{
				  				m_ses.m_alerts.post_alert(torrent_checked_alert(
					 				processing->torrent_ptr->get_handle()
					 				, "torrent finished checking"));
							}
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
								t->torrent_ptr->get_policy().peer_from_tracker(*i, id
									, peer_info::resume_data, 0);
							}

							for (std::vector<tcp::endpoint>::const_iterator i = t->banned_peers.begin();
								i != t->banned_peers.end(); ++i)
							{
								policy::peer* p = t->torrent_ptr->get_policy().peer_from_tracker(*i, id
									, peer_info::resume_data, 0);
								if (p) p->banned = true;
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
					TORRENT_ASSERT(m_torrents.front() == t);

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

				TORRENT_ASSERT(!m_torrents.empty());
				m_torrents.pop_front();
			}
			catch(...)
			{
#ifndef NDEBUG
				std::cerr << "error while checking resume data\n";
#endif
				mutex::scoped_lock l(m_mutex);
				TORRENT_ASSERT(!m_torrents.empty());
				m_torrents.pop_front();
				TORRENT_ASSERT(false);
			}

			if (!processing) continue;

			try
			{	
				TORRENT_ASSERT(processing);
	
				float finished = false;
				float progress = 0.f;
				boost::tie(finished, progress) = processing->torrent_ptr->check_files();

				{
					mutex::scoped_lock l2(m_mutex);

					INVARIANT_CHECK;

					processing->progress = progress;
					if (processing->abort)
					{
						TORRENT_ASSERT(!m_processing.empty());
						TORRENT_ASSERT(m_processing.front() == processing);
						m_processing.pop_front();

						// make sure the lock order is correct
						l2.unlock();
						session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
						l2.lock();
						processing->torrent_ptr->abort();

						processing.reset();
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

					TORRENT_ASSERT(!m_processing.empty());
					TORRENT_ASSERT(m_processing.front() == processing);

					// TODO: factor out the adding of torrents to the session
					// and to the checker thread to avoid duplicating the
					// check for abortion.
					if (!m_ses.is_aborted())
					{
						processing->torrent_ptr->files_checked(processing->unfinished_pieces);
						m_ses.m_torrents.insert(std::make_pair(
							processing->info_hash, processing->torrent_ptr));
						if (m_ses.m_alerts.should_post(alert::info))
						{
							m_ses.m_alerts.post_alert(torrent_checked_alert(
								processing->torrent_ptr->get_handle()
								, "torrent finished checking"));
	 					}
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
							processing->torrent_ptr->get_policy().peer_from_tracker(*i, id
								, peer_info::resume_data, 0);
						}

						for (std::vector<tcp::endpoint>::const_iterator i = processing->banned_peers.begin();
							i != processing->banned_peers.end(); ++i)
						{
							policy::peer* p = processing->torrent_ptr->get_policy().peer_from_tracker(*i, id
								, peer_info::resume_data, 0);
							if (p) p->banned = true;
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

				processing->torrent_ptr->abort();

				if (!m_processing.empty()
					&& m_processing.front() == processing)
					m_processing.pop_front();
				processing.reset();
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
				TORRENT_ASSERT(!m_processing.empty());

				processing.reset();
				m_processing.pop_front();
				if (!m_processing.empty())
				{
					processing = m_processing.front();
					processing->processing = true;
				}

				TORRENT_ASSERT(false);
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

	void checker_impl::remove_torrent(sha1_hash const& info_hash, int options)
	{
		INVARIANT_CHECK;
		for (std::deque<boost::shared_ptr<piece_checker_data> >::iterator i
			= m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			if ((*i)->info_hash == info_hash)
			{
				TORRENT_ASSERT((*i)->processing == false);
				if (options & session::delete_files)
					(*i)->torrent_ptr->delete_files();
				m_torrents.erase(i);
				return;
			}
		}
		for (std::deque<boost::shared_ptr<piece_checker_data> >::iterator i
			= m_processing.begin(); i != m_processing.end(); ++i)
		{
			if ((*i)->info_hash == info_hash)
			{
				TORRENT_ASSERT((*i)->processing == false);
				if (options & session::delete_files)
					(*i)->torrent_ptr->delete_files();
				m_processing.erase(i);
				return;
			}
		}

		TORRENT_ASSERT(false);
	}

#ifndef NDEBUG
	void checker_impl::check_invariant() const
	{
		for (std::deque<boost::shared_ptr<piece_checker_data> >::const_iterator i
			= m_torrents.begin(); i != m_torrents.end(); ++i)
		{
			TORRENT_ASSERT(*i);
			TORRENT_ASSERT((*i)->torrent_ptr);
		}
		for (std::deque<boost::shared_ptr<piece_checker_data> >::const_iterator i
			= m_processing.begin(); i != m_processing.end(); ++i)
		{
			TORRENT_ASSERT(*i);
			TORRENT_ASSERT((*i)->torrent_ptr);
		}
	}
#endif

	struct seed_random_generator
	{
		seed_random_generator()
		{
			std::srand(total_microseconds(time_now() - min_time()));
		}
	};

	session_impl::session_impl(
		std::pair<int, int> listen_port_range
		, fingerprint const& cl_fprint
		, char const* listen_interface)
		: m_send_buffers(send_buffer_size)
		, m_files(40)
		, m_strand(m_io_service)
		, m_half_open(m_io_service)
		, m_download_channel(m_io_service, peer_connection::download_channel)
		, m_upload_channel(m_io_service, peer_connection::upload_channel)
		, m_tracker_manager(m_settings, m_tracker_proxy)
		, m_listen_port_retries(listen_port_range.second - listen_port_range.first)
		, m_listen_interface(address::from_string(listen_interface), listen_port_range.first)
		, m_abort(false)
		, m_max_uploads(8)
		, m_max_connections(200)
		, m_num_unchoked(0)
		, m_unchoke_time_scaler(0)
		, m_optimistic_unchoke_time_scaler(0)
		, m_disconnect_time_scaler(0)
		, m_incoming_connection(false)
		, m_last_tick(time_now())
#ifndef TORRENT_DISABLE_DHT
		, m_dht_same_port(true)
		, m_external_udp_port(0)
#endif
		, m_timer(m_io_service)
		, m_next_connect_torrent(0)
		, m_checker_impl(*this)
	{
#ifdef WIN32
		// windows XP has a limit on the number of
		// simultaneous half-open TCP connections
		DWORD windows_version = ::GetVersion();
		if ((windows_version & 0xff) >= 6)
		{
			// on vista the limit is 5 (in home edition)
			m_half_open.limit(4);
		}
		else
		{
			// on XP SP2 it's 10	
			m_half_open.limit(8);
		}
#endif

		m_bandwidth_manager[peer_connection::download_channel] = &m_download_channel;
		m_bandwidth_manager[peer_connection::upload_channel] = &m_upload_channel;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		m_logger = create_log("main_session", listen_port(), false);
		(*m_logger) << time_now_string() << "\n";
#endif

#ifdef TORRENT_STATS
		m_stats_logger.open("session_stats.log", std::ios::trunc);
		m_stats_logger <<
			"1. second\n"
			"2. upload rate\n"
			"3. download rate\n"
			"4. downloading torrents\n"
			"5. seeding torrents\n"
			"6. peers\n"
			"7. connecting peers\n"
			"8. disk block buffers\n"
			"\n";
		m_buffer_usage_logger.open("buffer_stats.log", std::ios::trunc);
		m_second_counter = 0;
#endif

		// ---- generate a peer id ----
		static seed_random_generator seeder;

		m_key = rand() + (rand() << 15) + (rand() << 30);
		std::string print = cl_fprint.to_string();
		TORRENT_ASSERT(print.length() <= 20);

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
		boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext)
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
		if (m_abort) return;
#if defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " *** ABORT CALLED ***\n";
#endif
		// abort the main thread
		m_abort = true;
		if (m_lsd) m_lsd->close();
		if (m_upnp) m_upnp->close();
		if (m_natpmp) m_natpmp->close();
#ifndef TORRENT_DISABLE_DHT
		if (m_dht) m_dht->stop();
#endif
		m_timer.cancel();

		// close the listen sockets
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			i->sock->close();
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " aborting all torrents\n";
#endif
		// abort all torrents
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->abort();
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " aborting all connections\n";
#endif
		// abort all connections
		for (connection_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			i->second->disconnect();
		}
		
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " aborting all tracker requests\n";
#endif
		m_tracker_manager.abort_all_requests();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " shutting down connection queue\n";
#endif
		m_half_open.close();

		mutex::scoped_lock l2(m_checker_impl.m_mutex);
		// abort the checker thread
		m_checker_impl.m_abort = true;
	}

	void session_impl::set_port_filter(port_filter const& f)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_port_filter = f;
	}

	void session_impl::set_ip_filter(ip_filter const& f)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->ip_filter_updated();
	}

	void session_impl::set_settings(session_settings const& s)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		TORRENT_ASSERT(s.connection_speed > 0);
		TORRENT_ASSERT(s.file_pool_size > 0);

		// less than 5 seconds unchoke interval is insane
		TORRENT_ASSERT(s.unchoke_interval >= 5);
		m_settings = s;
		m_files.resize(m_settings.file_pool_size);
		// replace all occurances of '\n' with ' '.
		std::string::iterator i = m_settings.user_agent.begin();
		while ((i = std::find(i, m_settings.user_agent.end(), '\n'))
			!= m_settings.user_agent.end())
			*i = ' ';
	}

	tcp::endpoint session_impl::get_ipv6_interface() const
	{
		return m_ipv6_interface;
	}

	session_impl::listen_socket_t session_impl::setup_listener(tcp::endpoint ep, int retries)
	{
		asio::error_code ec;
		listen_socket_t s;
		s.sock.reset(new socket_acceptor(m_io_service));
		s.sock->open(ep.protocol(), ec);
		s.sock->set_option(socket_acceptor::reuse_address(true), ec);
		s.sock->bind(ep, ec);
		while (ec && retries > 0)
		{
			ec = asio::error_code();
			TORRENT_ASSERT(!ec);
			--retries;
			ep.port(ep.port() + 1);
			s.sock->bind(ep, ec);
		}
		if (ec)
		{
			// instead of giving up, try
			// let the OS pick a port
			ep.port(0);
			ec = asio::error_code();
			s.sock->bind(ep, ec);
		}
		if (ec)
		{
			// not even that worked, give up
			if (m_alerts.should_post(alert::fatal))
			{
				std::stringstream msg;
				msg << "cannot bind to interface '";
				print_endpoint(msg, ep) << "' " << ec.message();
				m_alerts.post_alert(listen_failed_alert(ep, msg.str()));
			}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::stringstream msg;
			msg << "cannot bind to interface '";
			print_endpoint(msg, ep) << "' " << ec.message();
			(*m_logger) << msg.str() << "\n";
#endif
			return listen_socket_t();
		}
		s.external_port = s.sock->local_endpoint(ec).port();
		s.sock->listen(0, ec);
		if (ec)
		{
			if (m_alerts.should_post(alert::fatal))
			{
				std::stringstream msg;
				msg << "cannot listen on interface '";
				print_endpoint(msg, ep) << "' " << ec.message();
				m_alerts.post_alert(listen_failed_alert(ep, msg.str()));
			}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::stringstream msg;
			msg << "cannot listen on interface '";
			print_endpoint(msg, ep) << "' " << ec.message();
			(*m_logger) << msg.str() << "\n";
#endif
			return listen_socket_t();
		}

		if (m_alerts.should_post(alert::fatal))
		{
			std::string msg = "listening on interface "
				+ boost::lexical_cast<std::string>(ep);
			m_alerts.post_alert(listen_succeeded_alert(ep, msg));
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << "listening on: " << ep
			<< " external port: " << s.external_port << "\n";
#endif
		return s;
	}
	
	void session_impl::open_listen_port() throw()
	{
		// close the open listen sockets
		m_listen_sockets.clear();
		m_incoming_connection = false;

		if (is_any(m_listen_interface.address()))
		{
			// this means we should open two listen sockets
			// one for IPv4 and one for IPv6
		
			listen_socket_t s = setup_listener(
				tcp::endpoint(address_v4::any(), m_listen_interface.port())
				, m_listen_port_retries);

			if (s.sock)
			{
				m_listen_sockets.push_back(s);
				async_accept(s.sock);
			}

			s = setup_listener(
				tcp::endpoint(address_v6::any(), m_listen_interface.port())
				, m_listen_port_retries);

			if (s.sock)
			{
				m_listen_sockets.push_back(s);
				async_accept(s.sock);
			}
		}
		else
		{
			// we should only open a single listen socket, that
			// binds to the given interface

			listen_socket_t s = setup_listener(
				m_listen_interface, m_listen_port_retries);

			if (s.sock)
			{
				m_listen_sockets.push_back(s);
				async_accept(s.sock);
			}
		}

		m_ipv6_interface = tcp::endpoint();

		for (std::list<listen_socket_t>::const_iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			asio::error_code ec;
			tcp::endpoint ep = i->sock->local_endpoint(ec);
			if (ec || ep.address().is_v4()) continue;

			if (ep.address().to_v6() != address_v6::any())
			{
				// if we're listening on a specific address
				// pick it
				m_ipv6_interface = ep;
			}
			else
			{
				// if we're listening on any IPv6 address, enumerate them and
				// pick the first non-local address
				std::vector<address> const& ifs = enum_net_interfaces(m_io_service, ec);
				for (std::vector<address>::const_iterator i = ifs.begin()
					, end(ifs.end()); i != end; ++i)
				{
					if (i->is_v4() || i->to_v6().is_link_local() || i->to_v6().is_loopback()) continue;
					m_ipv6_interface = tcp::endpoint(*i, ep.port());
					break;
				}
				break;
			}
		}

		if (!m_listen_sockets.empty())
		{
			asio::error_code ec;
			tcp::endpoint local = m_listen_sockets.front().sock->local_endpoint(ec);
			if (!ec)
			{
				if (m_natpmp.get()) m_natpmp->set_mappings(local.port(), 0);
				if (m_upnp.get()) m_upnp->set_mappings(local.port(), 0);
			}
		}
	}

	void session_impl::async_accept(boost::shared_ptr<socket_acceptor> const& listener)
	{
		shared_ptr<socket_type> c(new socket_type);
		c->instantiate<stream_socket>(m_io_service);
		listener->async_accept(c->get<stream_socket>()
			, bind(&session_impl::on_incoming_connection, this, c
			, boost::weak_ptr<socket_acceptor>(listener), _1));
	}

	void session_impl::on_incoming_connection(shared_ptr<socket_type> const& s
		, weak_ptr<socket_acceptor> listen_socket, asio::error_code const& e) try
	{
		boost::shared_ptr<socket_acceptor> listener = listen_socket.lock();
		if (!listener) return;
		
		if (e == asio::error::operation_aborted) return;

		mutex_t::scoped_lock l(m_mutex);
		if (m_abort) return;

		asio::error_code ec;
		if (e)
		{
			tcp::endpoint ep = listener->local_endpoint(ec);
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::string msg = "error accepting connection on '"
				+ boost::lexical_cast<std::string>(ep) + "' " + e.message();
			(*m_logger) << msg << "\n";
#endif
#ifdef _WIN32
			// Windows sometimes generates this error. It seems to be
			// non-fatal and we have to do another async_accept.
			if (e.value() == ERROR_SEM_TIMEOUT)
			{
				async_accept(listener);
				return;
			}
#endif
			if (m_alerts.should_post(alert::fatal))
			{
				std::string msg = "error accepting connection on '"
					+ boost::lexical_cast<std::string>(ep) + "' " + ec.message();
				m_alerts.post_alert(listen_failed_alert(ep, msg));
			}
			return;
		}
		async_accept(listener);

		// we got a connection request!
		m_incoming_connection = true;
		tcp::endpoint endp = s->remote_endpoint(ec);

		if (ec)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << endp << " <== INCOMING CONNECTION FAILED, could "
				"not retrieve remote endpoint " << ec.message() << "\n";
#endif
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << endp << " <== INCOMING CONNECTION\n";
#endif
		if (m_ip_filter.access(endp.address()) & ip_filter::blocked)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << "filtered blocked ip\n";
#endif
			if (m_alerts.should_post(alert::info))
			{
				m_alerts.post_alert(peer_blocked_alert(endp.address()
					, "incoming connection blocked by IP filter"));
			}
			return;
		}

		// don't allow more connections than the max setting
		if (m_connections.size() > max_connections())
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << "number of connections limit exceeded (conns: "
				<< num_connections() << ", limit: " << max_connections()
				<< "), connection rejected\n";
#endif
			return;
		}

		// check if we have any active torrents
		// if we don't reject the connection
		if (m_torrents.empty()) return;

		bool has_active_torrent = false;
		for (torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
		{
			if (!i->second->is_paused())
			{
				has_active_torrent = true;
				break;
			}
		}
		if (!has_active_torrent) return;

		boost::intrusive_ptr<peer_connection> c(
			new bt_peer_connection(*this, s, 0));
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
	};
	
	void session_impl::connection_failed(boost::shared_ptr<socket_type> const& s
		, tcp::endpoint const& a, char const* message)
#ifndef NDEBUG
		try
#endif
	{
		mutex_t::scoped_lock l(m_mutex);
	
// too expensive
//		INVARIANT_CHECK;
		
		connection_map::iterator p = m_connections.find(s);

		// the connection may have been disconnected in the receive or send phase
		if (p == m_connections.end()) return;
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
#ifndef NDEBUG
	catch (...)
	{
		TORRENT_ASSERT(false);
	};
#endif

	void session_impl::close_connection(boost::intrusive_ptr<peer_connection> const& p)
	{
		mutex_t::scoped_lock l(m_mutex);

// too expensive
//		INVARIANT_CHECK;

		TORRENT_ASSERT(p->is_disconnecting());
		connection_map::iterator i = m_connections.find(p->get_socket());
		if (i != m_connections.end())
		{
			if (!i->second->is_choked()) --m_num_unchoked;
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

// too expensive
//		INVARIANT_CHECK;

		if (m_abort) return;

		if (e)
		{
#if defined(TORRENT_LOGGING)
			(*m_logger) << "*** SECOND TIMER FAILED " << e.message() << "\n";
#endif
			abort();
			return;
		}

		float tick_interval = total_microseconds(time_now() - m_last_tick) / 1000000.f;
		m_last_tick = time_now();

		m_timer.expires_from_now(seconds(1));
		m_timer.async_wait(m_strand.wrap(
			bind(&session_impl::second_tick, this, _1)));

#ifdef TORRENT_STATS
		++m_second_counter;
		int downloading_torrents = 0;
		int seeding_torrents = 0;
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			if (i->second->is_seed())
				++seeding_torrents;
			else
				++downloading_torrents;
		}
		int num_complete_connections = 0;
		int num_half_open = 0;
		for (connection_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			if (i->second->is_connecting())
				++num_half_open;
			else
				++num_complete_connections;
		}
		
		m_stats_logger
			<< m_second_counter << "\t"
			<< m_stat.upload_rate() << "\t"
			<< m_stat.download_rate() << "\t"
			<< downloading_torrents << "\t"
			<< seeding_torrents << "\t"
			<< num_complete_connections << "\t"
			<< num_half_open << "\t"
			<< m_disk_thread.disk_allocations() << "\t"
			<< std::endl;
#endif

	
		// let torrents connect to peers if they want to
		// if there are any torrents and any free slots

		// this loop will "hand out" max(connection_speed
		// , half_open.free_slots()) to the torrents, in a
		// round robin fashion, so that every torrent is
		// equallt likely to connect to a peer

		if (!m_torrents.empty()
			&& m_half_open.free_slots()
			&& num_connections() < m_max_connections)
		{
			// this is the maximum number of connections we will
			// attempt this tick
			int max_connections = m_settings.connection_speed;

			torrent_map::iterator i = m_torrents.begin();
			if (m_next_connect_torrent < int(m_torrents.size()))
				std::advance(i, m_next_connect_torrent);
			else
				m_next_connect_torrent = 0;
			int steps_since_last_connect = 0;
			int num_torrents = int(m_torrents.size());
			for (;;)
			{
				torrent& t = *i->second;
				if (t.want_more_peers())
				{
					if (t.try_connect_peer())
					{
						--max_connections;
						steps_since_last_connect = 0;
					}
				}
				++m_next_connect_torrent;
				++steps_since_last_connect;
				++i;
				if (i == m_torrents.end())
				{
					TORRENT_ASSERT(m_next_connect_torrent == num_torrents);
					i = m_torrents.begin();
					m_next_connect_torrent = 0;
				}
				// if we have gone one whole loop without
				// handing out a single connection, break
				if (steps_since_last_connect > num_torrents) break;
				// if there are no more free connection slots, abort
				if (m_half_open.free_slots() == 0) break;
				// if we should not make any more connections
				// attempts this tick, abort
				if (max_connections == 0) break;
				// maintain the global limit on number of connections
				if (num_connections() >= m_max_connections) break;
			}
		}

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
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end();)
		{
			torrent& t = *i->second;
			TORRENT_ASSERT(!t.is_aborted());
			if (t.should_request())
			{
				tracker_request req = t.generate_tracker_request();
				req.listen_port = 0;
				if (!m_listen_sockets.empty())
					req.listen_port = m_listen_sockets.front().external_port;
				req.key = m_key;
				m_tracker_manager.queue_request(m_strand, m_half_open, req
					, t.tracker_login(), m_listen_interface.address(), i->second);

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

		// --------------------------------------------------------------
		// unchoke set and optimistic unchoke calculations
		// --------------------------------------------------------------
		m_unchoke_time_scaler--;
		if (m_unchoke_time_scaler <= 0 && !m_connections.empty())
		{
			m_unchoke_time_scaler = settings().unchoke_interval;

			std::vector<peer_connection*> peers;
			for (connection_map::iterator i = m_connections.begin()
				, end(m_connections.end()); i != end; ++i)
			{
				peer_connection* p = i->second.get();
				torrent* t = p->associated_torrent().lock().get();
				if (!p->peer_info_struct()
					|| t == 0
					|| !p->is_peer_interested()
					|| p->is_disconnecting()
					|| p->is_connecting()
					|| (p->share_diff() < -free_upload_amount
						&& !t->is_seed()))
				{
					if (!i->second->is_choked() && t)
					{
						policy::peer* pi = p->peer_info_struct();
						if (pi && pi->optimistically_unchoked)
						{
							pi->optimistically_unchoked = false;
							// force a new optimistic unchoke
							m_optimistic_unchoke_time_scaler = 0;
						}
						t->choke_peer(*i->second);
					}
					continue;
				}
				peers.push_back(i->second.get());
			}

			// sort the peers that are eligible for unchoke by download rate and secondary
			// by total upload. The reason for this is, if all torrents are being seeded,
			// the download rate will be 0, and the peers we have sent the least to should
			// be unchoked
			std::sort(peers.begin(), peers.end()
				, bind(&stat::total_payload_upload, bind(&peer_connection::statistics, _1))
				< bind(&stat::total_payload_upload, bind(&peer_connection::statistics, _2)));

			std::stable_sort(peers.begin(), peers.end()
				, bind(&stat::download_payload_rate, bind(&peer_connection::statistics, _1))
				> bind(&stat::download_payload_rate, bind(&peer_connection::statistics, _2)));

			// reserve one upload slot for optimistic unchokes
			int unchoke_set_size = m_max_uploads - 1;

			m_num_unchoked = 0;
			// go through all the peers and unchoke the first ones and choke
			// all the other ones.
			for (std::vector<peer_connection*>::iterator i = peers.begin()
				, end(peers.end()); i != end; ++i)
			{
				peer_connection* p = *i;
				TORRENT_ASSERT(p);
				torrent* t = p->associated_torrent().lock().get();
				TORRENT_ASSERT(t);
				if (unchoke_set_size > 0)
				{
					if (p->is_choked())
					{
						if (!t->unchoke_peer(*p))
							continue;
					}
	
					--unchoke_set_size;
					++m_num_unchoked;

					TORRENT_ASSERT(p->peer_info_struct());
					if (p->peer_info_struct()->optimistically_unchoked)
					{
						// force a new optimistic unchoke
						m_optimistic_unchoke_time_scaler = 0;
						p->peer_info_struct()->optimistically_unchoked = false;
					}
				}
				else
				{
					TORRENT_ASSERT(p->peer_info_struct());
					if (!p->is_choked() && !p->peer_info_struct()->optimistically_unchoked)
						t->choke_peer(*p);
					if (!p->is_choked())
						++m_num_unchoked;
				}
			}

			m_optimistic_unchoke_time_scaler--;
			if (m_optimistic_unchoke_time_scaler <= 0)
			{
				m_optimistic_unchoke_time_scaler
					= settings().optimistic_unchoke_multiplier;

				// find the peer that has been waiting the longest to be optimistically
				// unchoked
				connection_map::iterator current_optimistic_unchoke = m_connections.end();
				connection_map::iterator optimistic_unchoke_candidate = m_connections.end();
				ptime last_unchoke = max_time();

				for (connection_map::iterator i = m_connections.begin()
					, end(m_connections.end()); i != end; ++i)
				{
					peer_connection* p = i->second.get();
					TORRENT_ASSERT(p);
					policy::peer* pi = p->peer_info_struct();
					if (!pi) continue;
					torrent* t = p->associated_torrent().lock().get();
					if (!t) continue;

					if (pi->optimistically_unchoked)
					{
						TORRENT_ASSERT(!p->is_choked());
						TORRENT_ASSERT(current_optimistic_unchoke == m_connections.end());
						current_optimistic_unchoke = i;
					}

					if (pi->last_optimistically_unchoked < last_unchoke
						&& !p->is_connecting()
						&& !p->is_disconnecting()
						&& p->is_peer_interested()
						&& t->free_upload_slots()
						&& p->is_choked())
					{
						last_unchoke = pi->last_optimistically_unchoked;
						optimistic_unchoke_candidate = i;
					}
				}

				if (optimistic_unchoke_candidate != m_connections.end()
					&& optimistic_unchoke_candidate != current_optimistic_unchoke)
				{
					if (current_optimistic_unchoke != m_connections.end())
					{
						torrent* t = current_optimistic_unchoke->second->associated_torrent().lock().get();
						TORRENT_ASSERT(t);
						current_optimistic_unchoke->second->peer_info_struct()->optimistically_unchoked = false;
						t->choke_peer(*current_optimistic_unchoke->second);
					}
					else
					{
						++m_num_unchoked;
					}

					torrent* t = optimistic_unchoke_candidate->second->associated_torrent().lock().get();
					TORRENT_ASSERT(t);
					bool ret = t->unchoke_peer(*optimistic_unchoke_candidate->second);
					TORRENT_ASSERT(ret);
					optimistic_unchoke_candidate->second->peer_info_struct()->optimistically_unchoked = true;
				}
			}
		}

		// --------------------------------------------------------------
		// disconnect peers when we have too many
		// --------------------------------------------------------------
		--m_disconnect_time_scaler;
		if (m_disconnect_time_scaler <= 0)
		{
			m_disconnect_time_scaler = 60;

			// every 60 seconds, disconnect the worst peer
			// if we have reached the connection limit
			if (num_connections() >= max_connections() && !m_torrents.empty())
			{
				torrent_map::iterator i = std::max_element(m_torrents.begin(), m_torrents.end()
					, bind(&torrent::num_peers, bind(&torrent_map::value_type::second, _1))
					< bind(&torrent::num_peers, bind(&torrent_map::value_type::second, _2)));
			
				TORRENT_ASSERT(i != m_torrents.end());
				i->second->get_policy().disconnect_one_peer();
			}
		}
	}
	catch (std::exception& exc)
	{
#ifndef NDEBUG
		std::cerr << exc.what() << std::endl;
		TORRENT_ASSERT(false);
#endif
	}; // msvc 7.1 seems to require this

	void session_impl::operator()()
	{
		eh_initializer();

		{
			session_impl::mutex_t::scoped_lock l(m_mutex);
			if (m_listen_interface.port() != 0) open_listen_port();
		}

		ptime timer = time_now();

		do
		{
			try
			{
				m_io_service.run();
				TORRENT_ASSERT(m_abort == true);
			}
			catch (std::exception& e)
			{
#ifndef NDEBUG
				std::cerr << e.what() << "\n";
				std::string err = e.what();
#endif
				TORRENT_ASSERT(false);
			}
		}
		while (!m_abort);

		deadline_timer tracker_timer(m_io_service);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " locking mutex\n";
#endif
		session_impl::mutex_t::scoped_lock l(m_mutex);

		m_tracker_manager.abort_all_requests();
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " sending stopped to all torrent's trackers\n";
#endif
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
				TORRENT_ASSERT(!m_listen_sockets.empty());
				req.listen_port = 0;
				if (!m_listen_sockets.empty())
					req.listen_port = m_listen_sockets.front().external_port;
				req.key = m_key;
				std::string login = i->second->tracker_login();
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				boost::shared_ptr<tracker_logger> tl(new tracker_logger(*this));
				m_tracker_loggers.push_back(tl);
				m_tracker_manager.queue_request(m_strand, m_half_open, req, login
					, m_listen_interface.address(), tl);
#else
				m_tracker_manager.queue_request(m_strand, m_half_open, req, login
					, m_listen_interface.address());
#endif
			}
		}

		// close the listen sockets
		m_listen_sockets.clear();

		ptime start(time_now());
		l.unlock();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " waiting for trackers to respond ("
			<< m_settings.stop_tracker_timeout << " seconds timeout)\n";
#endif

		while (time_now() - start < seconds(
			m_settings.stop_tracker_timeout)
			&& !m_tracker_manager.empty())
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << time_now_string() << " " << m_tracker_manager.num_requests()
				<< " tracker requests pending\n";
#endif
			tracker_timer.expires_from_now(milliseconds(100));
			tracker_timer.async_wait(m_strand.wrap(
				bind(&io_service::stop, &m_io_service)));

			m_io_service.reset();
			m_io_service.run();
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " tracker shutdown complete, locking mutex\n";
#endif

		l.lock();
		TORRENT_ASSERT(m_abort);
		m_abort = true;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " cleaning up connections\n";
#endif
		while (!m_connections.empty())
			m_connections.begin()->second->disconnect();

#ifndef NDEBUG
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end(); ++i)
		{
			TORRENT_ASSERT(i->second->num_peers() == 0);
		}
#endif

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " cleaning up torrents\n";
#endif
		m_torrents.clear();

		TORRENT_ASSERT(m_torrents.empty());
		TORRENT_ASSERT(m_connections.empty());
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
			TORRENT_ASSERT(p);
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
		boost::intrusive_ptr<torrent_info> ti
		, fs::path const& save_path
		, entry const& resume_data
		, storage_mode_t storage_mode
		, storage_constructor_type sc
		, bool paused
		, void* userdata)
	{
		TORRENT_ASSERT(!save_path.empty());

		if (ti->begin_files() == ti->end_files())
			throw std::runtime_error("no files in torrent");

		// lock the session and the checker thread (the order is important!)
		mutex_t::scoped_lock l(m_mutex);
		mutex::scoped_lock l2(m_checker_impl.m_mutex);

//		INVARIANT_CHECK;

		if (is_aborted())
			throw std::runtime_error("session is closing");
		
		// is the torrent already active?
		if (!find_torrent(ti->info_hash()).expired())
			throw duplicate_torrent();

		// is the torrent currently being checked?
		if (m_checker_impl.find_torrent(ti->info_hash()))
			throw duplicate_torrent();

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(*this, m_checker_impl, ti, save_path
				, m_listen_interface, storage_mode, 16 * 1024
				, sc, paused));
		torrent_ptr->start();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent_plugin> tp((*i)(torrent_ptr.get(), userdata));
			if (tp) torrent_ptr->add_extension(tp);
		}
#endif

		boost::shared_ptr<aux::piece_checker_data> d(
			new aux::piece_checker_data);
		d->torrent_ptr = torrent_ptr;
		d->save_path = save_path;
		d->info_hash = ti->info_hash();
		d->resume_data = resume_data;

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
		{
			torrent_info::nodes_t const& nodes = ti->nodes();
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

		return torrent_handle(this, &m_checker_impl, ti->info_hash());
	}

	torrent_handle session_impl::add_torrent(
		char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, fs::path const& save_path
		, entry const&
		, storage_mode_t storage_mode
		, storage_constructor_type sc
		, bool paused
		, void* userdata)
	{
	
		// TODO: support resume data in this case
		TORRENT_ASSERT(!save_path.empty());
		{
			// lock the checker_thread
			mutex::scoped_lock l(m_checker_impl.m_mutex);

			// is the torrent currently being checked?
			if (m_checker_impl.find_torrent(info_hash))
				throw duplicate_torrent();
		}

		// lock the session
		session_impl::mutex_t::scoped_lock l(m_mutex);

//		INVARIANT_CHECK;

		// is the torrent already active?
		if (!find_torrent(info_hash).expired())
			throw duplicate_torrent();

		// you cannot add new torrents to a session that is closing down
		TORRENT_ASSERT(!is_aborted());

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(*this, m_checker_impl, tracker_url, info_hash, name
			, save_path, m_listen_interface, storage_mode, 16 * 1024
			, sc, paused));
		torrent_ptr->start();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent_plugin> tp((*i)(torrent_ptr.get(), userdata));
			if (tp) torrent_ptr->add_extension(tp);
		}
#endif

		m_torrents.insert(
			std::make_pair(info_hash, torrent_ptr)).first;

		return torrent_handle(this, &m_checker_impl, info_hash);
	}

	void session_impl::remove_torrent(const torrent_handle& h, int options)
	{
		if (h.m_ses != this) return;
		TORRENT_ASSERT(h.m_chk == &m_checker_impl || h.m_chk == 0);
		TORRENT_ASSERT(h.m_ses != 0);

		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		session_impl::torrent_map::iterator i =
			m_torrents.find(h.m_info_hash);
		if (i != m_torrents.end())
		{
			torrent& t = *i->second;
			if (options & session::delete_files)
				t.delete_files();
			t.abort();

			if ((!t.is_paused() || t.should_request())
				&& !t.torrent_file().trackers().empty())
			{
				tracker_request req = t.generate_tracker_request();
				TORRENT_ASSERT(req.event == tracker_request::stopped);
				TORRENT_ASSERT(!m_listen_sockets.empty());
				req.listen_port = 0;
				if (!m_listen_sockets.empty())
					req.listen_port = m_listen_sockets.front().external_port;
				req.key = m_key;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				boost::shared_ptr<tracker_logger> tl(new tracker_logger(*this));
				m_tracker_loggers.push_back(tl);
				m_tracker_manager.queue_request(m_strand, m_half_open, req
					, t.tracker_login(), m_listen_interface.address(), tl);
#else
				m_tracker_manager.queue_request(m_strand, m_half_open, req
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
			TORRENT_ASSERT(m_torrents.find(i_hash) == m_torrents.end());
			return;
		}

		if (h.m_chk)
		{
			mutex::scoped_lock l(m_checker_impl.m_mutex);

			aux::piece_checker_data* d = m_checker_impl.find_torrent(h.m_info_hash);
			if (d != 0)
			{
				if (d->processing) d->abort = true;
				else m_checker_impl.remove_torrent(h.m_info_hash, options);
				return;
			}
		}
	}

	bool session_impl::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface)
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		tcp::endpoint new_interface;
		if (net_interface && std::strlen(net_interface) > 0)
			new_interface = tcp::endpoint(address::from_string(net_interface), port_range.first);
		else
			new_interface = tcp::endpoint(address_v4::any(), port_range.first);

		m_listen_port_retries = port_range.second - port_range.first;

		// if the interface is the same and the socket is open
		// don't do anything
		if (new_interface == m_listen_interface
			&& !m_listen_sockets.empty()) return true;

		m_listen_interface = new_interface;

		open_listen_port();

		bool new_listen_address = m_listen_interface.address() != new_interface.address();

#ifndef TORRENT_DISABLE_DHT
		if ((new_listen_address || m_dht_same_port) && m_dht)
		{
			if (m_dht_same_port)
				m_dht_settings.service_port = new_interface.port();
			// the listen interface changed, rebind the dht listen socket as well
			m_dht->rebind(new_interface.address()
				, m_dht_settings.service_port);
			if (m_natpmp.get())
				m_natpmp->set_mappings(0, m_dht_settings.service_port);
			if (m_upnp.get())
				m_upnp->set_mappings(0, m_dht_settings.service_port);
		}
#endif

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		m_logger = create_log("main_session", listen_port(), false);
		(*m_logger) << time_now_string() << "\n";
#endif

		return !m_listen_sockets.empty();
	}

	unsigned short session_impl::listen_port() const
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_listen_sockets.empty()) return 0;
		return m_listen_sockets.front().external_port;;
	}

	void session_impl::announce_lsd(sha1_hash const& ih)
	{
		mutex_t::scoped_lock l(m_mutex);
		// use internal listen port for local peers
		if (m_lsd.get())
			m_lsd->announce(ih, m_listen_interface.port());
	}

	void session_impl::on_lsd_peer(tcp::endpoint peer, sha1_hash const& ih)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = find_torrent(ih).lock();
		if (!t) return;
		// don't add peers from lsd to private torrents
		if (t->torrent_file().priv()) return;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string()
			<< ": added peer from local discovery: " << peer << "\n";
#endif
		t->get_policy().peer_from_tracker(peer, peer_id(0), peer_info::lsd, 0);
	}

	void session_impl::on_port_mapping(int tcp_port, int udp_port
		, std::string const& errmsg)
	{
#ifndef TORRENT_DISABLE_DHT
		if (udp_port != 0)
		{
			m_external_udp_port = udp_port;
			m_dht_settings.service_port = udp_port;
			if (m_alerts.should_post(alert::info))
			{
				std::stringstream msg;
				msg << "successfully mapped UDP port " << udp_port;
				m_alerts.post_alert(portmap_alert(msg.str()));
			}
		}
#endif

		if (tcp_port != 0)
		{
			if (!m_listen_sockets.empty())
				m_listen_sockets.front().external_port = tcp_port;
			if (m_alerts.should_post(alert::info))
			{
				std::stringstream msg;
				msg << "successfully mapped TCP port " << tcp_port;
				m_alerts.post_alert(portmap_alert(msg.str()));
			}
		}

		if (!errmsg.empty())
		{
			if (m_alerts.should_post(alert::warning))
			{
				std::stringstream msg;
				msg << "Error while mapping ports on NAT router: " << errmsg;
				m_alerts.post_alert(portmap_error_alert(msg.str()));
			}
		}
	}

	session_status session_impl::status() const
	{
		mutex_t::scoped_lock l(m_mutex);

//		INVARIANT_CHECK;

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
			s.dht_global_nodes = 0;
		}
#endif

		return s;
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::start_dht(entry const& startup_state)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (m_dht)
		{
			m_dht->stop();
			m_dht = 0;
		}
		if (m_dht_settings.service_port == 0
			|| m_dht_same_port)
		{
			m_dht_same_port = true;
			// if you hit this assert you are trying to start the
			// DHT with the same port as the tcp listen port
			// (which is default) _before_ you have opened the
			// tcp listen port (so there is no configured port to use)
			// basically, make sure you call listen_on() before
			// start_dht(). See documentation for listen_on() for
			// more information.
			TORRENT_ASSERT(m_listen_interface.port() > 0);
			m_dht_settings.service_port = m_listen_interface.port();
		}
		m_external_udp_port = m_dht_settings.service_port;
		if (m_natpmp.get())
			m_natpmp->set_mappings(0, m_dht_settings.service_port);
		if (m_upnp.get())
			m_upnp->set_mappings(0, m_dht_settings.service_port);
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
		// only change the dht listen port in case the settings
		// contains a vaiid port, and if it is different from
		// the current setting
		if (settings.service_port != 0)
			m_dht_same_port = false;
		else
			m_dht_same_port = true;
		if (!m_dht_same_port
			&& settings.service_port != m_dht_settings.service_port
			&& m_dht)
		{
			m_dht->rebind(m_listen_interface.address()
				, settings.service_port);
			if (m_natpmp.get())
				m_natpmp->set_mappings(0, m_dht_settings.service_port);
			if (m_upnp.get())
				m_upnp->set_mappings(0, m_dht_settings.service_port);
			m_external_udp_port = settings.service_port;
		}
		m_dht_settings = settings;
		if (m_dht_same_port)
			m_dht_settings.service_port = m_listen_interface.port();
	}

	entry session_impl::dht_state() const
	{
		mutex_t::scoped_lock l(m_mutex);
		if (!m_dht) return entry();
		return m_dht->state();
	}

	void session_impl::add_dht_node(std::pair<std::string, int> const& node)
	{
		TORRENT_ASSERT(m_dht);
		mutex_t::scoped_lock l(m_mutex);
		m_dht->add_node(node);
	}

	void session_impl::add_dht_router(std::pair<std::string, int> const& node)
	{
		TORRENT_ASSERT(m_dht);
		mutex_t::scoped_lock l(m_mutex);
		m_dht->add_router_node(node);
	}

#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
	void session_impl::set_pe_settings(pe_settings const& settings)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_pe_settings = settings;
	}
#endif

	bool session_impl::is_listening() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return !m_listen_sockets.empty();
	}

	session_impl::~session_impl()
	{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << "\n\n *** shutting down session *** \n\n";
#endif
		abort();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " waiting for main thread\n";
#endif
		m_thread->join();

		TORRENT_ASSERT(m_torrents.empty());

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

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " waiting for checker thread\n";
#endif
		m_checker_thread->join();

		TORRENT_ASSERT(m_torrents.empty());
		TORRENT_ASSERT(m_connections.empty());
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " shutdown complete!\n";
#endif
	}

	void session_impl::set_max_uploads(int limit)
	{
		TORRENT_ASSERT(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_max_uploads = limit;
	}

	void session_impl::set_max_connections(int limit)
	{
		TORRENT_ASSERT(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_max_connections = limit;
	}

	void session_impl::set_max_half_open_connections(int limit)
	{
		TORRENT_ASSERT(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_half_open.limit(limit);
	}

	void session_impl::set_download_rate_limit(int bytes_per_second)
	{
		TORRENT_ASSERT(bytes_per_second > 0 || bytes_per_second == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = bandwidth_limit::inf;
		m_bandwidth_manager[peer_connection::download_channel]->throttle(bytes_per_second);
	}

	void session_impl::set_upload_rate_limit(int bytes_per_second)
	{
		TORRENT_ASSERT(bytes_per_second > 0 || bytes_per_second == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = bandwidth_limit::inf;
		m_bandwidth_manager[peer_connection::upload_channel]->throttle(bytes_per_second);
	}

	std::auto_ptr<alert> session_impl::pop_alert()
	{
		mutex_t::scoped_lock l(m_mutex);

// too expensive
//		INVARIANT_CHECK;

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

		INVARIANT_CHECK;

		int ret = m_bandwidth_manager[peer_connection::upload_channel]->throttle();
		return ret == (std::numeric_limits<int>::max)() ? -1 : ret;
	}

	int session_impl::download_rate_limit() const
	{
		mutex_t::scoped_lock l(m_mutex);
		int ret = m_bandwidth_manager[peer_connection::download_channel]->throttle();
		return ret == (std::numeric_limits<int>::max)() ? -1 : ret;
	}

	void session_impl::start_lsd()
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		m_lsd = new lsd(m_io_service
			, m_listen_interface.address()
			, bind(&session_impl::on_lsd_peer, this, _1, _2));
	}
	
	void session_impl::start_natpmp()
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		m_natpmp = new natpmp(m_io_service
			, m_listen_interface.address()
			, bind(&session_impl::on_port_mapping
				, this, _1, _2, _3));

		m_natpmp->set_mappings(m_listen_interface.port(),
#ifndef TORRENT_DISABLE_DHT
			m_dht ? m_dht_settings.service_port : 
#endif
			0);
	}

	void session_impl::start_upnp()
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		m_upnp = new upnp(m_io_service, m_half_open
			, m_listen_interface.address()
			, m_settings.user_agent
			, bind(&session_impl::on_port_mapping
				, this, _1, _2, _3));

		m_upnp->set_mappings(m_listen_interface.port(), 
#ifndef TORRENT_DISABLE_DHT
			m_dht ? m_dht_settings.service_port : 
#endif
			0);
	}

	void session_impl::stop_lsd()
	{
		mutex_t::scoped_lock l(m_mutex);
		m_lsd = 0;
	}
	
	void session_impl::stop_natpmp()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_natpmp.get())
			m_natpmp->close();
		m_natpmp = 0;
	}
	
	void session_impl::stop_upnp()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_upnp.get())
			m_upnp->close();
		m_upnp = 0;
	}
	
	void session_impl::free_disk_buffer(char* buf)
	{
		m_disk_thread.free_buffer(buf);
	}
	
	std::pair<char*, int> session_impl::allocate_buffer(int size)
	{
		int num_buffers = (size + send_buffer_size - 1) / send_buffer_size;
#ifdef TORRENT_STATS
		m_buffer_allocations += num_buffers;
		m_buffer_usage_logger << log_time() << " protocol_buffer: "
			<< (m_buffer_allocations * send_buffer_size) << std::endl;
#endif
		return std::make_pair((char*)m_send_buffers.ordered_malloc(num_buffers)
			, num_buffers * send_buffer_size);
	}

	void session_impl::free_buffer(char* buf, int size)
	{
		TORRENT_ASSERT(size % send_buffer_size == 0);
		int num_buffers = size / send_buffer_size;
#ifdef TORRENT_STATS
		m_buffer_allocations -= num_buffers;
		TORRENT_ASSERT(m_buffer_allocations >= 0);
		m_buffer_usage_logger << log_time() << " protocol_buffer: "
			<< (m_buffer_allocations * send_buffer_size) << std::endl;
#endif
		m_send_buffers.ordered_free(buf, num_buffers);
	}	

#ifndef NDEBUG
	void session_impl::check_invariant() const
	{
		TORRENT_ASSERT(m_max_connections > 0);
		TORRENT_ASSERT(m_max_uploads > 0);
		int unchokes = 0;
		int num_optimistic = 0;
		for (connection_map::const_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			TORRENT_ASSERT(i->second);
			boost::shared_ptr<torrent> t = i->second->associated_torrent().lock();

			if (!i->second->is_choked()) ++unchokes;
			if (i->second->peer_info_struct()
				&& i->second->peer_info_struct()->optimistically_unchoked)
			{
				++num_optimistic;
				TORRENT_ASSERT(!i->second->is_choked());
			}
			if (t && i->second->peer_info_struct())
			{
				TORRENT_ASSERT(t->get_policy().has_connection(boost::get_pointer(i->second)));
			}
		}
		TORRENT_ASSERT(num_optimistic == 0 || num_optimistic == 1);
		if (m_num_unchoked != unchokes)
		{
			TORRENT_ASSERT(false);
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

			if (entry* peers_entry = rd.find_key("peers"))
			{
				entry::list_type& peer_list = peers_entry->list();

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

			if (entry* banned_peers_entry = rd.find_key("banned_peers"))
			{
				entry::list_type& peer_list = banned_peers_entry->list();

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

				banned_peers.swap(tmp_peers);
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
				int unfinished_size = int(unfinished.size());
				block_info.resize(num_blocks_per_piece * unfinished_size);
				tmp_unfinished.reserve(unfinished_size);
				int index = 0;
				for (entry::list_type::iterator i = unfinished.begin();
					i != unfinished.end(); ++i, ++index)
				{
					piece_picker::downloading_piece p;
					p.info = &block_info[index * num_blocks_per_piece];
					p.index = (int)(*i)["piece"].integer();
					if (p.index < 0 || p.index >= info.num_pieces())
					{
						error = "invalid piece index in unfinished piece list (index: "
							+ boost::lexical_cast<std::string>(p.index) + " size: "
							+ boost::lexical_cast<std::string>(info.num_pieces()) + ")";
						return;
					}

					const std::string& bitmask = (*i)["bitmask"].string();

					const int num_bitmask_bytes = (std::max)(num_blocks_per_piece / 8, 1);
					if ((int)bitmask.size() != num_bitmask_bytes)
					{
						error = "invalid size of bitmask (" + boost::lexical_cast<std::string>(bitmask.size()) + ")";
						return;
					}
					for (int j = 0; j < num_bitmask_bytes; ++j)
					{
						unsigned char bits = bitmask[j];
						int num_bits = (std::min)(num_blocks_per_piece - j*8, 8);
						for (int k = 0; k < num_bits; ++k)
						{
							const int bit = j * 8 + k;
							if (bits & (1 << k))
							{
								p.info[bit].state = piece_picker::block_info::state_finished;
								++p.finished;
							}
						}
					}

					if (p.finished == 0) continue;
	
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

					TORRENT_ASSERT(*slot_iter == p.index);
					int slot_index = static_cast<int>(slot_iter - tmp_pieces.begin());
					unsigned long adler
						= torrent_ptr->filesystem().piece_crc(
							slot_index
							, torrent_ptr->block_size()
							, p.info);

					const entry& ad = (*i)["adler32"];
	
					// crc's didn't match, don't use the resume data
					if (ad.integer() != entry::integer_type(adler))
					{
						error = "checksum mismatch on piece "
							+ boost::lexical_cast<std::string>(p.index);
						return;
					}

					tmp_unfinished.push_back(p);
				}
			}

			if (!torrent_ptr->verify_resume_data(rd, error))
				return;

			piece_map.swap(tmp_pieces);
			unfinished_pieces.swap(tmp_unfinished);
		}
		catch (invalid_encoding&)
		{
			return;
		}
		catch (type_error&)
		{
			return;
		}
		catch (file_error&)
		{
			return;
		}
	}
}}

