#include "session_view.hpp"
#include "print.hpp"

#include <algorithm> // for std::max

session_view::session_view()
	: m_position(0)
	, m_print_utp_stats(false)
{
	using libtorrent::find_metric_idx;

	m_width = 128;

	std::vector<lt::stats_metric> metrics = lt::session_stats_metrics();
	m_cnt[0].resize(metrics.size(), 0);
	m_cnt[1].resize(metrics.size(), 0);

	m_queued_bytes_idx = find_metric_idx("disk.queued_write_bytes");
	m_wasted_bytes_idx = find_metric_idx("net.recv_redundant_bytes");
	m_failed_bytes_idx = find_metric_idx("net.recv_failed_bytes");
	m_num_peers_idx = find_metric_idx("peer.num_peers_connected");
	m_recv_payload_idx = find_metric_idx("net.recv_payload_bytes");
	m_sent_payload_idx = find_metric_idx("net.sent_payload_bytes");
	m_unchoked_idx = find_metric_idx("peer.num_peers_up_unchoked");
	m_unchoke_slots_idx = find_metric_idx("ses.num_unchoke_slots");
	m_limiter_up_queue_idx = find_metric_idx("net.limiter_up_queue");
	m_limiter_down_queue_idx = find_metric_idx("net.limiter_down_queue");
	m_queued_writes_idx = find_metric_idx("disk.num_write_jobs");
	m_queued_reads_idx = find_metric_idx("disk.num_read_jobs");

	m_writes_cache_idx = find_metric_idx("disk.write_cache_blocks");
	m_reads_cache_idx = find_metric_idx("disk.read_cache_blocks");
	m_pinned_idx = find_metric_idx("disk.pinned_blocks");
	m_num_blocks_read_idx = find_metric_idx("disk.num_blocks_read");
	m_cache_hit_idx = find_metric_idx("disk.num_blocks_cache_hits");
	m_blocks_in_use_idx = find_metric_idx("disk.disk_blocks_in_use");
	m_blocks_written_idx = find_metric_idx("disk.num_blocks_written");
	m_write_ops_idx = find_metric_idx("disk.num_write_ops");

	m_mfu_size_idx = find_metric_idx("disk.arc_mfu_size");
	m_mfu_ghost_idx = find_metric_idx("disk.arc_mfu_ghost_size");
	m_mru_size_idx = find_metric_idx("disk.arc_mru_size");
	m_mru_ghost_idx = find_metric_idx("disk.arc_mru_ghost_size");

	m_utp_idle = find_metric_idx("utp.num_utp_idle");
	m_utp_syn_sent = find_metric_idx("utp.num_utp_syn_sent");
	m_utp_connected = find_metric_idx("utp.num_utp_connected");
	m_utp_fin_sent = find_metric_idx("utp.num_utp_fin_sent");
	m_utp_close_wait = find_metric_idx("utp.num_utp_close_wait");
}

void session_view::set_pos(int pos)
{
	m_position = pos;
}

int session_view::pos() const { return m_position; }

int session_view::height() const
{
	return 3 + m_print_utp_stats;
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

	pos += snprintf(str, sizeof(str), "%s%s fail: %s down: %s (%s) "
		"  bw queue: %s | %s conns: %3d  unchoked: %2d / %2d "
		"                                      %s\x1b[K"
		, esc("48;5;238")
		, esc("1")
		, add_suffix(m_cnt[0][m_failed_bytes_idx]).c_str()
		, color(add_suffix(download_rate, "/s"), col_green).c_str()
		, color(add_suffix(m_cnt[0][m_recv_payload_idx]), col_green).c_str()
		, color(to_string(m_cnt[0][m_limiter_up_queue_idx], 3), col_red).c_str()
		, color(to_string(m_cnt[0][m_limiter_down_queue_idx], 3), col_green).c_str()
		, int(m_cnt[0][m_num_peers_idx])
		, int(m_cnt[0][m_unchoked_idx])
		, int(m_cnt[0][m_unchoke_slots_idx])
		, esc("0"));

	set_cursor_pos(0, y++);
	print(str);

	snprintf(str, sizeof(str), "%s%swaste: %s   up: %s (%s) "
		"disk queue: %s | %s cache w: %3d%% r: %3d%% "
		"size: w: %s r: %s total: %s       %s\x1b[K"
#ifdef _WIN32
		, esc("40")
#else
		, esc("48;5;238")
#endif
		, esc("1")
		, add_suffix(m_cnt[0][m_wasted_bytes_idx]).c_str()
		, color(add_suffix(upload_rate, "/s"), col_red).c_str()
		, color(add_suffix(m_cnt[0][m_sent_payload_idx]), col_red).c_str()
		, color(to_string(m_cnt[0][m_queued_reads_idx], 3), col_red).c_str()
		, color(to_string(m_cnt[0][m_queued_writes_idx], 3), col_green).c_str()
		, int((m_cnt[0][m_blocks_written_idx] - m_cnt[0][m_write_ops_idx]) * 100
			/ (std::max)(boost::uint64_t(1), m_cnt[0][m_blocks_written_idx]))
		, int(m_cnt[0][m_cache_hit_idx] * 100
			/ (std::max)(boost::uint64_t(1), m_cnt[0][m_num_blocks_read_idx]))
		, add_suffix(m_cnt[0][m_writes_cache_idx] * 16 * 1024).c_str()
		, add_suffix(m_cnt[0][m_reads_cache_idx] * 16 * 1024).c_str()
		, add_suffix(m_cnt[0][m_blocks_in_use_idx] * 16 * 1024).c_str()
		, esc("0"));
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
*/
	int mru_size = int(m_cnt[0][m_mru_size_idx] + m_cnt[0][m_mru_ghost_idx]);
	int mfu_size = int(m_cnt[0][m_mfu_size_idx] + m_cnt[0][m_mfu_ghost_idx]);
	int arc_size = mru_size + mfu_size;

	char mru_caption[100];
	snprintf(mru_caption, sizeof(mru_caption), "MRU: %d (%d)"
		, int(m_cnt[0][m_mru_size_idx]), int(m_cnt[0][m_mru_ghost_idx]));
	char mfu_caption[100];
	snprintf(mfu_caption, sizeof(mfu_caption), "MFU: %d (%d)"
		, int(m_cnt[0][m_mfu_size_idx]), int(m_cnt[0][m_mfu_ghost_idx]));

	pos = snprintf(str, sizeof(str), "cache: ");
	if (arc_size > 0)
	{
		if (mru_size > 0)
		{
			pos += snprintf(str + pos, sizeof(str) - pos, "%s"
				, progress_bar(m_cnt[0][m_mru_ghost_idx] * 1000 / mru_size
					, mru_size * (m_width-8) / arc_size, col_yellow, '-', '#'
					, mru_caption, progress_invert).c_str());
		}
		pos += snprintf(str + pos, sizeof(str) - pos, "|");
		if (mfu_size)
		{
			pos += snprintf(str + pos, sizeof(str) - pos, "%s"
				, progress_bar(m_cnt[0][m_mfu_size_idx] * 1000 / mfu_size
					, mfu_size * (m_width-8) / arc_size, col_green, '#', '-'
					, mfu_caption).c_str());
		}
	}
	pos += snprintf(str + pos, sizeof(str) - pos, "\x1b[K");
	set_cursor_pos(0, y++);
	print(str);

	if (m_print_utp_stats)
	{
		snprintf(str, sizeof(str), "uTP idle: %d syn: %d est: %d fin: %d wait: %d\x1b[K"
			, int(m_cnt[0][m_utp_idle])
			, int(m_cnt[0][m_utp_syn_sent])
			, int(m_cnt[0][m_utp_connected])
			, int(m_cnt[0][m_utp_fin_sent])
			, int(m_cnt[0][m_utp_close_wait]));
		set_cursor_pos(0, y++);
		print(str);
	}
}

void session_view::update_counters(boost::uint64_t* stats_counters
	, int num_cnt, boost::uint64_t t)
{
	// only update the previous counters if there's been enough
	// time since it was last updated
	if (t - m_timestamp[1] > 2000000)
	{
		m_cnt[1].swap(m_cnt[0]);
		m_timestamp[1] = m_timestamp[0];
	}

	m_cnt[0].assign(stats_counters, stats_counters + num_cnt);
	m_timestamp[0] = t;
	render();
}

