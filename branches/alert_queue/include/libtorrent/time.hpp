/*

Copyright (c) 2007-2014, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include <boost/cstdint.hpp>

#if defined BOOST_ASIO_HAS_STD_CHRONO
#include <chrono>
#else
#include <boost/chrono.hpp>
#endif

namespace libtorrent {

#if defined BOOST_ASIO_HAS_STD_CHRONO
	typedef std::chrono::high_resolution_clock clock_type;
#else
	typedef boost::chrono::high_resolution_clock clock_type;
#endif

	typedef clock_type::time_point time_point;
	typedef clock_type::duration time_duration;

#if defined BOOST_ASIO_HAS_STD_CHRONO
	using std::chrono::seconds;
	using std::chrono::milliseconds;
	using std::chrono::microseconds;
	using std::chrono::minutes;
	using std::chrono::hours;
	using std::chrono::duration_cast;
#else
	using boost::chrono::seconds;
	using boost::chrono::milliseconds;
	using boost::chrono::microseconds;
	using boost::chrono::minutes;
	using boost::chrono::hours;
	using boost::chrono::duration_cast;
#endif

	// internal
	inline time_point min_time() { return (time_point::min)(); }

	// internal
	inline time_point max_time() { return (time_point::max)(); }

	template<class T>
	boost::int64_t total_seconds(T td)
	{ return duration_cast<seconds>(td).count(); }

	template<class T>
	boost::int64_t total_milliseconds(T td)
	{ return duration_cast<milliseconds>(td).count(); }

	template<class T>
	boost::int64_t total_microseconds(T td)
	{ return duration_cast<microseconds>(td).count(); }

#ifndef TORRENT_NO_DEPRECATE

	TORRENT_DEPRECATED_PREFIX
	time_point time_now() TORRENT_DEPRECATED;

	TORRENT_DEPRECATED_PREFIX
	time_point time_now_hires() TORRENT_DEPRECATED;

	inline time_point time_now() 
	{ return clock_type::now(); }

	inline time_point time_now_hires()
	{ return clock_type::now(); }

	typedef time_point ptime TORRENT_DEPRECATED;

#endif

}

#endif // TORRENT_TIME_HPP_INCLUDED

