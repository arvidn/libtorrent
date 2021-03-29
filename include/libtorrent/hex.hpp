/*

Copyright (c) 2004, 2009, 2013, 2015-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_HEX_HPP_INCLUDED
#define TORRENT_HEX_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/span.hpp"

#include <string>

namespace lt {

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
} // namespace lt

#endif // TORRENT_HEX_HPP_INCLUDED
