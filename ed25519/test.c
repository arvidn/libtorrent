#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

//#define ED25519_DLL
#include "src/ed25519.h"

#include "src/ge.h"
#include "src/sc.h"

const char message[] = "Hello, world!";


int main(int argc, char *argv[]) {
    unsigned char public_key[32], private_key[64], seed[32], scalar[32];
    unsigned char other_public_key[32], other_private_key[64];
    unsigned char shared_secret[32], other_shared_secret[32];
    unsigned char signature[64];

    clock_t start;
    clock_t end;
    int i;

    /* create a random seed, and a keypair out of that seed */
    ed25519_create_seed(seed);
    ed25519_create_keypair(public_key, private_key, seed);

    /* create signature on the message with the keypair */
    ed25519_sign(signature, message, strlen(message), public_key, private_key);

    /* verify the signature */
    if (ed25519_verify(signature, message, strlen(message), public_key)) {
        printf("valid signature\n");
    } else {
        printf("invalid signature\n");
    }

    /* create scalar and add it to the keypair */
    ed25519_create_seed(scalar);
    ed25519_add_scalar(public_key, private_key, scalar);

    /* create signature with the new keypair */
    ed25519_sign(signature, message, strlen(message), public_key, private_key);

    /* verify the signature with the new keypair */
    if (ed25519_verify(signature, message, strlen(message), public_key)) {
        printf("valid signature\n");
    } else {
        printf("invalid signature\n");
    }

    /* make a slight adjustment and verify again */
    signature[44] ^= 0x10;
    if (ed25519_verify(signature, message, strlen(message), public_key)) {
        printf("did not detect signature change\n");
    } else {
        printf("correctly detected signature change\n");
    }

    /* generate two keypairs for testing key exchange */
    ed25519_create_seed(seed);
    ed25519_create_keypair(public_key, private_key, seed);
    ed25519_create_seed(seed);
    ed25519_create_keypair(other_public_key, other_private_key, seed);

    /* create two shared secrets - from both perspectives - and check if they're equal */
    ed25519_key_exchange(shared_secret, other_public_key, private_key);
    ed25519_key_exchange(other_shared_secret, public_key, other_private_key);

    for (i = 0; i < 32; ++i) {
        if (shared_secret[i] != other_shared_secret[i]) {
            printf("key exchange was incorrect\n");
            break;
        }
    }

    if (i == 32) {
        printf("key exchange was correct\n");
    }

    /* test performance */
    printf("testing seed generation performance: ");
    start = clock();
    for (i = 0; i < 10000; ++i) {
        ed25519_create_seed(seed);
    }
    end = clock();

    printf("%fus per seed\n", ((double) ((end - start) * 1000)) / CLOCKS_PER_SEC / i * 1000);


    printf("testing key generation performance: ");
    start = clock();
    for (i = 0; i < 10000; ++i) {
        ed25519_create_keypair(public_key, private_key, seed);
    }
    end = clock();

    printf("%fus per keypair\n", ((double) ((end - start) * 1000)) / CLOCKS_PER_SEC / i * 1000);

    printf("testing sign performance: ");
    start = clock();
    for (i = 0; i < 10000; ++i) {
        ed25519_sign(signature, message, strlen(message), public_key, private_key);
    }
    end = clock();

    printf("%fus per signature\n", ((double) ((end - start) * 1000)) / CLOCKS_PER_SEC / i * 1000);

    printf("testing verify performance: ");
    start = clock();
    for (i = 0; i < 10000; ++i) {
        ed25519_verify(signature, message, strlen(message), public_key);
    }
    end = clock();

    printf("%fus per signature\n", ((double) ((end - start) * 1000)) / CLOCKS_PER_SEC / i * 1000);


    printf("testing keypair scalar addition performance: ");
    start = clock();
    for (i = 0; i < 10000; ++i) {
        ed25519_add_scalar(public_key, private_key, scalar);
    }
    end = clock();

    printf("%fus per keypair\n", ((double) ((end - start) * 1000)) / CLOCKS_PER_SEC / i * 1000);

    printf("testing public key scalar addition performance: ");
    start = clock();
    for (i = 0; i < 10000; ++i) {
        ed25519_add_scalar(public_key, NULL, scalar);
    }
    end = clock();

    printf("%fus per key\n", ((double) ((end - start) * 1000)) / CLOCKS_PER_SEC / i * 1000);

    printf("testing key exchange performance: ");
    start = clock();
    for (i = 0; i < 10000; ++i) {
        ed25519_key_exchange(shared_secret, other_public_key, private_key);
    }
    end = clock();

    printf("%fus per shared secret\n", ((double) ((end - start) * 1000)) / CLOCKS_PER_SEC / i * 1000);

    return 0;
}
