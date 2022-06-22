/*

Copyright (c) 2017-2019, 2021, Arvid Norberg
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
#ifndef TORRENT_NOEXCEPT_MOVABLE_HPP_INCLUDED
#define TORRENT_NOEXCEPT_MOVABLE_HPP_INCLUDED

#include <type_traits>
#include <utility>

namespace libtorrent {
namespace aux {

#if defined _MSC_VER && defined TORRENT_BUILD_SIMULATOR
	// see simulation/test_error_handling.cpp for a description of this variable
	extern thread_local int g_must_not_fail;

	template <typename T>
	T&& wrap(T&& v) {
		++g_must_not_fail;
		return v;
	}
#endif

	// this is a silly wrapper for address and endpoint to make their move
	// constructors be noexcept (because boost.asio is incorrectly making them
	// potentially throwing). This can be removed once libtorrent uses the
	// networking TS.
	template <typename T>
	struct noexcept_movable : T
	{
		noexcept_movable() = default;
#if defined _MSC_VER && defined TORRENT_BUILD_SIMULATOR
		noexcept_movable(noexcept_movable<T>&& rhs) noexcept
			: T(std::forward<T>(wrap(rhs)))
		{
			--g_must_not_fail;
		}
		noexcept_movable(T&& rhs) noexcept : T(std::forward<T>(wrap(rhs))) // NOLINT
		{
			--g_must_not_fail;
		}
#else
		noexcept_movable(noexcept_movable<T>&& rhs) noexcept
			: T(std::forward<T>(rhs)) {}
		noexcept_movable(T&& rhs) noexcept : T(std::forward<T>(rhs)) {} // NOLINT
#endif
		noexcept_movable(noexcept_movable<T> const& rhs) = default;
		noexcept_movable(T const& rhs) : T(rhs) {} // NOLINT
		noexcept_movable& operator=(noexcept_movable const& rhs) = default;
		noexcept_movable& operator=(noexcept_movable&& rhs) = default;
		using T::T;
		using T::operator=;
	};

	template <typename T>
	struct noexcept_move_only : T
	{
#if defined _MSC_VER && defined TORRENT_BUILD_SIMULATOR
		noexcept_move_only(noexcept_move_only<T>&& rhs) noexcept
			: T(std::forward<T>(wrap(rhs)))
		{
			--g_must_not_fail;
		}
		noexcept_move_only(T&& rhs) noexcept : T(std::forward<T>(wrap(rhs))) // NOLINT
		{
			--g_must_not_fail;
		}
#else
		noexcept_move_only(noexcept_move_only<T>&& rhs) noexcept
			: T(std::forward<T>(rhs))
		{}
		noexcept_move_only(T&& rhs) noexcept : T(std::forward<T>(rhs)) {} // NOLINT
#endif
		noexcept_move_only(noexcept_move_only<T> const& rhs) = default;
		noexcept_move_only(T const& rhs) : T(rhs) {} // NOLINT
		noexcept_move_only& operator=(noexcept_move_only const& rhs) = default;
		noexcept_move_only& operator=(noexcept_move_only&& rhs) = default;
		using T::T;
	};

}
}

#endif
