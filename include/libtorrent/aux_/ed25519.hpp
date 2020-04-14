#ifndef ED25519_HPP
#define ED25519_HPP

#include "libtorrent/aux_/export.hpp" // for TORRENT_EXPORT
#include <cstddef> // for ptrdiff_t, size_t

namespace libtorrent {
namespace aux {

void TORRENT_EXTRA_EXPORT ed25519_create_keypair(unsigned char *public_key, unsigned char *private_key, const unsigned char *seed);
void TORRENT_EXTRA_EXPORT ed25519_sign(unsigned char *signature, const unsigned char *message, std::ptrdiff_t message_len, const unsigned char *public_key, const unsigned char *private_key);
int TORRENT_EXTRA_EXPORT ed25519_verify(const unsigned char *signature, const unsigned char *message, std::ptrdiff_t message_len, const unsigned char *public_key);
void TORRENT_EXTRA_EXPORT ed25519_add_scalar(unsigned char *public_key, unsigned char *private_key, const unsigned char *scalar);
void TORRENT_EXTRA_EXPORT ed25519_key_exchange(unsigned char *shared_secret, const unsigned char *public_key, const unsigned char *private_key);

} }

#endif // ED25519_HPP
