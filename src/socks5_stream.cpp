/*

Copyright (c) 2007-2011, 2013-2019, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2019, Alden Torres
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

#include "libtorrent/socks5_stream.hpp"
#include "libtorrent/socket_io.hpp"

using namespace std::placeholders;

namespace libtorrent {

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
