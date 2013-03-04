/*

Copyright (c) 2013, Arvid Norberg
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

#include "pam_auth.hpp"
#include <security/pam_appl.h>
#include "libtorrent/string_util.hpp"

namespace libtorrent
{
	pam_auth::pam_auth(std::string service_name)
		: m_service_name(service_name)
	{}

	pam_auth::~pam_auth() {}

	permissions_interface const* fail(int ret, pam_handle_t* h)
	{
		pam_end(h, ret);
		return NULL;
	}

	struct auth_context
	{
		std::string username;
		std::string password;
	};

	int pam_conversation(int num_msgs, const struct pam_message** msg, struct pam_response** r, void* user)
	{
		auth_context* ctx = (auth_context*)user;

		if (num_msgs == 0) return PAM_SUCCESS;

		// allocate an array for responses.
	   // memory freed is by PAM.
	   *r = (pam_response*)calloc(num_msgs, sizeof(pam_response));
		if (*r == NULL) return PAM_BUF_ERR;

		for (int i = 0; i < num_msgs; ++i)
		{
			switch (msg[i]->msg_style)
			{
				// echo on is code for "username"
				case PAM_PROMPT_ECHO_ON:
					r[i]->resp = allocate_string_copy(ctx->username.c_str());
					break;

				// echo off is code for "password"
				case PAM_PROMPT_ECHO_OFF:
					r[i]->resp = allocate_string_copy(ctx->password.c_str());
					break;

				case PAM_ERROR_MSG:
					fprintf(stderr, "authentication error: %s\n", msg[i]->msg);
					break;

				case PAM_TEXT_INFO:
					fprintf(stderr, "auth: %s\n", msg[i]->msg);
					break;
			}
		}
		return PAM_SUCCESS;
	}

	permissions_interface const* pam_auth::find_user(std::string username, std::string password) const
	{
		pam_handle_t* handle;

		auth_context ctx;
		ctx.username = username;
		ctx.password = password;

		pam_conv c;
		c.conv = &pam_conversation;
		c.appdata_ptr = &ctx;
		int ret = pam_start(m_service_name.c_str(), username.c_str(), &c, &handle);
		if (ret != PAM_SUCCESS) return fail(ret, handle);

		ret = pam_set_item(handle, PAM_RUSER, (void *)username.c_str()); 
		if (ret != PAM_SUCCESS) return fail(ret, handle);

		ret = pam_set_item(handle, PAM_RHOST, "localhost");
		if (ret != PAM_SUCCESS) return fail(ret, handle);

		ret = pam_authenticate(handle, 0);
		if (ret != PAM_SUCCESS) return fail(ret, handle);

		ret = pam_acct_mgmt(handle, 0);
		if (ret != PAM_SUCCESS) return fail(ret, handle);

		pam_end(handle, ret);

		std::map<std::string, permissions_interface*>::const_iterator i = m_users.find(username);
		if (i != m_users.end()) return i->second;
	
		static full_permissions full;
		return m_perms ? m_perms : &full;
	}

}

