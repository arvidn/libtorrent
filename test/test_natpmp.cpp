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

#include "libtorrent/natpmp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include <functional>
#include <iostream>
#include <memory>

using namespace libtorrent;

using libtorrent::aux::portmap_protocol;

namespace
{
	struct natpmp_callback : aux::portmap_callback
	{
		void on_port_mapping(int mapping, address const& ip, int port
			, portmap_protocol const protocol, error_code const& err
			, aux::portmap_transport transport) override
		{
			std::cerr
				<< "mapping: " << mapping
				<< ", port: " << port
				<< ", protocol: " << static_cast<int>(protocol)
				<< ", external-IP: " << print_address(ip)
				<< ", error: \"" << err.message() << "\"\n";
		}
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log_portmap(aux::portmap_transport transport) const override
		{
			return true;
		}

		virtual void log_portmap(aux::portmap_transport transport, char const* msg) const override
		{
			std::cerr << msg << std::endl;
		}
#endif
	};
}

int main(int argc, char* argv[])
{
	io_service ios;
	std::string user_agent = "test agent";

	if (argc != 3)
	{
		std::cerr << "usage: " << argv[0] << " tcp-port udp-port" << std::endl;
		return 1;
	}

	natpmp_callback cb;
	auto natpmp_handler = std::make_shared<natpmp>(ios, cb);

	deadline_timer timer(ios);

	int const tcp_map = natpmp_handler->add_mapping(portmap_protocol::tcp
		, atoi(argv[1]), atoi(argv[1]));
	natpmp_handler->add_mapping(portmap_protocol::udp, atoi(argv[2]), atoi(argv[2]));

	error_code ec;
	timer.expires_from_now(seconds(2), ec);
	timer.async_wait([&] (error_code const&) { ios.io_service::stop(); });
	std::cerr << "mapping ports TCP: " << argv[1]
		<< " UDP: " << argv[2] << std::endl;

	ios.reset();
	ios.run(ec);
	timer.expires_from_now(seconds(2), ec);
	timer.async_wait([&] (error_code const&) { ios.io_service::stop(); });
	std::cerr << "removing mapping " << tcp_map << std::endl;
	natpmp_handler->delete_mapping(tcp_map);

	ios.reset();
	ios.run(ec);
	std::cerr << "removing mappings" << std::endl;
	natpmp_handler->close();

	ios.reset();
	ios.run(ec);
	std::cerr << "closing" << std::endl;
}
