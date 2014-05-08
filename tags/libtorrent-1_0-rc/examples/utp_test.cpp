/*

Copyright (c) 2012, Arvid Norberg
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

#include "libtorrent/error_code.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/utp_stream.hpp"

using namespace libtorrent;

void on_connect(error_code const& e)
{
}

void on_udp_receive(error_code const& e, udp::endpoint const& ep
		, char const* buf, int size)
{
}

void on_utp_incoming(void* userdata
		, boost::shared_ptr<utp_stream> const& utp_sock)
{
}

int main(int argc, char* argv[])
{
	//int rtt, rtt_var;
	//int max_window, cur_window;
	//int delay_factor, window_factor, scaled_gain;

	/*session s;
	s.listen_on(std::make_pair(6881, 6889));*/

	io_service ios;
	connection_queue cc(ios);
	udp_socket udp_sock(ios, boost::bind(&on_udp_receive, _1, _2, _3, _4), cc);
	
	void* userdata;
	utp_socket_manager utp_sockets(udp_sock, boost::bind(&on_utp_incoming, _1, _2), userdata);
	
	/*error_code ec;
	utp_stream sock(ios, cc);
	sock.bind(udp::endpoint(address_v4::any(), 0), ec);
	
	tcp::endpoint ep(address_v4::from_string("239.192.152.143", ec), 6771);
	
	sock.async_connect(ep, boost::bind(on_connect, _1));*/

	return 0;
}
