/*

Copyright (c) 2015, Arvid Norberg
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

#ifndef TORRENT_READ_RESUME_DATA_HPP_INCLUDE
#define TORRENT_READ_RESUME_DATA_HPP_INCLUDE

#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/span.hpp"

namespace libtorrent {

	// these functions are used to parse resume data and populate the appropriate
	// fields in an add_torrent_params object. This object can then be used to add
	// the actual torrent_info object to and pass to session::add_torrent() or
	// session::async_add_torrent().
	//
	// If the client wants to override any field that was loaded from the resume
	// data, e.g. save_path, those fields must be changed after loading resume
	// data but before adding the torrent.
	TORRENT_EXPORT add_torrent_params read_resume_data(bdecode_node const& rd
		, error_code& ec);
	TORRENT_EXPORT add_torrent_params read_resume_data(span<char const> buffer
		, error_code& ec);
	TORRENT_EXPORT add_torrent_params read_resume_data(bdecode_node const& rd);
	TORRENT_EXPORT add_torrent_params read_resume_data(span<char const> buffer);
}

#endif
