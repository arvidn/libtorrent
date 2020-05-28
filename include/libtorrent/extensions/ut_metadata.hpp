/*

Copyright (c) 2007, 2013, 2016-2017, 2019, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
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

#ifndef TORRENT_UT_METADATA_HPP_INCLUDED
#define TORRENT_UT_METADATA_HPP_INCLUDED

#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/config.hpp"

#include <memory>

namespace libtorrent {

	struct torrent_plugin;
	struct torrent_handle;
	struct client_data_t;

	// constructor function for the ut_metadata extension. The ut_metadata
	// extension allows peers to request the .torrent file (or more
	// specifically the info-dictionary of the .torrent file) from each
	// other. This is the main building block in making magnet links work.
	// This extension is enabled by default unless explicitly disabled in
	// the session constructor.
	//
	// This can either be passed in the add_torrent_params::extensions field, or
	// via torrent_handle::add_extension().
	TORRENT_EXPORT std::shared_ptr<torrent_plugin> create_ut_metadata_plugin(torrent_handle const&, client_data_t);
}

#endif // TORRENT_DISABLE_EXTENSIONS
#endif // TORRENT_UT_METADATA_HPP_INCLUDED
