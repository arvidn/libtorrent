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

#include <boost/cstdint.hpp>
#include <vector>

struct session_view
{
	session_view();

	void set_pos(int pos);

	int pos() const;

	int height() const;

	void render();

	void print_utp_stats(bool p) { m_print_utp_stats = p; }
	bool print_utp_stats() const { return m_print_utp_stats; }

	void update_counters(boost::uint64_t* stats_counters, int num_cnt
		, boost::uint64_t t);

private:

	int m_position;
	int m_width;

	// there are two sets of counters. the current one and the last one. This
	// is used to calculate rates
	std::vector<boost::uint64_t> m_cnt[2];

	// the timestamps of the counters in m_cnt[0] and m_cnt[1]
	// respectively. The timestamps are microseconds since session start
	boost::uint64_t m_timestamp[2];

	bool m_print_utp_stats;

	int m_queued_bytes_idx;
	int m_wasted_bytes_idx;
	int m_failed_bytes_idx;
	int m_num_peers_idx;
	int m_recv_payload_idx;
	int m_sent_payload_idx;
	int m_unchoked_idx;
	int m_unchoke_slots_idx;
	int m_limiter_up_queue_idx;
	int m_limiter_down_queue_idx;
	int m_queued_writes_idx;
	int m_queued_reads_idx;
	int m_writes_cache_idx;
	int m_reads_cache_idx;
	int m_pinned_idx;
	int m_num_blocks_read_idx;
	int m_cache_hit_idx;
	int m_blocks_in_use_idx;
	int m_blocks_written_idx;
	int m_write_ops_idx;

	int m_mfu_size_idx;
	int m_mfu_ghost_idx;
	int m_mru_size_idx;
	int m_mru_ghost_idx;

	int m_utp_idle;
	int m_utp_syn_sent;
	int m_utp_connected;
	int m_utp_fin_sent;
	int m_utp_close_wait;
};

#endif

