/*

Copyright (c) 2014-2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
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

	// torrent filter
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

	// sort order
	enum order: std::uint8_t {
		queue,
		name,
		size,
	};

	int filter() const;
	void set_filter(int filter);

	int sort_order() const;
	void set_sort_order(int);

	// returns the lt::torrent_status of the currently selected torrent.
	lt::torrent_status const& get_active_torrent() const;
	lt::torrent_handle get_active_handle() const;

	void remove_torrent(lt::torrent_handle st);
	void update_torrents(std::vector<lt::torrent_status> st);
	int num_visible_torrents() const { return int(m_filtered_handles.size()); }

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

	// re-sorts m_filtered_handles based on m_sort_order
	void update_sort_order();

	// all torrents
	std::unordered_map<lt::torrent_handle, lt::torrent_status> m_all_handles;

	// pointers into m_all_handles of the remaining torrents after filtering
	std::vector<lt::torrent_status const*> m_filtered_handles;

	mutable int m_active_torrent = 0; // index into m_filtered_handles
	int m_scroll_position = 0;
	int m_torrent_filter = 0;
	order m_sort_order = order::queue;
	int m_width = 80;
	int m_height = 30;
};

#endif // TORRENT_VIEW_HPP_

