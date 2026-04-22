import unittest

import libtorrent as lt


class Ed25519CreateSeedTest(unittest.TestCase):
    def test_returns_32_bytes(self) -> None:
        seed = lt.ed25519_create_seed()
        self.assertEqual(len(seed), 32)

    def test_seeds_are_unique(self) -> None:
        seed1 = lt.ed25519_create_seed()
        seed2 = lt.ed25519_create_seed()
        self.assertNotEqual(seed1, seed2)


class Ed25519CreateKeypairTest(unittest.TestCase):
    def test_keypair_sizes(self) -> None:
        seed = lt.ed25519_create_seed()
        pk, sk = lt.ed25519_create_keypair(seed)
        self.assertEqual(len(pk), 32)
        self.assertEqual(len(sk), 64)

    def test_deterministic(self) -> None:
        seed = lt.ed25519_create_seed()
        pk1, sk1 = lt.ed25519_create_keypair(seed)
        pk2, sk2 = lt.ed25519_create_keypair(seed)
        self.assertEqual(pk1, pk2)
        self.assertEqual(sk1, sk2)

    def test_different_seeds_produce_different_keys(self) -> None:
        pk1, _ = lt.ed25519_create_keypair(lt.ed25519_create_seed())
        pk2, _ = lt.ed25519_create_keypair(lt.ed25519_create_seed())
        self.assertNotEqual(pk1, pk2)

    def test_invalid_seed_length(self) -> None:
        with self.assertRaises(Exception):
            lt.ed25519_create_keypair(b"\x00" * 16)


class Ed25519SignVerifyTest(unittest.TestCase):
    def setUp(self) -> None:
        seed = lt.ed25519_create_seed()
        self.pk, self.sk = lt.ed25519_create_keypair(seed)

    def test_sign_and_verify(self) -> None:
        msg = b"hello world"
        sig = lt.ed25519_sign(msg, self.pk, self.sk)
        self.assertEqual(len(sig), 64)
        self.assertTrue(lt.ed25519_verify(sig, msg, self.pk))

    def test_verify_wrong_message(self) -> None:
        sig = lt.ed25519_sign(b"hello", self.pk, self.sk)
        self.assertFalse(lt.ed25519_verify(sig, b"world", self.pk))

    def test_verify_wrong_key(self) -> None:
        msg = b"hello"
        sig = lt.ed25519_sign(msg, self.pk, self.sk)
        other_pk, _ = lt.ed25519_create_keypair(lt.ed25519_create_seed())
        self.assertFalse(lt.ed25519_verify(sig, msg, other_pk))

    def test_empty_message(self) -> None:
        sig = lt.ed25519_sign(b"", self.pk, self.sk)
        self.assertTrue(lt.ed25519_verify(sig, b"", self.pk))

    def test_sign_invalid_pk_length(self) -> None:
        with self.assertRaises(Exception):
            lt.ed25519_sign(b"msg", b"\x00" * 16, self.sk)

    def test_sign_invalid_sk_length(self) -> None:
        with self.assertRaises(Exception):
            lt.ed25519_sign(b"msg", self.pk, b"\x00" * 16)

    def test_verify_invalid_sig_length(self) -> None:
        with self.assertRaises(Exception):
            lt.ed25519_verify(b"\x00" * 32, b"msg", self.pk)

    def test_verify_invalid_pk_length(self) -> None:
        sig = lt.ed25519_sign(b"msg", self.pk, self.sk)
        with self.assertRaises(Exception):
            lt.ed25519_verify(sig, b"msg", b"\x00" * 16)


class Ed25519AddScalarTest(unittest.TestCase):
    def setUp(self) -> None:
        seed = lt.ed25519_create_seed()
        self.pk, self.sk = lt.ed25519_create_keypair(seed)
        self.scalar = b"\x01" + b"\x00" * 31

    def test_add_scalar_public(self) -> None:
        result = lt.ed25519_add_scalar_public(self.pk, self.scalar)
        self.assertEqual(len(result), 32)
        self.assertNotEqual(result, self.pk)

    def test_add_scalar_secret(self) -> None:
        result = lt.ed25519_add_scalar_secret(self.sk, self.scalar)
        self.assertEqual(len(result), 64)
        self.assertNotEqual(result, self.sk)

    def test_derived_keypair_signs_and_verifies(self) -> None:
        derived_pk = lt.ed25519_add_scalar_public(self.pk, self.scalar)
        derived_sk = lt.ed25519_add_scalar_secret(self.sk, self.scalar)
        msg = b"test message"
        sig = lt.ed25519_sign(msg, derived_pk, derived_sk)
        self.assertTrue(lt.ed25519_verify(sig, msg, derived_pk))

    def test_invalid_pk_length(self) -> None:
        with self.assertRaises(Exception):
            lt.ed25519_add_scalar_public(b"\x00" * 16, self.scalar)

    def test_invalid_scalar_length(self) -> None:
        with self.assertRaises(Exception):
            lt.ed25519_add_scalar_public(self.pk, b"\x00" * 16)

    def test_invalid_sk_length(self) -> None:
        with self.assertRaises(Exception):
            lt.ed25519_add_scalar_secret(b"\x00" * 16, self.scalar)


class Ed25519KeyExchangeTest(unittest.TestCase):
    def test_shared_secret(self) -> None:
        seed1 = lt.ed25519_create_seed()
        pk1, sk1 = lt.ed25519_create_keypair(seed1)
        seed2 = lt.ed25519_create_seed()
        pk2, sk2 = lt.ed25519_create_keypair(seed2)

        shared1 = lt.ed25519_key_exchange(pk2, sk1)
        shared2 = lt.ed25519_key_exchange(pk1, sk2)

        self.assertEqual(len(shared1), 32)
        self.assertEqual(shared1, shared2)

    def test_invalid_pk_length(self) -> None:
        _, sk = lt.ed25519_create_keypair(lt.ed25519_create_seed())
        with self.assertRaises(Exception):
            lt.ed25519_key_exchange(b"\x00" * 16, sk)

    def test_invalid_sk_length(self) -> None:
        pk, _ = lt.ed25519_create_keypair(lt.ed25519_create_seed())
        with self.assertRaises(Exception):
            lt.ed25519_key_exchange(pk, b"\x00" * 16)
