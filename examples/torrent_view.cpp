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

#include "torrent_view.hpp"
#include "print.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/torrent_info.hpp"

#include <array>

const int header_size = 2;
using lt::queue_position_t;

std::string torrent_state(lt::torrent_status const& s)
{
	static char const* state_str[] =
		{"checking (q)", "checking", "dl metadata"
		, "downloading", "finished", "seeding", "allocating", "checking (r)"};

	if (s.errc) return s.errc.message();
	std::string ret;
	if ((s.flags & lt::torrent_flags::paused) &&
		(s.flags & lt::torrent_flags::auto_managed))
	{
		ret += "queued ";
	}

	if ((s.flags & lt::torrent_flags::upload_mode)) ret += "upload mode";
	else ret += state_str[s.state];

	if (!(s.flags & lt::torrent_flags::auto_managed))
	{
		if (s.flags & lt::torrent_flags::paused)
			ret += " [P]";
		else
			ret += " [F]";
	}
	char buf[10];
	std::snprintf(buf, sizeof(buf), " (%.1f%%)", s.progress_ppm / 10000.f);
	ret += buf;
	return ret;
}

bool compare_torrent(lt::torrent_status const* lhs, lt::torrent_status const* rhs)
{
	if (lhs->queue_position != queue_position_t{-1} && rhs->queue_position != queue_position_t{-1})
	{
		// both are downloading, sort by queue pos
		return lhs->queue_position < rhs->queue_position;
	}
	else if (lhs->queue_position == queue_position_t{-1}
		&& rhs->queue_position == queue_position_t{-1})
	{
		// both are seeding, sort by seed-rank
		if (lhs->seed_rank != rhs->seed_rank)
			return lhs->seed_rank > rhs->seed_rank;

		return lhs->info_hash < rhs->info_hash;
	}

	return (lhs->queue_position == queue_position_t{-1})
		< (rhs->queue_position == queue_position_t{-1});
}

torrent_view::torrent_view()
	: m_active_torrent(0)
	, m_scroll_position(0)
	, m_torrent_filter(0)
	, m_width(80)
	, m_height(30)
{}

void torrent_view::set_size(int width, int height)
{
	if (m_width == width && m_height == height) return;

	m_width = width;
	m_height = height;
	render();
}

int torrent_view::filter() const
{
	return m_torrent_filter;
}

void torrent_view::set_filter(int filter)
{
	if (filter == m_torrent_filter) return;
	m_torrent_filter = filter;

	update_filtered_torrents();
	render();
}

// returns the lt::torrent_status of the currently selected torrent.
lt::torrent_status const& torrent_view::get_active_torrent() const
{
	if (m_active_torrent >= int(m_filtered_handles.size()))
		m_active_torrent = int(m_filtered_handles.size()) - 1;
	if (m_active_torrent < 0) m_active_torrent = 0;
	TORRENT_ASSERT(m_active_torrent >= 0);

	return *m_filtered_handles[m_active_torrent];
}

lt::torrent_handle torrent_view::get_active_handle() const
{
	if (m_active_torrent >= int(m_filtered_handles.size()))
		m_active_torrent = int(m_filtered_handles.size()) - 1;
	if (m_active_torrent < 0) m_active_torrent = 0;
	TORRENT_ASSERT(m_active_torrent >= 0);

	if (m_filtered_handles.empty()) return lt::torrent_handle();

	return m_filtered_handles[m_active_torrent]->handle;
}

void torrent_view::remove_torrent(lt::torrent_handle h)
{
	auto i = m_all_handles.find(h);
	if (i == m_all_handles.end()) return;
	bool need_rerender = false;
	if (show_torrent(i->second))
	{
		auto j = std::find(m_filtered_handles.begin(), m_filtered_handles.end()
			, &i->second);
		if (j != m_filtered_handles.end())
		{
			m_filtered_handles.erase(j);
			need_rerender = true;
		}
	}
	m_all_handles.erase(i);
	if (need_rerender) render();
}

void torrent_view::update_torrents(std::vector<lt::torrent_status> st)
{
	std::set<lt::torrent_handle> updates;
	bool need_filter_update = false;
	for (lt::torrent_status& t : st)
	{
		auto j = m_all_handles.find(t.handle);
		// add new entries here
		if (j == m_all_handles.end())
		{
			auto handle = t.handle;
			j = m_all_handles.emplace(handle, std::move(t)).first;
			if (show_torrent(j->second))
			{
				m_filtered_handles.push_back(&j->second);
				need_filter_update = true;
			}
		}
		else
		{
			bool const prev_show = show_torrent(j->second);
			j->second = std::move(t);
			if (prev_show != show_torrent(j->second))
				need_filter_update = true;
			else
				updates.insert(j->second.handle);
		}
	}
	if (need_filter_update)
	{
		update_filtered_torrents();
		render();
	}
	else
	{
		int torrent_index = 0;
		for (auto i = m_filtered_handles.begin();
			i != m_filtered_handles.end(); ++i)
		{
			if (torrent_index < m_scroll_position
				|| torrent_index >= m_scroll_position + m_height - header_size)
			{
				++torrent_index;
				continue;
			}

			lt::torrent_status const& s = **i;

			if (!s.handle.is_valid())
				continue;

			if (updates.count(s.handle) == 0)
			{
				++torrent_index;
				continue;
			}

			set_cursor_pos(0, header_size + torrent_index - m_scroll_position);
			print_torrent(s, torrent_index == m_active_torrent);
			++torrent_index;
		}
	}
}

int torrent_view::height() const
{
	return m_height;
}

void torrent_view::arrow_up()
{
	if (m_filtered_handles.empty()) return;
	if (m_active_torrent <= 0) return;

	if (m_active_torrent - 1 < m_scroll_position)
	{
		--m_active_torrent;
		m_scroll_position = m_active_torrent;
		TORRENT_ASSERT(m_scroll_position >= 0);
		render();
		return;
	}

	set_cursor_pos(0, header_size + m_active_torrent - m_scroll_position);
	print_torrent(*m_filtered_handles[m_active_torrent], false);
	--m_active_torrent;
	TORRENT_ASSERT(m_active_torrent >= 0);

	set_cursor_pos(0, header_size + m_active_torrent - m_scroll_position);
	print_torrent(*m_filtered_handles[m_active_torrent], true);
}

void torrent_view::arrow_down()
{
	if (m_filtered_handles.empty()) return;
	if (m_active_torrent >= int(m_filtered_handles.size()) - 1) return;

	int bottom_pos = m_height - header_size - 1;
	if (m_active_torrent - m_scroll_position + 1 > bottom_pos)
	{
		++m_active_torrent;
		m_scroll_position = m_active_torrent - bottom_pos;
		TORRENT_ASSERT(m_scroll_position >= 0);
		render();
		return;
	}

	set_cursor_pos(0, header_size + m_active_torrent - m_scroll_position);
	print_torrent(*m_filtered_handles[m_active_torrent], false);

	TORRENT_ASSERT(m_active_torrent >= 0);
	++m_active_torrent;

	set_cursor_pos(0, header_size + m_active_torrent - m_scroll_position);
	print_torrent(*m_filtered_handles[m_active_torrent], true);
}

void torrent_view::render()
{
	print_tabs();
	print_headers();

	int lines_printed = header_size;

	int torrent_index = 0;

	for (std::vector<lt::torrent_status const*>::iterator i = m_filtered_handles.begin();
		i != m_filtered_handles.end();)
	{
		if (torrent_index < m_scroll_position)
		{
			++i;
			++torrent_index;
			continue;
		}
		if (lines_printed >= m_height)
			break;

		lt::torrent_status const& s = **i;
		if (!s.handle.is_valid())
		{
			i = m_filtered_handles.erase(i);
			continue;
		}
		++i;

		set_cursor_pos(0, torrent_index + header_size - m_scroll_position);
		print_torrent(s, torrent_index == m_active_torrent);
		++lines_printed;
		++torrent_index;
	}

	clear_rows(torrent_index + header_size, m_height);
}

void torrent_view::print_tabs()
{
	set_cursor_pos(0, 0);

	std::array<char, 400> str;
	lt::span<char> dest(str);
	static std::array<char const*, 7> const filter_names{{ "all", "downloading", "non-paused"
		, "seeding", "queued", "stopped", "checking"}};
	for (int i = 0; i < int(filter_names.size()); ++i)
	{
		int const ret = std::snprintf(dest.data(), dest.size(), "%s[%s]%s"
			, m_torrent_filter == i?esc("7"):""
			, filter_names[i], m_torrent_filter == i?esc("0"):"");
		if (ret >= 0 && ret <= dest.size()) dest = dest.subspan(ret);
	}
	int const ret = std::snprintf(dest.data(), dest.size(), "\x1b[K");
	if (ret >= 0 && ret <= dest.size()) dest = dest.subspan(ret);

	if (m_width + 1 < int(str.size()))
		str[m_width + 1] = '\0';
	print(str.data());
}

void torrent_view::print_headers()
{
	set_cursor_pos(0, 1);

	std::array<char, 400> str;

	// print title bar for torrent list
	std::snprintf(str.data(), str.size()
		, " %-3s %-50s %-35s %-14s %-17s %-17s %-11s %-6s %-6s %-4s\x1b[K"
		, "#", "Name", "Progress", "Pieces", "Download", "Upload", "Peers (D:S)"
		, "Down", "Up", "Flags");

	if (m_width + 1 < int(str.size()))
		str[m_width + 1] = '\0';

	print(str.data());
}

void torrent_view::print_torrent(lt::torrent_status const& s, bool selected)
{
	std::array<char, 512> str;
	lt::span<char> dest(str);

	// the active torrent is highligted in the list
	// this inverses the forground and background colors
	char const* selection = "";
	if (selected)
		selection = "\x1b[1m\x1b[44m";

	char queue_pos[16] = {0};
	if (s.queue_position == queue_position_t{-1})
		std::snprintf(queue_pos, sizeof(queue_pos), "-");
	else
		std::snprintf(queue_pos, sizeof(queue_pos), "%d"
			, static_cast<int>(s.queue_position));

	std::string name = s.name;
	if (name.size() > 50) name.resize(50);

	color_code progress_bar_color = col_yellow;
	if (s.errc) progress_bar_color = col_red;
	else if (s.flags & lt::torrent_flags::paused) progress_bar_color = col_blue;
	else if (s.state == lt::torrent_status::downloading_metadata)
		progress_bar_color = col_magenta;
	else if (s.current_tracker.empty())
		progress_bar_color = col_green;

	auto ti = s.torrent_file.lock();
	int const total_pieces = ti && ti->is_valid() ? ti->num_pieces() : 0;
	color_code piece_color = total_pieces == s.num_pieces ? col_green : col_yellow;

	int const ret = std::snprintf(dest.data(), dest.size(), "%s%-3s %-50s %s%s %s/%s %s (%s) "
		"%s (%s) %5d:%-5d %s %s %c"
		, selection
		, queue_pos
		, name.c_str()
		, progress_bar(s.progress_ppm / 1000, 35, progress_bar_color, '-', '#', torrent_state(s)).c_str()
		, selection
		, color(to_string(s.num_pieces, 6), piece_color).c_str()
		, color(to_string(total_pieces, 6), piece_color).c_str()
		, color(add_suffix(s.download_rate, "/s"), col_green).c_str()
		, color(add_suffix(s.total_download), col_green).c_str()
		, color(add_suffix(s.upload_rate, "/s"), col_red).c_str()
		, color(add_suffix(s.total_upload), col_red).c_str()
		, s.num_peers - s.num_seeds, s.num_seeds
		, color(add_suffix(s.all_time_download), col_green).c_str()
		, color(add_suffix(s.all_time_upload), col_red).c_str()
		, s.need_save_resume?'S':' ');
	if (ret >= 0 && ret <= dest.size()) dest = dest.subspan(ret);

	// if this is the selected torrent, restore the background color
	if (selected)
	{
		int const ret2 = std::snprintf(dest.data(), dest.size(), "%s", esc("0"));
		if (ret2 >= 0 && ret2 <= dest.size()) dest = dest.subspan(ret2);
	}

	int const ret2 = std::snprintf(dest.data(), dest.size(), "\x1b[K");
	if (ret2 >= 0 && ret2 <= dest.size()) dest = dest.subspan(ret2);

	print(str.data());
}

bool torrent_view::show_torrent(lt::torrent_status const& st)
{
	switch (m_torrent_filter)
	{
		case torrents_all: return true;
		case torrents_downloading:
			return !(st.flags & lt::torrent_flags::paused)
				&& st.state != lt::torrent_status::seeding
				&& st.state != lt::torrent_status::finished;
		case torrents_not_paused:
			return !(st.flags & lt::torrent_flags::paused);
		case torrents_seeding:
			return !(st.flags & lt::torrent_flags::paused)
				&& (st.state == lt::torrent_status::seeding
				|| st.state == lt::torrent_status::finished);
		case torrents_queued:
			return (st.flags & lt::torrent_flags::paused)
				&& (st.flags & lt::torrent_flags::auto_managed);
		case torrents_stopped:
			return (st.flags & lt::torrent_flags::paused)
				&& !(st.flags & lt::torrent_flags::auto_managed);
		case torrents_checking: return st.state == lt::torrent_status::checking_files;
	}
	return true;
}

// refresh all pointers in m_filtered_handles. This must be done when
// inserting or removing elements from m_all_handles, since pointers may
// be invalidated or when a torrent changes status to either become
// visible or filtered
void torrent_view::update_filtered_torrents()
{
	m_filtered_handles.clear();
	for (auto const& h : m_all_handles)
	{
		if (!show_torrent(h.second)) continue;
		m_filtered_handles.push_back(&h.second);
	}
	if (m_active_torrent >= int(m_filtered_handles.size())) m_active_torrent = int(m_filtered_handles.size()) - 1;
	if (m_active_torrent < 0) m_active_torrent = 0;
	TORRENT_ASSERT(m_active_torrent >= 0);
	std::sort(m_filtered_handles.begin(), m_filtered_handles.end(), &compare_torrent);
	if (m_scroll_position + m_height - header_size > int(m_filtered_handles.size()))
	{
		m_scroll_position = std::max(0, int(m_filtered_handles.size()) - m_height + header_size);
	}
}

