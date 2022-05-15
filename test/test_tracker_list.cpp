/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/tracker_list.hpp"
#include "libtorrent/aux_/announce_entry.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/announce_entry.hpp"
#include "test.hpp"

using namespace libtorrent::aux;

TORRENT_TEST(test_initial_state)
{
	tracker_list tl;
	TEST_EQUAL(tl.empty(), true);
	TEST_EQUAL(tl.size(), 0);
	TEST_CHECK(tl.begin() == tl.end());
	TEST_CHECK(tl.last_working() == nullptr);
	TEST_EQUAL(tl.last_working_url(), "");
}

TORRENT_TEST(test_duplicate_add)
{
	tracker_list tl;

	tl.add_tracker(announce_entry("http://example1.com/announce"));
	TEST_EQUAL(tl.size(), 1);
	tl.add_tracker(announce_entry("http://example2.com/announce"));
	TEST_EQUAL(tl.size(), 2);
	tl.add_tracker(announce_entry("http://example3.com/announce"));
	TEST_EQUAL(tl.size(), 3);

	// duplicate ignored
	tl.add_tracker(announce_entry("http://example1.com/announce"));
	TEST_EQUAL(tl.size(), 3);

	// we want the trackers to have been inserted in the most efficient order
	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_add_sort_by_tier)
{
	tracker_list tl;
	announce_entry ae;

	ae.url = "http://example1.com/announce";
	ae.tier = 5;
	tl.add_tracker(ae);
	TEST_EQUAL(tl.size(), 1);

	ae.url = "http://example2.com/announce";
	ae.tier = 4;
	tl.add_tracker(ae);
	TEST_EQUAL(tl.size(), 2);

	ae.url = "http://example3.com/announce";
	ae.tier = 3;
	tl.add_tracker(ae);
	TEST_EQUAL(tl.size(), 3);

	ae.url = "http://example1.com/announce";
	ae.tier = 2;
	tl.add_tracker(ae);

	// duplicate ignored
	TEST_EQUAL(tl.size(), 3);

	// the trackers should be ordered by low tiers first
	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_replace_duplicate)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("http://example1.com/announce");
	trackers.emplace_back("http://example2.com/announce");
	trackers.emplace_back("http://example3.com/announce");
	trackers.emplace_back("http://example1.com/announce");

	tl.replace(trackers);

	// duplicate ignored
	TEST_EQUAL(tl.size(), 3);

	// we want the trackers to have been inserted in the most efficient order
	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_replace_sort_by_tier)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("http://example1.com/announce");
	trackers.back().tier = 5;
	trackers.emplace_back("http://example2.com/announce");
	trackers.back().tier = 4;
	trackers.emplace_back("http://example3.com/announce");
	trackers.back().tier = 3;
	trackers.emplace_back("http://example1.com/announce");
	trackers.back().tier = 1;

	tl.replace(trackers);

	// duplicate ignored
	TEST_EQUAL(tl.size(), 3);

	// the trackers should be ordered by low tiers first
	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_prioritize_udp_noop)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("http://example1.com/announce");
	trackers.emplace_back("http://example2.com/announce");
	trackers.emplace_back("http://example3.com/announce");
	trackers.emplace_back("udp://example4.com/announce");

	tl.replace(trackers);

	// duplicate ignored
	TEST_EQUAL(tl.size(), 4);

	// the trackers should be ordered by low tiers first
	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_EQUAL(i->url, "udp://example4.com/announce");
	++i;
	TEST_CHECK(i == tl.end());

	tl.prioritize_udp_trackers();

	// UDP trackers are prioritized over HTTP for the same hostname. These
	// hostnames are all different, so no reordering happens
	i = tl.begin();
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_EQUAL(i->url, "udp://example4.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_prioritize_udp)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("http://example1.com/announce");
	trackers.emplace_back("http://example2.com/announce");
	trackers.emplace_back("http://example3.com/announce");
	trackers.emplace_back("udp://example1.com/announce");

	tl.replace(trackers);

	// duplicate ignored
	TEST_EQUAL(tl.size(), 4);

	// the trackers should be ordered by low tiers first
	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_EQUAL(i->url, "udp://example1.com/announce");
	++i;
	TEST_CHECK(i == tl.end());

	tl.prioritize_udp_trackers();

	i = tl.begin();
	TEST_EQUAL(i->url, "udp://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example2.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example3.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_prioritize_udp_tier)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("http://example1.com/announce");
	trackers.emplace_back("udp://example1.com/announce");
	trackers.back().tier = 2;

	tl.replace(trackers);

	// the trackers should be ordered by low tiers first
	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "udp://example1.com/announce");
	++i;
	TEST_CHECK(i == tl.end());

	tl.prioritize_udp_trackers();

	// trackers are also re-ordered across tiers
	i = tl.begin();
	TEST_EQUAL(i->url, "udp://example1.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://example1.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_replace_find_tracker)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("http://a.com/announce");
	trackers.emplace_back("http://b.com/announce");
	trackers.emplace_back("http://c.com/announce");
	tl.replace(trackers);

	TEST_EQUAL(tl.find_tracker("http://a.com/announce")->url, "http://a.com/announce");
	TEST_EQUAL(tl.find_tracker("http://b.com/announce")->url, "http://b.com/announce");
	TEST_EQUAL(tl.find_tracker("http://c.com/announce")->url, "http://c.com/announce");
	TEST_CHECK(tl.find_tracker("http://d.com/announce") == nullptr);
}

TORRENT_TEST(test_add_find_tracker)
{
	tracker_list tl;

	tl.add_tracker(announce_entry("http://a.com/announce"));
	tl.add_tracker(announce_entry("http://b.com/announce"));
	tl.add_tracker(announce_entry("http://c.com/announce"));

	TEST_EQUAL(tl.find_tracker("http://a.com/announce")->url, "http://a.com/announce");
	TEST_EQUAL(tl.find_tracker("http://b.com/announce")->url, "http://b.com/announce");
	TEST_EQUAL(tl.find_tracker("http://c.com/announce")->url, "http://c.com/announce");
	TEST_CHECK(tl.find_tracker("http://d.com/announce") == nullptr);
}

TORRENT_TEST(test_deprioritize_tracker)
{
	tracker_list tl;

	tl.add_tracker(announce_entry("http://a.com/announce"));
	tl.add_tracker(announce_entry("http://b.com/announce"));
	tl.add_tracker(announce_entry("http://c.com/announce"));

	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://a.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://b.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://c.com/announce");
	++i;
	TEST_CHECK(i == tl.end());

	tl.deprioritize_tracker(tl.first());

	i = tl.begin();
	TEST_EQUAL(i->url, "http://b.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://c.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://a.com/announce");
	++i;
	TEST_CHECK(i == tl.end());

	tl.deprioritize_tracker(&*std::next(tl.begin()));

	i = tl.begin();
	TEST_EQUAL(i->url, "http://b.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://a.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://c.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_deprioritize_tracker_tier)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("http://a.com/announce");
	trackers.back().tier = 1;
	trackers.emplace_back("http://b.com/announce");
	trackers.back().tier = 1;
	trackers.emplace_back("http://c.com/announce");
	tl.replace(trackers);

	auto i = tl.begin();
	TEST_EQUAL(i->url, "http://c.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://a.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://b.com/announce");
	++i;
	TEST_CHECK(i == tl.end());

	// the tracker won't move across the tier
	tl.deprioritize_tracker(tl.first());

	i = tl.begin();
	TEST_EQUAL(i->url, "http://c.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://a.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://b.com/announce");
	++i;
	TEST_CHECK(i == tl.end());

	tl.deprioritize_tracker(&*std::next(tl.begin()));

	i = tl.begin();
	TEST_EQUAL(i->url, "http://c.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://b.com/announce");
	++i;
	TEST_EQUAL(i->url, "http://a.com/announce");
	++i;
	TEST_CHECK(i == tl.end());
}

TORRENT_TEST(test_add_empty)
{
	tracker_list tl;

	tl.add_tracker(announce_entry(""));
	TEST_EQUAL(tl.size(), 0);
}

TORRENT_TEST(test_replace_empty)
{
	tracker_list tl;

	std::vector<lt::announce_entry> trackers;
	trackers.emplace_back("");
	tl.replace(trackers);
	TEST_EQUAL(tl.size(), 0);
}

TORRENT_TEST(test_last_working)
{
	tracker_list tl;
	tl.add_tracker(announce_entry("http://a.com/announce"));
	tl.add_tracker(announce_entry("http://b.com/announce"));
	tl.add_tracker(announce_entry("http://c.com/announce"));

	TEST_CHECK(tl.last_working() == nullptr);
	TEST_EQUAL(tl.last_working_url(), "");

	tl.record_working(tl.first());
	TEST_EQUAL(tl.last_working()->url, "http://a.com/announce");
	TEST_EQUAL(tl.last_working_url(), "http://a.com/announce");

	tl.record_working(&*std::next(tl.begin()));
	TEST_EQUAL(tl.last_working()->url, "http://b.com/announce");
	TEST_EQUAL(tl.last_working_url(), "http://b.com/announce");

	tl.record_working(&*std::next(std::next(tl.begin())));
	TEST_EQUAL(tl.last_working()->url, "http://c.com/announce");
	TEST_EQUAL(tl.last_working_url(), "http://c.com/announce");
}

TORRENT_TEST(complete_sent)
{
	tracker_list tl;
	tl.add_tracker(announce_entry("http://a.com/announce"));
	tl.add_tracker(announce_entry("http://b.com/announce"));
	tl.add_tracker(announce_entry("http://c.com/announce"));

	listen_socket_handle s;
	for (auto& ae : tl)
		ae.endpoints.emplace_back(s, false);

	for (auto const& ae : tl)
		for (auto const& aep : ae.endpoints)
			for (auto const& a : aep.info_hashes)
				TEST_EQUAL(a.complete_sent, false);

	tl.set_complete_sent();

	for (auto const& ae : tl)
		for (auto const& aep : ae.endpoints)
			for (auto const& a : aep.info_hashes)
				TEST_EQUAL(a.complete_sent, true);
}

TORRENT_TEST(enable_all)
{
	tracker_list tl;
	tl.add_tracker(announce_entry("http://a.com/announce"));
	tl.add_tracker(announce_entry("http://b.com/announce"));
	tl.add_tracker(announce_entry("http://c.com/announce"));

	listen_socket_handle s;
	for (auto& ae : tl)
		ae.endpoints.emplace_back(s, false);

	for (auto& ae : tl)
		for (auto& aep : ae.endpoints)
		{
			TEST_EQUAL(aep.enabled, true);
			aep.enabled = false;
		}

	tl.enable_all();

	for (auto const& ae : tl)
		for (auto const& aep : ae.endpoints)
			TEST_EQUAL(aep.enabled, true);
}

// TODO: reset
// TODO: completed
// TODO: dont_try_again
