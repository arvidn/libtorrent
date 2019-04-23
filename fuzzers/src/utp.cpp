/*

Copyright (c) 2018, Arvid Norberg
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

#include "libtorrent/version.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/utp_stream.hpp"
#include "libtorrent/udp_socket.hpp"

using namespace lt;

#if LIBTORRENT_VERSION_NUM >= 10300
io_context ios;
#else
io_service ios;
#endif
lt::aux::session_settings sett;
counters cnt;

#if LIBTORRENT_VERSION_NUM >= 10200
utp_socket_manager man(
	[](std::weak_ptr<utp_socket_interface>, udp::endpoint const&, span<char const>, error_code&, udp_send_flags_t){}
	, [](std::shared_ptr<aux::socket_type> const&){}
	, ios
	, sett
	, cnt
	, nullptr);
#else
udp_socket sock(ios);
utp_socket_manager man(
	sett
	, sock
	, cnt
	, nullptr
	, [](boost::shared_ptr<socket_type> const&){}
	);
#endif

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	utp_socket_impl* sock = NULL;
	{
		utp_stream str(ios);
#if LIBTORRENT_VERSION_NUM >= 10200
		sock = construct_utp_impl(1, 0, &str, man);
#else
		sock = construct_utp_impl(1, 0, &str, &man);
#endif
		str.set_impl(sock);
		udp::endpoint ep;
		time_point ts(seconds(100));
#if LIBTORRENT_VERSION_NUM >= 10200
		span<char const> buf(reinterpret_cast<char const*>(data), size);
		utp_incoming_packet(sock, buf, ep, ts);
#else
		utp_incoming_packet(sock, reinterpret_cast<char const*>(data), size, ep, ts);
#endif

		// clear any deferred acks
		man.socket_drained();
	}
	delete_utp_impl(sock);
	return 0;
}

