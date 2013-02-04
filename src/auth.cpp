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

#include <string.h> // for strcmp() 
#include <stdio.h>

namespace libtorrent
{
auth::auth(save_settings* sett)
	: m_sett(sett)
{
	if (m_sett)
	{
		m_username = sett->get_str("remote.user");
		std::string pwdhash = sett->get_str("remote.password_hash");
		memcpy(&m_password_hash[0], pwdhash.c_str(), (std::min)(pwdhash.size(), size_t(20)));
		std::string salt = sett->get_str("remote.password_salt");
		memcpy(m_salt, salt.c_str(), (std::min)(salt.size(), sizeof(m_salt)));
		if (salt.empty())
		{
			for (int i = 0; i < sizeof(m_salt); ++i)
				m_salt[i] = rand();
			sett->set_str("remote.password_salt", std::string(m_salt, &m_salt[sizeof(salt)]));
		}
	}
	else
	{
		for (int i = 0; i < sizeof(m_salt); ++i)
			m_salt[i] = rand();
	}
}

void auth::set_password(std::string const& pwd)
{
	m_password_hash = password_hash(pwd);
	if (m_sett) m_sett->set_str("remote.password_hash", m_password_hash.to_string());
}

void auth::set_username(std::string const& user)
{
	m_username = user;
	if (m_sett) m_sett->set_str("remote.user", m_username);
}

sha1_hash auth::password_hash(std::string const& pwd) const
{
	hasher h;
	if (pwd.size()) h.update(pwd);
	h.update(m_salt, sizeof(m_salt));
	sha1_hash ret = h.final();

	return ret;
}

bool auth::handle_http(mg_connection* conn, mg_request_info const* request_info)
{
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
			std::string user = cred.substr(0, cred.find_first_of(':'));
			std::string pwd = cred.substr(user.size()+1);
			if (authenticate(user, pwd)) return false;
		}
	}

	mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\n"
		"WWW-Authenticate: Basic realm=\"BitTorrent\"\r\n"
		"Content-Length: 0\r\n\r\n");
	return true;
}

bool auth::authenticate(std::string const& username, std::string const& password) const
{
	if (username != m_username) return false;
	if (password_hash(password) != m_password_hash) return false;
	return true;
}

}

