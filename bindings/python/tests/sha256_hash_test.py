import unittest

import libtorrent as lt

from . import lib


class Sha256HashTest(unittest.TestCase):
    def test_init_bytes(self) -> None:
        data = lib.get_random_bytes(32)
        sha256 = lt.sha256_hash(data)
        self.assertEqual(sha256.to_bytes(), data)

    def test_init_short_buffer(self) -> None:
        with self.assertRaises(ValueError):
            lt.sha256_hash(b"a" * 31)

    def test_init_long_buffer_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            sha256 = lt.sha256_hash(b"a" * 33)
        self.assertEqual(sha256, lt.sha256_hash(b"a" * 32))

    def test_init_str_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            sha256 = lt.sha256_hash("a" * 32)
        self.assertEqual(sha256, lt.sha256_hash(b"a" * 32))

    def test_equal(self) -> None:
        data = lib.get_random_bytes(32)
        sha256_1 = lt.sha256_hash(data)
        sha256_2 = lt.sha256_hash(data)

        self.assertEqual(sha256_1, sha256_2)
        self.assertEqual(hash(sha256_1), hash(sha256_2))

    def test_not_equal(self) -> None:
        sha256_1 = lt.sha256_hash(lib.get_random_bytes(32))
        sha256_2 = lt.sha256_hash(lib.get_random_bytes(32))

        self.assertNotEqual(sha256_1, sha256_2)

    def test_less_than(self) -> None:
        sha256_1 = lt.sha256_hash(b"\0" * 32)
        sha256_2 = lt.sha256_hash(b"\0" * 31 + b"\1")

        self.assertLess(sha256_1, sha256_2)

    def test_convert(self) -> None:
        data = lib.get_random_bytes(32)
        sha256 = lt.sha256_hash(data)

        self.assertEqual(str(sha256), data.hex())
        self.assertEqual(sha256.to_bytes(), data)

    def test_to_string(self) -> None:
        sha256 = lt.sha256_hash(b"a" * 32)
        self.assertEqual(sha256.to_string(), b"a" * 32)

    def test_clear(self) -> None:
        sha256 = lt.sha256_hash(lib.get_random_bytes(32))

        sha256.clear()

        self.assertEqual(sha256.to_bytes(), b"\0" * 32)

    def test_empty(self) -> None:
        sha256 = lt.sha256_hash()
        self.assertTrue(sha256.is_all_zeros())

    def test_is_all_zeros(self) -> None:
        sha256 = lt.sha256_hash(lib.get_random_bytes(32))
        self.assertFalse(sha256.is_all_zeros())

        sha256 = lt.sha256_hash(b"\0" * 32)
        self.assertTrue(sha256.is_all_zeros())
