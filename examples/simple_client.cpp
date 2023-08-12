/*

Copyright (c) 2003-2017, Arvid Norberg
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

#include <cstdlib>

#include "print.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/settings_pack.hpp"

using namespace libtorrent;

#include <iostream>

char const* timestamp()
{
	time_t t = std::time(nullptr);
	tm* timeinfo = std::localtime(&t);
	static char str[200];
	std::strftime(str, 200, "%b %d %X", timeinfo);
	return str;
}

void pop_alerts(lt::session& ses)
{
	std::vector<lt::alert*> alerts;
	ses.pop_alerts(&alerts);
	for (auto a : alerts)
	{
		std::cout << timestamp() << " " << a->message() << std::endl;
	}
}

int main(int argc, char* argv[]) try
{
	if (argc != 3) {
		std::cerr << "usage: ./simple_client <save path> <torrent seed file>\n";
		return 1;
	}

	lt::session_params params;
	auto& settings = params.settings;

	settings.set_int(settings_pack::cache_size, -1);
	settings.set_int(settings_pack::choking_algorithm, settings_pack::rate_based_choker);
	settings.set_bool(lt::setting_by_name("enable_outgoing_utp"), false);
	settings.set_bool(lt::setting_by_name("enable_ingoing_utp"), false);
	settings.set_bool(lt::setting_by_name("enable_outgoing_tcp"), true);
	settings.set_bool(lt::setting_by_name("enable_ingoing_tcp"), true);
	settings.set_bool(lt::setting_by_name("enable_dht"), false);

	settings.set_int(settings_pack::alert_mask
		, lt::alert_category::error
		| lt::alert_category::peer
		| lt::alert_category::port_mapping
		| lt::alert_category::storage
		| lt::alert_category::tracker
		| lt::alert_category::connect
		| lt::alert_category::status
		| lt::alert_category::ip_block
		| lt::alert_category::performance_warning
		| lt::alert_category::dht
		| lt::alert_category::incoming_request
		| lt::alert_category::dht_operation
		| lt::alert_category::port_mapping_log
		| lt::alert_category::file_progress);


	lt::session ses(std::move(params));
	lt::add_torrent_params p;
	p.save_path = argv[1];
	p.ti = std::make_shared<lt::torrent_info>(argv[2]);
	auto handle = ses.add_torrent(p);

	while (!handle.is_finished())
	{
		libtorrent::alert const* a = ses.wait_for_alert(std::chrono::seconds(2));
		if (a == nullptr) continue;
		pop_alerts(ses);
	}
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
}
