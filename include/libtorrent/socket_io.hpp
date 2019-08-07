/*

Copyright (c) 2009-2018, Arvid Norberg
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

#ifndef TORRENT_SOCKET_IO_HPP_INCLUDED
#define TORRENT_SOCKET_IO_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/string_view.hpp"
#include <string>

namespace libtorrent {

	TORRENT_EXTRA_EXPORT std::string print_address(address const& addr);
	TORRENT_EXTRA_EXPORT std::string print_endpoint(address const& addr, int port);
	TORRENT_EXTRA_EXPORT std::string print_endpoint(tcp::endpoint const& ep);
	TORRENT_EXTRA_EXPORT std::string print_endpoint(udp::endpoint const& ep);
	TORRENT_EXTRA_EXPORT tcp::endpoint parse_endpoint(string_view str, error_code& ec);

	TORRENT_EXTRA_EXPORT std::string address_to_bytes(address const& a);
	TORRENT_EXTRA_EXPORT std::string endpoint_to_bytes(udp::endpoint const& ep);
	TORRENT_EXTRA_EXPORT sha1_hash hash_address(address const& ip);

namespace detail {

		template <class Proto>
		std::size_t address_size(Proto p)
		{
			if (p == Proto::v6())
				return std::tuple_size<address_v6::bytes_type>::value;
			else
				return std::tuple_size<address_v4::bytes_type>::value;
		}

		template<class OutIt>
		void write_address(address const& a, OutIt&& out)
		{
			if (a.is_v4())
			{
				write_uint32(a.to_v4().to_ulong(), out);
			}
			else if (a.is_v6())
			{
				for (auto b : a.to_v6().to_bytes())
					write_uint8(b, out);
			}
		}

		template<class InIt>
		address read_v4_address(InIt&& in)
		{
			std::uint32_t const ip = read_uint32(in);
			return address_v4(ip);
		}

		template<class InIt>
		address read_v6_address(InIt&& in)
		{
			address_v6::bytes_type bytes;
			for (auto& b : bytes)
				b = read_uint8(in);
			return address_v6(bytes);
		}

		template<class Endpoint, class OutIt>
		void write_endpoint(Endpoint const& e, OutIt&& out)
		{
			write_address(e.address(), out);
			write_uint16(e.port(), out);
		}

		template<class Endpoint, class InIt>
		Endpoint read_v4_endpoint(InIt&& in)
		{
			address addr = read_v4_address(in);
			std::uint16_t port = read_uint16(in);
			return Endpoint(addr, port);
		}

		template<class Endpoint, class InIt>
		Endpoint read_v6_endpoint(InIt&& in)
		{
			address addr = read_v6_address(in);
			std::uint16_t port = read_uint16(in);
			return Endpoint(addr, port);
		}

		template <class EndpointType>
		std::vector<EndpointType> read_endpoint_list(libtorrent::bdecode_node const& n)
		{
			std::vector<EndpointType> ret;
			if (n.type() != bdecode_node::list_t) return ret;
			for (int i = 0; i < n.list_size(); ++i)
			{
				bdecode_node e = n.list_at(i);
				if (e.type() != bdecode_node::string_t) return ret;
				if (e.string_length() < 6) continue;
				char const* in = e.string_ptr();
				if (e.string_length() == 6)
					ret.push_back(read_v4_endpoint<EndpointType>(in));
				else if (e.string_length() == 18)
					ret.push_back(read_v6_endpoint<EndpointType>(in));
			}
			return ret;
		}
} // namespace detail

}

#endif
