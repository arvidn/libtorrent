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
#include "libtorrent/peer_id.hpp" // for sha1_hash
#include <string>

namespace libtorrent
{
	TORRENT_EXTRA_EXPORT std::string print_address(address const& addr);
	TORRENT_EXTRA_EXPORT std::string print_endpoint(tcp::endpoint const& ep);
	TORRENT_EXTRA_EXPORT std::string print_endpoint(udp::endpoint const& ep);
	TORRENT_EXTRA_EXPORT tcp::endpoint parse_endpoint(std::string str, error_code& ec);

	TORRENT_EXTRA_EXPORT std::string address_to_bytes(address const& a);
	// internal
	TORRENT_EXPORT std::string endpoint_to_bytes(udp::endpoint const& ep);
	TORRENT_EXTRA_EXPORT void hash_address(address const& ip, sha1_hash& h);

	namespace detail
	{
		template<class OutIt>
		void write_address(address const& a, OutIt& out)
		{
#if TORRENT_USE_IPV6
			if (a.is_v4())
			{
#endif
				write_uint32(a.to_v4().to_ulong(), out);
#if TORRENT_USE_IPV6
			}
			else if (a.is_v6())
			{
				typedef address_v6::bytes_type bytes_t;
				bytes_t bytes = a.to_v6().to_bytes();
				for (bytes_t::iterator i = bytes.begin()
					, end(bytes.end()); i != end; ++i)
					write_uint8(*i, out);
			}
#endif
		}

		template<class InIt>
		address read_v4_address(InIt& in)
		{
			unsigned long ip = read_uint32(in);
			return address_v4(ip);
		}

#if TORRENT_USE_IPV6
		template<class InIt>
		address read_v6_address(InIt& in)
		{
			typedef address_v6::bytes_type bytes_t;
			bytes_t bytes;
			for (bytes_t::iterator i = bytes.begin()
				, end(bytes.end()); i != end; ++i)
				*i = read_uint8(in);
			return address_v6(bytes);
		}
#endif

		template<class Endpoint, class OutIt>
		void write_endpoint(Endpoint const& e, OutIt& out)
		{
			write_address(e.address(), out);
			write_uint16(e.port(), out);
		}

		template<class Endpoint, class InIt>
		Endpoint read_v4_endpoint(InIt& in)
		{
			address addr = read_v4_address(in);
			int port = read_uint16(in);
			return Endpoint(addr, port);
		}

#if TORRENT_USE_IPV6
		template<class Endpoint, class InIt>
		Endpoint read_v6_endpoint(InIt& in)
		{
			address addr = read_v6_address(in);
			int port = read_uint16(in);
			return Endpoint(addr, port);
		}
#endif

		template <class EndpointType>
		void read_endpoint_list(libtorrent::bdecode_node const& n
			, std::vector<EndpointType>& epl)
		{
			using namespace libtorrent;
			if (n.type() != bdecode_node::list_t) return;
			for (int i = 0; i < n.list_size(); ++i)
			{
				bdecode_node e = n.list_at(i);
				if (e.type() != bdecode_node::string_t) return;
				if (e.string_length() < 6) continue;
				char const* in = e.string_ptr();
				if (e.string_length() == 6)
					epl.push_back(read_v4_endpoint<EndpointType>(in));
#if TORRENT_USE_IPV6
				else if (e.string_length() == 18)
					epl.push_back(read_v6_endpoint<EndpointType>(in));
#endif
			}
		}
	}

	template <class EndpointType>
	void read_endpoint_list(libtorrent::entry const* n, std::vector<EndpointType>& epl)
	{
		using namespace libtorrent;
		if (n->type() != entry::list_t) return;
		entry::list_type const& contacts = n->list();
		for (entry::list_type::const_iterator i = contacts.begin()
			, end(contacts.end()); i != end; ++i)
		{
			if (i->type() != entry::string_t) return;
			std::string const& p = i->string();
			if (p.size() < 6) continue;
			std::string::const_iterator in = p.begin();
			if (p.size() == 6)
				epl.push_back(detail::read_v4_endpoint<EndpointType>(in));
#if TORRENT_USE_IPV6
			else if (p.size() == 18)
				epl.push_back(detail::read_v6_endpoint<EndpointType>(in));
#endif
		}
	}

}

#endif

