/*

Copyright (c) 2009-2010, 2012, 2017-2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_ALLOCA_HPP_INCLUDED
#define TORRENT_ALLOCA_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include <iterator> // for iterator_traits
#include <memory> // for addressof

namespace lt::aux {

template<class ForwardIt>
inline void uninitialized_default_construct(ForwardIt first, ForwardIt last)
{
	using Value = typename std::iterator_traits<ForwardIt>::value_type;
	ForwardIt current = first;
	try {
		for (; current != last; ++current) {
			::new (static_cast<void*>(std::addressof(*current))) Value;
		}
	}  catch (...) {
		for (; first != current; ++first) {
			first->~Value();
		}
		throw;
	}
}

template <typename T>
struct alloca_destructor
{
	static std::ptrdiff_t const cutoff = 4096 / sizeof(T);

	span<T> objects;
	~alloca_destructor()
	{
		if (objects.size() > cutoff)
		{
			delete [] objects.data();
		}
		else
		{
			for (auto& o : objects)
			{
				TORRENT_UNUSED(o);
				o.~T();
			}
		}
	}
};

}

#if defined TORRENT_WINDOWS || defined TORRENT_MINGW

#include <malloc.h>
#define TORRENT_ALLOCA_FUN _alloca

#elif defined TORRENT_BSD

#include <stdlib.h>
#define TORRENT_ALLOCA_FUN alloca

#else

#include <alloca.h>
#define TORRENT_ALLOCA_FUN alloca

#endif

#define TORRENT_ALLOCA(v, t, n) ::lt::span<t> v; { \
	auto TORRENT_ALLOCA_size = ::lt::aux::numeric_cast<std::ptrdiff_t>(n); \
	if (TORRENT_ALLOCA_size > ::lt::aux::alloca_destructor<t>::cutoff) {\
		v = ::lt::span<t>(new t[::lt::aux::numeric_cast<std::size_t>(n)], TORRENT_ALLOCA_size); \
	} \
	else { \
		auto* TORRENT_ALLOCA_tmp = static_cast<t*>(TORRENT_ALLOCA_FUN(sizeof(t) * static_cast<std::size_t>(n))); \
		v = ::lt::span<t>(TORRENT_ALLOCA_tmp, TORRENT_ALLOCA_size); \
		::lt::aux::uninitialized_default_construct(v.begin(), v.end()); \
	} \
} \
::lt::aux::alloca_destructor<t> v##_destructor{v}

#endif // TORRENT_ALLOCA_HPP_INCLUDED
