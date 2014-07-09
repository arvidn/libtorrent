#ifndef TORRENT_VIEW_HPP_
#define TORRENT_VIEW_HPP_

#include <set>

#include "libtorrent/torrent_handle.hpp"

namespace lt = libtorrent;

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
		torrents_loaded,

		torrents_max
	};

	int filter() const;

	void set_filter(int filter);

	// returns the lt::torrent_status of the currently selected torrent.
	lt::torrent_status const& get_active_torrent() const;
	lt::torrent_handle get_active_handle() const;

	void update_torrents(std::vector<lt::torrent_status> const& st);

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
	boost::unordered_set<lt::torrent_status> m_all_handles;

	// pointers into m_all_handles of the remaining torrents after filtering
	std::vector<lt::torrent_status const*> m_filtered_handles;

	mutable int m_active_torrent; // index into m_filtered_handles
	int m_scroll_position;
	int m_torrent_filter;
	int m_width;
	int m_height;
};

#endif // TORRENT_VIEW_HPP_

