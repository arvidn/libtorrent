/*

Copyright (c) 2008-2016, Arvid Norberg
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

#ifndef TORRENT_ALLOCA_HPP_INCLUDED
#define TORRENT_ALLOCA_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include <iterator> // for iterator_traits
#include <memory> // for addressof

namespace libtorrent { namespace aux {

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
	span<T> objects;
	~alloca_destructor()
	{
		for (auto& o : objects)
		{
			TORRENT_UNUSED(o);
			o.~T();
		}
	}
};

}}

#if defined TORRENT_WINDOWS || defined TORRENT_MINGW

#include <malloc.h>
#define TORRENT_ALLOCA(v, t, n) ::libtorrent::span<t> v; { \
	auto TORRENT_ALLOCA_size = ::libtorrent::aux::numeric_cast<std::ptrdiff_t>(n); \
	auto* TORRENT_ALLOCA_tmp = static_cast<t*>(_alloca(sizeof(t) * static_cast<std::size_t>(TORRENT_ALLOCA_size))); \
	v = ::libtorrent::span<t>(TORRENT_ALLOCA_tmp, TORRENT_ALLOCA_size); \
	::libtorrent::aux::uninitialized_default_construct(v.begin(), v.end()); \
	} \
	::libtorrent::aux::alloca_destructor<t> v##_destructor{v}

#elif defined TORRENT_BSD

#include <stdlib.h>
#define TORRENT_ALLOCA(v, t, n) ::libtorrent::span<t> v; { \
	auto TORRENT_ALLOCA_size = ::libtorrent::aux::numeric_cast<std::ptrdiff_t>(n); \
	auto* TORRENT_ALLOCA_tmp = static_cast<t*>(alloca(sizeof(t) * static_cast<std::size_t>(TORRENT_ALLOCA_size))); \
	v = ::libtorrent::span<t>(TORRENT_ALLOCA_tmp, TORRENT_ALLOCA_size); \
	::libtorrent::aux::uninitialized_default_construct(v.begin(), v.end()); \
	} \
	::libtorrent::aux::alloca_destructor<t> v##_destructor{v}

#else

#include <alloca.h>
#define TORRENT_ALLOCA(v, t, n) ::libtorrent::span<t> v; { \
	auto TORRENT_ALLOCA_size = ::libtorrent::aux::numeric_cast<std::ptrdiff_t>(n); \
	auto* TORRENT_ALLOCA_tmp = static_cast<t*>(alloca(sizeof(t) * static_cast<std::size_t>(TORRENT_ALLOCA_size))); \
	v = ::libtorrent::span<t>(TORRENT_ALLOCA_tmp, TORRENT_ALLOCA_size); \
	::libtorrent::aux::uninitialized_default_construct(v.begin(), v.end()); \
	} \
	::libtorrent::aux::alloca_destructor<t> v##_destructor{v}

#endif

#endif // TORRENT_ALLOCA_HPP_INCLUDED
