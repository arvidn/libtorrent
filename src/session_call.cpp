/*

Copyright (c) 2014-2019, Steven Siloti
Copyright (c) 2014, 2016, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/session_call.hpp"

namespace lt::aux {

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

} // namespace lt::aux
