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

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isprint;
};
#endif

namespace
{


	// This struct is used by control_upload_rates() below. It keeps
	// track how much bandwidth has been allocated to each connection
	// and other relevant information to assist in the allocation process.
	struct connection_info
	{
		libtorrent::peer_connection* p; // which peer_connection this info refers to
		int allocated_quota;	// bandwidth allocated to this peer connection
		int quota_limit;	// bandwidth limit
		int estimated_upload_capacity; // estimated channel bandwidth

		bool operator < (const connection_info &other) const
		{
			return estimated_upload_capacity < other.estimated_upload_capacity;
		}
		
		int give(int amount)
		{

			// if amount > 0, try to add amount to the allocated quota.
			// if amount < 0, try to subtract abs(amount) from the allocated quota
			// 
			// Quota will not go above quota_limit or below 0. This means that
			// not all the amount given or taken may be accepted.
			//
			// return value: how much quota was actually added (or subtracted if negative).

			int old_quota=allocated_quota;
			allocated_quota+=amount;
			if(quota_limit!=-1)
				allocated_quota=std::min(allocated_quota,quota_limit);
			allocated_quota=std::max(0,allocated_quota);
			return allocated_quota-old_quota;
		}
	};

	// adjusts the upload rates of every peer connection
	// to make sure the sum of all send quotas equals
	// the given upload_limit. An upload limit of -1 means
	// unlimited upload rate, but the rates of each peer
	// has to be set anyway, since it depends on the download
	// rate from the peer.
	void control_upload_rates(
		int upload_limit
		, libtorrent::detail::session_impl::connection_map connections)
	{
		using namespace libtorrent;

		assert(upload_limit > 0 || upload_limit == -1);

		if (connections.empty()) return;


		if (upload_limit == -1)
		{
			for (detail::session_impl::connection_map::iterator i = connections.begin();
				i != connections.end();
				++i)
			{
				// there's no limit, set the quota to max
				// allowed
				peer_connection& p = *i->second;
				p.set_send_quota(p.send_quota_limit());
			}
			return;
		}
		else
		{
			// There's an upload limit, so we need to distribute the available
			// upload bandwidth among the peer_connections fairly, but not
			// wastefully.

			// For each peer_connection, keep some local data about their
			// quota limit and estimated upload capacity, and how much quota
			// has been allocated to them.

			std::vector<connection_info> peer_info;

			for (detail::session_impl::connection_map::iterator i = connections.begin();
				i != connections.end();
				++i)
			{
				peer_connection& p = *i->second;
				connection_info pi;

				pi.p = &p;
				pi.allocated_quota = 0; // we haven't given it any bandwith yet
				pi.quota_limit = p.send_quota_limit();

				pi.estimated_upload_capacity=
					p.has_data() ? std::max(10,(int)ceil(p.statistics().upload_rate()*1.1f))
					// If there's no data to send, upload capacity is practically 0.
					// Here we set it to 1 though, because otherwise it will not be able
					// to accept any quota at all, which may upset quota_limit balances.
					              : 1;

				peer_info.push_back(pi);
			}

			// Sum all peer_connections' quota limit to get the total quota limit.

			int sum_total_of_quota_limits=0;
			for (int i = 0; i < (int)peer_info.size(); ++i)
			{
				int quota_limit = peer_info[i].quota_limit;
				if (quota_limit == -1)
				{
					// quota_limit=-1 means infinite, so
					// sum_total_of_quota_limits will be infinite too...
					sum_total_of_quota_limits = std::numeric_limits<int>::max();
					break;
				}
				sum_total_of_quota_limits += quota_limit;
			}

			// This is how much total bandwidth that can be distributed.
			int quota_left_to_distribute = std::min(upload_limit,sum_total_of_quota_limits);


			// Sort w.r.t. channel capacitiy, lowest channel capacity first.
			// Makes it easy to traverse the list in sorted order.
			std::sort(peer_info.begin(),peer_info.end());


			// Distribute quota until there's nothing more to distribute

			while (quota_left_to_distribute != 0)
			{
				assert(quota_left_to_distribute > 0);

				for (int i = 0; i < (int)peer_info.size(); ++i)
				{
					// Traverse the peer list from slowest connection to fastest.

					// In each step, share bandwidth equally between this peer_connection
					// and the following faster peer_connections.
					//
					// Rounds upwards to avoid trying to give 0 bandwidth to someone
					// (may get caught in an endless loop otherwise)
					
					int num_peers_left_to_share_quota = (int)peer_info.size() - i;
					int try_to_give_to_this_peer
						= (quota_left_to_distribute + num_peers_left_to_share_quota - 1)
						/ num_peers_left_to_share_quota;

					// But do not allocate more than the estimated upload capacity.
					try_to_give_to_this_peer = std::min(
						peer_info[i].estimated_upload_capacity
						, try_to_give_to_this_peer);

					// Also, when the peer is given quota, it will
					// not accept more than it's quota_limit.
					int quota_actually_given_to_peer
						= peer_info[i].give(try_to_give_to_this_peer);

					quota_left_to_distribute -= quota_actually_given_to_peer;
				}
			}
			
			// Finally, inform the peers of how much quota they get.

			for(int i = 0; i < (int)peer_info.size(); ++i)
				peer_info[i].p->set_send_quota(peer_info[i].allocated_quota);
		}

#ifndef NDEBUG
		{
		int sum_quota = 0;
		int sum_quota_limit = 0;
		for (detail::session_impl::connection_map::iterator i = connections.begin();
			i != connections.end();
			++i)
		{
			peer_connection& p = *i->second;
			sum_quota += p.send_quota();

			if(p.send_quota_limit() == -1)
			{
				sum_quota_limit=std::numeric_limits<int>::max();
			}

			if(sum_quota_limit!=std::numeric_limits<int>::max())
			{
				sum_quota_limit += p.send_quota_limit();
			}
		}
		assert(sum_quota == std::min(upload_limit,sum_quota_limit));
		}
#endif
	}
}

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
					assert(t != 0);
					t->torrent_ptr->check_files(*t, m_mutex);
					// lock the session to add the new torrent

					boost::mutex::scoped_lock l(m_mutex);
					if (!t->abort)
					{
						boost::mutex::scoped_lock l(m_ses.m_mutex);

						m_ses.m_torrents.insert(
							std::make_pair(t->info_hash, t->torrent_ptr)).first;

						peer_id id;
						std::fill(id.begin(), id.end(), 0);
						for (std::vector<address>::const_iterator i = t->peers.begin();
							i != t->peers.end();
							++i)
						{
							t->torrent_ptr->get_policy().peer_from_tracker(*i, id);
						}
					}
				}
				catch(const std::exception& e)
				{
#ifndef NDEBUG
					std::cerr << "error while checking files: " << e.what() << "\n";
#endif
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

		session_impl::session_impl(std::pair<int, int> listen_port_range,
			const fingerprint& cl_fprint)
			: m_tracker_manager(m_settings)
			, m_listen_port_range(listen_port_range)
			, m_listen_port(listen_port_range.first)
			, m_abort(false)
			, m_upload_rate(-1)
			, m_incoming_connection(false)
		{
			assert(listen_port_range.first > 0);
			assert(listen_port_range.first < listen_port_range.second);
			assert(m_listen_port > 0);

			// ---- generate a peer id ----

			std::srand((unsigned int)std::time(0));

			std::string print = cl_fprint.to_string();
			assert(print.length() == 8);

			// the client's fingerprint
			std::copy(
				print.begin()
				, print.begin() + print.length()
				, m_peer_id.begin());

			// the random number
			for (unsigned char* i = m_peer_id.begin() + print.length();
				i != m_peer_id.end();
				++i)
			{
				*i = rand();
			}
		}

		void session_impl::purge_connections()
		{
			while (!m_disconnect_peer.empty())
			{
				m_connections.erase(m_disconnect_peer.back());
				m_disconnect_peer.pop_back();
				assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);
			}
		}

		void session_impl::operator()()
		{
			eh_initializer();
#ifndef NDEBUG
			m_logger = create_log("main session");

			try
			{
#endif
			// create listener socket
			boost::shared_ptr<socket> listener(new socket(socket::tcp, false));

			for(;;)
			{
				try
				{
					listener->listen(m_listen_port, 5);
				}
				catch (std::exception&)
				{
					m_listen_port++;
					if (m_listen_port > m_listen_port_range.second)
					{
						m_alerts.post_alert(listen_failed_alert(
							"none of the ports in the given range could be opened"));
						return;
					}
					continue;
				}
				break;
			}

#ifndef NDEBUG
			(*m_logger) << "listening on port: " << m_listen_port << "\n";
#endif
			m_selector.monitor_readability(listener);
			m_selector.monitor_errors(listener);

			std::vector<boost::shared_ptr<socket> > readable_clients;
			std::vector<boost::shared_ptr<socket> > writable_clients;
			std::vector<boost::shared_ptr<socket> > error_clients;
			boost::posix_time::ptime timer = boost::posix_time::second_clock::local_time();

#ifndef NDEBUG
			int loops_per_second = 0;
#endif
			for(;;)
			{

#ifndef NDEBUG
				check_invariant("loops_per_second++");
				loops_per_second++;
#endif


				// if nothing happens within 500000 microseconds (0.5 seconds)
				// do the loop anyway to check if anything else has changed
		//		 << "sleeping\n";
				m_selector.wait(500000, readable_clients, writable_clients, error_clients);

#ifndef NDEBUG
				for (std::vector<boost::shared_ptr<libtorrent::socket> >::iterator i =
					writable_clients.begin();
					i != writable_clients.end();
					++i)
				{
					assert((*i)->is_writable());
				}
#endif
				boost::mutex::scoped_lock l(m_mutex);

				// +1 for the listen socket
				assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);

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

#ifndef NDEBUG
				check_invariant("before SEND SOCKETS");
#endif

				// ************************
				// SEND SOCKETS
				// ************************

				// let the writable clients send data
				for (std::vector<boost::shared_ptr<socket> >::iterator i
					= writable_clients.begin();
					i != writable_clients.end();
					++i)
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
							assert(p->second->has_data());
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

							m_selector.remove(*i);
							m_connections.erase(p);
							assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);
							t->abort();
						}
						catch (std::exception& e)
						{
							// the connection wants to disconnect for some reason,
							// remove it from the connection-list
							if (m_alerts.should_post(alert::debug))
							{
								m_alerts.post_alert(
									peer_error_alert(p->first->sender(), e.what()));
							}

							m_selector.remove(*i);
							m_connections.erase(p);
							assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);
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
					i != readable_clients.end();
					++i)
				{
					// special case for listener socket
					if (*i == listener)
					{
						boost::shared_ptr<libtorrent::socket> s = (*i)->accept();
						s->set_blocking(false);
						if (s)
						{
							// we got a connection request!
							m_incoming_connection = true;
#ifndef NDEBUG
							(*m_logger) << s->sender().as_string() << " <== INCOMING CONNECTION\n";
#endif
							// TODO: filter ip:s

							boost::shared_ptr<peer_connection> c(
								new peer_connection(*this, m_selector, s));

							if (m_upload_rate != -1) c->set_send_quota(0);
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
//							(*m_logger) << "readable: " << p->first->sender().as_string() << "\n";
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

							m_selector.remove(*i);
							m_connections.erase(p);
							assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);
							t->abort();
						}
						catch (std::exception& e)
						{
							if (m_alerts.should_post(alert::debug))
							{
								m_alerts.post_alert(
									peer_error_alert(p->first->sender(), e.what()));
							}
							// the connection wants to disconnect for some reason, remove it
							// from the connection-list
							m_selector.remove(*i);
							m_connections.erase(p);
							assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);
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
					if (m_alerts.should_post(alert::debug))
					{
						m_alerts.post_alert(
							peer_error_alert(
								p->first->sender()
								, "socket received an exception"));
					}

					m_selector.remove(*i);
					// the connection may have been disconnected in the receive or send phase
					if (p != m_connections.end())
					{
						m_connections.erase(p);
						assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);
					}
				}

#ifndef NDEBUG
				check_invariant("after ERROR SOCKETS");
#endif

				boost::posix_time::time_duration d = boost::posix_time::second_clock::local_time() - timer;
				if (d.seconds() < 1) continue;
				timer = boost::posix_time::second_clock::local_time();

				// ************************
				// THE SECTION BELOW IS EXECUTED ONCE EVERY SECOND
				// ************************

#ifndef NDEBUG
				// std::cout << "\n\nloops: " << loops_per_second << "\n";
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
						m_selector.remove(j->first);
						m_connections.erase(j);
						assert(m_selector.count_read_monitors() == (int)m_connections.size() + 1);
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
						i->second->disconnect_all();
#ifndef NDEBUG
						sha1_hash i_hash = i->second->torrent_file().info_hash();
#endif
						std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator j = i;
						++i;
						m_torrents.erase(j);
						assert(m_torrents.find(i_hash) == m_torrents.end());
						continue;
					}
					else if (i->second->should_request())
					{
						m_tracker_manager.queue_request(
							i->second->generate_tracker_request(m_listen_port),
							boost::get_pointer(i->second));
					}

					i->second->second_tick();
					++i;
				}
				purge_connections();

				// distribute the maximum upload rate among the peers
				control_upload_rates(m_upload_rate, m_connections);


				m_tracker_manager.tick();
			}

			while (!m_tracker_manager.send_finished())
			{
				m_tracker_manager.tick();
				boost::xtime t;
				boost::xtime_get(&t, boost::TIME_UTC);
				t.nsec += 100000000;
				boost::thread::sleep(t);
			}

#ifndef NDEBUG
			}
			catch (std::bad_cast& e)
			{
				std::cerr << e.what() << "\n";
			}
			catch (std::exception& e)
			{
				std::cerr << e.what() << "\n";
			}
			catch (...)
			{
				std::cerr << "error!\n";
			}
#endif
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

#ifndef NDEBUG
		void session_impl::check_invariant(const char *place)
		{
			assert(place);

			for (connection_map::iterator i = m_connections.begin();
				i != m_connections.end();
				++i)
			{
				if (i->second->has_data() != m_selector.is_writability_monitored(i->first))
				{
					std::ofstream error_log("error.log", std::ios_base::app);
					boost::shared_ptr<peer_connection> p = i->second;
					error_log << "session_imple::check_invariant()\n"
						"peer_connection::has_data() != is_writability_monitored()\n";
					error_log << "peer_connection::has_data() " << p->has_data() << "\n";
					error_log << "peer_connection::send_quota_left " << p->send_quota_left() << "\n";
					error_log << "peer_connection::send_quota " << p->send_quota() << "\n";
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

	}

	session::session(std::pair<int, int> listen_port_range, const fingerprint& id)
		: m_impl(listen_port_range, id)
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

	session::session(std::pair<int, int> listen_port_range)
		: m_impl(listen_port_range, fingerprint("LT",0,0,1,0))
		, m_checker_impl(m_impl)
		, m_thread(boost::ref(m_impl))
		, m_checker_thread(boost::ref(m_checker_impl))
	{
		assert(listen_port_range.first > 0);
		assert(listen_port_range.first < listen_port_range.second);
#ifndef NDEBUG
		boost::function0<void> test = boost::ref(m_impl);
		assert(!test.empty());
#endif
	}

	// TODO: add a check to see if filenames are accepted on the
	// current platform.
	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session::add_torrent(
		const torrent_info& ti
		, const boost::filesystem::path& save_path
		, const entry& resume_data)
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
		boost::shared_ptr<torrent> torrent_ptr(
			new torrent(m_impl, ti, save_path));

		detail::piece_checker_data d;
		d.torrent_ptr = torrent_ptr;
		d.save_path = save_path;
		d.info_hash = ti.info_hash();
		d.parse_resume_data(resume_data, torrent_ptr->torrent_file());
		
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
		assert(h.m_chk == &m_checker_impl);

		{
			boost::mutex::scoped_lock l(m_impl.m_mutex);
			torrent* t = m_impl.find_torrent(h.m_info_hash);
			if (t != 0)
			{
				t->abort();
				return;
			}
		}

		{
			boost::mutex::scoped_lock l(m_checker_impl.m_mutex);

			detail::piece_checker_data* d = m_checker_impl.find_torrent(h.m_info_hash);
			if (d != 0)
			{
				d->abort = true;
				return;
			}
		}
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
		assert(bytes_per_second > 0 || bytes_per_second == -1);
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
		, const torrent_info& info)
	{
		// if we don't have any resume data, return
		if (resume_data.type() == entry::undefined_t) return;

		entry rd = resume_data;

		try
		{
			if (rd.dict()["file-format"].string() != "libtorrent resume file")
				return;

			if (rd.dict()["file-version"].integer() != 1)
				return;

			// verify info_hash
			const std::string &hash = rd.dict()["info-hash"].string();
			std::string real_hash((char*)info.info_hash().begin(), (char*)info.info_hash().end());
			if (hash != real_hash)
				return;

			// the peers

			entry::list_type& peer_list = rd.dict()["peers"].list();

			std::vector<address> tmp_peers;
			tmp_peers.reserve(peer_list.size());
			for (entry::list_type::iterator i = peer_list.begin();
				i != peer_list.end();
				++i)
			{
				address a(
					i->dict()["ip"].string()
					, (unsigned short)i->dict()["port"].integer());
				tmp_peers.push_back(a);
			}

			peers.swap(tmp_peers);



			// read piece map
			const entry::list_type& slots = rd.dict()["slots"].list();
			if ((int)slots.size() > info.num_pieces())
				return;

			std::vector<int> tmp_pieces;
			tmp_pieces.reserve(slots.size());
			for (entry::list_type::const_iterator i = slots.begin();
				i != slots.end();
				++i)
			{
				int index = (int)i->integer();
				if (index >= info.num_pieces() || index < -2)
					return;
				tmp_pieces.push_back(index);
			}


			int num_blocks_per_piece = (int)rd.dict()["blocks per piece"].integer();
			if (num_blocks_per_piece != info.piece_length() / torrent_ptr->block_size())
				return;

			// the unfinished pieces

			entry::list_type& unfinished = rd.dict()["unfinished"].list();

			std::vector<piece_picker::downloading_piece> tmp_unfinished;
			tmp_unfinished.reserve(unfinished.size());
			for (entry::list_type::iterator i = unfinished.begin();
				i != unfinished.end();
				++i)
			{
				piece_picker::downloading_piece p;

				p.index = (int)i->dict()["piece"].integer();
				if (p.index < 0 || p.index >= info.num_pieces())
					return;

				const std::string& bitmask = i->dict()["bitmask"].string();

				const int num_bitmask_bytes = std::max(num_blocks_per_piece / 8, 1);
				if ((int)bitmask.size() != num_bitmask_bytes) return;
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
					return;
				}

				assert(*slot_iter == p.index);
				int slot_index = static_cast<int>(slot_iter - tmp_pieces.begin());
				unsigned long adler
					= torrent_ptr->filesystem().piece_crc(
						slot_index
						, torrent_ptr->block_size()
						, p.finished_blocks);

				entry::dictionary_type::iterator ad = i->dict().find("adler32");
				if (ad != i->dict().end())
				{
					// crc's didn't match, don't use the resume data
					if (ad->second.integer() != adler)
						return;
				}

				tmp_unfinished.push_back(p);
			}

			// verify file sizes

			std::vector<size_type> file_sizes;
			entry::list_type& l = rd.dict()["file sizes"].list();

#if defined(_MSC_VER) && _MSC_VER < 1300
			for (entry::list_type::iterator i = l.begin();
				i != l.end();
				++i)
			{
				file_sizes.push_back(i->integer());
			}
#else
//			typedef entry::integer_type (entry::*mem_fun_type)() const;

			std::transform(
				l.begin()
				, l.end()
				, std::back_inserter(file_sizes)
				, boost::bind(&entry::integer, _1));
#endif
			if (!match_filesizes(info, save_path, file_sizes))
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
	}
}
