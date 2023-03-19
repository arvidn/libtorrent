/*

Copyright (c) 2014-2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_VIEW_HPP_
#define TORRENT_VIEW_HPP_

#include <set>
#include <vector>
#include <unordered_map>

#include "libtorrent/fwd.hpp"
#include "libtorrent/torrent_status.hpp"


struct torrent_view
{
	torrent_view();

	void set_size(int width, int height);

	enum {
		torrents_all,
		torrents_downloading,
		torrents_not_paused,
		torrents_seeding,
		torrents_queued,
		torrents_stopped,
		torrents_checking,

		torrents_max
	};

	int filter() const;

	void set_filter(int filter);

	// returns the lt::torrent_status of the currently selected torrent.
	lt::torrent_status const& get_active_torrent() const;
	lt::torrent_handle get_active_handle() const;

	void remove_torrent(lt::torrent_handle st);
	void update_torrents(std::vector<lt::torrent_status> st);
	int num_visible_torrents() const { return int(m_filtered_handles.size()); }

	void for_each_torrent(std::function<void(lt::torrent_status const&)> f);
	int height() const;

	void arrow_up();
	void arrow_down();

	void render();

private:

	void print_tabs();

	void print_headers();

	void print_torrent(lt::torrent_status const& s, bool selected);

	bool show_torrent(lt::torrent_status const& st);

	// refresh all pointers in m_filtered_handles. This must be done when
	// inserting or removing elements from m_all_handles, since pointers may
	// be invalidated or when a torrent changes status to either become
	// visible or filtered
	void update_filtered_torrents();

	// all torrents
	std::unordered_map<lt::torrent_handle, lt::torrent_status> m_all_handles;

	// pointers into m_all_handles of the remaining torrents after filtering
	std::vector<lt::torrent_status const*> m_filtered_handles;

	mutable int m_active_torrent = 0; // index into m_filtered_handles
	int m_scroll_position = 0;
	int m_torrent_filter = 0;
	int m_width = 80;
	int m_height = 30;
};

#endif // TORRENT_VIEW_HPP_

