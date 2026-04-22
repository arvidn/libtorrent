// Copyright James Keane 2026. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/kademlia/ed25519.hpp>
#include "bytes.hpp"

using namespace boost::python;
using namespace lt;

namespace {

bytes create_seed()
{
    auto const seed = dht::ed25519_create_seed();
    return bytes(seed.data(), seed.size());
}

tuple create_keypair(bytes const& seed)
{
    if (seed.arr.size() != 32)
        throw std::invalid_argument("seed must be 32 bytes");

    std::array<char, 32> s;
    std::copy(seed.arr.begin(), seed.arr.end(), s.begin());

    auto const [pk, sk] = dht::ed25519_create_keypair(s);
    return make_tuple(
        bytes(pk.bytes.data(), pk.bytes.size()),
        bytes(sk.bytes.data(), sk.bytes.size()));
}

bytes sign(bytes const& msg, bytes const& pk, bytes const& sk)
{
    if (pk.arr.size() != 32)
        throw std::invalid_argument("public key must be 32 bytes");
    if (sk.arr.size() != 64)
        throw std::invalid_argument("secret key must be 64 bytes");

    dht::public_key public_key(pk.arr.data());
    dht::secret_key secret_key(sk.arr.data());

    auto const sig = dht::ed25519_sign(
        {msg.arr.data(), static_cast<std::ptrdiff_t>(msg.arr.size())},
        public_key, secret_key);
    return bytes(sig.bytes.data(), sig.bytes.size());
}

bool verify(bytes const& sig, bytes const& msg, bytes const& pk)
{
    if (sig.arr.size() != 64)
        throw std::invalid_argument("signature must be 64 bytes");
    if (pk.arr.size() != 32)
        throw std::invalid_argument("public key must be 32 bytes");

    dht::signature signature(sig.arr.data());
    dht::public_key public_key(pk.arr.data());

    return dht::ed25519_verify(signature,
        {msg.arr.data(), static_cast<std::ptrdiff_t>(msg.arr.size())},
        public_key);
}

bytes add_scalar_public(bytes const& pk, bytes const& scalar)
{
    if (pk.arr.size() != 32)
        throw std::invalid_argument("public key must be 32 bytes");
    if (scalar.arr.size() != 32)
        throw std::invalid_argument("scalar must be 32 bytes");

    dht::public_key public_key(pk.arr.data());
    std::array<char, 32> s;
    std::copy(scalar.arr.begin(), scalar.arr.end(), s.begin());

    auto const result = dht::ed25519_add_scalar(public_key, s);
    return bytes(result.bytes.data(), result.bytes.size());
}

bytes add_scalar_secret(bytes const& sk, bytes const& scalar)
{
    if (sk.arr.size() != 64)
        throw std::invalid_argument("secret key must be 64 bytes");
    if (scalar.arr.size() != 32)
        throw std::invalid_argument("scalar must be 32 bytes");

    dht::secret_key secret_key(sk.arr.data());
    std::array<char, 32> s;
    std::copy(scalar.arr.begin(), scalar.arr.end(), s.begin());

    auto const result = dht::ed25519_add_scalar(secret_key, s);
    return bytes(result.bytes.data(), result.bytes.size());
}

bytes key_exchange(bytes const& pk, bytes const& sk)
{
    if (pk.arr.size() != 32)
        throw std::invalid_argument("public key must be 32 bytes");
    if (sk.arr.size() != 64)
        throw std::invalid_argument("secret key must be 64 bytes");

    dht::public_key public_key(pk.arr.data());
    dht::secret_key secret_key(sk.arr.data());

    auto const secret = dht::ed25519_key_exchange(public_key, secret_key);
    return bytes(secret.data(), secret.size());
}

} // anonymous namespace

void bind_ed25519()
{
    def("ed25519_create_seed", &create_seed);
    def("ed25519_create_keypair", &create_keypair);
    def("ed25519_sign", &sign);
    def("ed25519_verify", &verify);
    def("ed25519_add_scalar_public", &add_scalar_public);
    def("ed25519_add_scalar_secret", &add_scalar_secret);
    def("ed25519_key_exchange", &key_exchange);
}
