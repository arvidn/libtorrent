/*

Copyright (c) 2014-2018, Arvid Norberg, Steven Siloti
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

#include "libtorrent/aux_/session_call.hpp"

namespace libtorrent { namespace aux {

#ifdef TORRENT_PROFILE_CALLS
static std::mutex g_calls_mutex;
static std::unordered_map<std::string, int> g_blocking_calls;
#endif

void blocking_call()
{
#ifdef TORRENT_PROFILE_CALLS
	char stack[2048];
	print_backtrace(stack, sizeof(stack), 20);
	std::unique_lock<std::mutex> l(g_calls_mutex);
	g_blocking_calls[stack] += 1;
#endif
}

void dump_call_profile()
{
#ifdef TORRENT_PROFILE_CALLS
	FILE* out = fopen("blocking_calls.txt", "w+");

	std::map<int, std::string> profile;

	std::unique_lock<std::mutex> l(g_calls_mutex);
	for (auto const& c : g_blocking_calls)
	{
		profile[c.second] = c.first;
	}
	for (std::map<int, std::string>::const_reverse_iterator i = profile.rbegin()
		, end(profile.rend()); i != end; ++i)
	{
		std::fprintf(out, "\n\n%d\n%s\n", i->first, i->second.c_str());
	}
	fclose(out);
#endif
}

void torrent_wait(bool& done, aux::session_impl& ses)
{
	blocking_call();
	std::unique_lock<std::mutex> l(ses.mut);
	while (!done) { ses.cond.wait(l); }
}

} } // namespace aux namespace libtorrent
