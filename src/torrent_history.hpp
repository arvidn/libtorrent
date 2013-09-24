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

#ifndef TORRENT_TORRENT_HISTORY_HPP
#define TORRENT_TORRENT_HISTORY_HPP

#include "libtorrent/alert_observer.hpp"
#include "libtorrent/torrent_handle.hpp"
#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <deque>

namespace libtorrent
{
	struct alert_handler;

	// this is the type that keeps track of frame counters for each
	// field in torrent_status. The frame counters indicate which frame
	// they were last modified in. This is used to send minimal updates
	// of changes to torrents.
	struct torrent_history_entry
	{
		// this is the current state of the torrent
		torrent_status status;

		void update_status(torrent_status const& s, int frame);

		bool operator==(torrent_history_entry const& e) const { return e.status.info_hash == status.info_hash; }

		enum
		{
			state,
			paused,
			auto_managed,
			sequential_download,
			is_seeding,
			is_finished,
			is_loaded,
			has_metadata,
			progress,
			progress_ppm,
			error,
			save_path,
			name,
			next_announce,
			announce_interval,
			current_tracker,
			total_download,
			total_upload,
			total_payload_download,
			total_payload_upload,
			total_failed_bytes,
			total_redundant_bytes,
			download_rate,
			upload_rate,
			download_payload_rate,
			upload_payload_rate,
			num_seeds,
			num_peers,
			num_complete,
			num_incomplete,
			list_seeds,
			list_peers,
			connect_candidates,
			num_pieces,
			total_done,
			total_wanted_done,
			total_wanted,
			distributed_full_copies,
			distributed_fraction,
			distributed_copies,
			block_size,
			num_uploads,
			num_connections,
			num_undead_peers,
			uploads_limit,
			connections_limit,
			storage_mode,
			up_bandwidth_queue,
			down_bandwidth_queue,
			all_time_upload,
			all_time_download,
			active_time,
			finished_time,
			seeding_time,
			seed_rank,
			last_scrape,
			has_incoming,
			sparse_regions,
			seed_mode,
			upload_mode,
			share_mode,
			super_seeding,
			priority,
			added_time,
			completed_time,
			last_seen_complete,
			time_since_upload,
			time_since_download,
			queue_position,
			need_save_resume,
			ip_filter_applies,
			listen_port,

			num_fields,
		};

		// these are the frames each individual field was last changed
		int frame[num_fields];

		torrent_history_entry() {}

		torrent_history_entry(torrent_status const& st, int f)
			: status(st)
		{
			for (int i = 0; i < num_fields; ++i)
				frame[i] = f;
		}

		void debug_print(int current_frame) const;
	};

	inline std::size_t hash_value(torrent_history_entry const& te)
	{ return hash_value(te.status.info_hash); }

	struct torrent_history : alert_observer
	{

		torrent_history(alert_handler* h);
		~torrent_history();

		// returns the info-hashes of the torrents that have been
		// removed since the specified frame number
		void removed_since(int frame, std::vector<sha1_hash>& torrents) const;

		// returns the torrent_status structures for the torrents
		// that have changed since the specified frame number
		void updated_since(int frame, std::vector<torrent_status>& torrents) const;

		void updated_fields_since(int frame, std::vector<torrent_history_entry>& torrents) const;

		torrent_status get_torrent_status(sha1_hash const& ih) const;

		// the current frame number
		int frame() const;

		virtual void handle_alert(alert const* a);

	private:	

		// first is the frame this torrent was last
		// seen modified in, second is the information
		// about the torrent that was modified
		typedef boost::bimap<boost::bimaps::list_of<int>
			, boost::bimaps::unordered_set_of<torrent_history_entry> > queue_t;

		mutable mutex m_mutex;

		queue_t m_queue;

		std::deque<std::pair<int, sha1_hash> > m_removed;

		alert_handler* m_alerts;

		// frame counter. This is incremented every
		// time we get a status update for torrents
		mutable int m_frame;

		// if we haven't gotten any status updates
		// but we have received add or delete alerts,
		// we increment the frame counter on access,
		// in order to make added and deleted event also
		// fall into distinct time-slots, instead of being
		// potentially returned twice, once when they
		// happen and once after we've received an
		// update and increment the frame counter
		mutable bool m_deferred_frame_count;
	};
}

#endif

