// ignore warnings in this file
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include "libtorrent/ed25519.hpp"
#include "libtorrent/hasher512.hpp"
#include "ge.h"
#include "sc.h"

using namespace libtorrent;

void ed25519_sign(unsigned char *signature, const unsigned char *message, size_t message_len, const unsigned char *public_key, const unsigned char *private_key) {
    hasher512 hash;
    unsigned char hram[64];
    unsigned char r[64];
    ge_p3 R;

    hash.update({reinterpret_cast<char const*>(private_key) + 32, 32});
    hash.update({reinterpret_cast<char const*>(message), message_len});
    std::memcpy(r, hash.final().data(), 64);

    sc_reduce(r);
    ge_scalarmult_base(&R, r);
    ge_p3_tobytes(signature, &R);

    hash.reset();
    hash.update({reinterpret_cast<char const*>(signature), 32});
    hash.update({reinterpret_cast<char const*>(public_key), 32});
    hash.update({reinterpret_cast<char const*>(message), message_len});
    std::memcpy(hram, hash.final().data(), 64);

    sc_reduce(hram);
    sc_muladd(signature + 32, hram, private_key, r);
}
