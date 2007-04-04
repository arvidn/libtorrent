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
#include <numeric>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_impl.hpp"

using namespace libtorrent;
using namespace boost::posix_time;
using boost::tuples::tuple;
using boost::tuples::get;
using boost::tuples::make_tuple;
using boost::filesystem::complete;
using boost::bind;
using boost::mutex;
using libtorrent::aux::session_impl;

// PROFILING CODE

#ifdef TORRENT_PROFILE
#include <boost/date_time/posix_time/ptime.hpp>

namespace libtorrent
{
	namespace
	{
		using boost::posix_time::ptime;
		using boost::posix_time::time_duration;
		using boost::posix_time::microsec_clock;
		std::vector<std::pair<ptime, std::string> > checkpoints;
	}

	void add_checkpoint(std::string const& str)
	{
		checkpoints.push_back(std::make_pair(microsec_clock::universal_time(), str));
	}

	void print_checkpoints()
	{
		for (std::vector<std::pair<ptime, std::string> >::iterator i
			= checkpoints.begin(); i != checkpoints.end(); ++i)
		{
			ptime cur = i->first;
			if (i + 1 != checkpoints.end())
			{
				time_duration diff = (i + 1)->first - cur;
				std::cout << diff.total_microseconds() << " " << i->second << "\n";
			}
			else
			{
				std::cout << "    " << i->second << "\n";
			}
		}
	}
}

#endif

namespace
{

	enum
	{
		// wait 60 seconds before retrying a failed tracker
		tracker_retry_delay_min = 60
		// when tracker_failed_max trackers
		// has failed, wait 10 minutes instead
		, tracker_retry_delay_max = 10 * 60
		, tracker_failed_max = 5
	};

	int calculate_block_size(const torrent_info& i, int default_block_size)
	{
		if (default_block_size < 1024) default_block_size = 1024;

		// if pieces are too small, adjust the block size
		if (i.piece_length() < default_block_size)
		{
			return static_cast<int>(i.piece_length());
		}

		// if pieces are too large, adjust the block size
		if (i.piece_length() / default_block_size > piece_picker::max_blocks_per_piece)
		{
			return static_cast<int>(i.piece_length() / piece_picker::max_blocks_per_piece);
		}

		// otherwise, go with the default
		return default_block_size;
	}

	struct find_peer_by_ip
	{
		find_peer_by_ip(tcp::endpoint const& a, const torrent* t)
			: ip(a)
			, tor(t)
		{ assert(t != 0); }
		
		bool operator()(const session_impl::connection_map::value_type& c) const
		{
			tcp::endpoint sender = c.first->remote_endpoint();
			if (sender.address() != ip.address()) return false;
			if (tor != c.second->associated_torrent().lock().get()) return false;
			return true;
		}

		tcp::endpoint const& ip;
		torrent const* tor;
	};

	struct peer_by_id
	{
		peer_by_id(const peer_id& i): pid(i) {}
		
		bool operator()(const std::pair<tcp::endpoint, peer_connection*>& p) const
		{
			if (p.second->pid() != pid) return false;
			// have a special case for all zeros. We can have any number
			// of peers with that pid, since it's used to indicate no pid.
			if (std::count(pid.begin(), pid.end(), 0) == 20) return false;
			return true;
		}

		peer_id const& pid;
	};
	
#ifdef TORRENT_LOGGING
	void print_legend(boost::shared_ptr<logger> l)
	{
		(*l) << "1. time, seconds\n"
			<< "2. hard send quota, bytes\n"
			<< "3. soft send quota, bytes\n"
			<< "4. excess bytes sent\n"
			<< "5. excess bytes sent last time slice\n"
			<< "6. hard receive quota, bytes\n"
			<< "7. soft receive quota, bytes\n"
			<< "8. excess bytes received\n"
			<< "9. excess bytes received last time slice\n"
			<< "10. num peers\n"
			<< "11. max ul quota limit\n"
			<< "12. max dl quota limit\n"
			<< "13. bytes sent\n"
			<< "14. bytes sent 10 seconds mean\n"
			<< "15. bytes received\n"
			<< "16. bytes received 10 seconds mean\n"
			<< "17. total payload download\n"
			<< "18. total web seed payload download\n"
			<< "19. total redundant bytes downloaded\n"
			<< "\n";
	}
#endif

}

namespace libtorrent
{
	torrent::torrent(
		session_impl& ses
		, aux::checker_impl& checker
		, torrent_info const& tf
		, boost::filesystem::path const& save_path
		, tcp::endpoint const& net_interface
		, bool compact_mode
		, int block_size
		, session_settings const& s)
		: m_torrent_file(tf)
		, m_abort(false)
		, m_paused(false)
		, m_just_paused(false)
		, m_event(tracker_request::started)
		, m_block_size(0)
		, m_storage(0)
		, m_next_request(second_clock::universal_time())
		, m_duration(1800)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_host_resolver(ses.m_io_service)
		, m_resolving_country(false)
		, m_resolve_countries(false)
#ifndef TORRENT_DISABLE_DHT
		, m_dht_announce_timer(ses.m_io_service)
#endif
		, m_policy()
		, m_ses(ses)
		, m_checker(checker)
		, m_picker(0)
		, m_trackers(m_torrent_file.trackers())
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_num_pieces(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
		, m_default_block_size(block_size)
		, m_connections_initialized(true)
		, m_settings(s)
	{
#ifndef NDEBUG
		m_initial_done = 0;
#endif
#ifdef TORRENT_LOGGING
		m_log = ses.create_log("torrent_"
			+ boost::lexical_cast<std::string>(tf.info_hash())
			, m_ses.listen_port(), false);
		print_legend(m_log);
		m_second_count = 0;
		std::fill_n(m_ul_history, 10, 0);
		std::fill_n(m_dl_history, 10, 0);

		m_peer_log = ses.create_log("torrent_peers_"
			+ boost::lexical_cast<std::string>(tf.info_hash())
			, m_ses.listen_port(), false);

#endif
		INVARIANT_CHECK;

		m_uploads_quota.min = 2;
		m_connections_quota.min = 2;
		// this will be corrected the next time the main session
		// distributes resources, i.e. on average in 0.5 seconds
		m_connections_quota.given = 100;
		m_uploads_quota.max = std::numeric_limits<int>::max();
		m_connections_quota.max = std::numeric_limits<int>::max();
		m_policy.reset(new policy(this));
		init();
	
#ifndef TORRENT_DISABLE_DHT
		if (should_announce_dht())
		{
			m_dht_announce_timer.expires_from_now(seconds(10));
			m_dht_announce_timer.async_wait(m_ses.m_strand.wrap(
				bind(&torrent::on_dht_announce, this, _1)));
		}
#endif
	}

	torrent::torrent(
		session_impl& ses
		, aux::checker_impl& checker
		, char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, boost::filesystem::path const& save_path
		, tcp::endpoint const& net_interface
		, bool compact_mode
		, int block_size
		, session_settings const& s)
		: m_torrent_file(info_hash)
		, m_abort(false)
		, m_paused(false)
		, m_just_paused(false)
		, m_event(tracker_request::started)
		, m_block_size(0)
		, m_storage(0)
		, m_next_request(second_clock::universal_time())
		, m_duration(1800)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_host_resolver(ses.m_io_service)
		, m_resolving_country(false)
		, m_resolve_countries(false)
#ifndef TORRENT_DISABLE_DHT
		, m_dht_announce_timer(ses.m_io_service)
#endif
		, m_policy()
		, m_ses(ses)
		, m_checker(checker)
		, m_picker(0)
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_num_pieces(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
		, m_default_block_size(block_size)
		, m_connections_initialized(false)
		, m_settings(s)
	{
#ifndef NDEBUG
		m_initial_done = 0;
#endif

#ifdef TORRENT_LOGGING
		m_log = ses.create_log("torrent_"
			+ boost::lexical_cast<std::string>(info_hash)
			, m_ses.listen_port(), true);
		print_legend(m_log);
		m_second_count = 0;
		std::fill_n(m_ul_history, 10, 0);
		std::fill_n(m_dl_history, 10, 0);
#endif

		INVARIANT_CHECK;

		if (name) m_name.reset(new std::string(name));

		m_uploads_quota.min = 2;
		m_connections_quota.min = 2;
		// this will be corrected the next time the main session
		// distributes resources, i.e. on average in 0.5 seconds
		m_connections_quota.given = 100;
		m_uploads_quota.max = std::numeric_limits<int>::max();
		m_connections_quota.max = std::numeric_limits<int>::max();
		if (tracker_url)
		{
			m_trackers.push_back(announce_entry(tracker_url));
			m_torrent_file.add_tracker(tracker_url);
		}

		m_policy.reset(new policy(this));
#ifndef TORRENT_DISABLE_DHT
		if (should_announce_dht())
		{
			m_dht_announce_timer.expires_from_now(seconds(10));
			m_dht_announce_timer.async_wait(m_ses.m_strand.wrap(
				bind(&torrent::on_dht_announce, this, _1)));
		}
#endif
	}

#ifndef TORRENT_DISABLE_DHT
	bool torrent::should_announce_dht() const
	{
		// don't announce private torrents
		if (m_torrent_file.is_valid() && m_torrent_file.priv()) return false;
	
		if (m_trackers.empty()) return true;
			
		return m_failed_trackers > 0 || !m_ses.settings().use_dht_as_fallback;
	}
#endif

	torrent::~torrent()
	{
		// The invariant can't be maintained here, since the torrent
		// is being destructed, all weak references to it have been
		// reset, which means that all its peers already have an
		// invalidated torrent pointer (so it cannot be verified to be correct)
		
		// i.e. the invariant can only be maintained if all connections have
		// been closed by the time the torrent is destructed. And they are
		// supposed to be closed. So we can still do the invariant check.

		assert(m_connections.empty());
		
		INVARIANT_CHECK;

		if (m_ses.is_aborted())
			m_abort = true;
		if (!m_connections.empty())
			disconnect_all();
	}

	std::string torrent::name() const
	{
		if (valid_metadata()) return m_torrent_file.name();
		if (m_name) return *m_name;
		return "";
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void torrent::add_extension(boost::shared_ptr<torrent_plugin> ext)
	{
		m_extensions.push_back(ext);
	}
#endif

	void torrent::init()
	{
		INVARIANT_CHECK;

		assert(m_torrent_file.is_valid());
		assert(m_torrent_file.num_files() > 0);
		assert(m_torrent_file.total_size() >= 0);

		m_have_pieces.resize(m_torrent_file.num_pieces(), false);
		m_storage.reset(new piece_manager(m_torrent_file, m_save_path, m_ses.m_files));
		m_block_size = calculate_block_size(m_torrent_file, m_default_block_size);
		m_picker.reset(new piece_picker(
			static_cast<int>(m_torrent_file.piece_length() / m_block_size)
			, static_cast<int>((m_torrent_file.total_size()+m_block_size-1)/m_block_size)));

		std::vector<std::string> const& url_seeds = m_torrent_file.url_seeds();
		std::copy(url_seeds.begin(), url_seeds.end(), std::inserter(m_web_seeds
			, m_web_seeds.begin()));
	}

	void torrent::use_interface(const char* net_interface)
	{
		INVARIANT_CHECK;

		m_net_interface = tcp::endpoint(address::from_string(net_interface), 0);
	}

#ifndef TORRENT_DISABLE_DHT

	void torrent::on_dht_announce_response_disp(boost::weak_ptr<libtorrent::torrent> t
		, std::vector<tcp::endpoint> const& peers)
	{
		boost::shared_ptr<libtorrent::torrent> tor = t.lock();
		if (!tor) return;
		tor->on_dht_announce_response(peers);
	}

	void torrent::on_dht_announce(asio::error_code const& e)
	{
		if (e) return;
		if (should_announce_dht())
		{
			m_dht_announce_timer.expires_from_now(boost::posix_time::minutes(30));
			m_dht_announce_timer.async_wait(m_ses.m_strand.wrap(
				bind(&torrent::on_dht_announce, this, _1)));
		}
		if (!m_ses.m_dht) return;
		// TODO: There should be a way to abort an announce operation on the dht.
		// when the torrent is destructed
		boost::weak_ptr<torrent> self(shared_from_this());
		m_ses.m_dht->announce(m_torrent_file.info_hash()
			, m_ses.m_listen_interface.port()
			, m_ses.m_strand.wrap(bind(&torrent::on_dht_announce_response_disp, self, _1)));
	}

	void torrent::on_dht_announce_response(std::vector<tcp::endpoint> const& peers)
	{
		if (peers.empty()) return;

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), peers.size(), "Got peers from DHT"));
		}
		std::for_each(peers.begin(), peers.end(), bind(
			&policy::peer_from_tracker, boost::ref(m_policy), _1, peer_id(0)));
	}

#endif

	// returns true if it is time for this torrent to make another
	// tracker request
	bool torrent::should_request()
	{
		INVARIANT_CHECK;
		
		if (m_torrent_file.trackers().empty()) return false;

		if (m_just_paused)
		{
			m_just_paused = false;
			return true;
		}
		return !m_paused &&
			m_next_request < second_clock::universal_time();
	}

	void torrent::tracker_warning(std::string const& msg)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_ses.m_alerts.should_post(alert::warning))
		{
			m_ses.m_alerts.post_alert(tracker_warning_alert(get_handle(), msg));
		}
	}
	
	void torrent::tracker_response(
		tracker_request const&
		, std::vector<peer_entry>& peer_list
		, int interval
		, int complete
		, int incomplete)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		m_failed_trackers = 0;
		// announce intervals less than 5 minutes
		// are insane.
		if (interval < 60 * 5) interval = 60 * 5;

		m_last_working_tracker
			= prioritize_tracker(m_currently_trying_tracker);
		m_currently_trying_tracker = 0;

		m_duration = interval;
		m_next_request = second_clock::universal_time() + boost::posix_time::seconds(m_duration);

		if (complete >= 0) m_complete = complete;
		if (incomplete >= 0) m_incomplete = incomplete;

		// connect to random peers from the list
		std::random_shuffle(peer_list.begin(), peer_list.end());

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		std::stringstream s;
		s << "TRACKER RESPONSE:\n"
			"interval: " << m_duration << "\n"
			"peers:\n";
		for (std::vector<peer_entry>::const_iterator i = peer_list.begin();
			i != peer_list.end(); ++i)
		{
			s << "  " << std::setfill(' ') << std::setw(16) << i->ip
				<< " " << std::setw(5) << std::dec << i->port << "  ";
			if (!i->pid.is_all_zeros()) s << " " << i->pid << " " << identify_client(i->pid);
			s << "\n";
		}
		debug_log(s.str());
#endif
		// for each of the peers we got from the tracker
		for (std::vector<peer_entry>::iterator i = peer_list.begin();
			i != peer_list.end(); ++i)
		{
			// don't make connections to ourself
			if (i->pid == m_ses.get_peer_id())
				continue;

			try
			{
				tcp::endpoint a(address::from_string(i->ip), i->port);

				if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
				{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
					debug_log("blocked ip from tracker: " + i->ip);
#endif
					continue;
				}
			
				m_policy->peer_from_tracker(a, i->pid);
			}
			catch (std::exception&)
			{
				// assume this is because we got a hostname instead of
				// an ip address from the tracker

				tcp::resolver::query q(i->ip, boost::lexical_cast<std::string>(i->port));
				m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
					bind(&torrent::on_peer_name_lookup, shared_from_this(), _1, _2, i->pid)));
			}	
		}

		if (m_ses.m_alerts.should_post(alert::info))
		{
			std::stringstream s;
			s << "Got response from tracker: "
				<< m_trackers[m_last_working_tracker].url;
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), peer_list.size(), s.str()));
		}
		m_got_tracker_response = true;
	}

	void torrent::on_peer_name_lookup(asio::error_code const& e, tcp::resolver::iterator host
		, peer_id pid) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (e || host == tcp::resolver::iterator() ||
			m_ses.is_aborted()) return;

		if (m_ses.m_ip_filter.access(host->endpoint().address()) & ip_filter::blocked)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			debug_log("blocked ip from tracker: " + host->endpoint().address().to_string());
#endif
			return;
		}
			
		m_policy->peer_from_tracker(*host, pid);
	}
	catch (std::exception&)
	{};

	size_type torrent::bytes_left() const
	{
		// if we don't have the metadata yet, we
		// cannot tell how big the torrent is.
		if (!valid_metadata()) return -1;
		return m_torrent_file.total_size()
			- quantized_bytes_done();
	}

	size_type torrent::quantized_bytes_done() const
	{
//		INVARIANT_CHECK;

		if (!valid_metadata()) return 0;

		if (m_torrent_file.num_pieces() == 0)
			return 0;

		if (is_seed()) return m_torrent_file.total_size();

		const int last_piece = m_torrent_file.num_pieces() - 1;

		size_type total_done
			= m_num_pieces * m_torrent_file.piece_length();

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_have_pieces[last_piece])
		{
			int corr = m_torrent_file.piece_size(last_piece)
				- m_torrent_file.piece_length();
			total_done += corr;
		}
		return total_done;
	}

	// the first value is the total number of bytes downloaded
	// the second value is the number of bytes of those that haven't
	// been filtered as not wanted we have downloaded
	tuple<size_type, size_type> torrent::bytes_done() const
	{
		INVARIANT_CHECK;

		if (!valid_metadata()) return tuple<size_type, size_type>(0,0);

		if (m_torrent_file.num_pieces() == 0)
			return tuple<size_type, size_type>(0,0);
		const int last_piece = m_torrent_file.num_pieces() - 1;

		if (is_seed())
			return make_tuple(m_torrent_file.total_size()
				, m_torrent_file.total_size());

		size_type wanted_done = (m_num_pieces - m_picker->num_have_filtered())
			* m_torrent_file.piece_length();
		
		size_type total_done
			= m_num_pieces * m_torrent_file.piece_length();
		assert(m_num_pieces < m_torrent_file.num_pieces());

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_have_pieces[last_piece])
		{
			int corr = m_torrent_file.piece_size(last_piece)
				- m_torrent_file.piece_length();
			total_done += corr;
			if (!m_picker->is_filtered(last_piece))
				wanted_done += corr;
		}

		assert(total_done <= m_torrent_file.total_size());
		assert(wanted_done <= m_torrent_file.total_size());

		const std::vector<piece_picker::downloading_piece>& dl_queue
			= m_picker->get_download_queue();

		const int blocks_per_piece = static_cast<int>(
			m_torrent_file.piece_length() / m_block_size);

		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			int corr = 0;
			int index = i->index;
			assert(!m_have_pieces[index]);
			assert(int(i->finished_blocks.count())
				< m_picker->blocks_in_piece(index));

#ifndef NDEBUG
			for (std::vector<piece_picker::downloading_piece>::const_iterator j = boost::next(i);
				j != dl_queue.end(); ++j)
			{
				assert(j->index != index);
			}
#endif

			for (int j = 0; j < blocks_per_piece; ++j)
			{
				assert(i->finished_blocks[j] == 0 || i->finished_blocks[j] == 1);
				assert(m_picker->is_finished(piece_block(index, j)) == i->finished_blocks[j]);
				corr += i->finished_blocks[j] * m_block_size;
				assert(index != last_piece || j < m_picker->blocks_in_last_piece()
					|| i->finished_blocks[j] == 0);
			}

			// correction if this was the last piece
			// and if we have the last block
			if (i->index == last_piece
				&& i->finished_blocks[m_picker->blocks_in_last_piece()-1])
			{
				corr -= m_block_size;
				corr += m_torrent_file.piece_size(last_piece) % m_block_size;
			}
			total_done += corr;
			if (!m_picker->is_filtered(index))
				wanted_done += corr;
		}

		assert(total_done < m_torrent_file.total_size());
		assert(wanted_done < m_torrent_file.total_size());

		std::map<piece_block, int> downloading_piece;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			peer_connection* pc = i->second;
			boost::optional<piece_block_progress> p
				= pc->downloading_piece_progress();
			if (p)
			{
				if (m_have_pieces[p->piece_index])
					continue;

				piece_block block(p->piece_index, p->block_index);
				if (m_picker->is_finished(block))
					continue;

				std::map<piece_block, int>::iterator dp
					= downloading_piece.find(block);
				if (dp != downloading_piece.end())
				{
					if (dp->second < p->bytes_downloaded)
						dp->second = p->bytes_downloaded;
				}
				else
				{
					downloading_piece[block] = p->bytes_downloaded;
				}
#ifndef NDEBUG
				assert(p->bytes_downloaded <= p->full_block_bytes);
				int last_piece = m_torrent_file.num_pieces() - 1;
				if (p->piece_index == last_piece
					&& p->block_index == m_torrent_file.piece_size(last_piece) / block_size())
					assert(p->full_block_bytes == m_torrent_file.piece_size(last_piece) % block_size());
				else
					assert(p->full_block_bytes == block_size());
#endif
			}
		}
		for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
			i != downloading_piece.end(); ++i)
		{
			total_done += i->second;
			if (!m_picker->is_filtered(i->first.piece_index))
				wanted_done += i->second;
		}

#ifndef NDEBUG

		if (total_done >= m_torrent_file.total_size())
		{
			std::copy(m_have_pieces.begin(), m_have_pieces.end()
				, std::ostream_iterator<bool>(std::cerr, " "));
			std::cerr << std::endl;
			std::cerr << "num_pieces: " << m_num_pieces << std::endl;
			
			std::cerr << "unfinished:" << std::endl;
			
			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dl_queue.begin(); i != dl_queue.end(); ++i)
			{
				std::cerr << "   " << i->index << " ";
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					std::cerr << i->finished_blocks[j];
				}
				std::cerr << std::endl;
			}
			
			std::cerr << "downloading pieces:" << std::endl;

			for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
				i != downloading_piece.end(); ++i)
			{
				std::cerr << "   " << i->first.piece_index << ":" << i->first.block_index
					<< "  " << i->second << std::endl;
			}

		}

		assert(total_done < m_torrent_file.total_size());
		assert(wanted_done < m_torrent_file.total_size());

#endif

		assert(total_done >= wanted_done);
		return make_tuple(total_done, wanted_done);
	}

	void torrent::piece_failed(int index)
	{
		// if the last piece fails the peer connection will still
		// think that it has received all of it until this function
		// resets the download queue. So, we cannot do the
		// invariant check here since it assumes:
		// (total_done == m_torrent_file.total_size()) => is_seed()
//		INVARIANT_CHECK;

		assert(m_storage.get());
		assert(m_picker.get());
		assert(index >= 0);
	  	assert(index < m_torrent_file.num_pieces());

		if (m_ses.m_alerts.should_post(alert::info))
		{
			std::stringstream s;
			s << "hash for piece " << index << " failed";
			m_ses.m_alerts.post_alert(hash_failed_alert(get_handle(), index, s.str()));
		}
		// increase the total amount of failed bytes
		m_total_failed_bytes += m_torrent_file.piece_size(index);

		std::vector<tcp::endpoint> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<tcp::endpoint> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { (*i)->on_piece_failed(index); } catch (std::exception&) {}
		}
#endif

		for (std::set<tcp::endpoint>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_iterator p = m_connections.find(*i);
			if (p == m_connections.end()) continue;
			p->second->received_invalid_data(index);

			// either, we have received too many failed hashes
			// or this was the only peer that sent us this piece.
			// TODO: make this a changable setting
			if (p->second->trust_points() <= -7 || peers.size() == 1)
			{
				// we don't trust this peer anymore
				// ban it.
				if (m_ses.m_alerts.should_post(alert::info))
				{
					m_ses.m_alerts.post_alert(peer_ban_alert(
						p->first
						, get_handle()
						, "banning peer because of too many corrupt pieces"));
				}
				m_policy->ban_peer(*p->second);

#if defined(TORRENT_VERBOSE_LOGGING)
				(*p->second->m_logger) << "*** BANNING PEER 'too many corrupt pieces'\n";
#endif
				p->second->disconnect();
			}
		}

		// we have to let the piece_picker know that
		// this piece failed the check as it can restore it
		// and mark it as being interesting for download
		// TODO: do this more intelligently! and keep track
		// of how much crap (data that failed hash-check) and
		// how much redundant data we have downloaded
		// if some clients has sent more than one piece
		// start with redownloading the pieces that the client
		// that has sent the least number of pieces
		m_picker->restore_piece(index);
		m_storage->mark_failed(index);

		assert(m_have_pieces[index] == false);
	}

	void torrent::abort()
	{
		INVARIANT_CHECK;

		m_abort = true;
		// if the torrent is paused, it doesn't need
		// to announce with even=stopped again.
		if (!m_paused)
			m_event = tracker_request::stopped;
		// disconnect all peers and close all
		// files belonging to the torrents
		disconnect_all();
		if (m_storage.get()) m_storage->release_files();
	}

	void torrent::announce_piece(int index)
	{
//		INVARIANT_CHECK;

		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		std::vector<tcp::endpoint> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<tcp::endpoint> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		if (!m_have_pieces[index])
			m_num_pieces++;
		m_have_pieces[index] = true;

		assert(std::accumulate(m_have_pieces.begin(), m_have_pieces.end(), 0)
			== m_num_pieces);

		m_picker->we_have(index);
		for (peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
			try { i->second->announce_piece(index); } catch (std::exception&) {}

		for (std::set<tcp::endpoint>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_iterator p = m_connections.find(*i);
			if (p == m_connections.end()) continue;
			p->second->received_valid_data(index);
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { (*i)->on_piece_pass(index); } catch (std::exception&) {}
		}
#endif
		if (is_seed()) m_picker.reset();
	}

	std::string torrent::tracker_login() const
	{
		if (m_username.empty() && m_password.empty()) return "";
		return m_username + ":" + m_password;
	}

	void torrent::filter_piece(int index, bool filter)
	{
		INVARIANT_CHECK;

		assert(valid_metadata());
		if (is_seed()) return;

		// this call is only valid on torrents with metadata
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		// TODO: update peer's interesting-bit
		
		if (filter) m_picker->mark_as_filtered(index);
		else m_picker->mark_as_unfiltered(index);
	}

	void torrent::filter_pieces(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed()) return;

		assert(m_picker.get());

		// TODO: update peer's interesting-bit
		
		std::vector<int> state;
		state.reserve(100);
		int index = 0;
		for (std::vector<bool>::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
		{
			if (m_picker->is_filtered(index) == *i) continue;
			if (*i)
				m_picker->mark_as_filtered(index);
			else
				state.push_back(index);
		}

		for (std::vector<int>::reverse_iterator i = state.rbegin();
			i != state.rend(); ++i)
		{
			m_picker->mark_as_unfiltered(*i);
		}
	}

	bool torrent::is_piece_filtered(int index) const
	{
		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed()) return false;
		
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		return m_picker->is_filtered(index);
	}

	void torrent::filtered_pieces(std::vector<bool>& bitmask) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed())
		{
			bitmask.clear();
			bitmask.resize(m_torrent_file.num_pieces(), false);
			return;
		}

		assert(m_picker.get());
		m_picker->filtered_pieces(bitmask);
	}

	void torrent::filter_files(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		// the bitmask need to have exactly one bit for every file
		// in the torrent
		assert((int)bitmask.size() == m_torrent_file.num_files());
		
		size_type position = 0;

		if (m_torrent_file.num_pieces())
		{
			int piece_length = m_torrent_file.piece_length();
			// mark all pieces as filtered, then clear the bits for files
			// that should be downloaded
			std::vector<bool> piece_filter(m_torrent_file.num_pieces(), true);
			for (int i = 0; i < (int)bitmask.size(); ++i)
			{
				size_type start = position;
				position += m_torrent_file.file_at(i).size;
				// is the file selected for download?
				if (!bitmask[i])
				{           
					// mark all pieces of the file as downloadable
					int start_piece = int(start / piece_length);
					int last_piece = int(position / piece_length);
					// if one piece spans several files, we might
					// come here several times with the same start_piece, end_piece
					std::fill(piece_filter.begin() + start_piece, piece_filter.begin()
						+ last_piece + 1, false);
				}
			}
			filter_pieces(piece_filter);
		}
	}

	void torrent::replace_trackers(std::vector<announce_entry> const& urls)
	{
		assert(!urls.empty());
		m_trackers = urls;
		if (m_currently_trying_tracker >= (int)m_trackers.size())
			m_currently_trying_tracker = (int)m_trackers.size()-1;
		m_last_working_tracker = -1;
	}

	tracker_request torrent::generate_tracker_request()
	{
		INVARIANT_CHECK;

		assert(!m_trackers.empty());

		m_next_request
			= second_clock::universal_time()
			+ boost::posix_time::seconds(tracker_retry_delay_max);

		tracker_request req;
		req.info_hash = m_torrent_file.info_hash();
		req.pid = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download();
		req.web_downloaded = m_web_stat.total_payload_download();
		req.uploaded = m_stat.total_payload_upload();
		req.left = bytes_left();
		if (req.left == -1) req.left = 16*1024;
		req.event = m_event;

		if (m_event != tracker_request::stopped)
			m_event = tracker_request::none;
		req.url = m_trackers[m_currently_trying_tracker].url;
		req.num_want = 50;
		// if we are aborting. we don't want any new peers
		if (req.event == tracker_request::stopped)
			req.num_want = 0;

		// default initialize, these should be set by caller
		// before passing the request to the tracker_manager
		req.listen_port = 0;
		req.key = 0;

		return req;
	}

	void torrent::remove_peer(peer_connection* p) try
	{
		INVARIANT_CHECK;

		assert(p != 0);

		peer_iterator i = m_connections.find(p->remote());
		if (i == m_connections.end()) return;

		if (ready_for_connections())
		{
			assert(p->associated_torrent().lock().get() == this);

			std::vector<int> piece_list;
			const std::vector<bool>& pieces = p->get_bitfield();

			for (std::vector<bool>::const_iterator i = pieces.begin();
				i != pieces.end(); ++i)
			{
				if (*i) piece_list.push_back(static_cast<int>(i - pieces.begin()));
			}

			for (std::vector<int>::reverse_iterator i = piece_list.rbegin();
				i != piece_list.rend(); ++i)
			{
				peer_lost(*i);
			}
		}

		m_policy->connection_closed(*p);
		m_connections.erase(i);
#ifndef NDEBUG
		m_policy->check_invariant();
#endif
	}
	catch (std::exception& e)
	{
#ifndef NDEBUG
		std::string err = e.what();
#endif
		assert(false);
	};

	void torrent::connect_to_url_seed(std::string const& url)
	{
		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		std::string now(to_simple_string(second_clock::universal_time()));
		(*m_ses.m_logger) << now << " resolving: " << url << "\n";
#endif

		m_resolving_web_seeds.insert(url);
		if (m_ses.settings().proxy_ip.empty())
		{
			std::string protocol;
			std::string hostname;
			int port;
			std::string path;
			boost::tie(protocol, hostname, port, path)
				= parse_url_components(url);

			tcp::resolver::query q(hostname, boost::lexical_cast<std::string>(port));
			m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
				bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, url
					, tcp::endpoint())));
		}
		else
		{
			// use proxy
			tcp::resolver::query q(m_ses.settings().proxy_ip
				, boost::lexical_cast<std::string>(m_ses.settings().proxy_port));
			m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
				bind(&torrent::on_proxy_name_lookup, shared_from_this(), _1, _2, url)));
		}

	}

	void torrent::on_proxy_name_lookup(asio::error_code const& e, tcp::resolver::iterator host
		, std::string url) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		std::string now(to_simple_string(second_clock::universal_time()));
		(*m_ses.m_logger) << now << " completed resolve proxy hostname for: " << url << "\n";
#endif

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post(alert::warning))
			{
				std::stringstream msg;
				msg << "HTTP seed proxy hostname lookup failed: " << e.message();
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, msg.str()));
			}

			// the name lookup failed for the http host. Don't try
			// this host again
			remove_url_seed(url);
			return;
		}

		if (m_ses.is_aborted()) return;

		tcp::endpoint a(host->endpoint());

		if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
		{
			// TODO: post alert: "proxy at " + a.address().to_string() + " (" + hostname + ") blocked by ip filter");
			return;
		}

		std::string protocol;
		std::string hostname;
		int port;
		std::string path;
		boost::tie(protocol, hostname, port, path)
			= parse_url_components(url);

		tcp::resolver::query q(hostname, boost::lexical_cast<std::string>(port));
		m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
			bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, url, a)));
	}
	catch (std::exception& exc)
	{
		assert(false);
	};

	void torrent::on_name_lookup(asio::error_code const& e, tcp::resolver::iterator host
		, std::string url, tcp::endpoint proxy) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		std::string now(to_simple_string(second_clock::universal_time()));
		(*m_ses.m_logger) << now << " completed resolve: " << url << "\n";
#endif

		std::set<std::string>::iterator i = m_resolving_web_seeds.find(url);
		if (i != m_resolving_web_seeds.end()) m_resolving_web_seeds.erase(i);

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post(alert::warning))
			{
				std::stringstream msg;
				msg << "HTTP seed hostname lookup failed: " << e.message();
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, msg.str()));
			}
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_ses.m_logger) << " ** HOSTNAME LOOKUP FAILED!**: " << url << "\n";
#endif

			// the name lookup failed for the http host. Don't try
			// this host again
			remove_url_seed(url);
			return;
		}

		if (m_ses.is_aborted()) return;

		tcp::endpoint a(host->endpoint());

		if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
		{
			// TODO: post alert: "web seed at " + a.address().to_string() + " blocked by ip filter");
			return;
		}
		
		peer_iterator conn = m_connections.find(a);
		if (conn != m_connections.end())
		{
			if (dynamic_cast<web_peer_connection*>(conn->second) == 0
				|| conn->second->is_disconnecting()) conn->second->disconnect();
			else return;
		}

		boost::shared_ptr<stream_socket> s(new stream_socket(m_ses.m_io_service));
		boost::intrusive_ptr<peer_connection> c(new web_peer_connection(
			m_ses, shared_from_this(), s, a, proxy, url));
			
#ifndef NDEBUG
		c->m_in_constructor = false;
#endif

		try
		{
			m_ses.m_connection_queue.push_back(c);

			assert(m_connections.find(a) == m_connections.end());

#ifndef NDEBUG
			m_policy->check_invariant();
#endif
			// add the newly connected peer to this torrent's peer list
			m_connections.insert(
				std::make_pair(a, boost::get_pointer(c)));

#ifndef NDEBUG
			m_policy->check_invariant();
#endif

			m_ses.process_connection_queue();
		}
		catch (std::exception& e)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_ses.m_logger) << " ** HOSTNAME LOOKUP FAILED!**: " << e.what() << "\n";
#endif

			// TODO: post an error alert!
			std::map<tcp::endpoint, peer_connection*>::iterator i = m_connections.find(a);
			if (i != m_connections.end()) m_connections.erase(i);
			m_ses.connection_failed(s, a, e.what());
			c->disconnect();
		}
	}
	catch (std::exception& exc)
	{
		assert(false);
	};

	void torrent::resolve_peer_country(boost::intrusive_ptr<peer_connection> const& p) const
	{
		if (m_resolving_country
			|| p->has_country()
			|| p->is_connecting()
			|| p->is_queued()
			|| p->in_handshake()) return;

		m_resolving_country = true;
		tcp::resolver::query q(boost::lexical_cast<std::string>(p->remote().address())
			+ ".zz.countries.nerd.dk", "0");
		m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
			bind(&torrent::on_country_lookup, shared_from_this(), _1, _2, p)));
	}

	namespace
	{
		typedef std::pair<int, char const*> country_entry;

		bool compare_first(country_entry const& lhs, country_entry const& rhs)
		{
			return lhs.first < rhs.first;
		}
	}

	void torrent::on_country_lookup(asio::error_code const& error, tcp::resolver::iterator i
		, intrusive_ptr<peer_connection> p) const
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;
		
		m_resolving_country = false;

		// must be ordered in increasing order
		country_entry country_map[] =
		{
			  country_entry(  4,  "AF")
			, country_entry(  8,  "AL")
			, country_entry( 10,  "AQ")
			, country_entry( 12,  "DZ")
			, country_entry( 16,  "AS")
			, country_entry( 20,  "AD")
			, country_entry( 24,  "AO")
			, country_entry( 28,  "AG")
			, country_entry( 31,  "AZ")
			, country_entry( 32,  "AR")
			, country_entry( 36,  "AU")
			, country_entry( 40,  "AT")
			, country_entry( 44,  "BS")
			, country_entry( 48,  "BH")
			, country_entry( 50,  "BD")
			, country_entry( 51,  "AM")
			, country_entry( 52,  "BB")
			, country_entry( 56,  "BE")
			, country_entry( 60,  "BM")
			, country_entry( 64,  "BT")
			, country_entry( 68,  "BO")
			, country_entry( 70,  "BA")
			, country_entry( 72,  "BW")
			, country_entry( 74,  "BV")
			, country_entry( 76,  "BR")
			, country_entry( 84,  "BZ")
			, country_entry( 86,  "IO")
			, country_entry( 90,  "SB")
			, country_entry( 92,  "VG")
			, country_entry( 96,  "BN")
			, country_entry(100,  "BG")
			, country_entry(104,  "MM")
			, country_entry(108,  "BI")
			, country_entry(112,  "BY")
			, country_entry(116,  "KH")
			, country_entry(120,  "CM")
			, country_entry(124,  "CA")
			, country_entry(132,  "CV")
			, country_entry(136,  "KY")
			, country_entry(140,  "CF")
			, country_entry(144,  "LK")
			, country_entry(148,  "TD")
			, country_entry(152,  "CL")
			, country_entry(156,  "CN")
			, country_entry(158,  "TW")
			, country_entry(162,  "CX")
			, country_entry(166,  "CC")
			, country_entry(170,  "CO")
			, country_entry(174,  "KM")
			, country_entry(175,  "YT")
			, country_entry(178,  "CG")
			, country_entry(180,  "CD")
			, country_entry(184,  "CK")
			, country_entry(188,  "CR")
			, country_entry(191,  "HR")
			, country_entry(192,  "CU")
			, country_entry(203,  "CZ")
			, country_entry(204,  "BJ")
			, country_entry(208,  "DK")
			, country_entry(212,  "DM")
			, country_entry(214,  "DO")
			, country_entry(218,  "EC")
			, country_entry(222,  "SV")
			, country_entry(226,  "GQ")
			, country_entry(231,  "ET")
			, country_entry(232,  "ER")
			, country_entry(233,  "EE")
			, country_entry(234,  "FO")
			, country_entry(238,  "FK")
			, country_entry(239,  "GS")
			, country_entry(242,  "FJ")
			, country_entry(246,  "FI")
			, country_entry(248,  "AX")
			, country_entry(250,  "FR")
			, country_entry(254,  "GF")
			, country_entry(258,  "PF")
			, country_entry(260,  "TF")
			, country_entry(262,  "DJ")
			, country_entry(266,  "GA")
			, country_entry(268,  "GE")
			, country_entry(270,  "GM")
			, country_entry(275,  "PS")
			, country_entry(276,  "DE")
			, country_entry(288,  "GH")
			, country_entry(292,  "GI")
			, country_entry(296,  "KI")
			, country_entry(300,  "GR")
			, country_entry(304,  "GL")
			, country_entry(308,  "GD")
			, country_entry(312,  "GP")
			, country_entry(316,  "GU")
			, country_entry(320,  "GT")
			, country_entry(324,  "GN")
			, country_entry(328,  "GY")
			, country_entry(332,  "HT")
			, country_entry(334,  "HM")
			, country_entry(336,  "VA")
			, country_entry(340,  "HN")
			, country_entry(344,  "HK")
			, country_entry(348,  "HU")
			, country_entry(352,  "IS")
			, country_entry(356,  "IN")
			, country_entry(360,  "ID")
			, country_entry(364,  "IR")
			, country_entry(368,  "IQ")
			, country_entry(372,  "IE")
			, country_entry(376,  "IL")
			, country_entry(380,  "IT")
			, country_entry(384,  "CI")
			, country_entry(388,  "JM")
			, country_entry(392,  "JP")
			, country_entry(398,  "KZ")
			, country_entry(400,  "JO")
			, country_entry(404,  "KE")
			, country_entry(408,  "KP")
			, country_entry(410,  "KR")
			, country_entry(414,  "KW")
			, country_entry(417,  "KG")
			, country_entry(418,  "LA")
			, country_entry(422,  "LB")
			, country_entry(426,  "LS")
			, country_entry(428,  "LV")
			, country_entry(430,  "LR")
			, country_entry(434,  "LY")
			, country_entry(438,  "LI")
			, country_entry(440,  "LT")
			, country_entry(442,  "LU")
			, country_entry(446,  "MO")
			, country_entry(450,  "MG")
			, country_entry(454,  "MW")
			, country_entry(458,  "MY")
			, country_entry(462,  "MV")
			, country_entry(466,  "ML")
			, country_entry(470,  "MT")
			, country_entry(474,  "MQ")
			, country_entry(478,  "MR")
			, country_entry(480,  "MU")
			, country_entry(484,  "MX")
			, country_entry(492,  "MC")
			, country_entry(496,  "MN")
			, country_entry(498,  "MD")
			, country_entry(500,  "MS")
			, country_entry(504,  "MA")
			, country_entry(508,  "MZ")
			, country_entry(512,  "OM")
			, country_entry(516,  "NA")
			, country_entry(520,  "NR")
			, country_entry(524,  "NP")
			, country_entry(528,  "NL")
			, country_entry(530,  "AN")
			, country_entry(533,  "AW")
			, country_entry(540,  "NC")
			, country_entry(548,  "VU")
			, country_entry(554,  "NZ")
			, country_entry(558,  "NI")
			, country_entry(562,  "NE")
			, country_entry(566,  "NG")
			, country_entry(570,  "NU")
			, country_entry(574,  "NF")
			, country_entry(578,  "NO")
			, country_entry(580,  "MP")
			, country_entry(581,  "UM")
			, country_entry(583,  "FM")
			, country_entry(584,  "MH")
			, country_entry(585,  "PW")
			, country_entry(586,  "PK")
			, country_entry(591,  "PA")
			, country_entry(598,  "PG")
			, country_entry(600,  "PY")
			, country_entry(604,  "PE")
			, country_entry(608,  "PH")
			, country_entry(612,  "PN")
			, country_entry(616,  "PL")
			, country_entry(620,  "PT")
			, country_entry(624,  "GW")
			, country_entry(626,  "TL")
			, country_entry(630,  "PR")
			, country_entry(634,  "QA")
			, country_entry(634,  "QA")
			, country_entry(638,  "RE")
			, country_entry(642,  "RO")
			, country_entry(643,  "RU")
			, country_entry(646,  "RW")
			, country_entry(654,  "SH")
			, country_entry(659,  "KN")
			, country_entry(660,  "AI")
			, country_entry(662,  "LC")
			, country_entry(666,  "PM")
			, country_entry(670,  "VC")
			, country_entry(674,  "SM")
			, country_entry(678,  "ST")
			, country_entry(682,  "SA")
			, country_entry(686,  "SN")
			, country_entry(690,  "SC")
			, country_entry(694,  "SL")
			, country_entry(702,  "SG")
			, country_entry(703,  "SK")
			, country_entry(704,  "VN")
			, country_entry(705,  "SI")
			, country_entry(706,  "SO")
			, country_entry(710,  "ZA")
			, country_entry(716,  "ZW")
			, country_entry(724,  "ES")
			, country_entry(732,  "EH")
			, country_entry(736,  "SD")
			, country_entry(740,  "SR")
			, country_entry(744,  "SJ")
			, country_entry(748,  "SZ")
			, country_entry(752,  "SE")
			, country_entry(756,  "CH")
			, country_entry(760,  "SY")
			, country_entry(762,  "TJ")
			, country_entry(764,  "TH")
			, country_entry(768,  "TG")
			, country_entry(772,  "TK")
			, country_entry(776,  "TO")
			, country_entry(780,  "TT")
			, country_entry(784,  "AE")
			, country_entry(788,  "TN")
			, country_entry(792,  "TR")
			, country_entry(795,  "TM")
			, country_entry(796,  "TC")
			, country_entry(798,  "TV")
			, country_entry(800,  "UG")
			, country_entry(804,  "UA")
			, country_entry(807,  "MK")
			, country_entry(818,  "EG")
			, country_entry(826,  "GB")
			, country_entry(834,  "TZ")
			, country_entry(840,  "US")
			, country_entry(850,  "VI")
			, country_entry(854,  "BF")
			, country_entry(858,  "UY")
			, country_entry(860,  "UZ")
			, country_entry(862,  "VE")
			, country_entry(876,  "WF")
			, country_entry(882,  "WS")
			, country_entry(887,  "YE")
			, country_entry(891,  "CS")
			, country_entry(894,  "ZM")
		};

		if (error || i == tcp::resolver::iterator())
		{
			// this is used to indicate that we shouldn't
			// try to resolve it again
			p->set_country("--");
			return;
		}

		while (i != tcp::resolver::iterator()
			&& !i->endpoint().address().is_v4()) ++i;
		if (i != tcp::resolver::iterator())
		{
			// country is an ISO 3166 country code
			int country = i->endpoint().address().to_v4().to_ulong() & 0xffff;
			
			// look up the country code in the map
			const int size = sizeof(country_map)/sizeof(country_map[0]);
			country_entry* i =
				std::lower_bound(country_map, country_map + size
					, country_entry(country, ""), &compare_first);
			if (i == country_map + size
				|| i->first != country)
			{
				// unknown country!
				p->set_country("!!");
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				(*m_ses.m_logger) << "IP " << p->remote().address() << " was mapped to unknown country: " << country << "\n";
#endif
				return;
			}
			
			p->set_country(i->second);
		}
	}

	peer_connection& torrent::connect_to_peer(const tcp::endpoint& a)
	{
		INVARIANT_CHECK;

		if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
			throw protocol_error(a.address().to_string() + " blocked by ip filter");

		if (m_connections.find(a) != m_connections.end())
			throw protocol_error("already connected to peer");

		boost::shared_ptr<stream_socket> s(new stream_socket(m_ses.m_io_service));
		boost::intrusive_ptr<peer_connection> c(new bt_peer_connection(
			m_ses, shared_from_this(), s, a));
			
#ifndef NDEBUG
		c->m_in_constructor = false;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<peer_plugin> pp((*i)->new_connection(c.get()));
			if (pp) c->add_extension(pp);
		}
#endif

		try
		{
			m_ses.m_connection_queue.push_back(c);

			assert(m_connections.find(a) == m_connections.end());

#ifndef NDEBUG
			m_policy->check_invariant();
#endif
			// add the newly connected peer to this torrent's peer list
			m_connections.insert(
				std::make_pair(a, boost::get_pointer(c)));

#ifndef NDEBUG
			m_policy->check_invariant();
#endif

			m_ses.process_connection_queue();
		}
		catch (std::exception& e)
		{
			// TODO: post an error alert!
			std::map<tcp::endpoint, peer_connection*>::iterator i = m_connections.find(a);
			if (i != m_connections.end()) m_connections.erase(i);
			m_ses.connection_failed(s, a, e.what());
			c->disconnect();
			throw;
		}
		if (c->is_disconnecting()) throw protocol_error("failed to connect");
		return *c;
	}

	void torrent::set_metadata(entry const& metadata)
	{
		m_torrent_file.parse_info_section(metadata);

		boost::mutex::scoped_lock(m_checker.m_mutex);

		boost::shared_ptr<aux::piece_checker_data> d(
				new aux::piece_checker_data);
		d->torrent_ptr = shared_from_this();
		d->save_path = m_save_path;
		d->info_hash = m_torrent_file.info_hash();
		// add the torrent to the queue to be checked
		m_checker.m_torrents.push_back(d);
		typedef session_impl::torrent_map torrent_map;
		torrent_map::iterator i = m_ses.m_torrents.find(
			m_torrent_file.info_hash());
		assert(i != m_ses.m_torrents.end());
		m_ses.m_torrents.erase(i);
		// and notify the thread that it got another
		// job in its queue
		m_checker.m_cond.notify_one();

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(metadata_received_alert(
				get_handle(), "metadata successfully received from swarm"));
		}
	}

	void torrent::attach_peer(peer_connection* p)
	{
		INVARIANT_CHECK;

		assert(p != 0);
		assert(!p->is_local());

		std::map<tcp::endpoint, peer_connection*>::iterator c
			= m_connections.find(p->remote());
		if (c != m_connections.end())
		{
			// we already have a peer_connection to this ip.
			// It may currently be waiting for completing a
			// connection attempt that might fail. So,
			// prioritize this current connection since
			// it has already succeeded.
			if (!c->second->is_connecting())
			{
				throw protocol_error("already connected to peer");
			}
			c->second->disconnect();
		}

		if (m_ses.m_connections.find(p->get_socket())
			== m_ses.m_connections.end())
		{
			throw protocol_error("peer is not properly constructed");
		}

		if (m_ses.is_aborted())
		{
			throw protocol_error("session is closing");
		}

		peer_iterator ci = m_connections.insert(
			std::make_pair(p->remote(), p)).first;
		try
		{
			// if new_connection throws, we have to remove the
			// it from the list.

#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				boost::shared_ptr<peer_plugin> pp((*i)->new_connection(p));
				if (pp) p->add_extension(pp);
			}
#endif
			m_policy->new_connection(*ci->second);
		}
		catch (std::exception& e)
		{
			m_connections.erase(ci);
			throw;
		}
		assert((p->proxy() == tcp::endpoint() && p->remote() == p->get_socket()->remote_endpoint())
			|| p->proxy() == p->get_socket()->remote_endpoint());

#ifndef NDEBUG
		m_policy->check_invariant();
#endif
	}

	void torrent::disconnect_all()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		while (!m_connections.empty())
		{
			peer_connection& p = *m_connections.begin()->second;
			assert(p.associated_torrent().lock().get() == this);

#if defined(TORRENT_VERBOSE_LOGGING)
			if (m_abort)
				(*p.m_logger) << "*** CLOSING CONNECTION 'aborting'\n";
			else
				(*p.m_logger) << "*** CLOSING CONNECTION 'pausing'\n";
#endif
#ifndef NDEBUG
			std::size_t size = m_connections.size();
#endif
			p.disconnect();
			assert(m_connections.size() <= size);
		}
	}

	bool torrent::request_bandwidth_from_session(int channel) const
	{
		int max_assignable = m_bandwidth_limit[channel].max_assignable();
		return max_assignable > max_bandwidth_block_size
			|| (m_bandwidth_limit[channel].throttle() < max_bandwidth_block_size
				&& max_assignable == m_bandwidth_limit[channel].throttle());
	}

	int torrent::bandwidth_throttle(int channel) const
	{
		return m_bandwidth_limit[channel].throttle();
	}

	void torrent::request_bandwidth(int channel
		, boost::intrusive_ptr<peer_connection> p
		, bool non_prioritized)
	{
		if (request_bandwidth_from_session(channel))
		{
			if (channel == peer_connection::upload_channel)
				m_ses.m_ul_bandwidth_manager.request_bandwidth(p, non_prioritized);
			else if (channel == peer_connection::download_channel)
				m_ses.m_dl_bandwidth_manager.request_bandwidth(p, non_prioritized);
			
			m_bandwidth_limit[channel].assign(max_bandwidth_block_size);
		}
		else
		{
			m_bandwidth_queue[channel].push_back(bw_queue_entry(p, non_prioritized));
		}
	}

	void torrent::expire_bandwidth(int channel, int amount)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		assert(amount >= -1);
		if (amount == -1) amount = max_bandwidth_block_size;
		m_bandwidth_limit[channel].expire(amount);
		
		while (!m_bandwidth_queue[channel].empty()
			&& request_bandwidth_from_session(channel))
		{
			bw_queue_entry qe = m_bandwidth_queue[channel].front();
			m_bandwidth_queue[channel].pop_front();
			if (channel == peer_connection::upload_channel)
				m_ses.m_ul_bandwidth_manager.request_bandwidth(qe.peer, qe.non_prioritized);
			else if (channel == peer_connection::download_channel)
				m_ses.m_dl_bandwidth_manager.request_bandwidth(qe.peer, qe.non_prioritized);
			m_bandwidth_limit[channel].assign(max_bandwidth_block_size);
		}
	}

	void torrent::assign_bandwidth(int channel, int amount)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		assert(amount >= 0);
		if (amount < max_bandwidth_block_size)
			expire_bandwidth(channel, max_bandwidth_block_size - amount);
	}

	// called when torrent is finished (all interested pieces downloaded)
	void torrent::finished()
	{
		INVARIANT_CHECK;

		if (alerts().should_post(alert::info))
		{
			alerts().post_alert(torrent_finished_alert(
				get_handle()
				, "torrent has finished downloading"));
		}

	// disconnect all seeds
	// TODO: should disconnect all peers that have the pieces we have
	// not just seeds
		std::vector<peer_connection*> seeds;
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			assert(i->second->associated_torrent().lock().get() == this);
			if (i->second->is_seed())
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*i->second->m_logger) << "*** SEED, CLOSING CONNECTION\n";
#endif
				seeds.push_back(i->second);
			}
		}
		std::for_each(seeds.begin(), seeds.end()
			, bind(&peer_connection::disconnect, _1));

		m_storage->release_files();
	}
	
	// called when torrent is complete (all pieces downloaded)
	void torrent::completed()
	{
		INVARIANT_CHECK;

		// make the next tracker request
		// be a completed-event
		m_event = tracker_request::completed;
		force_tracker_request();
	}

	// this will move the tracker with the given index
	// to a prioritized position in the list (move it towards
	// the begining) and return the new index to the tracker.
	int torrent::prioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		assert(index >= 0);
		if (index >= (int)m_trackers.size()) return (int)m_trackers.size()-1;

		while (index > 0 && m_trackers[index].tier == m_trackers[index-1].tier)
		{
			std::swap(m_trackers[index].url, m_trackers[index-1].url);
			--index;
		}
		return index;
	}

	void torrent::try_next_tracker()
	{
		INVARIANT_CHECK;

		using namespace boost::posix_time;
		++m_currently_trying_tracker;

		if ((unsigned)m_currently_trying_tracker >= m_trackers.size())
		{
			int delay = tracker_retry_delay_min
				+ std::min(m_failed_trackers, (int)tracker_failed_max)
				* (tracker_retry_delay_max - tracker_retry_delay_min)
				/ tracker_failed_max;

			++m_failed_trackers;
			// if we've looped the tracker list, wait a bit before retrying
			m_currently_trying_tracker = 0;
			m_next_request = second_clock::universal_time() + seconds(delay);

#ifndef TORRENT_DISABLE_DHT
			// only start the dht announce unless we already are already running
			// the announce timer (a positive expiration time indicates
			// that it's running)
			if (m_dht_announce_timer.expires_from_now().is_negative() && should_announce_dht())
			{
				m_dht_announce_timer.expires_from_now(boost::posix_time::seconds(1));
				m_dht_announce_timer.async_wait(m_ses.m_strand.wrap(
					bind(&torrent::on_dht_announce, this, _1)));
			}
#endif

		}
		else
		{
			// don't delay before trying the next tracker
			m_next_request = second_clock::universal_time();
		}

	}

	bool torrent::check_fastresume(aux::piece_checker_data& data)
	{
		INVARIANT_CHECK;

		if (!m_storage.get())
		{
			// this means we have received the metadata through the
			// metadata extension, and we have to initialize
			init();
		}

		assert(m_storage.get());
		bool done = true;
		try
		{
			done = m_storage->check_fastresume(data, m_have_pieces, m_num_pieces
				, m_compact_mode);
		}
		catch (std::exception& e)
		{
			// probably means file permission failure or invalid filename
			std::fill(m_have_pieces.begin(), m_have_pieces.end(), false);
			m_num_pieces = 0;

			if (m_ses.m_alerts.should_post(alert::fatal))
			{
				m_ses.m_alerts.post_alert(
					file_error_alert(
						get_handle()
						, e.what()));
			}
			pause();
		}
#ifndef NDEBUG
		m_initial_done = boost::get<0>(bytes_done());
#endif
		return done;
	}
	
	std::pair<bool, float> torrent::check_files()
	{
		INVARIANT_CHECK;

		assert(m_storage.get());

		std::pair<bool, float> progress(true, 1.f);
		try
		{
			progress = m_storage->check_files(m_have_pieces, m_num_pieces
				, m_ses.m_mutex);
		}
		catch (std::exception& e)
		{
			// probably means file permission failure or invalid filename
			std::fill(m_have_pieces.begin(), m_have_pieces.end(), false);
			m_num_pieces = 0;

			if (m_ses.m_alerts.should_post(alert::fatal))
			{
				m_ses.m_alerts.post_alert(
					file_error_alert(
						get_handle()
						, e.what()));
			}
			pause();
		}

#ifndef NDEBUG
		m_initial_done = boost::get<0>(bytes_done());
#endif
		return progress;
	}

	void torrent::files_checked(std::vector<piece_picker::downloading_piece> const&
		unfinished_pieces)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		
		INVARIANT_CHECK;

		if (!is_seed())
		{
			m_picker->files_checked(m_have_pieces, unfinished_pieces);
		}
		else
		{
			m_picker.reset();
		}
		if (!m_connections_initialized)
		{
			m_connections_initialized = true;
			// all peer connections have to initialize themselves now that the metadata
			// is available
			typedef std::map<tcp::endpoint, peer_connection*> conn_map;
			for (conn_map::iterator i = m_connections.begin()
				, end(m_connections.end()); i != end;)
			{
				try
				{
					i->second->init();
					i->second->on_metadata();
					++i;
				}
				catch (std::exception& e)
				{
					// the connection failed, close it
					conn_map::iterator j = i;
					++j;
					m_ses.connection_failed(i->second->get_socket()
						, i->first, e.what());
					i = j;
				}
			}
		}
#ifndef NDEBUG
		m_initial_done = boost::get<0>(bytes_done());
#endif
	}

	alert_manager& torrent::alerts() const
	{
		return m_ses.m_alerts;
	}

	boost::filesystem::path torrent::save_path() const
	{
		return m_save_path;
	}

	bool torrent::move_storage(boost::filesystem::path const& save_path)
	{
		INVARIANT_CHECK;

		bool ret = true;
		if (m_storage.get())
		{
			ret = m_storage->move_storage(save_path);
			m_save_path = m_storage->save_path();
		}
		else
		{
			m_save_path = save_path;
		}
		return ret;
	}

	piece_manager& torrent::filesystem()
	{
		INVARIANT_CHECK;

		assert(m_storage.get());
		return *m_storage;
	}


	torrent_handle torrent::get_handle() const
	{
		INVARIANT_CHECK;

		return torrent_handle(&m_ses, &m_checker, m_torrent_file.info_hash());
	}

	session_settings const& torrent::settings() const
	{
//		INVARIANT_CHECK;

		return m_ses.settings();
	}

#ifndef NDEBUG
	void torrent::check_invariant() const
	{
//		size_type download = m_stat.total_payload_download();
//		size_type done = boost::get<0>(bytes_done());
//		assert(download >= done - m_initial_done);
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			peer_connection const& p = *i->second;
			torrent* associated_torrent = p.associated_torrent().lock().get();
			if (associated_torrent != this)
				assert(false);
		}

		if (valid_metadata())
		{
			assert(int(m_have_pieces.size()) == m_torrent_file.num_pieces());
		}
		else
		{
			assert(m_have_pieces.empty());
		}

		size_type total_done = quantized_bytes_done();
		if (m_torrent_file.is_valid())
		{
			if (is_seed())
				assert(total_done == m_torrent_file.total_size());
			else
				assert(total_done != m_torrent_file.total_size());
		}
		else
		{
			assert(total_done == 0);
		}

// This check is very expensive.
		assert(m_num_pieces
			== std::count(m_have_pieces.begin(), m_have_pieces.end(), true));
		assert(!valid_metadata() || m_block_size > 0);
		assert(!valid_metadata() || (m_torrent_file.piece_length() % m_block_size) == 0);
//		if (is_seed()) assert(m_picker.get() == 0);
	}
#endif

	void torrent::set_sequenced_download_threshold(int threshold)
	{
		// TODO: if there is not valid metadata, save this setting and
		// set it once the piece picker is created.
		if (valid_metadata() && !is_seed())
			picker().set_sequenced_download_threshold(threshold);
	}


	void torrent::set_max_uploads(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		m_uploads_quota.max = std::max(m_uploads_quota.min, limit);
	}

	void torrent::set_max_connections(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		m_connections_quota.max = std::max(m_connections_quota.min, limit);
	}

	void torrent::set_peer_upload_limit(tcp::endpoint ip, int limit)
	{
		assert(limit >= -1);
		peer_connection* p = connection_for(ip);
		if (p == 0) return;
		p->set_upload_limit(limit);
	}

	void torrent::set_peer_download_limit(tcp::endpoint ip, int limit)
	{
		assert(limit >= -1);
		peer_connection* p = connection_for(ip);
		if (p == 0) return;
		p->set_download_limit(limit);
	}

	void torrent::set_upload_limit(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		if (limit < num_peers() * 10) limit = num_peers() * 10;
		m_bandwidth_limit[peer_connection::upload_channel].throttle(limit);
	}

	void torrent::set_download_limit(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		if (limit < num_peers() * 10) limit = num_peers() * 10;
		m_bandwidth_limit[peer_connection::download_channel].throttle(limit);
	}

	void torrent::pause()
	{
		INVARIANT_CHECK;

		if (m_paused) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { if ((*i)->on_pause()) return; } catch (std::exception&) {}
		}
#endif

		disconnect_all();
		m_paused = true;
		// tell the tracker that we stopped
		m_event = tracker_request::stopped;
		m_just_paused = true;
		// this will make the storage close all
		// files and flush all cached data
		if (m_storage.get()) m_storage->release_files();
	}

	void torrent::resume()
	{
		INVARIANT_CHECK;

		if (!m_paused) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { if ((*i)->on_resume()) return; } catch (std::exception&) {}
		}
#endif

		m_paused = false;
		m_uploads_quota.min = 2;
		m_connections_quota.min = 2;
		m_uploads_quota.max = std::numeric_limits<int>::max();
		m_connections_quota.max = std::numeric_limits<int>::max();

		// tell the tracker that we're back
		m_event = tracker_request::started;
		force_tracker_request();

		// make pulse be called as soon as possible
		m_time_scaler = 0;
	}

	void torrent::second_tick(stat& accumulator, float tick_interval)
	{
		INVARIANT_CHECK;

		m_connections_quota.used = (int)m_connections.size();
		m_uploads_quota.used = m_policy->num_uploads();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { (*i)->tick(); } catch (std::exception&) {}
		}
#endif

		if (m_paused)
		{
			// let the stats fade out to 0
 			m_stat.second_tick(tick_interval);
 			m_web_stat.second_tick(tick_interval);
			m_connections_quota.min = 0;
			m_connections_quota.max = 0;
			m_uploads_quota.min = 0;
			m_uploads_quota.max = 0;
			return;
		}

		// ---- WEB SEEDS ----

		// if we're a seed, we don't need to connect to any web-seed
		if (!is_seed() && !m_web_seeds.empty())
		{
			// keep trying web-seeds if there are any
			// first find out which web seeds we are connected to
			std::set<std::string> web_seeds;
			for (peer_iterator i = m_connections.begin();
				i != m_connections.end(); ++i)
			{
				web_peer_connection* p
					= dynamic_cast<web_peer_connection*>(i->second);
				if (!p) continue;
				web_seeds.insert(p->url());
			}

			for (std::set<std::string>::iterator i = m_resolving_web_seeds.begin()
				, end(m_resolving_web_seeds.end()); i != end; ++i)
				web_seeds.insert(web_seeds.begin(), *i);

			// from the list of available web seeds, subtract the ones we are
			// already connected to.
			std::vector<std::string> not_connected_web_seeds;
			std::set_difference(m_web_seeds.begin(), m_web_seeds.end(), web_seeds.begin()
				, web_seeds.end(), std::back_inserter(not_connected_web_seeds));

			// connect to all of those that we aren't connected to
			std::for_each(not_connected_web_seeds.begin(), not_connected_web_seeds.end()
				, bind(&torrent::connect_to_url_seed, this, _1));
		}
		
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			peer_connection* p = i->second;
			m_stat += p->statistics();
			if (dynamic_cast<web_peer_connection*>(p))
			{
				m_web_stat += p->statistics();
			}
			// updates the peer connection's ul/dl bandwidth
			// resource requests
			p->second_tick(tick_interval);
		}
		accumulator += m_stat;
		m_stat.second_tick(tick_interval);
		m_web_stat.second_tick(tick_interval);
	}

	void torrent::distribute_resources(float tick_interval)
	{
		INVARIANT_CHECK;

		m_time_scaler--;
		if (m_time_scaler <= 0)
		{
			m_time_scaler = 10;
			m_policy->pulse();
		}
	}

	bool torrent::verify_piece(int piece_index)
	{
//		INVARIANT_CHECK;

		assert(m_storage.get());
		assert(piece_index >= 0);
		assert(piece_index < m_torrent_file.num_pieces());
		assert(piece_index < (int)m_have_pieces.size());

		int size = static_cast<int>(m_torrent_file.piece_size(piece_index));
		std::vector<char> buffer(size);
		assert(size > 0);
		m_storage->read(&buffer[0], piece_index, 0, size);

		hasher h;
		h.update(&buffer[0], size);
		sha1_hash digest = h.final();

		if (m_torrent_file.hash_for_piece(piece_index) != digest)
			return false;

		return true;
	}

	const tcp::endpoint& torrent::current_tracker() const
	{
		return m_tracker_address;
	}

	bool torrent::is_allocating() const
	{ return m_storage.get() && m_storage->is_allocating(); }
	
	void torrent::file_progress(std::vector<float>& fp) const
	{
		assert(valid_metadata());
	
		fp.clear();
		fp.resize(m_torrent_file.num_files(), 0.f);
		
		for (int i = 0; i < m_torrent_file.num_files(); ++i)
		{
			peer_request ret = m_torrent_file.map_file(i, 0, 0);
			size_type size = m_torrent_file.file_at(i).size;

// zero sized files are considered
// 100% done all the time
			if (size == 0)
			{
				fp[i] = 1.f;
				continue;
			}

			size_type done = 0;
			while (size > 0)
			{
				size_type bytes_step = std::min(m_torrent_file.piece_size(ret.piece)
					- ret.start, size);
				if (m_have_pieces[ret.piece]) done += bytes_step;
				++ret.piece;
				ret.start = 0;
				size -= bytes_step;
			}
			assert(size == 0);

			fp[i] = static_cast<float>(done) / m_torrent_file.file_at(i).size;
		}
	}
	
	torrent_status torrent::status() const
	{
		INVARIANT_CHECK;

		assert(std::accumulate(
			m_have_pieces.begin()
			, m_have_pieces.end()
			, 0) == m_num_pieces);

		torrent_status st;

		st.num_peers = (int)std::count_if(m_connections.begin(), m_connections.end(),
			boost::bind<bool>(std::logical_not<bool>(), boost::bind(&peer_connection::is_connecting,
			boost::bind(&std::map<tcp::endpoint,peer_connection*>::value_type::second, _1))));

		st.num_complete = m_complete;
		st.num_incomplete = m_incomplete;
		st.paused = m_paused;
		boost::tie(st.total_done, st.total_wanted_done) = bytes_done();

		// payload transfer
		st.total_payload_download = m_stat.total_payload_download();
		st.total_payload_upload = m_stat.total_payload_upload();

		// total transfer
		st.total_download = m_stat.total_payload_download()
			+ m_stat.total_protocol_download();
		st.total_upload = m_stat.total_payload_upload()
			+ m_stat.total_protocol_upload();

		// failed bytes
		st.total_failed_bytes = m_total_failed_bytes;
		st.total_redundant_bytes = m_total_redundant_bytes;

		// transfer rate
		st.download_rate = m_stat.download_rate();
		st.upload_rate = m_stat.upload_rate();
		st.download_payload_rate = m_stat.download_payload_rate();
		st.upload_payload_rate = m_stat.upload_payload_rate();

		st.next_announce = next_announce()
			- second_clock::universal_time();
		if (st.next_announce.is_negative()) st.next_announce
			= boost::posix_time::seconds(0);
		st.announce_interval = boost::posix_time::seconds(m_duration);

		if (m_last_working_tracker >= 0)
		{
			st.current_tracker
				= m_trackers[m_last_working_tracker].url;
		}

		// if we don't have any metadata, stop here

		if (!valid_metadata())
		{
			if (m_got_tracker_response == false)
				st.state = torrent_status::connecting_to_tracker;
			else
				st.state = torrent_status::downloading_metadata;

// TODO: add a progress member to the torrent that will be used in this case
// and that may be set by a plugin
//			if (m_metadata_size == 0) st.progress = 0.f;
//			else st.progress = std::min(1.f, m_metadata_progress / (float)m_metadata_size);
			st.progress = 0.f;

			st.block_size = 0;

			return st;
		}

		st.block_size = block_size();

		// fill in status that depends on metadata

		st.total_wanted = m_torrent_file.total_size();

		if (m_picker.get() && (m_picker->num_filtered() > 0
			|| m_picker->num_have_filtered() > 0))
		{
			int filtered_pieces = m_picker->num_filtered()
				+ m_picker->num_have_filtered();
			int last_piece_index = m_torrent_file.num_pieces() - 1;
			if (m_picker->is_filtered(last_piece_index))
			{
				st.total_wanted -= m_torrent_file.piece_size(last_piece_index);
				--filtered_pieces;
			}
			
			st.total_wanted -= filtered_pieces * m_torrent_file.piece_length();
		}

		assert(st.total_wanted >= st.total_wanted_done);

		if (st.total_wanted == 0) st.progress = 1.f;
		else st.progress = st.total_wanted_done
			/ static_cast<double>(st.total_wanted);

		st.pieces = &m_have_pieces;
		st.num_pieces = m_num_pieces;

		if (m_got_tracker_response == false)
		{
			st.state = torrent_status::connecting_to_tracker;
		}
		else if (is_seed())
		{
			assert(st.total_done == m_torrent_file.total_size());
			st.state = torrent_status::seeding;
		}
		else if (st.total_wanted_done == st.total_wanted)
		{
			assert(st.total_done != m_torrent_file.total_size());
			st.state = torrent_status::finished;
		}
		else
		{
			st.state = torrent_status::downloading;
		}

		st.num_seeds = num_seeds();
		if (m_picker.get())
			st.distributed_copies = m_picker->distributed_copies();
		else
			st.distributed_copies = -1;
		return st;
	}

	int torrent::num_seeds() const
	{
		INVARIANT_CHECK;

		return (int)std::count_if(m_connections.begin(),	m_connections.end(),
			boost::bind(&peer_connection::is_seed,
				boost::bind(&std::map<tcp::endpoint
					,peer_connection*>::value_type::second, _1)));
	}

	void torrent::tracker_request_timed_out(
		tracker_request const&)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		debug_log("*** tracker timed out");
#endif

		if (m_ses.m_alerts.should_post(alert::warning))
		{
			std::stringstream s;
			s << "tracker: \""
				<< m_trackers[m_currently_trying_tracker].url
				<< "\" timed out";
			m_ses.m_alerts.post_alert(tracker_alert(get_handle()
				, m_failed_trackers + 1, 0, s.str()));
		}
		try_next_tracker();
	}

	// TODO: with some response codes, we should just consider
	// the tracker as a failure and not retry
	// it anymore
	void torrent::tracker_request_error(tracker_request const&
		, int response_code, const std::string& str)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		debug_log(std::string("*** tracker error: ") + str);
#endif
		if (m_ses.m_alerts.should_post(alert::warning))
		{
			std::stringstream s;
			s << "tracker: \""
				<< m_trackers[m_currently_trying_tracker].url
				<< "\" " << str;
			m_ses.m_alerts.post_alert(tracker_alert(get_handle()
				, m_failed_trackers + 1, response_code, s.str()));
		}

		try_next_tracker();
	}


#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << line << "\n";
	}
#endif

}

