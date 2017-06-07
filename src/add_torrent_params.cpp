/*

Copyright (c) 2017, Arvid Norberg, Alden Torres
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

#include "libtorrent/add_torrent_params.hpp"

namespace libtorrent {

	add_torrent_params::add_torrent_params(storage_constructor_type sc)
		: storage(storage_constructor_type(sc)) {}
	add_torrent_params::add_torrent_params(add_torrent_params&&) noexcept = default;
	add_torrent_params& add_torrent_params::operator=(add_torrent_params&&) = default;
	add_torrent_params::add_torrent_params(add_torrent_params const&) = default;
	add_torrent_params& add_torrent_params::operator=(add_torrent_params const&) = default;

	static_assert(std::is_nothrow_move_constructible<add_torrent_params>::value
		, "should be nothrow move constructible");

	// TODO: pre C++17, GCC and msvc does not make std::string nothrow move
	// assignable, which means no type containing a string will be nothrow move
	// assignable by default either
//	static_assert(std::is_nothrow_move_assignable<add_torrent_params>::value
//		, "should be nothrow move assignable");

	// TODO: it would be nice if this was nothrow default constructible
//	static_assert(std::is_nothrow_default_constructible<add_torrent_params>::value
//		, "should be nothrow default constructible");
}
