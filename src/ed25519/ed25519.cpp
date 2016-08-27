#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/hasher512.hpp"
#include "libtorrent/ed25519/ed25519.hpp"
#include "libtorrent/ed25519/ge.hpp"
#include "libtorrent/ed25519/sc.hpp"

#if TORRENT_USE_CRYPTOAPI
#include <windows.h>
#include <wincrypt.h>

#elif defined TORRENT_USE_LIBCRYPTO
extern "C" {
#include <openssl/rand.h>
#include <openssl/err.h>
}

#endif

namespace libtorrent {
namespace ed25519
{
	void ed25519_create_seed(ed25519_seed& seed)
	{
#if TORRENT_USE_CRYPTOAPI
		HCRYPTPROV prov;

		if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}

		if (!CryptGenRandom(prov, int(seed.size()), reinterpret_cast<BYTE*>(seed.data())))
		{
			CryptReleaseContext(prov, 0);
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}

		CryptReleaseContext(prov, 0);
#elif defined TORRENT_USE_LIBCRYPTO
		int r = RAND_bytes(reinterpret_cast<unsigned char*>(seed.data())
			, int(seed.size()));
		if (r != 1)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(ERR_get_error(), system_category()));
#else
			std::terminate();
#endif
		}
#else
		std::uint32_t s = random(0xffffffff);
		std::independent_bits_engine<std::mt19937, 8, std::uint8_t> generator(s);
		std::generate(seed.begin(), seed.end(), std::ref(generator));
#endif
	}

	void ed25519_create_keypair(ed25519_public_key& public_key
		, ed25519_private_key& private_key, ed25519_seed const& seed)
	{
		auto const public_key_ptr = reinterpret_cast<unsigned char*>(public_key.data());
		auto const private_key_ptr = reinterpret_cast<unsigned char*>(private_key.data());

		ge_p3 A;

		hasher512 hash(seed);
		std::memcpy(private_key_ptr, hash.final().data(), 64);
		private_key[0] &= 248;
		private_key[31] &= 63;
		private_key[31] |= 64;


		ge_scalarmult_base(&A, private_key_ptr);
		ge_p3_tobytes(public_key_ptr, &A);
	}

	void ed25519_sign(ed25519_signature& signature
		, span<char const> message
		, ed25519_public_key const& public_key
		, ed25519_private_key const& private_key)
	{
		ge_p3 R;

		hasher512 hash;
		hash.update(span<char const>(private_key).subspan(32));
		hash.update(message);
		sha512_hash r = hash.final();

		auto const signature_ptr = reinterpret_cast<unsigned char*>(signature.data());
		auto const private_key_ptr = reinterpret_cast<unsigned char const*>(private_key.data());
		auto const r_ptr = reinterpret_cast<unsigned char*>(r.data());

		sc_reduce(r_ptr);
		ge_scalarmult_base(&R, r_ptr);
		ge_p3_tobytes(signature_ptr, &R);

		hash.reset();
		hash.update(span<char const>(signature).first(32));
		hash.update(public_key);
		hash.update(message);
		sha512_hash hram = hash.final();
		auto const hram_ptr = reinterpret_cast<unsigned char*>(hram.data());

		sc_reduce(hram_ptr);
		sc_muladd(signature_ptr + 32
			, hram_ptr
			, private_key_ptr
			, r_ptr);
	}

	namespace
	{
		static int consttime_equal(const unsigned char *x, const unsigned char *y)
		{
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
	}

	bool ed25519_verify(ed25519_signature const& signature
		, span<char const> message
		, ed25519_public_key const& public_key)
	{
		unsigned char checker[32];
		ge_p3 A;
		ge_p2 R;

		if (signature[63] & 224) {
			return false;
		}

		auto const signature_ptr = reinterpret_cast<unsigned char const*>(signature.data());
		auto const public_key_ptr = reinterpret_cast<unsigned char const*>(public_key.data());

		if (ge_frombytes_negate_vartime(&A, public_key_ptr) != 0) {
			return false;
		}

		hasher512 hash;
		hash.update(span<char const>(signature).first(32));
		hash.update(public_key);
		hash.update(message);
		sha512_hash h = hash.final();
		auto const h_ptr = reinterpret_cast<unsigned char*>(h.data());

		sc_reduce(h_ptr);
		ge_double_scalarmult_vartime(&R, h_ptr
				, &A, signature_ptr + 32);
		ge_tobytes(checker, &R);

		if (!consttime_equal(checker, signature_ptr)) {
			return false;
		}

		return true;
	}

	void ed25519_add_scalar(ed25519_public_key* public_key
		, ed25519_private_key* private_key
		, ed25519_scalar const& scalar)
	{
		const unsigned char SC_1[32] = {1}; /* scalar with value 1 */

		unsigned char n[32];
		ge_p3 nB;
		ge_p1p1 A_p1p1;
		ge_p3 A;
		ge_p3 public_key_unpacked;
		ge_cached T;

		int i;

		/* copy the scalar and clear highest bit */
		for (i = 0; i < 31; ++i) {
			n[i] = scalar[i];
		}
		n[31] = scalar[31] & 127;

		/* private key: a = n + t */
		if (private_key) {
			auto const private_key_ptr = reinterpret_cast<unsigned char*>(private_key->data());
			sc_muladd(private_key_ptr, SC_1, n, private_key_ptr);

			// https://github.com/orlp/ed25519/issues/3
			hasher512 hash;
			hash.update(span<char const>(*private_key).subspan(32));
			hash.update(scalar);
			sha512_hash hashbuf = hash.final();
			for (i = 0; i < 32; ++i)
				private_key_ptr[32 + i] = hashbuf[i];
		}

		/* public key: A = nB + T */
		if (public_key) {
			/* if we know the private key we don't need a point addition, which is faster */
			/* using a "timing attack" you could find out whether or not we know the private
			   key, but this information seems rather useless - if this is important pass
			   public_key and private_key separately in 2 function calls */
			if (private_key) {
				ge_scalarmult_base(&A, reinterpret_cast<unsigned char*>(private_key->data()));
			} else {
				/* unpack public key into T */
				ge_frombytes_negate_vartime(&public_key_unpacked
					, reinterpret_cast<unsigned char*>(public_key->data()));
				fe_neg(public_key_unpacked.X, public_key_unpacked.X); // undo negate
				fe_neg(public_key_unpacked.T, public_key_unpacked.T); // undo negate
				ge_p3_to_cached(&T, &public_key_unpacked);

				/* calculate n*B */
				ge_scalarmult_base(&nB, n);

				/* A = n*B + T */
				ge_add(&A_p1p1, &nB, &T);
				ge_p1p1_to_p3(&A, &A_p1p1);
			}

			/* pack public key */
			ge_p3_tobytes(reinterpret_cast<unsigned char*>(public_key->data()), &A);
		}
	}

	void ed25519_key_exchange(ed25519_shared_secret& shared_secret
		, ed25519_public_key const& public_key
		, ed25519_private_key const& private_key)
	{
		unsigned char e[32];
		unsigned int i;

		fe x1;
		fe x2;
		fe z2;
		fe x3;
		fe z3;
		fe tmp0;
		fe tmp1;

		int pos;
		unsigned int swap;
		unsigned int b;

		/* copy the private key and make sure it's valid */
		for (i = 0; i < 32; ++i) {
			e[i] = private_key[i];
		}

		e[0] &= 248;
		e[31] &= 63;
		e[31] |= 64;

		/* unpack the public key and convert edwards to montgomery */
		/* due to CodesInChaos: montgomeryX = (edwardsY + 1)*inverse(1 - edwardsY) mod p */
		fe_frombytes(x1, reinterpret_cast<unsigned char const*>(public_key.data()));
		fe_1(tmp1);
		fe_add(tmp0, x1, tmp1);
		fe_sub(tmp1, tmp1, x1);
		fe_invert(tmp1, tmp1);
		fe_mul(x1, tmp0, tmp1);

		fe_1(x2);
		fe_0(z2);
		fe_copy(x3, x1);
		fe_1(z3);

		swap = 0;
		for (pos = 254; pos >= 0; --pos) {
			b = e[pos / 8] >> (pos & 7);
			b &= 1;
			swap ^= b;
			fe_cswap(x2, x3, swap);
			fe_cswap(z2, z3, swap);
			swap = b;

			/* from montgomery.h */
			fe_sub(tmp0, x3, z3);
			fe_sub(tmp1, x2, z2);
			fe_add(x2, x2, z2);
			fe_add(z2, x3, z3);
			fe_mul(z3, tmp0, x2);
			fe_mul(z2, z2, tmp1);
			fe_sq(tmp0, tmp1);
			fe_sq(tmp1, x2);
			fe_add(x3, z3, z2);
			fe_sub(z2, z3, z2);
			fe_mul(x2, tmp1, tmp0);
			fe_sub(tmp1, tmp1, tmp0);
			fe_sq(z2, z2);
			fe_mul121666(z3, tmp1);
			fe_sq(x3, x3);
			fe_add(tmp0, tmp0, z3);
			fe_mul(z3, x1, z2);
			fe_mul(z2, tmp1, tmp0);
		}

		fe_cswap(x2, x3, swap);
		fe_cswap(z2, z3, swap);

		fe_invert(z2, z2);
		fe_mul(x2, x2, z2);
		fe_tobytes(reinterpret_cast<unsigned char*>(shared_secret.data()), x2);
	}

}}
