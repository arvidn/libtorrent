#include "torrent_view.hpp"
#include "print.hpp"

const int header_size = 2;

std::string torrent_state(lt::torrent_status const& s)
{
	static char const* state_str[] =
		{"checking (q)", "checking", "dl metadata"
		, "downloading", "finished", "seeding", "allocating", "checking (r)"};

	if (!s.error.empty()) return s.error;
	std::string ret;
	if (s.paused && !s.auto_managed) ret += "paused";
	else if (s.paused && s.auto_managed) ret += "queued";
	else if (s.upload_mode) ret += "upload mode";
	else ret += state_str[s.state];
	if (!s.paused && !s.auto_managed) ret += " [F]";
	char buf[10];
	snprintf(buf, sizeof(buf), " (%.1f%%)", s.progress_ppm / 10000.f);
	ret += buf;
	return ret;
}

bool compare_torrent(lt::torrent_status const* lhs, lt::torrent_status const* rhs)
{
	if (lhs->queue_position != -1 && rhs->queue_position != -1)
	{
		// both are downloading, sort by queue pos
		return lhs->queue_position < rhs->queue_position;
	}
	else if (lhs->queue_position == -1 && rhs->queue_position == -1)
	{
		// both are seeding, sort by seed-rank
		if (lhs->seed_rank != rhs->seed_rank)
			return lhs->seed_rank > rhs->seed_rank;

		return lhs->info_hash < rhs->info_hash;
	}

	return (lhs->queue_position == -1) < (rhs->queue_position == -1);
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

void torrent_view::update_torrents(std::vector<lt::torrent_status> const& st)
{
	std::set<lt::torrent_handle> updates;
	bool need_filter_update = false;
	for (std::vector<lt::torrent_status>::const_iterator i = st.begin();
		i != st.end(); ++i)
	{
		boost::unordered_set<lt::torrent_status>::iterator j = m_all_handles.find(*i);
		// add new entries here
		if (j == m_all_handles.end())
		{
			j = m_all_handles.insert(*i).first;
			if (show_torrent(*j))
			{
				m_filtered_handles.push_back(&*j);
				need_filter_update = true;
			}
		}
		else
		{
			bool prev_show = show_torrent(*j);
			((lt::torrent_status&)*j) = *i;
			if (prev_show != show_torrent(*j))
				need_filter_update = true;
			else
				updates.insert(i->handle);
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
		for (std::vector<lt::torrent_status const*>::iterator i
			= m_filtered_handles.begin();
			i != m_filtered_handles.end(); ++torrent_index)
		{
			if (torrent_index < m_scroll_position
				|| torrent_index >= m_scroll_position + m_height - header_size)
			{
				++i;
				continue;
			}

			lt::torrent_status const& s = **i;
			++i;

			if (!s.handle.is_valid())
				continue;

			if (updates.count(s.handle) == 0)
				continue;

			set_cursor_pos(0, header_size + torrent_index - m_scroll_position);
			print_torrent(s, torrent_index == m_active_torrent);
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
		i != m_filtered_handles.end(); ++torrent_index)
	{
		if (torrent_index < m_scroll_position)
		{
			++i;
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
	}

	clear_rows(torrent_index + header_size, m_height);
}

void torrent_view::print_tabs()
{
	set_cursor_pos(0, 0);

	char str[400];
	int pos = 0;
	char const* filter_names[] = { "all", "downloading", "non-paused"
		, "seeding", "queued", "stopped", "checking", "loaded"};
	for (int i = 0; i < int(sizeof(filter_names)/sizeof(filter_names[0])); ++i)
	{
		pos += snprintf(str+ pos, sizeof(str) - pos, "%s[%s]%s"
			, m_torrent_filter == i?esc("7"):""
			, filter_names[i], m_torrent_filter == i?esc("0"):"");
	}
	pos += snprintf(str + pos, sizeof(str) - pos, "\x1b[K");

	if (m_width + 1 < sizeof(str))
		str[m_width + 1] = '\0';
	print(str);
}

void torrent_view::print_headers()
{
	set_cursor_pos(0, 1);

	char str[400];
	int pos = 0;

	// print title bar for torrent list
	pos = snprintf(str, sizeof(str)
		, " %-3s %-50s %-35s %-17s %-17s %-11s %-6s %-6s %-4s\x1b[K"
		, "#", "Name", "Progress", "Download", "Upload", "Peers (D:S)"
		, "Down", "Up", "Flags");

	if (m_width + 1 < sizeof(str))
		str[m_width + 1] = '\0';

	print(str);
}

void torrent_view::print_torrent(lt::torrent_status const& s, bool selected)
{
	int pos = 0;
	char str[512];

	// the active torrent is highligted in the list
	// this inverses the forground and background colors
	char const* selection = "";
	if (selected)
		selection = "\x1b[1m\x1b[44m";

	char queue_pos[16] = {0};
	if (s.queue_position == -1)
		snprintf(queue_pos, sizeof(queue_pos), "-");
	else
		snprintf(queue_pos, sizeof(queue_pos), "%d", s.queue_position);

	std::string name = s.name;
	if (name.size() > 50) name.resize(50);

	color_code progress_bar_color = col_yellow;
	if (!s.error.empty()) progress_bar_color = col_red;
	else if (s.paused) progress_bar_color = col_blue;
	else if (s.state == lt::torrent_status::downloading_metadata)
		progress_bar_color = col_magenta;
	else if (s.current_tracker.empty())
		progress_bar_color = col_green;

	pos += snprintf(str + pos, sizeof(str) - pos, "%s%c%-3s %-50s %s%s %s (%s) "
		"%s (%s) %5d:%-5d %s %s %c%s"
		, selection
		, s.is_loaded ? 'L' : ' '
		, queue_pos
		, name.c_str()
		, progress_bar(s.progress_ppm / 1000, 35, progress_bar_color, '-', '#', torrent_state(s)).c_str()
		, selection
		, color(add_suffix(s.download_rate, "/s"), col_green).c_str()
		, color(add_suffix(s.total_download), col_green).c_str()
		, color(add_suffix(s.upload_rate, "/s"), col_red).c_str()
		, color(add_suffix(s.total_upload), col_red).c_str()
		, s.num_peers - s.num_seeds, s.num_seeds
		, color(add_suffix(s.all_time_download), col_green).c_str()
		, color(add_suffix(s.all_time_upload), col_red).c_str()
		, s.need_save_resume?'S':' ', esc("0"));

	// if this is the selected torrent, restore the background color
	if (selected)
		pos += snprintf(str + pos, sizeof(str) - pos, "%s", esc("0"));

	pos += snprintf(str + pos, sizeof(str) - pos, "\x1b[K");

	if (m_width + 1 < sizeof(str))
		str[m_width + 1] = '\0';

	print(str);
}

bool torrent_view::show_torrent(lt::torrent_status const& st)
{
	switch (m_torrent_filter)
	{
		case torrents_all: return true;
		case torrents_downloading:
			return !st.paused
				&& st.state != lt::torrent_status::seeding
				&& st.state != lt::torrent_status::finished;
		case torrents_not_paused: return !st.paused;
		case torrents_seeding:
			return !st.paused
				&& (st.state == lt::torrent_status::seeding
				|| st.state == lt::torrent_status::finished);
		case torrents_queued: return st.paused && st.auto_managed;
		case torrents_stopped: return st.paused && !st.auto_managed;
		case torrents_checking: return st.state == lt::torrent_status::checking_files;
		case torrents_loaded: return st.is_loaded;
	}
	return true;
}

// refresh all pointers in m_filtered_handles. This must be done when
// inserting or removing elements from m_all_handles, since pointers may
// be invalidated or when a torrent changes status to either become
// visible or filtered
void torrent_view::update_filtered_torrents()
{
	m_scroll_position = 0;
	m_filtered_handles.clear();
	for (boost::unordered_set<lt::torrent_status>::iterator i = m_all_handles.begin()
		, end(m_all_handles.end()); i != end; ++i)
	{
		if (!show_torrent(*i)) continue;
		m_filtered_handles.push_back(&*i);
	}
	if (m_active_torrent >= int(m_filtered_handles.size())) m_active_torrent = m_filtered_handles.size() - 1;
	if (m_active_torrent < 0) m_active_torrent = 0;
	TORRENT_ASSERT(m_active_torrent >= 0);
	std::sort(m_filtered_handles.begin(), m_filtered_handles.end(), &compare_torrent);
}

