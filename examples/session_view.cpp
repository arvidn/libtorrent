/*

Copyright (c) 2018, Alden Torres
Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
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

#include "session_view.hpp"
#include "print.hpp"
#include "libtorrent/torrent_handle.hpp"

#include <algorithm> // for std::max

using lt::span;

session_view::session_view()
{
	std::vector<lt::stats_metric> metrics = lt::session_stats_metrics();
	m_cnt[0].resize(metrics.size(), 0);
	m_cnt[1].resize(metrics.size(), 0);
}

void session_view::set_pos(int pos)
{
	m_position = pos;
}

void session_view::set_width(int width)
{
	m_width = width;
}

int session_view::pos() const { return m_position; }

int session_view::height() const
{
	return 3;
}

std::int64_t session_view::value(int idx) const
{
	if (idx < 0) return 0;
	return m_cnt[0][std::size_t(idx)];
}

std::int64_t session_view::prev_value(int idx) const
{
	if (idx < 0) return 0;
	return m_cnt[1][std::size_t(idx)];
}

void session_view::render()
{
	char str[1024];

	int y = m_position;

	using std::chrono::duration_cast;
	double const seconds = duration_cast<lt::milliseconds>(m_timestamp[0] - m_timestamp[1]).count() / 1000.0;

	int const download_rate = int((value(m_recv_idx) - prev_value(m_recv_idx))
		/ seconds);
	int const upload_rate = int((value(m_sent_idx) - prev_value(m_sent_idx))
		/ seconds);

	std::snprintf(str, sizeof(str), "%s%s fail: %s down: %s (%s) "
		"  bw queue: %s | %s conns: %3d  unchoked: %2d / %2d queued-trackers: %02d%*s\x1b[K"
		, esc("48;5;238")
		, esc("1")
		, add_suffix(value(m_failed_bytes_idx)).c_str()
		, color(add_suffix(download_rate, "/s"), col_green).c_str()
		, color(add_suffix(value(m_recv_idx)), col_green).c_str()
		, color(to_string(int(value(m_limiter_up_queue_idx)), 3), col_red).c_str()
		, color(to_string(int(value(m_limiter_down_queue_idx)), 3), col_green).c_str()
		, int(value(m_num_peers_idx))
		, int(value(m_unchoked_idx))
		, int(value(m_unchoke_slots_idx))
		, int(value(m_queued_tracker_announces))
		, std::max(0, m_width - 86)
		, esc("0"));

	set_cursor_pos(0, y++);
	print(str);

	std::snprintf(str, sizeof(str), "%s%swaste: %s   up: %s (%s) "
		"disk queue: %s | %s cache w: %3d%% total: %s %*s\x1b[K"
#ifdef _WIN32
		, esc("40")
#else
		, esc("48;5;238")
#endif
		, esc("1")
		, add_suffix(value(m_wasted_bytes_idx)).c_str()
		, color(add_suffix(upload_rate, "/s"), col_red).c_str()
		, color(add_suffix(value(m_sent_idx)), col_red).c_str()
		, color(to_string(int(value(m_queued_reads_idx)), 3), col_red).c_str()
		, color(to_string(int(value(m_queued_writes_idx)), 3), col_green).c_str()
		, int((value(m_blocks_written_idx) - value(m_write_ops_idx)) * 100
			/ std::max(std::int64_t(1), value(m_blocks_written_idx)))
		, add_suffix(value(m_blocks_in_use_idx) * 16 * 1024).c_str()
		, std::max(0, m_width - 85)
		, esc("0"));
	set_cursor_pos(0, y++);
	print(str);

	std::snprintf(str, sizeof(str), "%s%suTP idle: %d syn: %d est: %d fin: %d wait: %d%*s\x1b[K"
		, esc("48;5;238")
		, esc("1")
		, int(value(m_utp_idle))
		, int(value(m_utp_syn_sent))
		, int(value(m_utp_connected))
		, int(value(m_utp_fin_sent))
		, int(value(m_utp_close_wait))
		, int(m_width - 37)
		, esc("0"));
	set_cursor_pos(0, y++);
	print(str);
}

void session_view::update_counters(span<std::int64_t const> stats_counters
	, lt::clock_type::time_point const t)
{
	// only update the previous counters if there's been enough
	// time since it was last updated
	if (t - m_timestamp[1] > lt::seconds(2))
	{
		m_cnt[1].swap(m_cnt[0]);
		m_timestamp[1] = m_timestamp[0];
	}

	m_cnt[0].assign(stats_counters.begin(), stats_counters.end());
	m_timestamp[0] = t;
	render();
}

