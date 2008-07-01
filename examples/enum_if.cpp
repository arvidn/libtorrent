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

#include <libtorrent/enum_net.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/broadcast_socket.hpp>
#include <vector>

using namespace libtorrent;

int main()
{
	io_service ios;
	asio::error_code ec;
	std::vector<ip_interface> const& net = enum_net_interfaces(ios, ec);

	for (std::vector<ip_interface>::const_iterator i = net.begin()
		, end(net.end()); i != end; ++i)
	{
		std::cout << "address: " << i->interface_address << std::endl
			<< "   mask: " << i->netmask << std::endl
			<< "   flags: ";
		if (is_multicast(i->interface_address)) std::cout << "multicast ";
		if (is_local(i->interface_address)) std::cout << "local ";
		if (is_loopback(i->interface_address)) std::cout << "loopback ";
		std::cout << std::endl;
		std::cout << "  router: " << router_for_interface(i->interface_address, ec);
		std::cout << std::endl;
	}

	address local = guess_local_address(ios);

	std::cout << "Local address: " << local << std::endl;
}

