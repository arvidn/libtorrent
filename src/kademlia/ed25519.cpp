/*

Copyright (c) 2016, 2021, Alden Torres
Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <libtorrent/kademlia/ed25519.hpp>
#include <libtorrent/aux_/random.hpp>
#include <libtorrent/aux_/ed25519.hpp>

namespace lt { namespace dht {

	std::array<char, 32> ed25519_create_seed()
	{
		std::array<char, 32> seed;
		aux::crypto_random_bytes(seed);
		return seed;
	}

	std::tuple<public_key, secret_key> ed25519_create_keypair(
		std::array<char, 32> const& seed)
	{
		public_key pk;
		secret_key sk;

		auto* const pk_ptr = reinterpret_cast<unsigned char*>(pk.bytes.data());
		auto* const sk_ptr = reinterpret_cast<unsigned char*>(sk.bytes.data());
		auto const* const seed_ptr = reinterpret_cast<unsigned char const*>(seed.data());

		lt::aux::ed25519_create_keypair(pk_ptr, sk_ptr, seed_ptr);

		return std::make_tuple(pk, sk);
	}

	signature ed25519_sign(span<char const> msg
		, public_key const& pk, secret_key const& sk)
	{
		signature sig;

		auto* const sig_ptr = reinterpret_cast<unsigned char*>(sig.bytes.data());
		auto const* const msg_ptr = reinterpret_cast<unsigned char const*>(msg.data());
		auto const* const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());
		auto const* const sk_ptr = reinterpret_cast<unsigned char const*>(sk.bytes.data());

		lt::aux::ed25519_sign(sig_ptr, msg_ptr, msg.size(), pk_ptr, sk_ptr);

		return sig;
	}

	bool ed25519_verify(signature const& sig
		, span<char const> msg, public_key const& pk)
	{
		auto const* const sig_ptr = reinterpret_cast<unsigned char const*>(sig.bytes.data());
		auto const* const msg_ptr = reinterpret_cast<unsigned char const*>(msg.data());
		auto const* const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());

		return lt::aux::ed25519_verify(sig_ptr, msg_ptr, msg.size(), pk_ptr) == 1;
	}

	public_key ed25519_add_scalar(public_key const& pk
		, std::array<char, 32> const& scalar)
	{
		public_key ret(pk.bytes.data());

		auto* const ret_ptr = reinterpret_cast<unsigned char*>(ret.bytes.data());
		auto const* const scalar_ptr = reinterpret_cast<unsigned char const*>(scalar.data());

		lt::aux::ed25519_add_scalar(ret_ptr, nullptr, scalar_ptr);

		return ret;
	}

	secret_key ed25519_add_scalar(secret_key const& sk
		, std::array<char, 32> const& scalar)
	{
		secret_key ret(sk.bytes.data());

		auto* const ret_ptr = reinterpret_cast<unsigned char*>(ret.bytes.data());
		auto const* const scalar_ptr = reinterpret_cast<unsigned char const*>(scalar.data());

		lt::aux::ed25519_add_scalar(nullptr, ret_ptr, scalar_ptr);

		return ret;
	}

	std::array<char, 32> ed25519_key_exchange(
		public_key const& pk, secret_key const& sk)
	{
		std::array<char, 32> secret;

		auto* const secret_ptr = reinterpret_cast<unsigned char*>(secret.data());
		auto const* const pk_ptr = reinterpret_cast<unsigned char const*>(pk.bytes.data());
		auto const* const sk_ptr = reinterpret_cast<unsigned char const*>(sk.bytes.data());

		lt::aux::ed25519_key_exchange(secret_ptr, pk_ptr, sk_ptr);

		return secret;
	}

}}
