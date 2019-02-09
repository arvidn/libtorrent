// ignore warnings in this file
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include "libtorrent/ed25519.hpp"
#include "libtorrent/hasher512.hpp"
#include "ge.h"
#include "sc.h"

namespace libtorrent
{

static int consttime_equal(const unsigned char *x, const unsigned char *y) {
    unsigned char r = 0;

    r = x[0] ^ y[0];
    #define F(i) r |= x[i] ^ y[i]
    F(1);
    F(2);
    F(3);
    F(4);
    F(5);
    F(6);
    F(7);
    F(8);
    F(9);
    F(10);
    F(11);
    F(12);
    F(13);
    F(14);
    F(15);
    F(16);
    F(17);
    F(18);
    F(19);
    F(20);
    F(21);
    F(22);
    F(23);
    F(24);
    F(25);
    F(26);
    F(27);
    F(28);
    F(29);
    F(30);
    F(31);
    #undef F

    return !r;
}

int ed25519_verify(const unsigned char *signature, const unsigned char *message, std::ptrdiff_t message_len, const unsigned char *public_key) {
    unsigned char checker[32];
    ge_p3 A;
    ge_p2 R;

    if (signature[63] & 224) {
        return 0;
    }

    if (ge_frombytes_negate_vartime(&A, public_key) != 0) {
        return 0;
    }

    hasher512 hash;
    hash.update({reinterpret_cast<char const*>(signature), 32});
    hash.update({reinterpret_cast<char const*>(public_key), 32});
    hash.update({reinterpret_cast<char const*>(message), message_len});
    sha512_hash h = hash.final();
    
    sc_reduce(reinterpret_cast<unsigned char*>(h.data()));
    ge_double_scalarmult_vartime(&R, reinterpret_cast<unsigned char*>(h.data())
        , &A, signature + 32);
    ge_tobytes(checker, &R);

    if (!consttime_equal(checker, signature)) {
        return 0;
    }

    return 1;
}

}
