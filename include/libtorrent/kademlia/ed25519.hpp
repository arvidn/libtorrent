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

#ifndef LIBTORRENT_ED25519_HPP
#define LIBTORRENT_ED25519_HPP

#include <libtorrent/config.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/kademlia/types.hpp>

#include <array>
#include <tuple>

namespace libtorrent { namespace dht {

	// See documentation of internal random_bytes
	TORRENT_EXPORT std::array<char, 32> ed25519_create_seed();

	// Creates a new key pair from the given seed.
	//
	// It's important to clarify that the seed completely determines
	// the key pair. Then it's enough to save the seed and the
	// public key as the key-pair in a buffer of 64 bytes. The standard
	// is (32 bytes seed, 32 bytes public key).
	//
	// This function does work with a given seed, giving you a pair of
	// (64 bytes private key, 32 bytes public key). It's a trade-off between
	// space and CPU, saving in one format or another.
	//
	// The smaller format is not weaker by any means, in fact, it is only
	// the seed (32 bytes) that determines the point in the curve.
	TORRENT_EXPORT std::tuple<public_key, secret_key> ed25519_create_keypair(
		std::array<char, 32> const& seed);

	// Creates a signature of the given message with the given key pair.
	TORRENT_EXPORT signature ed25519_sign(span<char const> msg
		, public_key const& pk, secret_key const& sk);

	// Verifies the signature on the given message using ``pk``
	TORRENT_EXPORT bool ed25519_verify(signature const& sig
		, span<char const> msg, public_key const& pk);

	// Adds a scalar to the given key pair where scalar is a 32 byte buffer
	// (possibly generated with `ed25519_create_seed`), generating a new key pair.
	//
	// You can calculate the public key sum without knowing the private key and
	// vice versa by passing in null for the key you don't know. This is useful
	// when a third party (an authoritative server for example) needs to enforce
	// randomness on a key pair while only knowing the public key of the other
	// side.
	//
	// Warning: the last bit of the scalar is ignored - if comparing scalars make
	// sure to clear it with `scalar[31] &= 127`.
	//
	// see http://crypto.stackexchange.com/a/6215/4697
	// see test_ed25519 for a practical example
	TORRENT_EXPORT public_key ed25519_add_scalar(public_key const& pk
		, std::array<char, 32> const& scalar);
	TORRENT_EXPORT secret_key ed25519_add_scalar(secret_key const& sk
		, std::array<char, 32> const& scalar);

	// Performs a key exchange on the given public key and private key, producing a
	// shared secret. It is recommended to hash the shared secret before using it.
	//
	// This is useful when two parties want to share a secret but both only knows
	// their respective public keys.
	// see test_ed25519 for a practical example
	TORRENT_EXPORT std::array<char, 32> ed25519_key_exchange(
		public_key const& pk, secret_key const& sk);

}}

#endif // LIBTORRENT_ED25519_HPP
