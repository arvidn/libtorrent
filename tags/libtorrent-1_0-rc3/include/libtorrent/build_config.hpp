/*

Copyright (c) 2010-2014, Arvid Norberg
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

#ifndef TORRENT_BUILD_CONFIG_HPP_INCLUDED
#define TORRENT_BUILD_CONFIG_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#if TORRENT_USE_BOOST_DATE_TIME
#define TORRENT_CFG_TIME boosttime_
#elif TORRENT_USE_ABSOLUTE_TIME
#define TORRENT_CFG_TIME absolutetime_
#elif TORRENT_USE_QUERY_PERFORMANCE_TIMER
#define TORRENT_CFG_TIME performancetimer_
#elif TORRENT_USE_CLOCK_GETTIME
#define TORRENT_CFG_TIME clocktime_
#elif TORRENT_USE_SYSTEM_TIME
#define TORRENT_CFG_TIME systime_
#else
#error what timer is used?
#endif

#if TORRENT_USE_IPV6
#define TORRENT_CFG_IPV6 ipv6_
#else
#define TORRENT_CFG_IPV6 noipv_-
#endif

#ifdef TORRENT_NO_DEPRECATE
#define TORRENT_CFG_DEPR nodeprecate_
#else
#define TORRENT_CFG_DEPR deprecated_
#endif

#define TORRENT_CFG \
	BOOST_PP_CAT(TORRENT_CFG_TIME, \
	TORRENT_CFG_DEPR)

#define TORRENT_CFG_STRING BOOST_PP_STRINGIZE(TORRENT_CFG)

#endif

