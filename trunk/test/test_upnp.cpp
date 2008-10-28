/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/upnp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/connection_queue.hpp"
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/intrusive_ptr.hpp>

using namespace libtorrent;

void callback(int mapping, int port, std::string const& err)
{
	if (mapping == -1)
	{
		std::cerr << "UPnP: " << err << std::endl;
		return;
	}
	std::cerr << "mapping: " << mapping << ", port: " << port << ", error: \"" << err << "\"\n";
}

int main(int argc, char* argv[])
{
	libtorrent::io_service ios;
	std::string user_agent = "test agent";

	if (argc != 3)
	{
		std::cerr << "usage: " << argv[0] << " tcp-port udp-port" << std::endl;
		return 1;
	}

	connection_queue cc(ios);
	boost::intrusive_ptr<upnp> upnp_handler = new upnp(ios, cc, address_v4(), user_agent, &callback, false);
	upnp_handler->discover_device();

	libtorrent::deadline_timer timer(ios);
	timer.expires_from_now(seconds(2));
	timer.async_wait(boost::bind(&libtorrent::io_service::stop, boost::ref(ios)));

	std::cerr << "broadcasting for UPnP device" << std::endl;

	ios.reset();
	ios.run();

	upnp_handler->add_mapping(upnp::tcp, atoi(argv[1]), atoi(argv[1]));
	upnp_handler->add_mapping(upnp::udp, atoi(argv[2]), atoi(argv[2]));
	timer.expires_from_now(seconds(10));
	timer.async_wait(boost::bind(&libtorrent::io_service::stop, boost::ref(ios)));
	std::cerr << "mapping ports TCP: " << argv[1]
		<< " UDP: " << argv[2] << std::endl;

	ios.reset();
	ios.run();
	std::cerr << "router: " << upnp_handler->router_model() << std::endl;
	std::cerr << "removing mappings" << std::endl;
	upnp_handler->close();

	ios.reset();
	ios.run();
	std::cerr << "closing" << std::endl;
}


