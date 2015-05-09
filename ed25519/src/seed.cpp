// ignore warnings in this file
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include "libtorrent/ed25519.hpp"

#ifndef ED25519_NO_SEED

#ifdef _WIN32
#include <Windows.h>
#include <Wincrypt.h>
#else
#include <stdio.h>
#endif

void ed25519_create_seed(unsigned char *seed) {
#ifdef _WIN32
    HCRYPTPROV prov;

    if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))  {
        throw boost::system::system_error(boost::system::error_code(GetLastError()
            , boost::system::system_category()));
    }

    if (!CryptGenRandom(prov, 32, seed))  {
        CryptReleaseContext(prov, 0);
        throw boost::system::system_error(boost::system::error_code(GetLastError()
            , boost::system::system_category()));
    }

    CryptReleaseContext(prov, 0);
#else
    FILE *f = fopen("/dev/urandom", "rb");

    if (f == NULL) {
        throw boost::system::system_error(boost::system::error_code(errno, boost::system::generic_category()));
    }

    int read = fread(seed, 1, 32, f);
    if (read != 32) {
        fclose(f);
        throw boost::system::system_error(boost::system::error_code(errno, boost::system::generic_category()));
    }

    fclose(f);
#endif
}

#endif

