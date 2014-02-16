/*

Copyright (c) 2011, Arvid Norberg, Magnus Jonsson
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

#include "libtorrent/rsa.hpp"
#include "libtorrent/hasher.hpp"

#if defined TORRENT_USE_OPENSSL

extern "C"
{
#include <openssl/rsa.h>
#include <openssl/objects.h> // for NID_sha1
}

namespace libtorrent
{

// returns the size of the resulting signature
int sign_rsa(sha1_hash const& digest
	, char const* private_key, int private_len
	, char* signature, int sig_len)
{
	// convert bytestring to internal representation
	// of the private key
	RSA* priv = 0;
	unsigned char const* key = (unsigned char const*)private_key;
	priv = d2i_RSAPrivateKey(&priv, &key, private_len);
	if (priv == 0) return -1;

	if (RSA_size(priv) > sig_len)
	{
		RSA_free(priv);
		return -1;
	}

	RSA_sign(NID_sha1, &digest[0], 20, (unsigned char*)signature, (unsigned int*)&sig_len, priv);

	RSA_free(priv);

	return sig_len;
}

// returns true if the signature is valid
bool verify_rsa(sha1_hash const& digest
	, char const* public_key, int public_len
	, char const* signature, int sig_len)
{
	// convert bytestring to internal representation
	// of the public key
	RSA* pub = 0;
	unsigned char const* key = (unsigned char const*)public_key;
	pub = d2i_RSAPublicKey(&pub, &key, public_len);
	if (pub == 0) return false;

	int ret = RSA_verify(NID_sha1, &digest[0], 20, (unsigned char*)signature, sig_len, pub);

	RSA_free(pub);

	return ret;
}

bool generate_rsa_keys(char* public_key, int* public_len
	, char* private_key, int* private_len, int key_size)
{
	RSA* keypair = RSA_generate_key(key_size, 3, 0, 0);
	if (keypair == 0) return false;

	bool ret = false;
	unsigned char* pub = (unsigned char*)public_key;
	unsigned char* priv = (unsigned char*)private_key;

	if (RSA_size(keypair) > *public_len) goto getout;
	if (RSA_size(keypair) > *private_len) goto getout;

	*public_len = i2d_RSAPublicKey(keypair, &pub);
	*private_len = i2d_RSAPrivateKey(keypair, &priv);

	ret = true;

getout:
	RSA_free(keypair);
	return ret;
}

} // namespace libtorrent

#else

// just stub these out for now, since they're not used anywhere yet
namespace libtorrent
{

// returns the size of the resulting signature
int sign_rsa(sha1_hash const& digest
	, char const* private_key, int private_len
	, char* signature, int sig_len)
{
	return 0;
}

// returns true if the signature is valid
bool verify_rsa(sha1_hash const& digest
	, char const* public_key, int public_len
	, char const* signature, int sig_len)
{
	return false;
}

bool generate_rsa_keys(char* public_key, int* public_len
	, char* private_key, int* private_len, int key_size)
{
	return false;
}

} // namespace libtorrent

#endif


