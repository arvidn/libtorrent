// ignore warnings in this file
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include "libtorrent/aux_/ed25519.hpp"
#include "libtorrent/aux_/hasher512.hpp"
#include "ge.h"

namespace libtorrent {
namespace aux {

void ed25519_create_keypair(unsigned char *public_key, unsigned char *private_key, const unsigned char *seed) {
    ge_p3 A;

    hasher512 hash({reinterpret_cast<char const*>(seed), 32});
    std::memcpy(private_key, hash.final().data(), 64);
    private_key[0] &= 248;
    private_key[31] &= 63;
    private_key[31] |= 64;

    ge_scalarmult_base(&A, private_key);
    ge_p3_tobytes(public_key, &A);
}

} }
