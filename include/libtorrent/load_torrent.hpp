/*

Copyright (c) 2022, Arvid Norberg
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

#ifndef TORRENT_LOAD_TORRENT_HPP_INCLUDED
#define TORRENT_LOAD_TORRENT_HPP_INCLUDED

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/torrent_info.hpp" // for load_torrent_limits
#include "libtorrent/aux_/export.hpp"

namespace libtorrent {

	// These functions load the content of a .torrent file into an
	// add_torrent_params object.
	// The immutable part of a torrent file (the info-dictionary) is stored in
	// the ``ti`` field in the add_torrent_params object (as a torrent_info
	// object).
	// The returned object is suitable to be:
	//
	//   * added to a session via add_torrent() or async_add_torrent()
	//   * saved as a .torrent_file via write_torrent_file()
	//   * turned into a magnet link via make_magnet_uri()
	TORRENT_EXPORT add_torrent_params load_torrent_file(
		std::string const& filename, load_torrent_limits const& cfg);
	TORRENT_EXPORT add_torrent_params load_torrent_file(
		std::string const& filename);
	TORRENT_EXPORT add_torrent_params load_torrent_buffer(
		span<char const> buffer, load_torrent_limits const& cfg);
	TORRENT_EXPORT add_torrent_params load_torrent_buffer(
		span<char const> buffer);
	TORRENT_EXPORT add_torrent_params load_torrent_parsed(
		bdecode_node const& torrent_file, load_torrent_limits const& cfg);
	TORRENT_EXPORT add_torrent_params load_torrent_parsed(
		bdecode_node const& torrent_file);

}

#endif
