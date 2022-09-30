/*

Copyright (c) 2017-2018, 2021, Arvid Norberg
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

#ifndef TORRENT_WRITE_RESUME_DATA_HPP_INCLUDE
#define TORRENT_WRITE_RESUME_DATA_HPP_INCLUDE

#include <vector>

#include "libtorrent/fwd.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

	// this function turns the resume data in an ``add_torrent_params`` object
	// into a bencoded structure
	TORRENT_EXPORT entry write_resume_data(add_torrent_params const& atp);
	TORRENT_EXPORT std::vector<char> write_resume_data_buf(add_torrent_params const& atp);

	// hidden
	using write_torrent_flags_t = flags::bitfield_flag<std::uint32_t, struct write_torrent_flags_tag>;

	namespace write_flags
	{
		// this makes write_torrent_file() not fail when attempting to write a
		// v2 torrent file that does not have all the piece layers
		constexpr write_torrent_flags_t allow_missing_piece_layer = 0_bit;

		// don't include http seeds in the torrent file, even if some are
		// present in the add_torrent_params object
		constexpr write_torrent_flags_t no_http_seeds = 1_bit;

		// When set, DHT nodes from the add_torrent_params objects are included
		// in the resulting .torrent file
		constexpr write_torrent_flags_t include_dht_nodes = 2_bit;
	}

	// writes only the fields to create a .torrent file. This function may fail
	// with a ``std::system_error`` exception if:
	//
	// * The add_torrent_params object passed to this function does not contain the
	//   info dictionary (the ``ti`` field)
	// * The piece layers are not complete for all files that need them
	//
	// The ``write_torrent_file_buf()`` overload returns the torrent file in
	// bencoded buffer form. This overload may be faster at the expense of lost
	// flexibility to add custom fields.
	TORRENT_EXPORT entry write_torrent_file(add_torrent_params const& atp);
	TORRENT_EXPORT entry write_torrent_file(add_torrent_params const& atp, write_torrent_flags_t flags);
	TORRENT_EXPORT std::vector<char> write_torrent_file_buf(add_torrent_params const& atp
		, write_torrent_flags_t flags);
}

#endif
