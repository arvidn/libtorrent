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

#include <boost/version.hpp>
#include "libtorrent/config.hpp"
#include "libtorrent/ptime.hpp"
#include <boost/cstdint.hpp>
#include <string>

namespace libtorrent
{
	TORRENT_EXPORT char const* time_now_string();
	TORRENT_EXPORT std::string log_time();

	TORRENT_EXPORT ptime time_now_hires();
	TORRENT_EXPORT ptime min_time();
	TORRENT_EXPORT ptime max_time();

#if defined TORRENT_USE_BOOST_DATE_TIME

	TORRENT_EXPORT time_duration seconds(int s);
	TORRENT_EXPORT time_duration milliseconds(int s);
	TORRENT_EXPORT time_duration microsec(int s);
	TORRENT_EXPORT time_duration minutes(int s);
	TORRENT_EXPORT time_duration hours(int s);

	TORRENT_EXPORT int total_seconds(time_duration td);
	TORRENT_EXPORT int total_milliseconds(time_duration td);
	TORRENT_EXPORT boost::int64_t total_microseconds(time_duration td);

#elif defined TORRENT_USE_QUERY_PERFORMANCE_TIMER

	namespace aux
	{
		TORRENT_EXPORT boost::int64_t performance_counter_to_microseconds(boost::int64_t pc);
		TORRENT_EXPORT boost::int64_t microseconds_to_performance_counter(boost::int64_t ms);
	}

	inline int total_seconds(time_duration td)
	{
		return boost::int64_t(aux::performance_counter_to_microseconds(td.diff)
			/ 1000000);
	}
	inline int total_milliseconds(time_duration td)
	{
		return boost::int64_t(aux::performance_counter_to_microseconds(td.diff)
			/ 1000);
	}
	inline boost::int64_t total_microseconds(time_duration td)
	{
		return aux::performance_counter_to_microseconds(td.diff);
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

#elif TORRENT_USE_CLOCK_GETTIME || TORRENT_USE_SYSTEM_TIME || TORRENT_USE_ABSOLUTE_TIME

	inline int total_seconds(time_duration td)
	{ return td.diff / 1000000; }
	inline int total_milliseconds(time_duration td)
	{ return td.diff / 1000; }
	inline boost::int64_t total_microseconds(time_duration td)
	{ return td.diff; }

	inline time_duration microsec(boost::int64_t s)
	{ return time_duration(s); }
	inline time_duration milliseconds(boost::int64_t s)
	{ return time_duration(s * 1000); }
	inline time_duration seconds(boost::int64_t s)
	{ return time_duration(s * 1000000); }
	inline time_duration minutes(boost::int64_t s)
	{ return time_duration(s * 1000000 * 60); }
	inline time_duration hours(boost::int64_t s)
	{ return time_duration(s * 1000000 * 60 * 60); }

#endif // TORRENT_USE_CLOCK_GETTIME

}

#endif // TORRENT_TIME_HPP_INCLUDED

