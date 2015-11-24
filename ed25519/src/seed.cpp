// ignore warnings in this file
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include "libtorrent/config.hpp"
#include "libtorrent/ed25519.hpp"

#ifndef ED25519_NO_SEED

#if TORRENT_USE_CRYPTOGRAPHIC_BUFFER
#include <robuffer.h>
#include <wrl/client.h>
using namespace Windows::Security::Cryptography;
using namespace Windows::Storage::Streams;
using namespace Microsoft::WRL;
#elif defined _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <stdio.h>
#endif

void ed25519_create_seed(unsigned char *seed) {
#if TORRENT_USE_CRYPTOGRAPHIC_BUFFER
    IBuffer^ seedBuffer = CryptographicBuffer::GenerateRandom(32);
    ComPtr<IBufferByteAccess> bufferByteAccess;
    reinterpret_cast<IInspectable*>(seedBuffer)->QueryInterface(IID_PPV_ARGS(&bufferByteAccess));
    bufferByteAccess->Buffer(&seed);
#elif defined _WIN32
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
        throw boost::system::system_error(boost::system::error_code(errno, boost::system::system_category()));
    }

    int read = fread(seed, 1, 32, f);
    if (read != 32) {
        fclose(f);
        throw boost::system::system_error(boost::system::error_code(errno, boost::system::system_category()));
    }

    fclose(f);
#endif
}

#endif

