/*

Copyright (c) 2009, 2015, 2017-2019, Arvid Norberg
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

#include "libtorrent/aux_/time.hpp"

#include <chrono>

namespace libtorrent { namespace aux {

	time_point time_now() { return clock_type::now(); }
	time_point32 time_now32() { return time_point_cast<seconds32>(clock_type::now()); }

	// for simplifying implementation
	using std::chrono::system_clock;


	// consider using std::chrono::clock_cast on C++20
	time_t to_time_t(const time_point32 tp)
	{
		// special case for unset value
		if (tp == time_point32(seconds32(0))) return 0;

		const auto lt_now = clock_type::now();
		const auto sys_now = system_clock::now();

		const auto r = sys_now + std::chrono::duration_cast<system_clock::duration>(tp - lt_now) + lt::milliseconds(500);
		return system_clock::to_time_t(r);
	}

	// consider using std::chrono::clock_cast on C++20
	time_point32 from_time_t(const std::time_t t)
	{
		// special case for unset value
		if (t == 0) return time_point32(seconds32(0));

		const auto tp = system_clock::from_time_t(t);
		const auto sys_now = system_clock::now();
		const auto lt_now = clock_type::now();

		auto r = lt_now + std::chrono::duration_cast<clock_type::duration>(tp - sys_now);
		// the conversion to seconds will truncate, make sure we round
		return std::chrono::time_point_cast<seconds32>(r + milliseconds(500));
	}

} }
