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

namespace libtorrent { namespace dht {

	std::array<char, 32> ed25519_create_seed()
	{
		std::array<char, 32> seed;
		aux::random_bytes(seed);
		return seed;
	}

	std::tuple<public_key, secret_key> ed25519_create_keypair(
		std::array<char, 32> const& seed)
	{
		public_key pk;
		secret_key sk;

		auto const pk_ptr = reinterpret_cast<unsigned char*>(pk.bytes.data());
		auto const sk_ptr = reinterpret_cast<unsigned char*>(sk.bytes.data());
		auto const seed_ptr = reinterpret_cast<unsigned char const*>(seed.data());

		libtorrent::ed25519_create_keypair(pk_ptr, sk_ptr, seed_ptr);

		return std::make_tuple(pk, sk);
	}

	signature ed25519_sign(span<char const> msg
		, public_key const& pk, secret_key const& sk)
	{
		signature sig;

		auto const sig_ptr = reinterpret_cast<unsigned char*>(sig.bytes.data());
		auto const msg_ptr = reinterpret_cast<unsigned char const*>(msg.data());
		auto const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());
		auto const sk_ptr = reinterpret_cast<unsigned char const*>(sk.bytes.data());

		libtorrent::ed25519_sign(sig_ptr, msg_ptr, msg.size(), pk_ptr, sk_ptr);

		return sig;
	}

	bool ed25519_verify(signature const& sig
		, span<char const> msg, public_key const& pk)
	{
		auto const sig_ptr = reinterpret_cast<unsigned char const*>(sig.bytes.data());
		auto const msg_ptr = reinterpret_cast<unsigned char const*>(msg.data());
		auto const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());

		return libtorrent::ed25519_verify(sig_ptr, msg_ptr, msg.size(), pk_ptr) == 1;
	}

	public_key ed25519_add_scalar(public_key const& pk
		, std::array<char, 32> const& scalar)
	{
		public_key ret(pk.bytes.data());

		auto const ret_ptr = reinterpret_cast<unsigned char*>(ret.bytes.data());
		auto const scalar_ptr = reinterpret_cast<unsigned char const*>(scalar.data());

		libtorrent::ed25519_add_scalar(ret_ptr, nullptr, scalar_ptr);

		return ret;
	}

	secret_key ed25519_add_scalar(secret_key const& sk
		, std::array<char, 32> const& scalar)
	{
		secret_key ret(sk.bytes.data());

		auto const ret_ptr = reinterpret_cast<unsigned char*>(ret.bytes.data());
		auto const scalar_ptr = reinterpret_cast<unsigned char const*>(scalar.data());

		libtorrent::ed25519_add_scalar(nullptr, ret_ptr, scalar_ptr);

		return ret;
	}

	std::array<char, 32> ed25519_key_exchange(
		public_key const& pk, secret_key const& sk)
	{
		std::array<char, 32> secret;

		auto const secret_ptr = reinterpret_cast<unsigned char*>(secret.data());
		auto const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());
		auto const sk_ptr = reinterpret_cast<unsigned char const*>(sk.bytes.data());

		libtorrent::ed25519_key_exchange(secret_ptr, pk_ptr, sk_ptr);

		return secret;
	}

}}
