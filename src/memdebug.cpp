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

#if defined __linux__ && defined __GNUC__
#include <execinfo.h>

// Prototypes for __malloc_hook, __free_hook
#include <malloc.h>
#include <boost/array.hpp>
#include <fstream>
#include <utility>
#include <map>
#include <boost/cstdint.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/mutex.hpp>
#include "libtorrent/time.hpp"
#include "libtorrent/assert.hpp"

using boost::multi_index_container;
using namespace boost::multi_index;
using libtorrent::time_now;

struct memdebug
{
	memdebug()
	{
		malloc_log.open("memory.log");
		malloc_index_log.open("memory_index.log");
	
		assert(old_malloc_hook == 0);
		assert(old_free_hook == 0);
		old_malloc_hook = __malloc_hook;
		old_free_hook = __free_hook;
		__malloc_hook = my_malloc_hook;
		__free_hook = my_free_hook;
	}

	static void my_free_hook(void *ptr, const void *caller);
	static void* my_malloc_hook(size_t size, const void *caller);

	static boost::mutex mutex;
	static std::ofstream malloc_log;
	static std::ofstream malloc_index_log;

	// the original library functions
	static void* (*old_malloc_hook)(size_t, const void *);
	static void (*old_free_hook)(void*, const void *);

	struct allocation_point_t
	{
		allocation_point_t()
			: allocated(0)
			, peak_allocated(0)
			, spacetime(0)
			, last_update(time_now()) {}

		int index;
		// total number of bytes allocated from this point
		int allocated;
		// the maximum total number of bytes allocated
		// from this point
		int peak_allocated;
		// total number of bytes allocated times the number of
		// milliseconds they were allocated from this point
		boost::int64_t spacetime;
		// the last malloc or free operation on
		// this allocation point. The spacetime
		// should be updated from this point to
		// the current operation
		libtorrent::ptime last_update;
	};

	typedef boost::array<void*, 15> stacktrace_t;
	typedef std::map<stacktrace_t, allocation_point_t> allocation_map_t;
	static allocation_map_t allocation_points;
	static std::map<void*, std::pair<allocation_map_t::iterator, int> > allocations;
	static int allocation_point_index;
	static libtorrent::ptime start_time;
};

boost::mutex memdebug::mutex;
int memdebug::allocation_point_index = 0;
std::ofstream memdebug::malloc_log;
std::ofstream memdebug::malloc_index_log;
void* (*memdebug::old_malloc_hook)(size_t, const void *) = 0;
void (*memdebug::old_free_hook)(void*, const void *) = 0;
memdebug::allocation_map_t memdebug::allocation_points;
std::map<void*, std::pair<memdebug::allocation_map_t::iterator, int> > memdebug::allocations;
libtorrent::ptime memdebug::start_time = time_now();

void* memdebug::my_malloc_hook(size_t size, const void *caller)
{
	boost::mutex::scoped_lock l(mutex);
	/* Restore all old hooks */
	__malloc_hook = old_malloc_hook;
	__free_hook = old_free_hook;
	/* Call recursively */
	void* result = malloc(size);
	/* Save underlying hooks */
	old_malloc_hook = __malloc_hook;
	old_free_hook = __free_hook;

	stacktrace_t stack;
	int stacksize = backtrace(&stack[0], stack.size());
	libtorrent::ptime now = time_now();

	allocation_map_t::iterator i = allocation_points.lower_bound(stack);
	if (i == allocation_points.end() || i->first != stack)
	{
		i = allocation_points.insert(i, std::make_pair(stack, allocation_point_t()));
		i->second.index = allocation_point_index++;
		i->second.allocated = size;

		malloc_index_log << i->second.index << "#";
		char** symbols = backtrace_symbols(&stack[0], stacksize);
		for (int j = 2; j < stacksize; ++j)
			malloc_index_log << demangle(symbols[j]) << "#";
		malloc_index_log << std::endl;
	}
	else
	{
		allocation_point_t& ap = i->second;
		ap.spacetime += libtorrent::total_milliseconds(now - ap.last_update) * ap.allocated;
		ap.allocated += size;
		if (ap.allocated > ap.peak_allocated) ap.peak_allocated = ap.allocated;
		ap.last_update = now;
	}
	allocation_point_t& ap = i->second;

	allocations[result] = std::make_pair(i, size);
	malloc_log << "#" << ap.index << " "
		<< libtorrent::total_milliseconds(time_now() - start_time) << " A "
		<< result << " " << size << " " << ap.allocated << " " << ap.spacetime
		<< " " << ap.peak_allocated << std::endl;

	/* Restore our own hooks */
	__malloc_hook = my_malloc_hook;
	__free_hook = my_free_hook;
	return result;
}

void memdebug::my_free_hook(void *ptr, const void *caller)
{
	boost::mutex::scoped_lock l(mutex);
	/* Restore all old hooks */
	__malloc_hook = old_malloc_hook;
	__free_hook = old_free_hook;
	/* Call recursively */
	free(ptr);
	/* Save underlying hooks */
	old_malloc_hook = __malloc_hook;
	old_free_hook = __free_hook;

	std::map<void*, std::pair<allocation_map_t::iterator, int> >::iterator i
		= allocations.find(ptr);

	if (i != allocations.end())
	{
		allocation_point_t& ap = i->second.first->second;
		int size = i->second.second;
		ap.allocated -= size;
		malloc_log << "#" << ap.index << " "
			<< libtorrent::total_milliseconds(time_now() - start_time) << " F "
			<< ptr << " " << size << " " << ap.allocated << " " << ap.spacetime
			<< " " << ap.peak_allocated << std::endl;

		allocations.erase(i);
	}

	/* Restore our own hooks */
	__malloc_hook = my_malloc_hook;
	__free_hook = my_free_hook;
}

static int ref_count = 0;

void start_malloc_debug()
{
	boost::mutex::scoped_lock l(memdebug::mutex);
	static memdebug mi;
	++ref_count;
}

void stop_malloc_debug()
{
	boost::mutex::scoped_lock l(memdebug::mutex);
	if (--ref_count == 0)
	{
		__malloc_hook = memdebug::old_malloc_hook;
		__free_hook = memdebug::old_free_hook;
	}
}

#endif

