/*

Copyright (c) 2019, Arvid Norberg
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

#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#include <memory>

using namespace lt;

aux::session_settings sett;
dht::dht_state state;
std::unique_ptr<lt::dht::dht_storage_interface> dht_storage(dht::dht_default_storage_constructor(sett));

counters cnt;

struct obs : dht::dht_observer
{
	void set_external_address(lt::aux::listen_socket_handle const&, lt::address const& /* addr */
		, lt::address const&) override
	{}
	int get_listen_port(aux::transport ssl, aux::listen_socket_handle const& s) override
	{ return 6881; }

	void get_peers(lt::sha1_hash const&) override {}
	void outgoing_get_peers(sha1_hash const&
		, sha1_hash const&, lt::udp::endpoint const&) override {}
	void announce(sha1_hash const&, lt::address const&, int) override {}
	bool on_dht_request(string_view
		, dht::msg const&, entry&) override
	{ return false; }

#ifndef TORRENT_DISABLE_LOGGING

	void log(dht_logger::module_t, char const*, ...) override {}

	bool should_log(module_t) const override { return true; }
	void log_packet(message_direction_t
		, span<char const>
		, lt::udp::endpoint const&) override {}
#endif // TORRENT_DISABLE_LOGGING
};

obs o;
io_context ios;
dht::dht_tracker dht_node(&o
	, ios
	, [](aux::listen_socket_handle const&, udp::endpoint const&
		, span<char const>, error_code&, udp_send_flags_t) {}
	, sett
	, cnt
	, *dht_storage
	, std::move(state));
auto listen_socket = std::make_shared<aux::listen_socket_t>();
aux::listen_socket_handle s(listen_socket);

error_code ignore;
lt::address_v4 src = make_address_v4("2.2.2.2", ignore);
udp::endpoint ep(src, 6881);
std::once_flag once_flag;

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	ep.address(src);
	src = lt::address_v4(aux::plus_one(src.to_bytes()));
	std::call_once(once_flag, []{ dht_node.new_socket(s); });
	dht_node.incoming_packet(s, ep, {reinterpret_cast<char const*>(data), int(size)});
	return 0;
}

