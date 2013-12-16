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
#ifndef TORRENT_NO_DEPRECATE
			, tracker_url(0)
#endif
			, resume_data(0)
			, storage_mode(storage_mode_sparse)
			, storage(sc)
			, userdata(0)
			, file_priorities(0)
#ifndef TORRENT_NO_DEPRECATE
			, flags(flag_ignore_flags | default_flags)
			, seed_mode(false)
			, override_resume_data(false)
			, upload_mode(false)
			, share_mode(false)
			, apply_ip_filter(true)
			, paused(true)
			, auto_managed(true)
			, duplicate_is_error(false)
			, merge_resume_trackers(false)
#else
			, flags(default_flags)
#endif
		{
		}

#ifndef TORRENT_NO_DEPRECATE
		void update_flags() const
		{
			if (flags != (flag_ignore_flags | default_flags)) return;

			boost::uint64_t& f = const_cast<boost::uint64_t&>(flags);
			f = flag_update_subscribe;
			if (seed_mode) f |= flag_seed_mode;
			if (override_resume_data) f |= flag_override_resume_data;
			if (upload_mode) f |= flag_upload_mode;
			if (share_mode) f |= flag_share_mode;
			if (apply_ip_filter) f |= flag_apply_ip_filter;
			if (paused) f |= flag_paused;
			if (auto_managed) f |= flag_auto_managed;
			if (duplicate_is_error) f |= flag_duplicate_is_error;
			if (merge_resume_trackers) f |= flag_merge_resume_trackers;
		}
#endif

		enum flags_t
		{
			flag_seed_mode = 0x001,
			flag_override_resume_data = 0x002,
			flag_upload_mode = 0x004,
			flag_share_mode = 0x008,
			flag_apply_ip_filter = 0x010,
			flag_paused = 0x020,
			flag_auto_managed = 0x040,
			flag_duplicate_is_error = 0x080,
			flag_merge_resume_trackers = 0x100,
			flag_update_subscribe = 0x200,

			default_flags = flag_update_subscribe | flag_auto_managed | flag_paused | flag_apply_ip_filter
#ifndef TORRENT_NO_DEPRECATE
			, flag_ignore_flags = 0x80000000
#endif
		};
	
		// libtorrent version. Used for forward binary compatibility
		int version;
		boost::intrusive_ptr<torrent_info> ti;
#ifndef TORRENT_NO_DEPRECATE
		char const* tracker_url;
#endif
		std::vector<std::string> trackers;
		std::vector<std::pair<std::string, int> > dht_nodes;
		sha1_hash info_hash;
		std::string name;
		std::string save_path;
		std::vector<char>* resume_data;
		storage_mode_t storage_mode;
		storage_constructor_type storage;
		void* userdata;
		std::vector<boost::uint8_t> const* file_priorities;
		std::string trackerid;
		std::string url;
		std::string uuid;
		std::string source_feed_url;
		boost::uint64_t flags;
#ifndef TORRENT_NO_DEPRECATE
		bool seed_mode;
		bool override_resume_data;
		bool upload_mode;
		bool share_mode;
		bool apply_ip_filter;
		bool paused;
		bool auto_managed;
		bool duplicate_is_error;
		bool merge_resume_trackers;
#endif
	};
}

#endif

