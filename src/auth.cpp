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

#include "auth.hpp"

#include "libtorrent/hasher.hpp"
#include "save_settings.hpp"
#include "base64.hpp"

extern "C" {
#include "local_mongoose.h"
}

#include <vector>
#include <string.h> // for strcmp() 
#include <stdio.h>

namespace libtorrent
{
auth::auth()
{
}

std::vector<std::string> auth::accounts() const
{
	std::vector<std::string> users;
	for (std::map<std::string, account_t>::const_iterator i = m_accounts.begin()
		, end(m_accounts.end()); i != end; ++i)
	{
		users.push_back(i->first);
	}
	return users;
}

void auth::add_account(std::string const& user, std::string const& pwd, bool read_only)
{
	std::map<std::string, account_t>::iterator i = m_accounts.find(user);
	if (i == m_accounts.end())
	{
		account_t acct;
		for (int i = 0; i < sizeof(acct.salt); ++i)
			acct.salt[i] = rand();
		acct.read_only = read_only;
		acct.hash = acct.password_hash(pwd);
		m_accounts.insert(std::make_pair(user, acct));
	}
	else
	{
		i->second.hash = i->second.password_hash(pwd);
		i->second.read_only = read_only;
	}
}

void auth::remove_account(std::string const& user)
{
	std::map<std::string, account_t>::iterator i = m_accounts.find(user);
	if (i == m_accounts.end()) return;
	m_accounts.erase(i);
}

permissions_interface const* auth::find_user(std::string username, std::string password) const
{
	const static read_only_permissions read_perms;
	const static full_permissions full_perms;

	std::map<std::string, account_t>::const_iterator i = m_accounts.find(username);
	if (i == m_accounts.end()) return NULL;

	sha1_hash ph = i->second.password_hash(password);
	if (ph != i->second.hash) return NULL;

	return i->second.read_only
		? (permissions_interface const*)&read_perms
		: (permissions_interface const*)&full_perms;
}

sha1_hash auth::account_t::password_hash(std::string const& pwd) const
{
	hasher h;
	if (pwd.size()) h.update(pwd);
	h.update(salt, sizeof(salt));
	sha1_hash ret = h.final();

	return ret;
}

permissions_interface const* parse_http_auth(mg_connection* conn, auth_interface const* auth)
{
	std::string user;
	std::string pwd;
	char const* authorization = mg_get_header(conn, "authorization");
	if (authorization)
	{
		authorization = strcasestr(authorization, "basic ");
		if (authorization)
		{
			authorization += 6;
			// skip whiltespace
			while (*authorization == ' '
				|| *authorization == '\t')
				++authorization;

			std::string cred = base64decode(authorization);
			user = cred.substr(0, cred.find_first_of(':'));
			pwd = cred.substr(user.size()+1);
		}
	}

	permissions_interface const* perms = auth->find_user(user, pwd);
	if (perms == NULL) return NULL;
	return perms;
}

}

