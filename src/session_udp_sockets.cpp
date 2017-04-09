/*

Copyright (c) 2017, Arvid Norberg, Steven Siloti
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

#include "libtorrent/aux_/session_udp_sockets.hpp"
#include "libtorrent/aux_/session_impl.hpp"

namespace libtorrent { namespace aux {

	std::vector<std::shared_ptr<outgoing_udp_socket>>::iterator
	outgoing_sockets::partition_outgoing_sockets(std::vector<listen_endpoint_t>& eps)
	{
		return std::partition(sockets.begin(), sockets.end()
			, [&eps](std::shared_ptr<outgoing_udp_socket> const& sock)
		{
			auto match = std::find_if(eps.begin(), eps.end()
				, [&sock](listen_endpoint_t const& ep)
			{
				return ep.device == sock->device
					&& ep.addr == sock->sock.local_endpoint().address();
			});

			if (match != eps.end())
			{
				// remove the matched endpoint so that another socket can't match it
				// this also signals to the caller that it doesn't need to create a
				// socket for the endpoint
				eps.erase(match);
				return true;
			}
			else
			{
				return false;
			}
		});
	}

	tcp::endpoint outgoing_sockets::bind(socket_type& s, address const& remote_address) const
	{
		TORRENT_ASSERT(!sockets.empty());

		auto& index = remote_address.is_v4() ? index4 : index6;
		auto const index_begin = index;

		for (;;)
		{
			if (++index >= sockets.size())
				index = 0;

			if (sockets[index]->local_endpoint().address().is_v4() != remote_address.is_v4())
			{
				if (index == index_begin) break;
				continue;
			}

			utp_init_socket(s.get<utp_stream>()->get_impl(), sockets[index]);
			auto udp_ep = sockets[index]->local_endpoint();
			return tcp::endpoint(udp_ep.address(), udp_ep.port());
		}

		return tcp::endpoint();
	}

	void outgoing_sockets::update_proxy(proxy_settings const& settings)
	{
		for (auto const& i : sockets)
			i->sock.set_proxy_settings(settings);
	}

	void outgoing_sockets::close()
	{
		for (auto const& l : sockets)
			l->sock.close();
	}

} }
