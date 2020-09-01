/*

Copyright (c) 2004, 2009, 2013, 2015-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
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

#ifndef TORRENT_HEX_HPP_INCLUDED
#define TORRENT_HEX_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/span.hpp"

#include <string>

namespace libtorrent {

namespace aux {

	TORRENT_EXTRA_EXPORT int hex_to_int(char in);
	TORRENT_EXTRA_EXPORT bool is_hex(span<char const> in);

#if TORRENT_ABI_VERSION == 1
#define TORRENT_CONDITIONAL_EXPORT TORRENT_EXPORT
#else
#define TORRENT_CONDITIONAL_EXPORT TORRENT_EXTRA_EXPORT
#endif

	// The overload taking a ``std::string`` converts (binary) the string ``s``
	// to hexadecimal representation and returns it.
	// The overload taking a ``char const*`` and a length converts the binary
	// buffer [``in``, ``in`` + len) to hexadecimal and prints it to the buffer
	// ``out``. The caller is responsible for making sure the buffer pointed to
	// by ``out`` is large enough, i.e. has at least len * 2 bytes of space.
	TORRENT_CONDITIONAL_EXPORT std::string to_hex(span<char const> s);
	TORRENT_CONDITIONAL_EXPORT void to_hex(span<char const> in, char* out);
	TORRENT_CONDITIONAL_EXPORT void to_hex(char const* in, int len, char* out);

	// converts the buffer [``in``, ``in`` + len) from hexadecimal to
	// binary. The binary output is written to the buffer pointed to
	// by ``out``. The caller is responsible for making sure the buffer
	// at ``out`` has enough space for the result to be written to, i.e.
	// (len + 1) / 2 bytes.
	TORRENT_CONDITIONAL_EXPORT bool from_hex(span<char const> in, char* out);

#undef TORRENT_CONDITIONAL_EXPORT

} // namespace aux

#if TORRENT_ABI_VERSION == 1

#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

	// deprecated in 1.2
	TORRENT_DEPRECATED
	inline void to_hex(char const* in, int len, char* out)
	{ aux::to_hex({in, len}, out); }
	TORRENT_DEPRECATED
	inline std::string to_hex(std::string const& s)
	{ return aux::to_hex(s); }
	TORRENT_DEPRECATED
	inline bool from_hex(char const *in, int len, char* out)
	{ return aux::from_hex({in, len}, out); }

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif
} // namespace libtorrent

#endif // TORRENT_HEX_HPP_INCLUDED
