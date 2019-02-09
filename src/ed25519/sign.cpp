// ignore warnings in this file
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include "libtorrent/ed25519.hpp"
#include "libtorrent/hasher512.hpp"
#include "ge.h"
#include "sc.h"

namespace libtorrent
{

void ed25519_sign(unsigned char *signature, const unsigned char *message, std::ptrdiff_t message_len, const unsigned char *public_key, const unsigned char *private_key) {
    ge_p3 R;

    hasher512 hash;
    hash.update({reinterpret_cast<char const*>(private_key) + 32, 32});
    hash.update({reinterpret_cast<char const*>(message), message_len});
    sha512_hash r = hash.final();

    sc_reduce(reinterpret_cast<unsigned char*>(r.data()));
    ge_scalarmult_base(&R, reinterpret_cast<unsigned char*>(r.data()));
    ge_p3_tobytes(signature, &R);

    hash.reset();
    hash.update({reinterpret_cast<char const*>(signature), 32});
    hash.update({reinterpret_cast<char const*>(public_key), 32});
    hash.update({reinterpret_cast<char const*>(message), message_len});
    sha512_hash hram = hash.final();

    sc_reduce(reinterpret_cast<unsigned char*>(hram.data()));
    sc_muladd(signature + 32
        , reinterpret_cast<unsigned char*>(hram.data())
        , private_key
        , reinterpret_cast<unsigned char*>(r.data()));
}

}
