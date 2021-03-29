/*

Copyright (c) 2009-2010, 2013-2014, 2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <string>

#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/io_bytes.hpp" // for write_uint16
#include "libtorrent/hasher.hpp" // for hasher
#include "libtorrent/aux_/escape_string.hpp" // for trim

namespace lt::aux {

	std::string print_address(address const& addr)
	{
		return addr.to_string();
	}

	std::string address_to_bytes(address const& a)
	{
		std::string ret;
		std::back_insert_iterator<std::string> out(ret);
		aux::write_address(a, out);
		return ret;
	}

	std::string endpoint_to_bytes(udp::endpoint const& ep)
	{
		std::string ret;
		std::back_insert_iterator<std::string> out(ret);
		aux::write_endpoint(ep, out);
		return ret;
	}

	std::string print_endpoint(address const& addr, int port)
	{
		char buf[200];
		if (addr.is_v6())
			std::snprintf(buf, sizeof(buf), "[%s]:%d", addr.to_string().c_str(), port);
		else
			std::snprintf(buf, sizeof(buf), "%s:%d", addr.to_string().c_str(), port);
		return buf;
	}

	std::string print_endpoint(tcp::endpoint const& ep)
	{
		return print_endpoint(ep.address(), ep.port());
	}

	std::string print_endpoint(udp::endpoint const& ep)
	{
		return print_endpoint(ep.address(), ep.port());
	}

	tcp::endpoint parse_endpoint(string_view str, error_code& ec)
	{
		tcp::endpoint ret;

		str = trim(str);

		string_view addr;
		string_view port;

		if (str.empty())
		{
			ec = errors::invalid_port;
			return ret;
		}

		// this is for IPv6 addresses
		if (str.front() == '[')
		{
			auto const close_bracket = str.find_first_of(']');
			if (close_bracket == string_view::npos)
			{
				ec = errors::expected_close_bracket_in_address;
				return ret;
			}
			addr = str.substr(1, close_bracket - 1);
			port = str.substr(close_bracket + 1);
			if (port.empty() || port.front() != ':')
			{
				ec = errors::invalid_port;
				return ret;
			}
			// shave off the ':'
			port = port.substr(1);
			ret.address(make_address_v6(addr, ec));
			if (ec) return ret;
		}
		else
		{
			auto const port_pos = str.find_first_of(':');
			if (port_pos == string_view::npos)
			{
				ec = errors::invalid_port;
				return ret;
			}
			addr = str.substr(0, port_pos);
			port = str.substr(port_pos + 1);
			ret.address(make_address_v4(addr, ec));
			if (ec) return ret;
		}

		if (port.empty())
		{
			ec = errors::invalid_port;
			return ret;
		}

		int const port_num = std::atoi(std::string(port).c_str());
		if (port_num <= 0 || port_num > std::numeric_limits<std::uint16_t>::max())
		{
			ec = errors::invalid_port;
			return ret;
		}
		ret.port(static_cast<std::uint16_t>(port_num));
		return ret;
	}

	sha1_hash hash_address(address const& ip)
	{
		if (ip.is_v6())
		{
			address_v6::bytes_type b = ip.to_v6().to_bytes();
			return hasher(reinterpret_cast<char const*>(b.data()), int(b.size())).final();
		}
		else
		{
			address_v4::bytes_type b = ip.to_v4().to_bytes();
			return hasher(reinterpret_cast<char const*>(b.data()), int(b.size())).final();
		}
	}

}
