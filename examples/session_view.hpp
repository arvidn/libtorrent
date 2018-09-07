/*

Copyright (c) 2003-2017, Arvid Norberg
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

#ifndef SESSION_VIEW_HPP_
#define SESSION_VIEW_HPP_

#include <cstdint>
#include <vector>

#include "libtorrent/session_stats.hpp"
#include "libtorrent/span.hpp"

struct session_view
{
	session_view();

	void set_pos(int pos);
	void set_width(int width);

	int pos() const;

	int height() const;

	void render();

	void update_counters(lt::span<std::int64_t const> stats_counters, std::uint64_t t);

private:

	int m_position;
	int m_width;

	// there are two sets of counters. the current one and the last one. This
	// is used to calculate rates
	std::vector<std::int64_t> m_cnt[2];

	// the timestamps of the counters in m_cnt[0] and m_cnt[1]
	// respectively. The timestamps are microseconds since session start
	std::uint64_t m_timestamp[2];

	int const m_queued_bytes_idx = lt::find_metric_idx("disk.queued_write_bytes");
	int const m_wasted_bytes_idx = lt::find_metric_idx("net.recv_redundant_bytes");
	int const m_failed_bytes_idx = lt::find_metric_idx("net.recv_failed_bytes");
	int const m_num_peers_idx = lt::find_metric_idx("peer.num_peers_connected");
	int const m_recv_idx = lt::find_metric_idx("net.recv_bytes");
	int const m_sent_idx = lt::find_metric_idx("net.sent_bytes");
	int const m_unchoked_idx = lt::find_metric_idx("peer.num_peers_up_unchoked");
	int const m_unchoke_slots_idx = lt::find_metric_idx("ses.num_unchoke_slots");
	int const m_limiter_up_queue_idx = lt::find_metric_idx("net.limiter_up_queue");
	int const m_limiter_down_queue_idx = lt::find_metric_idx("net.limiter_down_queue");
	int const m_queued_writes_idx = lt::find_metric_idx("disk.num_write_jobs");
	int const m_queued_reads_idx = lt::find_metric_idx("disk.num_read_jobs");

	int const m_writes_cache_idx = lt::find_metric_idx("disk.write_cache_blocks");
	int const m_reads_cache_idx = lt::find_metric_idx("disk.read_cache_blocks");
	int const m_pinned_idx = lt::find_metric_idx("disk.pinned_blocks");
	int const m_num_blocks_read_idx = lt::find_metric_idx("disk.num_blocks_read");
	int const m_cache_hit_idx = lt::find_metric_idx("disk.num_blocks_cache_hits");
	int const m_blocks_in_use_idx = lt::find_metric_idx("disk.disk_blocks_in_use");
	int const m_blocks_written_idx = lt::find_metric_idx("disk.num_blocks_written");
	int const m_write_ops_idx = lt::find_metric_idx("disk.num_write_ops");

	int const m_mfu_size_idx = lt::find_metric_idx("disk.arc_mfu_size");
	int const m_mfu_ghost_idx = lt::find_metric_idx("disk.arc_mfu_ghost_size");
	int const m_mru_size_idx = lt::find_metric_idx("disk.arc_mru_size");
	int const m_mru_ghost_idx = lt::find_metric_idx("disk.arc_mru_ghost_size");

	int const m_utp_idle = lt::find_metric_idx("utp.num_utp_idle");
	int const m_utp_syn_sent = lt::find_metric_idx("utp.num_utp_syn_sent");
	int const m_utp_connected = lt::find_metric_idx("utp.num_utp_connected");
	int const m_utp_fin_sent = lt::find_metric_idx("utp.num_utp_fin_sent");
	int const m_utp_close_wait = lt::find_metric_idx("utp.num_utp_close_wait");
};

#endif

