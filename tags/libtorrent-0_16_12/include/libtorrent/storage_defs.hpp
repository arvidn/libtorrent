/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_STORAGE_DEFS_HPP_INCLUDE
#define TORRENT_STORAGE_DEFS_HPP_INCLUDE

#include "libtorrent/config.hpp"
#include <boost/function.hpp>
#include <string>

namespace libtorrent
{
	struct storage_interface;
	class file_storage;
	struct file_pool;

	enum storage_mode_t
	{
		storage_mode_allocate = 0,
		storage_mode_sparse,
		// this is here for internal use
		internal_storage_mode_compact_deprecated,
#ifndef TORRENT_NO_DEPRECATE
		storage_mode_compact = internal_storage_mode_compact_deprecated
#endif
	};
	
	typedef boost::function<storage_interface*(file_storage const&, file_storage const*
		, std::string const&, file_pool&, std::vector<boost::uint8_t> const&)> storage_constructor_type;

	TORRENT_EXPORT storage_interface* default_storage_constructor(
		file_storage const&, file_storage const* mapped, std::string const&, file_pool&
		, std::vector<boost::uint8_t> const&);

	TORRENT_EXPORT storage_interface* disabled_storage_constructor(
		file_storage const&, file_storage const* mapped, std::string const&, file_pool&
		, std::vector<boost::uint8_t> const&);

}

#endif

