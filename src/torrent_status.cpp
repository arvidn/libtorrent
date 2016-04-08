/*

Copyright (c) 2015-2016, Arvid Norberg
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

#include "libtorrent/torrent_status.hpp"

namespace libtorrent
{
	torrent_status::torrent_status()
		: error_file(torrent_status::error_file_none)
		, next_announce(seconds(0))
		, total_download(0)
		, total_upload(0)
		, total_payload_download(0)
		, total_payload_upload(0)
		, total_failed_bytes(0)
		, total_redundant_bytes(0)
		, total_done(0)
		, total_wanted_done(0)
		, total_wanted(0)
		, all_time_upload(0)
		, all_time_download(0)
		, added_time(0)
		, completed_time(0)
		, last_seen_complete(0)
		, storage_mode(storage_mode_sparse)
		, progress(0.f)
		, progress_ppm(0)
		, queue_position(0)
		, download_rate(0)
		, upload_rate(0)
		, download_payload_rate(0)
		, upload_payload_rate(0)
		, num_seeds(0)
		, num_peers(0)
		, num_complete(-1)
		, num_incomplete(-1)
		, list_seeds(0)
		, list_peers(0)
		, connect_candidates(0)
		, num_pieces(0)
		, distributed_full_copies(0)
		, distributed_fraction(0)
		, distributed_copies(0.f)
		, block_size(0)
		, num_uploads(0)
		, num_connections(0)
		, uploads_limit(0)
		, connections_limit(0)
		, up_bandwidth_queue(0)
		, down_bandwidth_queue(0)
		, time_since_upload(0)
		, time_since_download(0)
		, active_time(0)
		, finished_time(0)
		, seeding_time(0)
		, seed_rank(0)
		, last_scrape(0)
		, priority(0)
		, state(checking_resume_data)
		, need_save_resume(false)
		, ip_filter_applies(true)
		, upload_mode(false)
		, share_mode(false)
		, super_seeding(false)
		, paused(false)
		, auto_managed(false)
		, sequential_download(false)
		, is_seeding(false)
		, is_finished(false)
		, has_metadata(false)
		, has_incoming(false)
		, seed_mode(false)
		, moving_storage(false)
		, is_loaded(true)
		, announcing_to_trackers(false)
		, announcing_to_lsd(false)
		, announcing_to_dht(false)
		, stop_when_ready(false)
		, info_hash(0)
	{}

	torrent_status::~torrent_status() {}

}

