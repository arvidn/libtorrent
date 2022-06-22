/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TRACKER_LIST_HPP_INCLUDED
#define TORRENT_TRACKER_LIST_HPP_INCLUDED

#include <string>
#include <unordered_map>

#include "libtorrent/aux_/announce_entry.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/invariant_check.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/intrusive/list.hpp>
#include <boost/pool/object_pool.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"


namespace libtorrent::aux {

struct TORRENT_EXTRA_EXPORT tracker_list
{
	using trackers_t = boost::intrusive::list<aux::announce_entry
		, boost::intrusive::member_hook<aux::announce_entry
			, boost::intrusive::list_member_hook<>
			, &aux::announce_entry::list_hook>
		>;
	using iterator = typename trackers_t::iterator;
	using const_iterator = typename trackers_t::const_iterator;

	aux::announce_entry* find_tracker(std::string const& url);

	// returns true if the tracker was added, and false if it was already
	// in the tracker list (in which case the source was added to the
	// entry in the list)
	bool add_tracker(announce_entry const& ae);

	void prioritize_udp_trackers();

	void deprioritize_tracker(aux::announce_entry* ae);
	void dont_try_again(aux::announce_entry* ae);

	bool empty() const { return m_trackers.empty(); }
	std::size_t size() const { return m_trackers.size(); }

	std::string last_working_url() const;
	aux::announce_entry* last_working();
	aux::announce_entry* first();

	void record_working(aux::announce_entry const* ae);

	void replace(std::vector<lt::announce_entry> const& aes);

	void enable_all();

	void completed(time_point32 now);

	void set_complete_sent();

	void reset();

	void stop_announcing(time_point32 now);

	bool any_verified() const;

	// TODO: make these iterators const
	iterator begin() { return m_trackers.begin(); }
	iterator end() { return m_trackers.end(); }

	const_iterator begin() const { return m_trackers.begin(); }
	const_iterator end() const { return m_trackers.end(); }

	aux::announce_entry const* find(int const idx) const;
	aux::announce_entry* find(int const idx);

private:

#if TORRENT_USE_INVARIANT_CHECKS
	friend struct libtorrent::invariant_access;
	void check_invariant() const;
#endif

	boost::object_pool<aux::announce_entry> m_storage;
	trackers_t m_trackers;
	std::unordered_map<string_view, aux::announce_entry*> m_url_index;

	// the index to the last tracker that worked
	aux::announce_entry* m_last_working_tracker = nullptr;
};

}
#endif

