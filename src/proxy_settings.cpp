/*

Copyright (c) 2015, 2017-2018, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/session_settings.hpp"

#include <boost/algorithm/string.hpp> 
#include <boost/algorithm/string/trim.hpp>

namespace libtorrent { namespace aux {

namespace {

template <typename Settings>
void init(proxy_settings& p, Settings const& sett)
{
	p.hostname = sett.get_str(settings_pack::proxy_hostname);
	p.username = sett.get_str(settings_pack::proxy_username);
	p.password = sett.get_str(settings_pack::proxy_password);
	p.type = settings_pack::proxy_type_t(sett.get_int(settings_pack::proxy_type));
	p.port = std::uint16_t(sett.get_int(settings_pack::proxy_port));
	p.proxy_hostnames = sett.get_bool(settings_pack::proxy_hostnames);
	p.proxy_peer_connections = sett.get_bool(
		settings_pack::proxy_peer_connections);
	p.proxy_tracker_connections = sett.get_bool(
		settings_pack::proxy_tracker_connections);
	p.proxy_tracker_list_enable = sett.get_bool(
		settings_pack::proxy_tracker_list_enable);
	/*
	std::string source = sett.get_str(settings_pack::proxy_tracker_list);
	if (source.length()>0) {
		std::vector<std::string> trackers;
     	boost::split(trackers, source, boost::is_any_of(";, "));
		//tracker trim
		for(std::string tracker : trackers) {
			boost::trim(tracker);
			if (tracker.length()>0) {
				p.proxy_tracker_list.push_back(tracker);
			}
		}
	}
	*/
}
}

proxy_settings::proxy_settings() = default;

proxy_settings::proxy_settings(settings_pack const& sett)
{ init(*this, sett); }

proxy_settings::proxy_settings(aux::session_settings const& sett)
{ init(*this, sett); }

} // namespace aux
} // namespace libtorrent
