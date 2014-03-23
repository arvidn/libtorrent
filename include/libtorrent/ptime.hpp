/*

Copyright (c) 2009, Arvid Norberg
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

#ifndef TORRENT_PTIME_HPP_INCLUDED
#define TORRENT_PTIME_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include <string>

#if defined TORRENT_USE_BOOST_DATE_TIME

#include <boost/assert.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>

namespace libtorrent
{
	typedef boost::posix_time::ptime ptime;
	typedef boost::posix_time::time_duration time_duration;
}

#else // TORRENT_USE_BOOST_DATE_TIME

#include <boost/cstdint.hpp>

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
		time_duration& operator*=(int v) { diff *= v; return *this; }
		time_duration operator+(time_duration const& c) { return time_duration(diff + c.diff); }
		time_duration operator-(time_duration const& c) { return time_duration(diff - c.diff); }
		boost::int64_t diff;
	};

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

	inline time_duration operator-(ptime lhs, ptime rhs)
	{ return time_duration(lhs.time - rhs.time); }
	inline ptime operator+(ptime lhs, time_duration rhs)
	{ return ptime(lhs.time + rhs.diff); }
	inline ptime operator+(time_duration lhs, ptime rhs)
	{ return ptime(rhs.time + lhs.diff); }
	inline ptime operator-(ptime lhs, time_duration rhs)
	{ return ptime(lhs.time - rhs.diff); }

}

#endif // TORRENT_USE_BOOST_DATE_TIME

namespace libtorrent
{
	TORRENT_EXPORT ptime time_now_hires();
	TORRENT_EXPORT ptime min_time();
	TORRENT_EXPORT ptime max_time();

	TORRENT_EXPORT char const* time_now_string();
	TORRENT_EXPORT std::string log_time();

	TORRENT_EXPORT ptime const& time_now();
}

#endif

