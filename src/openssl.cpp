/*

Copyright (c) 2020, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/openssl.hpp"
#include "libtorrent/settings_pack.hpp"

namespace libtorrent {
namespace aux {

#ifdef TORRENT_USE_OPENSSL

// all of OpenSSL causes warnings, so we just have to disable them
#include "libtorrent/aux_/disable_warnings_push.hpp"

void openssl_set_tlsext_hostname(SSL* s, char const* name)
{
#if OPENSSL_VERSION_NUMBER >= 0x90812f
	SSL_set_tlsext_host_name(s, name);
#endif
}

#if OPENSSL_VERSION_NUMBER >= 0x90812f

void openssl_set_tlsext_servername_callback(SSL_CTX* ctx
	, int (*servername_callback)(SSL*, int*, void*))
{
	SSL_CTX_set_tlsext_servername_callback(ctx, servername_callback);
}

void openssl_set_tlsext_servername_arg(SSL_CTX* ctx, void* userdata)
{
	SSL_CTX_set_tlsext_servername_arg(ctx, userdata);
}

int openssl_num_general_names(GENERAL_NAMES* gens)
{
	return sk_GENERAL_NAME_num(gens);
}

GENERAL_NAME* openssl_general_name_value(GENERAL_NAMES* gens, int i)
{
	return sk_GENERAL_NAME_value(gens, i);
}

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif // OPENSSL_VERSION_NUMBER

#endif // TORRENT_USE_OPENSSL

}
}
