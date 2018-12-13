/*

Copyright (c) 2012, Arvid Norberg
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

#include "torrent_history.hpp"
#include "libtorrent/alert_types.hpp"
#include "alert_handler.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/flags.hpp"
#include <cinttypes>
#include <chrono>

namespace libtorrent
{
	torrent_history::torrent_history(alert_handler* h)
		: m_alerts(h)
		, m_frame(1)
		, m_deferred_frame_count(false)
	{
		m_alerts->subscribe(this, 0
			, add_torrent_alert::alert_type
			, torrent_removed_alert::alert_type
			, state_update_alert::alert_type
			, 0);
	}

	torrent_history::~torrent_history()
	{
		m_alerts->unsubscribe(this);
	}

	void torrent_history::handle_alert(alert const* a) try
	{
		if (add_torrent_alert const* ta = alert_cast<add_torrent_alert>(a))
		{
			torrent_status st = ta->handle.status();
			TORRENT_ASSERT(st.info_hash == st.handle.info_hash());
			TORRENT_ASSERT(st.handle == ta->handle);

			std::unique_lock<std::mutex> l(m_mutex);
			m_queue.left.push_front(std::make_pair(m_frame + 1, torrent_history_entry(std::move(st), m_frame + 1)));
			m_deferred_frame_count = true;
		}
		else if (torrent_removed_alert const* td = alert_cast<torrent_removed_alert>(a))
		{
			std::unique_lock<std::mutex> l(m_mutex);

			m_removed.push_front(std::make_pair(m_frame + 1, td->info_hash));
			torrent_history_entry st;
			st.status.info_hash = td->info_hash;
			m_queue.right.erase(st);
			// weed out torrents that were removed a long time ago
			while (m_removed.size() > 1000 && m_removed.back().first < m_frame - 10)
				m_removed.pop_back();

			m_deferred_frame_count = true;
		}
		else if (state_update_alert const* su = alert_cast<state_update_alert>(a))
		{
			std::unique_lock<std::mutex> l(m_mutex);

			++m_frame;
			m_deferred_frame_count = false;

			std::vector<torrent_status> const& st = su->status;
			for (auto const& t : st)
			{
				torrent_history_entry e;
				e.status.info_hash = t.info_hash;

				queue_t::right_iterator it = m_queue.right.find(e);
				if (it == m_queue.right.end()) continue;
				const_cast<torrent_history_entry&>(it->first).update_status(t, m_frame);
				m_queue.right.replace_data(it, m_frame);
				// bump this torrent to the beginning of the list
				m_queue.left.relocate(m_queue.left.begin(), m_queue.project_left(it));
			}
/*
			printf("===== frame: %d =====\n", m_frame);
			for (auto const& e : m_queue.left)
			{
				e.second.debug_print(m_frame);
//				printf("%3d: (%s) %s\n", e.first, e.second.error.c_str(), e.second.name.c_str());
			}
*/
		}
	}
	catch (std::exception const&) {}

	void torrent_history::removed_since(frame_t frame, std::vector<sha1_hash>& torrents) const
	{
		torrents.clear();
		std::unique_lock<std::mutex> l(m_mutex);
		for (auto const& e : m_removed)
		{
			if (e.first <= frame) break;
			torrents.push_back(e.second);
		}
	}

	void torrent_history::updated_since(frame_t frame, std::vector<torrent_status>& torrents) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		for (auto const& e : m_queue.left)
		{
			if (e.first <= frame) break;
			torrents.push_back(e.second.status);
		}
	}

	void torrent_history::updated_fields_since(frame_t frame, std::vector<torrent_history_entry>& torrents) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		for (auto const& e : m_queue.left)
		{
			if (e.first <= frame) break;
			torrents.push_back(e.second);
		}
	}

	torrent_status torrent_history::get_torrent_status(sha1_hash const& ih) const
	{
		torrent_history_entry st;
		st.status.info_hash = ih;

		std::unique_lock<std::mutex> l(m_mutex);

		queue_t::right_const_iterator it = m_queue.right.find(st);
		if (it != m_queue.right.end()) return it->first.status;
		return st.status;
	}

	frame_t torrent_history::frame() const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		if (m_deferred_frame_count)
		{
			m_deferred_frame_count = false;
			++m_frame;
		}
		return m_frame;
	}

	void torrent_history_entry::update_status(torrent_status const& s, int f)
	{
#define CMP_SET(x) if (s.x != status.x) frame[int(x)] = f

		CMP_SET(state);
		CMP_SET(flags);
		CMP_SET(is_seeding);
		CMP_SET(is_finished);
		CMP_SET(has_metadata);
		CMP_SET(progress);
		CMP_SET(progress_ppm);
		CMP_SET(errc);
		CMP_SET(error_file);
		CMP_SET(save_path);
		CMP_SET(name);
		CMP_SET(next_announce);
		CMP_SET(current_tracker);
		CMP_SET(total_download);
		CMP_SET(total_upload);
		CMP_SET(total_payload_download);
		CMP_SET(total_payload_upload);
		CMP_SET(total_failed_bytes);
		CMP_SET(total_redundant_bytes);
		CMP_SET(download_rate);
		CMP_SET(upload_rate);
		CMP_SET(download_payload_rate);
		CMP_SET(upload_payload_rate);
		CMP_SET(num_seeds);
		CMP_SET(num_peers);
		CMP_SET(num_complete);
		CMP_SET(num_incomplete);
		CMP_SET(list_seeds);
		CMP_SET(list_peers);
		CMP_SET(connect_candidates);
		CMP_SET(num_pieces);
		CMP_SET(total_done);
		CMP_SET(total);
		CMP_SET(total_wanted_done);
		CMP_SET(total_wanted);
		CMP_SET(distributed_full_copies);
		CMP_SET(distributed_fraction);
		CMP_SET(block_size);
		CMP_SET(num_uploads);
		CMP_SET(num_connections);
		CMP_SET(uploads_limit);
		CMP_SET(connections_limit);
		CMP_SET(storage_mode);
		CMP_SET(up_bandwidth_queue);
		CMP_SET(down_bandwidth_queue);
		CMP_SET(all_time_upload);
		CMP_SET(all_time_download);
		CMP_SET(active_duration);
		CMP_SET(finished_duration);
		CMP_SET(seeding_duration);
		CMP_SET(seed_rank);
		CMP_SET(has_incoming);
		CMP_SET(added_time);
		CMP_SET(completed_time);
		CMP_SET(last_seen_complete);
		CMP_SET(last_upload);
		CMP_SET(last_download);
		CMP_SET(queue_position);
		CMP_SET(moving_storage);
		CMP_SET(announcing_to_trackers);
		CMP_SET(announcing_to_lsd);
		CMP_SET(announcing_to_dht);

		status = s;
	}

	char const* fmt(std::string const& s) { return s.c_str(); }
	float fmt(float v) { return v; }
	std::int64_t fmt(bool v) { return v; }
	template <class T>
	typename std::enable_if<std::is_integral<T>::value, std::int64_t>::type
		fmt(T v) { return std::uint64_t(v); }
	template <typename T, typename V>
	T fmt(lt::flags::bitfield_flag<T, V> const f)
	{ return static_cast<T>(f); }
	template <typename T, typename V>
	T fmt(lt::aux::strong_typedef<T, V> const v)
	{ return static_cast<T>(v); }
	std::int64_t fmt(lt::time_duration const d)
	{ return std::chrono::duration_cast<seconds>(d).count(); }
	std::int64_t fmt(lt::time_point const t)
	{ return std::chrono::duration_cast<seconds>(t.time_since_epoch()).count(); }
	char const* fmt(lt::error_code const& ec)
	{
		static std::string storage;
		storage = ec.message();
		return storage.c_str();
	}
	int fmt(lt::storage_mode_t const s)
	{ return static_cast<int>(s); }
	int fmt(lt::torrent_status::state_t const s)
	{ return static_cast<int>(s); }

	void torrent_history_entry::debug_print(int current_frame) const
	{
		int frame_diff;

#define PRINT(x, type) frame_diff = (std::min)(current_frame - frame[x], 20); \
		printf("%s\x1b[38;5;%dm%" type "\x1b[0m ", frame[x] >= current_frame  ? "\x1b[41m" : "", 255 - frame_diff, fmt(status.x));

		PRINT(state, "d");
		PRINT(flags, PRId64);
		PRINT(is_seeding, PRId64);
		PRINT(is_finished, PRId64);
		PRINT(has_metadata, PRId64);
		PRINT(progress, "f");
		PRINT(progress_ppm, PRId64);
		PRINT(errc, "s");
		PRINT(error_file, "d");
//		PRINT(save_path, "s");
		PRINT(name, "s");
//		PRINT(next_announce, PRId64);
		PRINT(current_tracker, "s");
		PRINT(total_download, PRId64);
		PRINT(total_upload, PRId64);
		PRINT(total_payload_download, PRId64);
		PRINT(total_payload_upload, PRId64);
		PRINT(total_failed_bytes, PRId64);
		PRINT(total_redundant_bytes, PRId64);
		PRINT(download_rate, PRId64);
		PRINT(upload_rate, PRId64);
		PRINT(download_payload_rate, PRId64);
		PRINT(upload_payload_rate, PRId64);
		PRINT(num_seeds, PRId64);
		PRINT(num_peers, PRId64);
		PRINT(num_complete, PRId64);
		PRINT(num_incomplete, PRId64);
		PRINT(list_seeds, PRId64);
		PRINT(list_peers, PRId64);
		PRINT(connect_candidates, PRId64);
		PRINT(num_pieces, PRId64);
		PRINT(total_done, PRId64);
		PRINT(total, PRId64);
		PRINT(total_wanted_done, PRId64);
		PRINT(total_wanted, PRId64);
		PRINT(distributed_full_copies, PRId64);
		PRINT(distributed_fraction, PRId64);
		PRINT(block_size, PRId64);
		PRINT(num_uploads, PRId64);
		PRINT(num_connections, PRId64);
		PRINT(uploads_limit, PRId64);
		PRINT(connections_limit, PRId64);
		PRINT(storage_mode, "d");
		PRINT(up_bandwidth_queue, PRId64);
		PRINT(down_bandwidth_queue, PRId64);
		PRINT(all_time_upload, PRId64);
		PRINT(all_time_download, PRId64);
		PRINT(active_duration, PRId64);
		PRINT(finished_duration, PRId64);
		PRINT(seeding_duration, PRId64);
		PRINT(seed_rank, PRId64);
		PRINT(has_incoming, PRId64);
		PRINT(added_time, PRId64);
		PRINT(completed_time, PRId64);
		PRINT(last_seen_complete, PRId64);
		PRINT(last_upload, PRId64);
		PRINT(last_download, PRId64);
		PRINT(queue_position, "d");
		PRINT(moving_storage, PRId64);
		PRINT(announcing_to_trackers, PRId64);
		PRINT(announcing_to_lsd, PRId64);
		PRINT(announcing_to_dht, PRId64);

		printf("\x1b[0m\n");
	}
}

