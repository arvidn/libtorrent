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

namespace
{

	std::ofstream malloc_log;
	std::ofstream malloc_index_log;

	using boost::multi_index_container;
	using namespace boost::multi_index;

	struct allocation_point_t
	{
		allocation_point_t(): allocated(0) {}
		int index;
		boost::int64_t allocated;
	};

	typedef boost::array<void*, 10> stacktrace_t;
	typedef std::map<stacktrace_t, allocation_point_t> allocation_map_t;
	allocation_map_t allocation_points;
	std::map<void*, std::pair<allocation_map_t::iterator, int> > allocations;
	int allocation_point_index = 0;

	// the original library functions
	void* (*old_malloc_hook)(size_t, const void *);
	void (*old_free_hook)(void*, const void *);

	void my_free_hook(void *ptr, const void *caller);

	void* my_malloc_hook(size_t size, const void *caller)
	{
		void *result;
		/* Restore all old hooks */
		__malloc_hook = old_malloc_hook;
		__free_hook = old_free_hook;
		/* Call recursively */
		result = malloc (size);
		/* Save underlying hooks */
		old_malloc_hook = __malloc_hook;
		old_free_hook = __free_hook;

		stacktrace_t stack;
		int stacksize = backtrace(&stack[0], 10);

		allocation_map_t::iterator i = allocation_points.lower_bound(stack);
		if (i == allocation_points.end() || i->first != stack)
		{
			i = allocation_points.insert(i, std::make_pair(stack, allocation_point_t()));
			i->second.index = allocation_point_index++;
			i->second.allocated = size;

			malloc_index_log << "#" << i->second.index << " ";
			char** symbols = backtrace_symbols(&stack[0], stacksize);
			for (int j = 0; j < stacksize; ++j)
				malloc_index_log << symbols[j] << " ";
			malloc_index_log << std::endl;
		}
		else
		{
			i->second.allocated += size;
		}

		allocations[result] = std::make_pair(i, size);
		malloc_log << "#" << i->second.index << " " << time(0) << " MALLOC "
			<< result << " " << size << " (" << i->second.allocated << ")" << std::endl;

		/* Restore our own hooks */
		__malloc_hook = my_malloc_hook;
		__free_hook = my_free_hook;
		return result;
	}

	void my_free_hook(void *ptr, const void *caller)
	{
		/* Restore all old hooks */
		__malloc_hook = old_malloc_hook;
		__free_hook = old_free_hook;
		/* Call recursively */
		free (ptr);
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
			malloc_log << "#" << ap.index << " " << time(0) << " FREE "
				<< ptr << " " << size << " (" << ap.allocated << ")" << std::endl;
			allocations.erase(i);
		}

		/* Restore our own hooks */
		__malloc_hook = my_malloc_hook;
		__free_hook = my_free_hook;
	}

	void my_init_hook(void)
	{
		old_malloc_hook = __malloc_hook;
		old_free_hook = __free_hook;
		__malloc_hook = my_malloc_hook;
		__free_hook = my_free_hook;

		malloc_log.open("memory.log");
		malloc_index_log.open("memory_index.log");
	}

}

// Override initializing hook from the C library.
void (*__malloc_initialize_hook) (void) = my_init_hook;

#endif

