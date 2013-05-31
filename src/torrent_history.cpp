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
#include "libtorrent/alert_handler.hpp"

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
			, torrent_update_alert::alert_type
			, 0);
	}

	torrent_history::~torrent_history()
	{
		m_alerts->unsubscribe(this);
	}

	void torrent_history::handle_alert(alert const* a)
	{
		add_torrent_alert const* ta = alert_cast<add_torrent_alert>(a);
		torrent_removed_alert const* td = alert_cast<torrent_removed_alert>(a);
		state_update_alert const* su = alert_cast<state_update_alert>(a);
		torrent_update_alert const* tu = alert_cast<torrent_update_alert>(a);
		if (tu)
		{
			mutex::scoped_lock l(m_mutex);

			// first remove the old hash
			m_removed.push_front(std::make_pair(m_frame + 1, tu->old_ih));
			torrent_history_entry st;
			st.status.info_hash = tu->old_ih;
			queue_t::right_iterator it = m_queue.right.find(st);
			TORRENT_ASSERT(it != m_queue.right.end());
			st = it->first;
			m_queue.right.erase(st);

			// then add the torrent under the new inf-hash
			st.status.info_hash = tu->new_ih;
			m_queue.left.push_front(std::make_pair(m_frame + 1, st));

			// weed out torrents that were removed a long time ago
			while (m_removed.size() > 1000 && m_removed.back().first < m_frame - 10)
				m_removed.pop_back();

			m_deferred_frame_count = true;
		}
		else if (ta)
		{
			torrent_status st = ta->handle.status();
			TORRENT_ASSERT(st.info_hash == st.handle.info_hash());
			TORRENT_ASSERT(st.handle == ta->handle);

			mutex::scoped_lock l(m_mutex);
			m_queue.left.push_front(std::make_pair(m_frame + 1, torrent_history_entry(st, m_frame + 1)));
			m_deferred_frame_count = true;
		}
		else if (td)
		{
			mutex::scoped_lock l(m_mutex);

			m_removed.push_front(std::make_pair(m_frame + 1, td->info_hash));
			torrent_history_entry st;
			st.status.info_hash = td->info_hash;
			m_queue.right.erase(st);
			// weed out torrents that were removed a long time ago
			while (m_removed.size() > 1000 && m_removed.back().first < m_frame - 10)
				m_removed.pop_back();

			m_deferred_frame_count = true;
		}
		else if (su)
		{
			mutex::scoped_lock l(m_mutex);

			++m_frame;
			m_deferred_frame_count = false;

			std::vector<torrent_status> const& st = su->status;
			for (std::vector<torrent_status>::const_iterator i = st.begin()
				, end(st.end()); i != end; ++i)
			{
				torrent_history_entry e;
				e.status.info_hash = i->info_hash;

				queue_t::right_iterator it = m_queue.right.find(e);
				if (it == m_queue.right.end()) continue;
				const_cast<torrent_history_entry&>(it->first).update_status(*i, m_frame);
				m_queue.right.replace_data(it, m_frame);
				// bump this torrent to the beginning of the list
				m_queue.left.relocate(m_queue.left.begin(), m_queue.project_left(it));
			}
/*
			printf("===== frame: %d =====\n", m_frame);
			for (queue_t::left_iterator i = m_queue.left.begin()
				, end(m_queue.left.end()); i != end; ++i)
			{
				i->second.debug_print(m_frame);
//				printf("%3d: (%s) %s\n", i->first, i->second.error.c_str(), i->second.name.c_str());
			}
*/
		}
	}

	void torrent_history::removed_since(int frame, std::vector<sha1_hash>& torrents) const
	{
		torrents.clear();
		mutex::scoped_lock l(m_mutex);
		for (std::deque<std::pair<int, sha1_hash> >::const_iterator i = m_removed.begin()
			, end(m_removed.end()); i != end; ++i)
		{
			if (i->first <= frame) break;
			torrents.push_back(i->second);
		}
	}

	void torrent_history::updated_since(int frame, std::vector<torrent_status>& torrents) const
	{
		mutex::scoped_lock l(m_mutex);
		for (queue_t::left_const_iterator i = m_queue.left.begin()
			, end(m_queue.left.end()); i != end; ++i)
		{
			if (i->first <= frame) break;
			torrents.push_back(i->second.status);
		}
	}

	void torrent_history::updated_fields_since(int frame, std::vector<torrent_history_entry>& torrents) const
	{
		mutex::scoped_lock l(m_mutex);
		for (queue_t::left_const_iterator i = m_queue.left.begin()
			, end(m_queue.left.end()); i != end; ++i)
		{
			if (i->first <= frame) break;
			torrents.push_back(i->second);
		}
	}

	torrent_status torrent_history::get_torrent_status(sha1_hash const& ih) const
	{
		torrent_history_entry st;
		st.status.info_hash = ih;

		mutex::scoped_lock l(m_mutex);

		queue_t::right_const_iterator it = m_queue.right.find(st);
		if (it != m_queue.right.end()) return it->first.status;
		return st.status;
	}

	int torrent_history::frame() const
	{
		mutex::scoped_lock l(m_mutex);
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
		CMP_SET(paused);
		CMP_SET(auto_managed);
		CMP_SET(sequential_download);
		CMP_SET(is_seeding);
		CMP_SET(is_finished);
		CMP_SET(is_loaded);
		CMP_SET(has_metadata);
		CMP_SET(progress);
		CMP_SET(progress_ppm);
		CMP_SET(error);
		CMP_SET(save_path);
		CMP_SET(name);
		CMP_SET(next_announce);
		CMP_SET(announce_interval);
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
		CMP_SET(total_wanted_done);
		CMP_SET(total_wanted);
		CMP_SET(distributed_full_copies);
		CMP_SET(distributed_fraction);
		CMP_SET(distributed_copies);
		CMP_SET(block_size);
		CMP_SET(num_uploads);
		CMP_SET(num_connections);
		CMP_SET(num_undead_peers);
		CMP_SET(uploads_limit);
		CMP_SET(connections_limit);
		CMP_SET(storage_mode);
		CMP_SET(up_bandwidth_queue);
		CMP_SET(down_bandwidth_queue);
		CMP_SET(all_time_upload);
		CMP_SET(all_time_download);
		CMP_SET(active_time);
		CMP_SET(finished_time);
		CMP_SET(seeding_time);
		CMP_SET(seed_rank);
		CMP_SET(last_scrape);
		CMP_SET(has_incoming);
		CMP_SET(sparse_regions);
		CMP_SET(seed_mode);
		CMP_SET(upload_mode);
		CMP_SET(share_mode);
		CMP_SET(super_seeding);
		CMP_SET(priority);
		CMP_SET(added_time);
		CMP_SET(completed_time);
		CMP_SET(last_seen_complete);
		CMP_SET(time_since_upload);
		CMP_SET(time_since_download);
		CMP_SET(queue_position);
		CMP_SET(need_save_resume);
		CMP_SET(ip_filter_applies);
		CMP_SET(listen_port);

		status = s;
	}

	char const* fmt(std::string const& s) { return s.c_str(); }
	int fmt(int v) { return v; }
	boost::uint64_t fmt(boost::uint64_t v) { return v; }
	boost::int64_t fmt(boost::int64_t v) { return v; }
#ifdef __APPLE__
	time_t fmt(time_t v) { return v; }
#endif
	float fmt(float v) { return v; }
	int fmt(bool v) { return v; }

	void torrent_history_entry::debug_print(int current_frame) const
	{
		int frame_diff;

#define PRINT(x, type) frame_diff = (std::min)(current_frame - frame[x], boost::uint32_t(20)); \
		printf("%s\x1b[38;5;%dm%" #type "\x1b[0m ", frame[x] >= current_frame  ? "\x1b[41m" : "", 255 - frame_diff, fmt(status.x));
	
		PRINT(state, d);
		PRINT(paused, d);
		PRINT(auto_managed, d);
		PRINT(sequential_download, d);
		PRINT(is_seeding, d);
		PRINT(is_finished, d);
		PRINT(is_loaded, d);
		PRINT(has_metadata, d);
		PRINT(progress, f);
		PRINT(progress_ppm, d);
		PRINT(error, s);
//		PRINT(save_path, s);
		PRINT(name, s);
//		PRINT(next_announce, d);
//		PRINT(announce_interval, d);
		PRINT(current_tracker, s);
		PRINT(total_download, lld);
		PRINT(total_upload, lld);
		PRINT(total_payload_download, lld);
		PRINT(total_payload_upload, lld);
		PRINT(total_failed_bytes, lld);
		PRINT(total_redundant_bytes, lld);
		PRINT(download_rate, d);
		PRINT(upload_rate, d);
		PRINT(download_payload_rate, d);
		PRINT(upload_payload_rate, d);
		PRINT(num_seeds, d);
		PRINT(num_peers, d);
		PRINT(num_complete, d);
		PRINT(num_incomplete, d);
		PRINT(list_seeds, d);
		PRINT(list_peers, d);
		PRINT(connect_candidates, d);
		PRINT(num_pieces, d);
		PRINT(total_done, lld);
		PRINT(total_wanted_done, lld);
		PRINT(total_wanted, lld);
		PRINT(distributed_full_copies, d);
		PRINT(distributed_fraction, d);
		PRINT(distributed_copies, f);
		PRINT(block_size, d);
		PRINT(num_uploads, d);
		PRINT(num_connections, d);
		PRINT(num_undead_peers, d);
		PRINT(uploads_limit, d);
		PRINT(connections_limit, d);
		PRINT(storage_mode, d);
		PRINT(up_bandwidth_queue, d);
		PRINT(down_bandwidth_queue, d);
		PRINT(all_time_upload, lld);
		PRINT(all_time_download, lld);
		PRINT(active_time, d);
		PRINT(finished_time, d);
		PRINT(seeding_time, d);
		PRINT(seed_rank, d);
		PRINT(last_scrape, d);
		PRINT(has_incoming, d);
		PRINT(sparse_regions, d);
		PRINT(seed_mode, d);
		PRINT(upload_mode, d);
		PRINT(share_mode, d);
		PRINT(super_seeding, d);
		PRINT(priority, d);
		PRINT(added_time, ld);
		PRINT(completed_time, ld);
		PRINT(last_seen_complete, ld);
		PRINT(time_since_upload, d);
		PRINT(time_since_download, d);
		PRINT(queue_position, d);
		PRINT(need_save_resume, d);
		PRINT(ip_filter_applies, d);
		PRINT(listen_port, d);

		printf("\x1b[0m\n");
	}
}

