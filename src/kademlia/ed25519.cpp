/*

Copyright (c) 2016, Arvid Norberg, Alden Torres
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

#include <libtorrent/kademlia/ed25519.hpp>
#include <libtorrent/random.hpp>
#include <libtorrent/ed25519.hpp>

namespace libtorrent {
namespace dht
{

	void ed25519_create_seed(std::array<char, 32>& seed)
	{
		aux::random_bytes(seed);
	}

	void ed25519_create_keypair(public_key& pk
		, secret_key& sk, std::array<char, 32> const& seed)
	{
		auto const pk_ptr = reinterpret_cast<unsigned char*>(pk.bytes.data());
		auto const sk_ptr = reinterpret_cast<unsigned char*>(sk.bytes.data());
		auto const seed_ptr = reinterpret_cast<unsigned char const*>(seed.data());

		libtorrent::ed25519_create_keypair(pk_ptr, sk_ptr, seed_ptr);
	}

	void ed25519_sign(signature& sig, span<char const> msg
		, public_key const& pk, secret_key const& sk)
	{
		auto const sig_ptr = reinterpret_cast<unsigned char*>(sig.bytes.data());
		auto const msg_ptr = reinterpret_cast<unsigned char const*>(msg.data());
		auto const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());
		auto const sk_ptr = reinterpret_cast<unsigned char const*>(sk.bytes.data());

		libtorrent::ed25519_sign(sig_ptr, msg_ptr, msg.size(), pk_ptr, sk_ptr);
	}

	bool ed25519_verify(signature const& sig
		, span<char const> msg, public_key const& pk)
	{
		auto const sig_ptr = reinterpret_cast<unsigned char const*>(sig.bytes.data());
		auto const msg_ptr = reinterpret_cast<unsigned char const*>(msg.data());
		auto const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());

		return libtorrent::ed25519_verify(sig_ptr, msg_ptr, msg.size(), pk_ptr) == 1;
	}

	void ed25519_add_scalar(std::shared_ptr<public_key> pk
		, std::shared_ptr<secret_key> sk, std::array<char, 32> const& scalar)
	{
		auto const pk_ptr = pk
			? reinterpret_cast<unsigned char*>(pk->bytes.data()) : nullptr;
		auto const sk_ptr = sk
			? reinterpret_cast<unsigned char*>(sk->bytes.data()) : nullptr;
		auto const scalar_ptr = reinterpret_cast<unsigned char const*>(scalar.data());

		libtorrent::ed25519_add_scalar(pk_ptr, sk_ptr, scalar_ptr);
	}

	void ed25519_key_exchange(std::array<char, 32>& secret
		, public_key const& pk, secret_key const& sk)
	{
		auto const secret_ptr = reinterpret_cast<unsigned char*>(secret.data());
		auto const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());
		auto const sk_ptr = reinterpret_cast<unsigned char const*>(sk.bytes.data());

		libtorrent::ed25519_key_exchange(secret_ptr, pk_ptr, sk_ptr);
	}

}}
