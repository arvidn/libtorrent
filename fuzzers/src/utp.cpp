/*

Copyright (c) 2019-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/utp_socket_manager.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/utp_stream.hpp"
#include "libtorrent/aux_/udp_socket.hpp"

using namespace lt;

io_context ios;
lt::aux::session_settings sett;
counters cnt;

aux::utp_socket_manager man(
	[](std::weak_ptr<aux::utp_socket_interface>, udp::endpoint const&, span<char const>, error_code&, aux::udp_send_flags_t){}
	, [](aux::socket_type){}
	, ios
	, sett
	, cnt
	, nullptr);

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	std::unique_ptr<aux::utp_socket_impl> sock;
	{
		aux::utp_stream str(ios);
		sock = std::make_unique<aux::utp_socket_impl>(1, 0, &str, man);
		str.set_impl(sock.get());
		udp::endpoint ep;
		time_point ts(seconds(100));
		span<char const> buf(reinterpret_cast<char const*>(data), size);
		sock->incoming_packet(buf, ep, ts);

		// clear any deferred acks
		man.socket_drained();
	}
	return 0;
}
