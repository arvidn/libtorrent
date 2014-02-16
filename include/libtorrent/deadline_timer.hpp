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

#ifndef TORRENT_DEADLINE_TIMER_HPP_INCLUDED
#define TORRENT_DEADLINE_TIMER_HPP_INCLUDED

#ifdef __OBJC__
#define Protocol Protocol_
#endif

#if __GNUC__ < 3
// in GCC 2.95 templates seems to have all symbols
// resolved as they are parsed, so the time_traits
// template actually needs the definitions it uses,
// even though it's never instantiated
#include <boost/date_time/posix_time/posix_time_types.hpp>
#else
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#endif

#if BOOST_VERSION < 103500
#include <asio/basic_deadline_timer.hpp>
#else
#include <boost/asio/basic_deadline_timer.hpp>
#endif

#ifdef __OBJC__ 
#undef Protocol
#endif

#include "libtorrent/time.hpp"

// asio time_traits
#if !TORRENT_USE_BOOST_DATE_TIME
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
#endif

namespace libtorrent
{

#if BOOST_VERSION < 103500
	typedef ::asio::basic_deadline_timer<libtorrent::ptime> deadline_timer;
#else
	typedef boost::asio::basic_deadline_timer<libtorrent::ptime> deadline_timer;
#endif
}

#endif // TORRENT_DEADLINE_TIMER_HPP_INCLUDED

