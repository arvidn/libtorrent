/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2007, 2009, 2014, 2016-2020, Arvid Norberg
Copyright (c) 2019, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/socks5_stream.hpp"
#include "libtorrent/aux_/socket_io.hpp"

using namespace std::placeholders;

namespace lt {

	namespace socks_error
	{
		boost::system::error_code make_error_code(socks_error_code e)
		{ return {e, socks_category()}; }
	}

	struct socks_error_category final : boost::system::error_category
	{
		const char* name() const BOOST_SYSTEM_NOEXCEPT override
		{ return "socks"; }
		std::string message(int ev) const override
		{
			static char const* messages[] =
			{
				"SOCKS no error",
				"SOCKS unsupported version",
				"SOCKS unsupported authentication method",
				"SOCKS unsupported authentication version",
				"SOCKS authentication error",
				"SOCKS username required",
				"SOCKS general failure",
				"SOCKS command not supported",
				"SOCKS no identd running",
				"SOCKS identd could not identify username"
			};

			if (ev < 0 || ev >= socks_error::num_errors) return "unknown error";
			return messages[ev];
		}
		boost::system::error_condition default_error_condition(
			int ev) const BOOST_SYSTEM_NOEXCEPT override
		{ return {ev, *this}; }
	};

	boost::system::error_category& socks_category()
	{
		static socks_error_category cat;
		return cat;
	}
}
