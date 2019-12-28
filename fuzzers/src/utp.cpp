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

#include "libtorrent/aux_/utp_socket_manager.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/utp_stream.hpp"
#include "libtorrent/udp_socket.hpp"

using namespace lt;

io_context ios;
lt::aux::session_settings sett;
counters cnt;

aux::utp_socket_manager man(
	[](std::weak_ptr<aux::utp_socket_interface>, udp::endpoint const&, span<char const>, error_code&, udp_send_flags_t){}
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

