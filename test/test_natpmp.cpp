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
#include "libtorrent/connection_queue.hpp"
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/intrusive_ptr.hpp>
#include <iostream>

using namespace libtorrent;

void callback(int mapping, address extip, int port, error_code const& err)
{
	std::cerr
		<< "mapping: " << mapping
		<< ", port: " << port
		<< ", external-IP: " << print_address(extip)
		<< ", error: \"" << err.message() << "\"\n";
}

void log_callback(char const* line)
{
	std::cerr << line << std::endl;
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

	connection_queue cc(ios);
	boost::intrusive_ptr<natpmp> natpmp_handler = new natpmp(ios, address_v4()
		, &callback, &log_callback);

	deadline_timer timer(ios);

	int tcp_map = natpmp_handler->add_mapping(natpmp::tcp, atoi(argv[1]), atoi(argv[1]));
	int udp_map = natpmp_handler->add_mapping(natpmp::udp, atoi(argv[2]), atoi(argv[2]));

	error_code ec;
	timer.expires_from_now(seconds(2), ec);
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));
	std::cerr << "mapping ports TCP: " << argv[1]
		<< " UDP: " << argv[2] << std::endl;

	ios.reset();
	ios.run(ec);
	timer.expires_from_now(seconds(2), ec);
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));
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


