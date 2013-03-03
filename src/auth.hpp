/*

Copyright (c) 2012, Arvid Norberg
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

#ifndef TORRENT_AUTH_HPP
#define TORRENT_AUTH_HPP

#include "auth_interface.hpp"

#include "libtorrent/peer_id.hpp" // sha1_hash
#include "libtorrent/error_code.hpp"
#include "libtorrent/thread.hpp" // for mutex
#include <string>
#include <map>
#include <vector>

struct mg_connection;

namespace libtorrent
{
	permissions_interface const* parse_http_auth(mg_connection* conn, auth_interface const* auth);

	struct auth : auth_interface
	{
		auth();
		void add_account(std::string const& user, std::string const& pwd, int group);
		void remove_account(std::string const& user);
		std::vector<std::string> users() const;

		void save_accounts(std::string const& filename, error_code& ec) const;
		void load_accounts(std::string const& filename, error_code& ec);

		void set_group(int g, permissions_interface const* perms);

		permissions_interface const* find_user(std::string username, std::string password) const;

	private:

		struct account_t
		{
			sha1_hash password_hash(std::string const& pwd) const;

			sha1_hash hash;
			char salt[10];
			int group;
		};

		mutable mutex m_mutex;
		std::map<std::string, account_t> m_accounts;

		// the permissions for each group
		std::vector<permissions_interface const*> m_groups;
	};
}

#endif

