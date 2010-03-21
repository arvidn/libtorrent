/*

Copyright (c) 2007, Arvid Norberg
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

#ifndef TORRENT_MAGNET_URI_HPP_INCLUDED
#define TORRENT_MAGNET_URI_HPP_INCLUDED

#include <string>
#include "libtorrent/config.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include <boost/filesystem/path.hpp>

namespace libtorrent
{
	namespace fs = boost::filesystem;

	struct torrent_handle;

	std::string TORRENT_EXPORT make_magnet_uri(torrent_handle const& handle);
	std::string TORRENT_EXPORT make_magnet_uri(torrent_info const& info);

#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
	// deprecated in 0.14
	torrent_handle TORRENT_EXPORT add_magnet_uri(session& ses, std::string const& uri
		, fs::path const& save_path
		, storage_mode_t storage_mode = storage_mode_sparse
		, bool paused = false
		, storage_constructor_type sc = default_storage_constructor
		, void* userdata = 0) TORRENT_DEPRECATED;
#endif

	torrent_handle TORRENT_EXPORT add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params p);
#endif

	torrent_handle TORRENT_EXPORT add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params p, error_code& ec);
}

#endif

