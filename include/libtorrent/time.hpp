/*

Copyright (c) 2007, Arvid Norberg
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

#ifndef TORRENT_TIME_HPP_INCLUDED
#define TORRENT_TIME_HPP_INCLUDED

#include <ctime>
#include <boost/version.hpp>
#include "libtorrent/config.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace libtorrent
{
	inline char const* time_now_string()
	{
		time_t t = std::time(0);
		tm* timeinfo = std::localtime(&t);
		static char str[200];
		std::strftime(str, 200, "%b %d %X", timeinfo);
		return str;
	}

	std::string log_time();
}

#if defined TORRENT_USE_BOOST_DATE_TIME

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	typedef boost::posix_time::ptime ptime;
	typedef boost::posix_time::time_duration time_duration;
	inline ptime time_now_hires()
	{ return boost::posix_time::microsec_clock::universal_time(); }
	inline ptime min_time()
	{ return boost::posix_time::ptime(boost::posix_time::min_date_time); }
	inline ptime max_time()
	{ return boost::posix_time::ptime(boost::posix_time::max_date_time); }
	inline time_duration seconds(int s) { return boost::posix_time::seconds(s); }
	inline time_duration milliseconds(int s) { return boost::posix_time::milliseconds(s); }
	inline time_duration microsec(int s) { return boost::posix_time::microsec(s); }
	inline time_duration minutes(int s) { return boost::posix_time::minutes(s); }
	inline time_duration hours(int s) { return boost::posix_time::hours(s); }

	inline int total_seconds(time_duration td)
	{ return td.total_seconds(); }
	inline int total_milliseconds(time_duration td)
	{ return td.total_milliseconds(); }
	inline boost::int64_t total_microseconds(time_duration td)
	{ return td.total_microseconds(); }

}

#else // TORRENT_USE_BOOST_DATE_TIME

#if BOOST_VERSION < 103500
#include <asio/time_traits.hpp>
#else
#include <boost/asio/time_traits.hpp>
#endif
#include <boost/cstdint.hpp>
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	// libtorrent time_duration type
	struct time_duration
	{
		time_duration() {}
		time_duration operator/(int rhs) const { return time_duration(diff / rhs); }
		explicit time_duration(boost::int64_t d) : diff(d) {}
		time_duration& operator-=(time_duration const& c) { diff -= c.diff; return *this; }
		time_duration& operator+=(time_duration const& c) { diff += c.diff; return *this; }
		time_duration operator+(time_duration const& c) { return time_duration(diff + c.diff); }
		time_duration operator-(time_duration const& c) { return time_duration(diff - c.diff); }
		boost::int64_t diff;
	};

	inline bool is_negative(time_duration dt) { return dt.diff < 0; }
	inline bool operator==(time_duration lhs, time_duration rhs)
	{ return lhs.diff == rhs.diff; }
	inline bool operator<(time_duration lhs, time_duration rhs)
	{ return lhs.diff < rhs.diff; }
	inline bool operator<=(time_duration lhs, time_duration rhs)
	{ return lhs.diff <= rhs.diff; }
	inline bool operator>(time_duration lhs, time_duration rhs)
	{ return lhs.diff > rhs.diff; }
	inline bool operator>=(time_duration lhs, time_duration rhs)
	{ return lhs.diff >= rhs.diff; }
	inline time_duration operator*(time_duration lhs, int rhs)
	{ return time_duration(boost::int64_t(lhs.diff * rhs)); }
	inline time_duration operator*(int lhs, time_duration rhs)
	{ return time_duration(boost::int64_t(lhs * rhs.diff)); }

	// libtorrent time type
	struct ptime
	{
		ptime() {}
		explicit ptime(boost::uint64_t t): time(t) {}
		ptime& operator+=(time_duration rhs) { time += rhs.diff; return *this; }
		ptime& operator-=(time_duration rhs) { time -= rhs.diff; return *this; }
		boost::uint64_t time;
	};

	inline bool operator>(ptime lhs, ptime rhs)
	{ return lhs.time > rhs.time; }
	inline bool operator>=(ptime lhs, ptime rhs)
	{ return lhs.time >= rhs.time; }
	inline bool operator<=(ptime lhs, ptime rhs)
	{ return lhs.time <= rhs.time; }
	inline bool operator<(ptime lhs, ptime rhs)
	{ return lhs.time < rhs.time; }
	inline bool operator!=(ptime lhs, ptime rhs)
	{ return lhs.time != rhs.time;}
	inline bool operator==(ptime lhs, ptime rhs)
	{ return lhs.time == rhs.time;}
	inline time_duration operator-(ptime lhs, ptime rhs)
	{ return time_duration(lhs.time - rhs.time); }
	inline ptime operator+(ptime lhs, time_duration rhs)
	{ return ptime(lhs.time + rhs.diff); }
	inline ptime operator+(time_duration lhs, ptime rhs)
	{ return ptime(rhs.time + lhs.diff); }
	inline ptime operator-(ptime lhs, time_duration rhs)
	{ return ptime(lhs.time - rhs.diff); }

	ptime time_now_hires();
	inline ptime min_time() { return ptime(0); }
	inline ptime max_time() { return ptime((std::numeric_limits<boost::uint64_t>::max)()); }
	int total_seconds(time_duration td);
	int total_milliseconds(time_duration td);
	boost::int64_t total_microseconds(time_duration td);
}

// asio time_traits
#if BOOST_VERSION >= 103500
namespace boost { 
#endif
namespace asio
{
	template<>
	struct time_traits<libtorrent::ptime>
	{
		typedef libtorrent::ptime time_type;
		typedef libtorrent::time_duration duration_type;
		static time_type now()
		{ return time_type(libtorrent::time_now_hires()); }
		static time_type add(time_type t, duration_type d)
		{ return time_type(t.time + d.diff);}
		static duration_type subtract(time_type t1, time_type t2)
		{ return duration_type(t1 - t2); }
		static bool less_than(time_type t1, time_type t2)
		{ return t1 < t2; }
		static boost::posix_time::time_duration to_posix_duration(
			duration_type d)
		{ return boost::posix_time::microseconds(libtorrent::total_microseconds(d)); }
	};
}
#if BOOST_VERSION >= 103500
}
#endif

#if defined TORRENT_USE_ABSOLUTE_TIME

#include <mach/mach_time.h>
#include <boost/cstdint.hpp>
#include "libtorrent/assert.hpp"

// high precision timer for darwin intel and ppc

namespace libtorrent
{
	inline int total_seconds(time_duration td)
	{
		return td.diff / 1000000;
	}
	inline int total_milliseconds(time_duration td)
	{
		return td.diff / 1000;
	}
	inline boost::int64_t total_microseconds(time_duration td)
	{
		return td.diff;
	}

	inline ptime time_now_hires()
	{
		static mach_timebase_info_data_t timebase_info = {0,0};
		if (timebase_info.denom == 0)
			mach_timebase_info(&timebase_info);
		boost::uint64_t at = mach_absolute_time();
		// make sure we don't overflow
		TORRENT_ASSERT((at >= 0 && at >= at / 1000 * timebase_info.numer / timebase_info.denom)
			|| (at < 0 && at < at / 1000 * timebase_info.numer / timebase_info.denom));
		return ptime(at / 1000 * timebase_info.numer / timebase_info.denom);
	}

	inline time_duration microsec(boost::int64_t s)
	{
		return time_duration(s);
	}
	inline time_duration milliseconds(boost::int64_t s)
	{
		return time_duration(s * 1000);
	}
	inline time_duration seconds(boost::int64_t s)
	{
		return time_duration(s * 1000000);
	}
	inline time_duration minutes(boost::int64_t s)
	{
		return time_duration(s * 1000000 * 60);
	}
	inline time_duration hours(boost::int64_t s)
	{
		return time_duration(s * 1000000 * 60 * 60);
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
	namespace aux
	{
		inline boost::int64_t performance_counter_to_microseconds(boost::int64_t pc)
		{
			static LARGE_INTEGER performace_counter_frequency = {0,0};
			if (performace_counter_frequency.QuadPart == 0)
				QueryPerformanceFrequency(&performace_counter_frequency);

#ifdef TORRENT_DEBUG
			// make sure we don't overflow
			boost::int64_t ret = (pc * 1000 / performace_counter_frequency.QuadPart) * 1000;
			TORRENT_ASSERT((pc >= 0 && pc >= ret) || (pc < 0 && pc < ret));
#endif
			return (pc * 1000 / performace_counter_frequency.QuadPart) * 1000;
		}

		inline boost::int64_t microseconds_to_performance_counter(boost::int64_t ms)
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
	}

	inline int total_seconds(time_duration td)
	{
		return int(aux::performance_counter_to_microseconds(td.diff)
			/ 1000000);
	}
	inline int total_milliseconds(time_duration td)
	{
		return int(aux::performance_counter_to_microseconds(td.diff)
			/ 1000);
	}
	inline boost::int64_t total_microseconds(time_duration td)
	{
		return aux::performance_counter_to_microseconds(td.diff);
	}

	inline ptime time_now_hires()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return ptime(now.QuadPart);
	}

	inline time_duration microsec(boost::int64_t s)
	{
		return time_duration(aux::microseconds_to_performance_counter(s));
	}
	inline time_duration milliseconds(boost::int64_t s)
	{
		return time_duration(aux::microseconds_to_performance_counter(
			s * 1000));
	}
	inline time_duration seconds(boost::int64_t s)
	{
		return time_duration(aux::microseconds_to_performance_counter(
			s * 1000000));
	}
	inline time_duration minutes(boost::int64_t s)
	{
		return time_duration(aux::microseconds_to_performance_counter(
			s * 1000000 * 60));
	}
	inline time_duration hours(boost::int64_t s)
	{
		return time_duration(aux::microseconds_to_performance_counter(
			s * 1000000 * 60 * 60));
	}

}

#elif defined TORRENT_USE_CLOCK_GETTIME

#include <time.h>
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	inline int total_seconds(time_duration td)
	{
		return td.diff / 1000000;
	}
	inline int total_milliseconds(time_duration td)
	{
		return td.diff / 1000;
	}
	inline boost::int64_t total_microseconds(time_duration td)
	{
		return td.diff;
	}

	inline ptime time_now_hires()
	{
		timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return ptime(boost::uint64_t(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000);
	}

	inline time_duration microsec(boost::int64_t s)
	{
		return time_duration(s);
	}
	inline time_duration milliseconds(boost::int64_t s)
	{
		return time_duration(s * 1000);
	}
	inline time_duration seconds(boost::int64_t s)
	{
		return time_duration(s * 1000000);
	}
	inline time_duration minutes(boost::int64_t s)
	{
		return time_duration(s * 1000000 * 60);
	}
	inline time_duration hours(boost::int64_t s)
	{
		return time_duration(s * 1000000 * 60 * 60);
	}

}

#endif // TORRENT_USE_CLOCK_GETTIME

#endif // TORRENT_USE_BOOST_DATE_TIME

namespace libtorrent
{
	TORRENT_EXPORT ptime const& time_now();
}

#endif // TORRENT_TIME_HPP_INCLUDED

