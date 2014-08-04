/*

Copyright (c) 2009-2014, Arvid Norberg
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

#include <ctime>
#include <string>
#include <cstdio>
#include <boost/limits.hpp>
#include <boost/version.hpp>
#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace libtorrent
{
	namespace aux
	{
		// used to cache the current time
		// every 100 ms. This is cheaper
		// than a system call and can be
		// used where more accurate time
		// is not necessary
		ptime g_current_time;
	}

	TORRENT_EXPORT ptime const& time_now() { return aux::g_current_time; }

	char const* time_now_string()
	{
		static const ptime start = time_now_hires();
		static char ret[200];
		int t = total_milliseconds(time_now_hires() - start);
		int h = t / 1000 / 60 / 60;
		t -= h * 60 * 60 * 1000;
		int m = t / 1000 / 60;
		t -= m * 60 * 1000;
		int s = t / 1000;
		t -= s * 1000;
		int ms = t;
		snprintf(ret, sizeof(ret), "%02d:%02d:%02d.%03d", h, m, s, ms);
		return ret;
	}

	std::string log_time()
	{
		static const ptime start = time_now_hires();
		char ret[200];
		snprintf(ret, sizeof(ret), "%" PRId64, total_microseconds(time_now_hires() - start));
		return ret;
	}
}

#if defined TORRENT_USE_BOOST_DATE_TIME

#include <boost/date_time/microsec_time_clock.hpp>

namespace libtorrent
{
	ptime time_now_hires()
	{ return boost::date_time::microsec_clock<ptime>::universal_time(); }
	ptime min_time()
	{ return boost::posix_time::ptime(boost::posix_time::min_date_time); }
	ptime max_time()
	{ return boost::posix_time::ptime(boost::posix_time::max_date_time); }
	time_duration seconds(boost::int64_t s) { return boost::posix_time::seconds(s); }
	time_duration milliseconds(boost::int64_t s) { return boost::posix_time::milliseconds(s); }
	time_duration microsec(boost::int64_t s) { return boost::posix_time::microsec(s); }
	time_duration minutes(boost::int64_t s) { return boost::posix_time::minutes(s); }
	time_duration hours(boost::int64_t s) { return boost::posix_time::hours(s); }

	boost::int64_t total_seconds(time_duration td)
	{ return td.total_seconds(); }
	boost::int64_t total_milliseconds(time_duration td)
	{ return td.total_milliseconds(); }
	boost::int64_t total_microseconds(time_duration td)
	{ return td.total_microseconds(); }
}

#else // TORRENT_USE_BOOST_DATE_TIME

namespace libtorrent
{
	ptime min_time() { return ptime(0); }
	ptime max_time() { return ptime((std::numeric_limits<boost::uint64_t>::max)()); }
}

#if defined TORRENT_USE_ABSOLUTE_TIME

#include <mach/mach_time.h>
#include <boost/cstdint.hpp>
#include "libtorrent/assert.hpp"

// high precision timer for darwin intel and ppc

namespace libtorrent
{
	ptime time_now_hires()
	{
		static mach_timebase_info_data_t timebase_info = {0,0};
		if (timebase_info.denom == 0)
			mach_timebase_info(&timebase_info);
		boost::uint64_t at = mach_absolute_time();
		// make sure we don't overflow
		TORRENT_ASSERT((at >= at / 1000 * timebase_info.numer / timebase_info.denom)
			|| (at < 0 && at < at / 1000 * timebase_info.numer / timebase_info.denom));
		return ptime(at / 1000 * timebase_info.numer / timebase_info.denom);
	}
}
#elif defined TORRENT_USE_QUERY_PERFORMANCE_TIMER

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	boost::int64_t performance_counter_to_microseconds(boost::int64_t pc)
	{
		static LARGE_INTEGER performace_counter_frequency = {0,0};
		if (performace_counter_frequency.QuadPart == 0)
			QueryPerformanceFrequency(&performace_counter_frequency);

#ifdef TORRENT_DEBUG
		// make sure we don't overflow
		boost::int64_t ret = (pc * 1000 / performace_counter_frequency.QuadPart) * 1000;
		TORRENT_ASSERT((pc >= 0 && pc >= ret) || (pc < 0 && pc < ret));
#endif
		return ((pc * 1000 + performace_counter_frequency.QuadPart / 2) / performace_counter_frequency.QuadPart) * 1000;
	}

	boost::int64_t microseconds_to_performance_counter(boost::int64_t ms)
	{
		static LARGE_INTEGER performace_counter_frequency = {0,0};
		if (performace_counter_frequency.QuadPart == 0)
			QueryPerformanceFrequency(&performace_counter_frequency);
#ifdef TORRENT_DEBUG
		// make sure we don't overflow
		boost::int64_t ret = (ms / 1000) * performace_counter_frequency.QuadPart / 1000;
		TORRENT_ASSERT((ms >= 0 && ms <= ret)
			|| (ms < 0 && ms > ret));
#endif
		return (ms / 1000) * performace_counter_frequency.QuadPart / 1000;
	}

	ptime time_now_hires()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return ptime(now.QuadPart);
	}

	boost::int64_t total_seconds(time_duration td)
	{
		return boost::int64_t(performance_counter_to_microseconds(td.diff)
			/ 1000000);
	}
	boost::int64_t total_milliseconds(time_duration td)
	{
		return boost::uint64_t(performance_counter_to_microseconds(td.diff)
			/ 1000);
	}
	boost::int64_t total_microseconds(time_duration td)
	{
		return performance_counter_to_microseconds(td.diff);
	}

	time_duration microsec(boost::int64_t s)
	{
		return time_duration(microseconds_to_performance_counter(s));
	}
	time_duration milliseconds(boost::int64_t s)
	{
		return time_duration(microseconds_to_performance_counter(
			s * 1000));
	}
	time_duration seconds(boost::int64_t s)
	{
		return time_duration(microseconds_to_performance_counter(
			s * 1000000));
	}
	time_duration minutes(boost::int64_t s)
	{
		return time_duration(microseconds_to_performance_counter(
			s * 1000000 * 60));
	}
	time_duration hours(boost::int64_t s)
	{
		return time_duration(microseconds_to_performance_counter(
			s * 1000000 * 60 * 60));
	}
}

#elif defined TORRENT_USE_CLOCK_GETTIME

#include <time.h>
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	ptime time_now_hires()
	{
		timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return ptime(boost::uint64_t(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000);
	}
}

#elif defined TORRENT_USE_SYSTEM_TIME

#include <kernel/OS.h>

namespace libtorrent
{
	ptime time_now_hires()
	{ return ptime(system_time()); }
}

#endif // TORRENT_USE_SYSTEM_TIME

#endif // TORRENT_USE_BOOST_DATE_TIME

