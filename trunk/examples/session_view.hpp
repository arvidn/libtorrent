#ifndef SESSION_VIEW_HPP_
#define SESSION_VIEW_HPP_

#include "libtorrent/session.hpp"

namespace lt = libtorrent;

struct session_view
{
	session_view();

	void set_pos(int pos);

	int pos() const;

	int height() const;

	void render();

	void print_utp_stats(bool p) { m_print_utp_stats = p; }
	bool print_utp_stats() const { return m_print_utp_stats; }

	void update_counters(std::vector<boost::uint64_t>& stats_counters
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

