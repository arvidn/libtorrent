#ifndef ED25519_H
#define ED25519_H

#include <stddef.h>

#include "libtorrent/config.hpp" // for TORRENT_EXPORT

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ED25519_NO_SEED
int TORRENT_EXPORT ed25519_create_seed(unsigned char *seed);
#endif

void TORRENT_EXPORT ed25519_create_keypair(unsigned char *public_key, unsigned char *private_key, const unsigned char *seed);
void TORRENT_EXPORT ed25519_sign(unsigned char *signature, const unsigned char *message, size_t message_len, const unsigned char *public_key, const unsigned char *private_key);
int TORRENT_EXPORT ed25519_verify(const unsigned char *signature, const unsigned char *message, size_t message_len, const unsigned char *private_key);
void TORRENT_EXPORT ed25519_add_scalar(unsigned char *public_key, unsigned char *private_key, const unsigned char *scalar);
void TORRENT_EXPORT ed25519_key_exchange(unsigned char *shared_secret, const unsigned char *public_key, const unsigned char *private_key);


#ifdef __cplusplus
}
#endif

#endif
