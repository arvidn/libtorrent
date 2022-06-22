/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#include <utility> // for swap()

#include "libtorrent/aux_/tracker_list.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/invariant_check.hpp"

namespace libtorrent::aux {

aux::announce_entry* tracker_list::find_tracker(std::string const& url)
{
	INVARIANT_CHECK;

	auto const i = m_url_index.find(url);
	if (i == m_url_index.end()) return nullptr;
	TORRENT_ASSERT(m_storage.is_from(i->second));
	return i->second;
}

aux::announce_entry const* tracker_list::find(int const idx) const
{
	TORRENT_ASSERT(idx >= 0);
	TORRENT_ASSERT(std::size_t(idx) < m_trackers.size());
	auto i = m_trackers.begin();
	std::advance(i, idx);
	return &*i;
}

aux::announce_entry* tracker_list::find(int const idx)
{
	TORRENT_ASSERT(idx >= 0);
	TORRENT_ASSERT(std::size_t(idx) < m_trackers.size());
	auto i = m_trackers.begin();
	std::advance(i, idx);
	return &*i;
}

void tracker_list::deprioritize_tracker(aux::announce_entry* ae)
{
	INVARIANT_CHECK;

	TORRENT_ASSERT(m_storage.is_from(ae));
	auto iter = m_trackers.iterator_to(*ae);
	iter = m_trackers.erase(iter);

	for (; iter != m_trackers.end()
		&& ae->tier == iter->tier; ++iter);

	m_trackers.insert(iter, *ae);
}

void tracker_list::dont_try_again(aux::announce_entry* ae)
{
	ae->fail_limit = 1;
}

bool tracker_list::add_tracker(announce_entry const& ae)
{
	INVARIANT_CHECK;

	if (ae.url.empty()) return false;
	auto* k = find_tracker(ae.url);
	if (k)
	{
		k->source |= ae.source;
		return false;
	}

	auto* new_ae = m_storage.construct(ae);
	if (m_trackers.empty())
	{
		m_trackers.push_back(*new_ae);
		// TODO: if this throws, new_ae needs to be freed
		m_url_index.insert(std::make_pair(string_view(new_ae->url), new_ae));
		return true;
	}

	auto iter = m_trackers.end();
	while (iter != m_trackers.begin() && std::prev(iter)->tier > new_ae->tier)
		--iter;

	m_trackers.insert(iter, *new_ae);
	// TODO: if this throws, new_ae needs to be freed
	m_url_index.insert(std::make_pair(string_view(new_ae->url), new_ae));

	if (new_ae->source == 0) new_ae->source = lt::announce_entry::source_client;
	return true;
}

void tracker_list::prioritize_udp_trackers()
{
	INVARIANT_CHECK;

	// look for udp-trackers
	for (auto i = m_trackers.begin(), end(m_trackers.end()); i != end; ++i)
	{
		if (i->url.substr(0, 6) != "udp://") continue;
		// now, look for trackers with the same hostname
		// that is has higher priority than this one
		// if we find one, swap with the udp-tracker
		error_code ec;
		std::string udp_hostname;
		using std::ignore;
		std::tie(ignore, ignore, udp_hostname, ignore, ignore)
			= parse_url_components(i->url, ec);
		for (auto j = m_trackers.begin(); j != i; ++j)
		{
			if (j->url.substr(0, 6) == "udp://") continue;
			std::string hostname;
			std::tie(ignore, ignore, hostname, ignore, ignore)
				= parse_url_components(j->url, ec);
			if (hostname != udp_hostname) continue;
			auto* ae1 = &*i;
			auto* ae2 = &*j;
			i = m_trackers.erase(i);
			m_trackers.insert(j, *ae1);
			j = m_trackers.erase(j);
			m_trackers.insert(i, *ae2);
			break;
		}
	}
}

void tracker_list::record_working(aux::announce_entry const* ae)
{
	m_last_working_tracker = const_cast<aux::announce_entry*>(ae);
	TORRENT_ASSERT(m_storage.is_from(m_last_working_tracker));
}

void tracker_list::replace(std::vector<lt::announce_entry> const& aes)
{
	INVARIANT_CHECK;

	m_trackers.clear();
	m_url_index.clear();

	m_storage.~object_pool<aux::announce_entry>();
	new (&m_storage) boost::object_pool<aux::announce_entry>();

	for (auto const& ae : aes)
	{
		if (ae.url.empty()) continue;
		aux::announce_entry* new_ae = m_storage.construct(ae);
		auto const [iter, added] = m_url_index.insert(std::make_pair(string_view(new_ae->url), new_ae));
		if (!added)
		{
			// if we already have an entry with this URL, skip it
			// but merge the source bits
			m_storage.destroy(new_ae);
			iter->second->source |= ae.source;
			continue;
		}
		m_trackers.push_back(*new_ae);
	}

	// make sure the trackers are correctly ordered by tier
	m_trackers.sort([](aux::announce_entry const& lhs, aux::announce_entry const& rhs)
	{ return lhs.tier < rhs.tier; });

	m_last_working_tracker = nullptr;
}

void tracker_list::enable_all()
{
	INVARIANT_CHECK;
	for (aux::announce_entry& ae : m_trackers)
		for (aux::announce_endpoint& aep : ae.endpoints)
			aep.enabled = true;
}

void tracker_list::completed(time_point32 const now)
{
	INVARIANT_CHECK;
	for (auto& t : m_trackers)
	{
		for (auto& aep : t.endpoints)
		{
			if (!aep.enabled) continue;
			for (auto& a : aep.info_hashes)
			{
				if (a.complete_sent) continue;
				a.next_announce = now;
				a.min_announce = now;
			}
		}
	}
}

void tracker_list::set_complete_sent()
{
	INVARIANT_CHECK;
	for (auto& t : m_trackers)
	{
		for (auto& aep : t.endpoints)
		{
			for (auto& a : aep.info_hashes)
				a.complete_sent = true;
		}
	}
}

void tracker_list::reset()
{
	INVARIANT_CHECK;
	for (auto& t : m_trackers) t.reset();
}

void tracker_list::stop_announcing(time_point32 const now)
{
	INVARIANT_CHECK;

	for (auto& t : m_trackers)
	{
		for (auto& aep : t.endpoints)
		{
			for (auto& a : aep.info_hashes)
			{
				a.next_announce = now;
				a.min_announce = now;
			}
		}
	}
}

std::string tracker_list::last_working_url() const
{
	if (m_last_working_tracker == nullptr) return {};
	return m_last_working_tracker->url;
}

aux::announce_entry* tracker_list::last_working()
{
	return m_last_working_tracker;
}

aux::announce_entry* tracker_list::first()
{
	if (m_trackers.empty()) return nullptr;
	return &*m_trackers.begin();
}

bool tracker_list::any_verified() const
{
	return std::any_of(m_trackers.begin(), m_trackers.end()
		, [](aux::announce_entry const& t) { return t.verified; });
}

#if TORRENT_USE_INVARIANT_CHECKS
void tracker_list::check_invariant() const
{
	m_trackers.check();
	for (auto const& ae : m_trackers)
	{
		TORRENT_ASSERT(m_storage.is_from(const_cast<aux::announce_entry*>(&ae)));
		auto i = m_url_index.find(ae.url);
		TORRENT_ASSERT(i != m_url_index.end());
		TORRENT_ASSERT(i->second == &ae);
	}

	TORRENT_ASSERT(m_url_index.size() == m_trackers.size());
	TORRENT_ASSERT(m_last_working_tracker == nullptr || m_storage.is_from(m_last_working_tracker));
}
#endif

}
