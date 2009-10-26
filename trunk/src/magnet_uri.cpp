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

#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/escape_string.hpp"

#include <string>

namespace libtorrent
{
	std::string make_magnet_uri(torrent_handle const& handle)
	{
		if (!handle.is_valid()) return "";

		char ret[1024];
		int num_chars = snprintf(ret, sizeof(ret), "magnet:?xt=urn:btih:%s"
			, base32encode(std::string((char*)handle.info_hash().begin(), 20)).c_str());

		std::string name = handle.name();

		if (!name.empty())
			num_chars += snprintf(ret + num_chars, sizeof(ret) - num_chars, "&dn=%s"
				, escape_string(name.c_str(), name.length()).c_str());

		char const* tracker = 0;
		torrent_status st = handle.status();
		if (!st.current_tracker.empty())
		{
			tracker = st.current_tracker.c_str();
		}
		else
		{
			std::vector<announce_entry> const& tr = handle.trackers();
			if (!tr.empty()) tracker = tr[0].url.c_str();
		}
		if (tracker)
			num_chars += snprintf(ret + num_chars, sizeof(ret) - num_chars, "&tr=%s"
				, escape_string(tracker, strlen(tracker)).c_str());

		return ret;
	}

	std::string make_magnet_uri(torrent_info const& info)
	{
		char ret[1024];
		int num_chars = snprintf(ret, sizeof(ret), "magnet:?xt=urn:btih:%s"
			, base32encode(std::string((char*)info.info_hash().begin(), 20)).c_str());

		std::string const& name = info.name();

		if (!name.empty())
			num_chars += snprintf(ret + num_chars, sizeof(ret) - num_chars, "&dn=%s"
				, escape_string(name.c_str(), name.length()).c_str());

		std::vector<announce_entry> const& tr = info.trackers();
		if (!tr.empty())
		{
			num_chars += snprintf(ret + num_chars, sizeof(ret) - num_chars, "&tr=%s"
				, escape_string(tr[0].url.c_str(), tr[0].url.length()).c_str());
		}

		return ret;
	}

#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, std::string const& save_path
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc
		, void* userdata)
	{
		std::string name;
		std::string tracker;

		error_code ec;
		boost::optional<std::string> display_name = url_has_argument(uri, "dn");
		if (display_name) name = unescape_string(display_name->c_str(), ec);
		boost::optional<std::string> tracker_string = url_has_argument(uri, "tr");
		if (tracker_string) tracker = unescape_string(tracker_string->c_str(), ec);
	
		boost::optional<std::string> btih = url_has_argument(uri, "xt");
		if (!btih) return torrent_handle();

		if (btih->compare(0, 9, "urn:btih:") != 0) return torrent_handle();

		sha1_hash info_hash;
		if (btih->size() == 40 + 9) from_hex(&(*btih)[9], 40, (char*)&info_hash[0]);
		else info_hash.assign(base32decode(btih->substr(9)));

		return ses.add_torrent(tracker.empty() ? 0 : tracker.c_str(), info_hash
			, name.empty() ? 0 : name.c_str(), save_path, entry()
			, storage_mode, paused, sc, userdata);
	}
#endif

	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params p)
	{
		error_code ec;
		torrent_handle ret = add_magnet_uri(ses, uri, p, ec);
		if (ec) throw libtorrent_exception(ec);
		return ret;
	}
#endif
	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params p, error_code& ec)
	{
		std::string name;
		std::string tracker;

		error_code e;
		boost::optional<std::string> display_name = url_has_argument(uri, "dn");
		if (display_name) name = unescape_string(display_name->c_str(), e);
		boost::optional<std::string> tracker_string = url_has_argument(uri, "tr");
		if (tracker_string) tracker = unescape_string(tracker_string->c_str(), e);
	
		boost::optional<std::string> btih = url_has_argument(uri, "xt");
		if (!btih)
		{
			ec = error_code(errors::missing_info_hash_in_uri, libtorrent_category);
			return torrent_handle();
		}

		if (btih->compare(0, 9, "urn:btih:") != 0)
		{
			ec = error_code(errors::missing_info_hash_in_uri, libtorrent_category);
			return torrent_handle();
		}

		sha1_hash info_hash;
		if (btih->size() == 40 + 9) from_hex(&(*btih)[9], 40, (char*)&info_hash[0]);
		else info_hash.assign(base32decode(btih->substr(9)));

		if (!tracker.empty()) p.tracker_url = tracker.c_str();
		p.info_hash = info_hash;
		if (!name.empty()) p.name = name.c_str();
		return ses.add_torrent(p, ec);
	}
}


