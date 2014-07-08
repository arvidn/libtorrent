#include "session_view.hpp"
#include "print.hpp"

session_view::session_view()
{
	std::vector<lt::stats_metric> metrics = lt::session_stats_metrics();
	m_cnt[0].resize(metrics.size(), 0);
	m_cnt[1].resize(metrics.size(), 0);

	m_queued_bytes_idx = find_metric_idx(metrics, "disk.queued_write_bytes");
	m_wasted_bytes_idx = find_metric_idx(metrics, "net.recv_redundant_bytes");
	m_failed_bytes_idx = find_metric_idx(metrics, "net.recv_failed_bytes");
	m_num_peers_idx = find_metric_idx(metrics, "peer.num_peers_connected");
	m_recv_payload_idx = find_metric_idx(metrics, "net.recv_payload_bytes");
	m_sent_payload_idx = find_metric_idx(metrics, "net.sent_payload_bytes");
	m_unchoked_idx = find_metric_idx(metrics, "peer.num_peers_up_unchoked");
	m_unchoke_slots_idx = find_metric_idx(metrics, "ses.num_unchoke_slots");
	m_limiter_up_queue_idx = find_metric_idx(metrics, "net.limiter_up_queue");
	m_limiter_down_queue_idx = find_metric_idx(metrics, "net.limiter_down_queue");
	m_limiter_up_bytes_idx = find_metric_idx(metrics, "net.limiter_up_bytes");
	m_limiter_down_bytes_idx = find_metric_idx(metrics, "net.limiter_down_bytes");
	m_queued_writes_idx = find_metric_idx(metrics, "disk.num_write_jobs");
	m_queued_reads_idx = find_metric_idx(metrics, "disk.num_write_jobs");

	m_writes_cache_idx = find_metric_idx(metrics, "disk.write_cache_blocks");
	m_reads_cache_idx = find_metric_idx(metrics, "disk.read_cache_blocks");
	m_pinned_idx = find_metric_idx(metrics, "disk.pinned_blocks");
	m_num_blocks_read_idx = find_metric_idx(metrics, "disk.num_blocks_read");
	m_cache_hit_idx = find_metric_idx(metrics, "disk.num_blocks_cache_hits");
	m_blocks_in_use_idx = find_metric_idx(metrics, "disk.disk_blocks_in_use");
	m_blocks_written_idx = find_metric_idx(metrics, "disk.num_blocks_written");
	m_write_ops_idx = find_metric_idx(metrics, "disk.num_write_ops");
}

void session_view::set_pos(int pos)
{
	m_position = pos;
}

int session_view::pos() const { return m_position; }

int session_view::height() const
{
	return 2;
}

void session_view::render()
{
	char str[1024];
	int pos = 0;

	int y = m_position;

	float seconds = (m_timestamp[0] - m_timestamp[1]) / 1000000.f;

	int download_rate = (m_cnt[0][m_recv_payload_idx] - m_cnt[1][m_recv_payload_idx])
		/ seconds;
	int upload_rate = (m_cnt[0][m_sent_payload_idx] - m_cnt[1][m_sent_payload_idx])
		/ seconds;

	pos += snprintf(str, sizeof(str), "| conns: %d down: %s (%s) up: %s (%s)\x1b[K"
		, int(m_cnt[0][m_num_peers_idx])
		, color(add_suffix(download_rate, "/s"), col_green).c_str()
		, color(add_suffix(m_cnt[0][m_recv_payload_idx]), col_green).c_str()
		, color(add_suffix(upload_rate, "/s"), col_red).c_str()
		, color(add_suffix(m_cnt[0][m_sent_payload_idx]), col_red).c_str());

	set_cursor_pos(0, y++);
	print(str);

	snprintf(str, sizeof(str), "| waste: %s fail: %s unchoked: %d / %d "
		"bw queues: %8d (%d) | %8d (%d) disk queues: %d | %d cache: w: %d%% r: %d%% "
		"size: w: %s r: %s total: %s\x1b[K"
		, add_suffix(m_cnt[0][m_wasted_bytes_idx]).c_str()
		, add_suffix(m_cnt[0][m_failed_bytes_idx]).c_str()
		, int(m_cnt[0][m_unchoked_idx])
		, int(m_cnt[0][m_unchoke_slots_idx])
		, int(m_cnt[0][m_limiter_up_bytes_idx])
		, int(m_cnt[0][m_limiter_up_queue_idx])
		, int(m_cnt[0][m_limiter_down_bytes_idx])
		, int(m_cnt[0][m_limiter_down_queue_idx])
		, int(m_cnt[0][m_queued_writes_idx])
		, int(m_cnt[0][m_queued_reads_idx])
		, int((m_cnt[0][m_blocks_written_idx] - m_cnt[0][m_write_ops_idx]) * 100
			/ (std::max)(boost::uint64_t(1), m_cnt[0][m_blocks_written_idx]))
		, int(m_cnt[0][m_cache_hit_idx] * 100
			/ (std::max)(boost::uint64_t(1), m_cnt[0][m_num_blocks_read_idx]))
		, add_suffix(m_cnt[0][m_writes_cache_idx] * 16 * 1024).c_str()
		, add_suffix(m_cnt[0][m_reads_cache_idx] * 16 * 1024).c_str()
		, add_suffix(m_cnt[0][m_blocks_in_use_idx] * 16 * 1024).c_str()
		);
	set_cursor_pos(0, y++);
	print(str);

/*
	snprintf(str, sizeof(str), "| timing - "
		" read: %6d ms | write: %6d ms | hash: %6d"
		, cs.average_read_time / 1000, cs.average_write_time / 1000
		, cs.average_hash_time / 1000);

	set_cursor_pos(0, y++);
	print(str);

	snprintf(str, sizeof(str), "| jobs   - queued: %4d (%4d) pending: %4d blocked: %4d "
		"queued-bytes: %5" PRId64 " kB"
		, cs.queued_jobs, cs.peak_queued, cs.pending_jobs, cs.blocked_jobs
		, m_cnt[0][m_queued_bytes_idx] / 1000);

	set_cursor_pos(0, y++);
	print(str);

	snprintf(str, sizeof(str), "|  cache  - total: %4d read: %4d write: %4d pinned: %4d write-queue: %4d"
		, cs.read_cache_size + cs.write_cache_size, cs.read_cache_size
		, cs.write_cache_size, cs.pinned_blocks
		, int(m_cnt[0][m_queued_bytes_idx] / 0x4000));
	set_cursor_pos(0, y++);
	print(str);

	int mru_size = cs.arc_mru_size + cs.arc_mru_ghost_size;
	int mfu_size = cs.arc_mfu_size + cs.arc_mfu_ghost_size;
	int arc_size = mru_size + mfu_size;

	snprintf(str, sizeof(str), "LRU: (%d) %d LFU: %d (%d)\n"
		, cs.arc_mru_ghost_size, cs.arc_mru_size
		, cs.arc_mfu_size, cs.arc_mfu_ghost_size);

	set_cursor_pos(0, y++);
	print(str);

	str[0] = '\0';
	if (arc_size > 0)
	{
		pos = snprintf(str, sizeof(str), " ");
		if (mru_size > 0)
		{
			pos = snprintf(str + pos, sizeof(str) - pos, "%s"
				, progress_bar(cs.arc_mru_ghost_size * 1000 / mru_size
					, mru_size * (terminal_width-3) / arc_size, col_yellow, '-', '#').c_str());
		}
		pos = snprintf(str, sizeof(str), "|");
		if (mfu_size)
		{
			pos = snprintf(str + pos, sizeof(str) - pos, "%s"
				, progress_bar(cs.arc_mfu_size * 1000 / mfu_size
					, mfu_size * (terminal_width-3) / arc_size, col_green, '=', '-').c_str());
		}
	}
	set_cursor_pos(0, y++);
	print(str);
*/
}

void session_view::update_counters(std::vector<boost::uint64_t>& stats_counters
	, boost::uint64_t t)
{
	// only update the previous counters if there's been enough
	// time since it was last updated
	if (t - m_timestamp[1] > 2000000)
	{
		m_cnt[1].swap(m_cnt[0]);
		m_timestamp[1] = m_timestamp[0];
	}

	m_cnt[0].swap(stats_counters);
	m_timestamp[0] = t;
	render();
}

