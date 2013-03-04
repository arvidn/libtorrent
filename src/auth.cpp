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
#include <sys/time.h>

namespace libtorrent
{

const static no_permissions no_perms;
const static read_only_permissions read_perms;
const static full_permissions full_perms;

auth::auth()
{
	// default groups are:
	// 0: full permissions
	// 1: read-only permissions
	// this is configurable via the set_group() function
	m_groups.push_back(&full_perms);
	m_groups.push_back(&read_perms);
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);
	srand(tv.tv_usec);
}

std::vector<std::string> auth::users() const
{
	mutex::scoped_lock l(m_mutex);

	std::vector<std::string> users;
	for (std::map<std::string, account_t>::const_iterator i = m_accounts.begin()
		, end(m_accounts.end()); i != end; ++i)
	{
		users.push_back(i->first);
	}
	return users;
}

void auth::add_account(std::string const& user, std::string const& pwd, int group)
{
	mutex::scoped_lock l(m_mutex);

	std::map<std::string, account_t>::iterator i = m_accounts.find(user);
	if (i == m_accounts.end())
	{
		account_t acct;
		for (int i = 0; i < sizeof(acct.salt); ++i)
			acct.salt[i] = rand();
		acct.group = group;
		acct.hash = acct.password_hash(pwd);
		m_accounts.insert(std::make_pair(user, acct));
	}
	else
	{
		i->second.hash = i->second.password_hash(pwd);
		i->second.group = group;
	}
}

void auth::remove_account(std::string const& user)
{
	mutex::scoped_lock l(m_mutex);

	std::map<std::string, account_t>::iterator i = m_accounts.find(user);
	if (i == m_accounts.end()) return;
	m_accounts.erase(i);
}

void auth::set_group(int g, permissions_interface const* perms)
{
	if (g < 0) return;

	mutex::scoped_lock l(m_mutex);

	if (g >= m_groups.size())
		m_groups.resize(g+1, &no_perms);
	m_groups[g] = perms;
}

permissions_interface const* auth::find_user(std::string username, std::string password) const
{
	mutex::scoped_lock l(m_mutex);

	std::map<std::string, account_t>::const_iterator i = m_accounts.find(username);
	if (i == m_accounts.end()) return NULL;

	sha1_hash ph = i->second.password_hash(password);
	if (ph != i->second.hash) return NULL;

	if (i->second.group < 0 || i->second.group >= m_groups.size())
		return &no_perms;

	return m_groups[i->second.group];
}

sha1_hash auth::account_t::password_hash(std::string const& pwd) const
{
	hasher h;
	h.update(salt, sizeof(salt));
	if (pwd.size()) h.update(pwd);
	sha1_hash ret = h.final();

	return ret;
}

void auth::save_accounts(std::string const& filename, error_code& ec) const
{
	FILE* f = fopen(filename.c_str(), "w+");
	if (f == NULL)
	{
		ec = error_code(errno, get_system_category());
		return;
	}

	mutex::scoped_lock l(m_mutex);

	for (std::map<std::string, account_t>::const_iterator i = m_accounts.begin()
		, end(m_accounts.end()); i != end; ++i)
	{
		account_t const& a = i->second;
		char salthash[21];
		to_hex(a.salt, 10, salthash);
		fprintf(f, "%s\t%s\t%s\t%d\n", i->first.c_str(), to_hex(a.hash.to_string()).c_str(), salthash, i->second.group);
	}

	fclose(f);
}

void auth::load_accounts(std::string const& filename, error_code& ec)
{
	FILE* f = fopen(filename.c_str(), "r");
	if (f == NULL)
	{
		ec = error_code(errno, get_system_category());
		return;
	}

	mutex::scoped_lock l(m_mutex);

	m_accounts.clear();

	char username[512];
	char pwdhash[41];
	char salt[21];
	int group;

	while (fscanf(f, "%512s\t%41s\t%21s\t%d\n", username, pwdhash, salt, &group) == 4)
	{
		account_t a;
		if (!from_hex(pwdhash, 40, (char*)&a.hash[0])) continue;
		if (!from_hex(salt, 20, a.salt)) continue;
		if (group < 0) continue;
		a.group = group;
		m_accounts[username] = a;
	}

	fclose(f);
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

