/*

Copyright (c) 2009, Arvid Norberg
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

#ifndef TORRENT_ADD_TORRENT_PARAMS_HPP_INCLUDED
#define TORRENT_ADD_TORRENT_PARAMS_HPP_INCLUDED

#include <string>
#include <vector>
#include <boost/intrusive_ptr.hpp>

#include "libtorrent/storage_defs.hpp"
#include "libtorrent/peer_id.hpp" // sha1_hash
#include "libtorrent/version.hpp"

namespace libtorrent
{
	class torrent_info;

	struct add_torrent_params
	{
		add_torrent_params(storage_constructor_type sc = default_storage_constructor)
			: version(LIBTORRENT_VERSION_NUM)
			, tracker_url(0)
			, name(0)
			, resume_data(0)
			, storage_mode(storage_mode_sparse)
			, paused(true)
			, auto_managed(true)
			, duplicate_is_error(false)
			, storage(sc)
			, userdata(0)
			, seed_mode(false)
			, override_resume_data(false)
			, upload_mode(false)
			, file_priorities(0)
			, share_mode(false)
		{}

		// libtorrent version. Used for forward binary compatibility
		int version;
		boost::intrusive_ptr<torrent_info> ti;
		char const* tracker_url;
		sha1_hash info_hash;
		char const* name;
		std::string save_path;
		std::vector<char>* resume_data;
		storage_mode_t storage_mode;
		bool paused;
		bool auto_managed;
		bool duplicate_is_error;
		storage_constructor_type storage;
		void* userdata;
		bool seed_mode;
		bool override_resume_data;
		bool upload_mode;
		std::vector<boost::uint8_t> const* file_priorities;
		bool share_mode;
		std::string trackerid;
		std::string url;
	};
}

#endif

