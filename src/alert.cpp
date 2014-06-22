/*

Copyright (c) 2003, Arvid Norberg, Daniel Wallin
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

#include <string>

#include "libtorrent/config.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/extensions.hpp"
#include <boost/bind.hpp>

namespace libtorrent {

	alert::alert() : m_timestamp(time_now()) {}
	alert::~alert() {}
	ptime alert::timestamp() const { return m_timestamp; }


	std::string torrent_alert::message() const
	{
		if (!handle.is_valid()) return " - ";
		if (handle.name().empty())
		{
			char msg[41];
			to_hex((char const*)&handle.info_hash()[0], 20, msg);
			return msg;
		}
		return handle.name();
	}

	std::string peer_alert::message() const
	{
		error_code ec;
		return torrent_alert::message() + " peer (" + ip.address().to_string(ec)
			+ ", " + identify_client(pid) + ")";
	}

	std::string tracker_alert::message() const
	{
		return torrent_alert::message() + " (" + url + ")";
	}

	std::string read_piece_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: piece %s %u", torrent_alert::message().c_str()
			, buffer ? "successful" : "failed", piece);
		return msg;
	}

	std::string file_completed_alert::message() const
	{
		char msg[200 + TORRENT_MAX_PATH];
		snprintf(msg, sizeof(msg), "%s: file %d finished downloading"
			, torrent_alert::message().c_str(), index);
		return msg;
	}

	std::string file_renamed_alert::message() const
	{
		char msg[200 + TORRENT_MAX_PATH * 2];
		snprintf(msg, sizeof(msg), "%s: file %d renamed to %s", torrent_alert::message().c_str()
			, index, name.c_str());
		return msg;
	}

	std::string file_rename_failed_alert::message() const
	{
		char ret[200 + TORRENT_MAX_PATH * 2];
		snprintf(ret, sizeof(ret), "%s: failed to rename file %d: %s"
			, torrent_alert::message().c_str(), index, convert_from_native(error.message()).c_str());
		return ret;
	}

	std::string performance_alert::message() const
	{
		static char const* warning_str[] =
		{
			"max outstanding disk writes reached",
			"max outstanding piece requests reached",
			"upload limit too low (download rate will suffer)",
			"download limit too low (upload rate will suffer)",
			"send buffer watermark too low (upload rate will suffer)",
			"too many optimistic unchoke slots",
			"using bittyrant unchoker with no upload rate limit set",
			"the disk queue limit is too high compared to the cache size. The disk queue eats into the cache size",
			"too few ports allowed for outgoing connections",
			"too few file descriptors are allowed for this process. connection limit lowered"
		};

		return torrent_alert::message() + ": performance warning: "
			+ warning_str[warning_code];
	}

	std::string state_changed_alert::message() const
	{
		static char const* state_str[] =
			{"checking (q)", "checking", "dl metadata"
			, "downloading", "finished", "seeding", "allocating"
			, "checking (r)"};

		return torrent_alert::message() + ": state changed to: "
			+ state_str[state];
	}

	std::string tracker_error_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s (%d) %s (%d)"
			, tracker_alert::message().c_str(), status_code
			, msg.c_str(), times_in_row);
		return ret;
	}

	std::string tracker_warning_alert::message() const
	{
		return tracker_alert::message() + " warning: " + msg;
	}

	std::string scrape_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s scrape reply: %u %u"
			, tracker_alert::message().c_str(), incomplete, complete);
		return ret;
	}

	std::string scrape_failed_alert::message() const
	{
		return tracker_alert::message() + " scrape failed: " + msg;
	}

	std::string tracker_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s received peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	std::string dht_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s received DHT peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	std::string tracker_announce_alert::message() const
	{
		const static char* event_str[] = {"none", "completed", "started", "stopped", "paused"};
		TORRENT_ASSERT_VAL(event < int(sizeof(event_str)/sizeof(event_str[0])), event);
		return tracker_alert::message() + " sending announce (" + event_str[event] + ")";
	}

	std::string hash_failed_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s hash for piece %u failed"
			, torrent_alert::message().c_str(), piece_index);
		return ret;
	}

	std::string peer_ban_alert::message() const
	{
		return peer_alert::message() + " banned peer";
	}

	std::string peer_unsnubbed_alert::message() const
	{
		return peer_alert::message() + " peer unsnubbed";
	}

	std::string peer_snubbed_alert::message() const
	{
		return peer_alert::message() + " peer snubbed";
	}



	std::string invalid_request_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s peer sent an invalid piece request (piece: %u start: %u len: %u)"
			, peer_alert::message().c_str(), request.piece, request.start, request.length);
		return ret;
	}


	std::string piece_finished_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s piece: %u finished downloading"
			, torrent_alert::message().c_str(), piece_index);
		return ret;
	}


	std::string request_dropped_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s peer dropped block ( piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	std::string block_timeout_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s peer timed out request ( piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	std::string block_finished_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s block finished downloading (piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	std::string block_downloading_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s requested block (piece: %u block: %u) %s"
			, torrent_alert::message().c_str(), piece_index, block_index, peer_speedmsg);
		return ret;
	}

	std::string unwanted_block_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s received block not in download queue (piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	std::string listen_failed_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "listening on %s failed: %s"
			, print_endpoint(endpoint).c_str(), convert_from_native(error.message()).c_str());
		return ret;
	}

	std::string listen_succeeded_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "successfully listening on %s", print_endpoint(endpoint).c_str());
		return ret;
	}

	std::string portmap_error_alert::message() const
	{
		static char const* type_str[] = {"NAT-PMP", "UPnP"};
		return std::string("could not map port using ") + type_str[map_type]
			+ ": " + convert_from_native(error.message());
	}

	std::string portmap_alert::message() const
	{
		static char const* type_str[] = {"NAT-PMP", "UPnP"};
		char ret[200];
		snprintf(ret, sizeof(ret), "successfully mapped port using %s. external port: %u"
			, type_str[map_type], external_port);
		return ret;
	}

	std::string portmap_log_alert::message() const
	{
		static char const* type_str[] = {"NAT-PMP", "UPnP"};
		char ret[600];
		snprintf(ret, sizeof(ret), "%s: %s", type_str[map_type], msg.c_str());
		return ret;
	}

	std::string dht_announce_alert::message() const
	{
		error_code ec;
		char ih_hex[41];
		to_hex((const char*)&info_hash[0], 20, ih_hex);
		char msg[200];
		snprintf(msg, sizeof(msg), "incoming dht announce: %s:%u (%s)"
			, ip.to_string(ec).c_str(), port, ih_hex);
		return msg;
	}

	std::string dht_get_peers_alert::message() const
	{
		char ih_hex[41];
		to_hex((const char*)&info_hash[0], 20, ih_hex);
		char msg[200];
		snprintf(msg, sizeof(msg), "incoming dht get_peers: %s", ih_hex);
		return msg;
	}



	alert_manager::alert_manager(io_service& ios, int queue_limit, boost::uint32_t alert_mask)
		: m_alert_mask(alert_mask)
		, m_queue_size_limit(queue_limit)
		, m_ios(ios)
	{}

	alert_manager::~alert_manager()
	{
		while (!m_alerts.empty())
		{
			TORRENT_ASSERT(alert_cast<save_resume_data_alert>(m_alerts.front()) == 0
				&& "shutting down session with remaining resume data alerts in the alert queue. "
				"You proabably wany to make sure you always wait for all resume data "
				"alerts before shutting down");
			delete m_alerts.front();
			m_alerts.pop_front();
		}
	}

	alert const* alert_manager::wait_for_alert(time_duration max_wait)
	{
		mutex::scoped_lock lock(m_mutex);

		if (!m_alerts.empty()) return m_alerts.front();
		
//		system_time end = get_system_time()
//			+ boost::posix_time::microseconds(total_microseconds(max_wait));

		// apparently this call can be interrupted
		// prematurely if there are other signals
//		while (m_condition.timed_wait(lock, end))
//			if (!m_alerts.empty()) return m_alerts.front();

		ptime start = time_now_hires();

		// TODO: change this to use an asio timer instead
		while (m_alerts.empty())
		{
			lock.unlock();
			sleep(50);
			lock.lock();
			if (time_now_hires() - start >= max_wait) return 0;
		}
		return m_alerts.front();
	}

	void alert_manager::set_dispatch_function(boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		mutex::scoped_lock lock(m_mutex);

		m_dispatch = fun;

		std::deque<alert*> alerts;
		m_alerts.swap(alerts);
		lock.unlock();

		while (!alerts.empty())
		{
			TORRENT_TRY {
				m_dispatch(std::auto_ptr<alert>(alerts.front()));
			} TORRENT_CATCH(std::exception&) {}
			alerts.pop_front();
		}
	}

	void dispatch_alert(boost::function<void(alert const&)> dispatcher
		, alert* alert_)
	{
		std::auto_ptr<alert> holder(alert_);
		dispatcher(*alert_);
	}

	void alert_manager::post_alert_ptr(alert* alert_)
	{
		std::auto_ptr<alert> a(alert_);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_alert(alert_);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif

		mutex::scoped_lock lock(m_mutex);
		post_impl(a);
	}

	void alert_manager::post_alert(const alert& alert_)
	{
		std::auto_ptr<alert> a(alert_.clone());

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_alert(&alert_);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif

		mutex::scoped_lock lock(m_mutex);
		post_impl(a);
	}
		
	void alert_manager::post_impl(std::auto_ptr<alert>& alert_)
	{
		if (m_dispatch)
		{
			TORRENT_ASSERT(m_alerts.empty());
			TORRENT_TRY {
				m_dispatch(alert_);
			} TORRENT_CATCH(std::exception&) {}
		}
		else if (m_alerts.size() < m_queue_size_limit || !alert_->discardable())
		{
			m_alerts.push_back(alert_.release());
		}
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void alert_manager::add_extension(boost::shared_ptr<plugin> ext)
	{
		m_ses_extensions.push_back(ext);
	}
#endif

	std::auto_ptr<alert> alert_manager::get()
	{
		mutex::scoped_lock lock(m_mutex);
		
		if (m_alerts.empty())
			return std::auto_ptr<alert>(0);

		alert* result = m_alerts.front();
		m_alerts.pop_front();
		return std::auto_ptr<alert>(result);
	}

	void alert_manager::get_all(std::deque<alert*>* alerts)
	{
		mutex::scoped_lock lock(m_mutex);
		if (m_alerts.empty()) return;
		m_alerts.swap(*alerts);
	}

	bool alert_manager::pending() const
	{
		mutex::scoped_lock lock(m_mutex);
		
		return !m_alerts.empty();
	}

	size_t alert_manager::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		mutex::scoped_lock lock(m_mutex);

		std::swap(m_queue_size_limit, queue_size_limit_);
		return queue_size_limit_;
	}

	stats_alert::stats_alert(torrent_handle const& h, int in
		, stat const& s)
		: torrent_alert(h)
		, interval(in)
	{
		for (int i = 0; i < num_channels; ++i)
			transferred[i] = s[i].counter();
	}

	std::string stats_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: [%d] %d %d %d %d %d %d %d %d %d %d"
			, torrent_alert::message().c_str()
			, interval
			, transferred[0]
			, transferred[1]
			, transferred[2]
			, transferred[3]
			, transferred[4]
			, transferred[5]
			, transferred[6]
			, transferred[7]
			, transferred[8]
			, transferred[9]);
		return msg;
	}

	cache_flushed_alert::cache_flushed_alert(torrent_handle const& h): torrent_alert(h) {}

	std::string anonymous_mode_alert::message() const
	{
		char msg[200];
		char const* msgs[] = {
			"tracker is not anonymous, set a proxy"
		};
		snprintf(msg, sizeof(msg), "%s: %s: %s"
			, torrent_alert::message().c_str()
			, msgs[kind], str.c_str());
		return msg;
	}

	std::string lsd_peer_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: received peer from local service discovery"
			, peer_alert::message().c_str());
		return msg;
	}

	std::string trackerid_alert::message() const
	{
		return "trackerid received: " + trackerid;
	}

	std::string dht_bootstrap_alert::message() const
	{
		return "DHT bootstrap complete";
	}

	std::string rss_alert::message() const
	{
		char msg[600];
		char const* state_msg[] = {"updating", "updated", "error"};
		snprintf(msg, sizeof(msg), "RSS feed %s: %s (%s)"
			, url.c_str(), state_msg[state], convert_from_native(error.message()).c_str());
		return msg;
	}

	std::string torrent_error_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), " ERROR: %s", convert_from_native(error.message()).c_str());
		return torrent_alert::message() + msg;
	}
	
	std::string torrent_added_alert::message() const
	{
		return torrent_alert::message() + " added";
	}

	std::string torrent_removed_alert::message() const
	{
		return torrent_alert::message() + " removed";
	}

	std::string torrent_need_cert_alert::message() const
	{
		return torrent_alert::message() + " needs SSL certificate";
	}

	std::string incoming_connection_alert::message() const
	{
		char msg[600];
		char const* type_str[] = {
			"null",
			"TCP",
			"Socks5/TCP",
			"HTTP",
			"uTP",
			"i2p",
			"SSL/TCP",
			"SSL/Socks5",
			"HTTPS",
			"SSL/uTP"
			};
		error_code ec;
		snprintf(msg, sizeof(msg), "incoming connection from %s (%s)"
			, print_endpoint(ip).c_str(), type_str[socket_type]);
		return msg;
	}

	std::string add_torrent_alert::message() const
	{
		char msg[600];
		if (error)
		{
			snprintf(msg, sizeof(msg), "failed to add torrent: %s"
				, convert_from_native(error.message()).c_str());
		}
		else
		{
			snprintf(msg, sizeof(msg), "added torrent: %s", !params.url.empty() ? params.url.c_str() : params.ti->name().c_str());
		}
		return msg;
	}

	std::string state_update_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "state updates for %d torrents", int(status.size()));
		return msg;
	}

	std::string peer_disconnected_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "%s disconnecting: [%s] %s", peer_alert::message().c_str()
			, error.category().name(), convert_from_native(error.message()).c_str());
		return msg;
	}

	std::string i2p_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "i2p_error: [%s] %s"
			, error.category().name(), convert_from_native(error.message()).c_str());
		return msg;
	}

} // namespace libtorrent

