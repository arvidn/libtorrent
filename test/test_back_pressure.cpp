/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/back_pressure.hpp"
#include "libtorrent/io_context.hpp"

using namespace lt;
using namespace lt::aux;

namespace {

struct test_observer : disk_observer
{
	void on_disk() override { ++called; }
	virtual ~test_observer() = default;
	int called = 0;
};

} // anonymous namespace

// set_max_size(100) produces:
//   m_max_size       = 100
//   m_low_watermark  = 75  (100 / 4 * 3, integer division: 25 * 3)
//   m_high_watermark = 84  (100 / 8 * 7, integer division: 12 * 7)

namespace {

struct fixture
{
	io_context ios;
	back_pressure bp{ios};

	fixture() { bp.set_max_size(100); }

	// drain all handlers posted to ios
	void poll() { ios.restart(); ios.poll(); }
};

} // anonymous namespace

TORRENT_TEST(no_back_pressure_below_max)
{
	fixture f;
	// below and just before the max – no back-pressure
	TEST_EQUAL(f.bp.has_back_pressure(0, nullptr), false);
	TEST_EQUAL(f.bp.has_back_pressure(50, nullptr), false);
	TEST_EQUAL(f.bp.has_back_pressure(99, nullptr), false);
}

TORRENT_TEST(back_pressure_at_and_above_max)
{
	fixture f;
	TEST_EQUAL(f.bp.has_back_pressure(100, nullptr), true);
	TEST_EQUAL(f.bp.has_back_pressure(150, nullptr), true);
}

TORRENT_TEST(no_flush_below_high_watermark)
{
	fixture f;
	// high watermark is 84; anything below should return nullopt
	TEST_EQUAL(f.bp.should_flush(0).has_value(), false);
	TEST_EQUAL(f.bp.should_flush(50).has_value(), false);
	TEST_EQUAL(f.bp.should_flush(83).has_value(), false);
}

TORRENT_TEST(flush_at_high_watermark)
{
	fixture f;
	// at and above the high watermark we should flush down to the low watermark
	auto const ret84 = f.bp.should_flush(84);
	TEST_EQUAL(ret84.has_value(), true);
	TEST_EQUAL(ret84.value(), 75); // low watermark

	auto const ret100 = f.bp.should_flush(100);
	TEST_EQUAL(ret100.has_value(), true);
	TEST_EQUAL(ret100.value(), 75);
}

TORRENT_TEST(observer_called_when_dropping_below_low_watermark)
{
	fixture f;
	auto obs = std::make_shared<test_observer>();

	// trigger back-pressure and register the observer
	TEST_EQUAL(f.bp.has_back_pressure(100, obs), true);

	// still above low watermark – callback must not fire
	f.bp.check_buffer_level(80);
	f.poll();
	TEST_EQUAL(obs->called, 0);

	// drop to exactly the low watermark – callback must fire
	f.bp.check_buffer_level(75);
	f.poll();
	TEST_EQUAL(obs->called, 1);
}

TORRENT_TEST(observer_called_below_low_watermark)
{
	fixture f;
	auto obs = std::make_shared<test_observer>();

	TEST_EQUAL(f.bp.has_back_pressure(100, obs), true);

	f.bp.check_buffer_level(0);
	f.poll();
	TEST_EQUAL(obs->called, 1);
}

TORRENT_TEST(observer_not_called_without_back_pressure)
{
	fixture f;
	auto obs = std::make_shared<test_observer>();

	// never hit max – m_exceeded_max_size stays false
	f.bp.check_buffer_level(0);
	f.poll();
	TEST_EQUAL(obs->called, 0);
}

TORRENT_TEST(multiple_observers_all_called)
{
	fixture f;
	auto obs1 = std::make_shared<test_observer>();
	auto obs2 = std::make_shared<test_observer>();
	auto obs3 = std::make_shared<test_observer>();

	f.bp.has_back_pressure(100, obs1);
	f.bp.has_back_pressure(110, obs2);
	f.bp.has_back_pressure(120, obs3);

	f.bp.check_buffer_level(75);
	f.poll();

	TEST_EQUAL(obs1->called, 1);
	TEST_EQUAL(obs2->called, 1);
	TEST_EQUAL(obs3->called, 1);
}

TORRENT_TEST(observers_not_called_twice)
{
	fixture f;
	auto obs = std::make_shared<test_observer>();

	f.bp.has_back_pressure(100, obs);
	f.bp.check_buffer_level(75);
	f.poll();
	TEST_EQUAL(obs->called, 1);

	// a second drop shouldn't fire again (m_exceeded_max_size was cleared)
	f.bp.check_buffer_level(0);
	f.poll();
	TEST_EQUAL(obs->called, 1);
}

TORRENT_TEST(flush_keeps_going_until_low_watermark)
{
	fixture f;
	// exceed the max to set m_exceeded_max_size = true
	TEST_EQUAL(f.bp.has_back_pressure(100, nullptr), true);

	// level drops below the high watermark (84) but is still above the low
	// watermark (75) – flushing must continue because m_exceeded_max_size is set
	auto const r80 = f.bp.should_flush(80);
	TEST_EQUAL(r80.has_value(), true);
	TEST_EQUAL(r80.value(), 75);

	auto const r76 = f.bp.should_flush(76);
	TEST_EQUAL(r76.has_value(), true);
	TEST_EQUAL(r76.value(), 75);

	// crossing the low watermark clears the flag
	f.bp.check_buffer_level(75);
	f.poll();

	// flag cleared – no flushing needed even though level hasn't changed
	TEST_EQUAL(f.bp.should_flush(75).has_value(), false);
	TEST_EQUAL(f.bp.should_flush(0).has_value(), false);
}

TORRENT_TEST(expired_observer_skipped)
{
	fixture f;
	auto obs = std::make_shared<test_observer>();
	f.bp.has_back_pressure(100, obs);

	// let the shared_ptr expire before the callback fires
	obs.reset();

	f.bp.check_buffer_level(75);
	f.poll();
	// no crash; the weak_ptr lock() returns nullptr and is skipped
}
