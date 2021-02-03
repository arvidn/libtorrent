import unittest

import libtorrent as lt

from . import lib


class Sha1HashTest(unittest.TestCase):
    def test_init_bytes(self) -> None:
        data = lib.get_random_bytes(20)
        sha1 = lt.sha1_hash(data)
        self.assertEqual(sha1.to_bytes(), data)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_init_short_buffer(self) -> None:
        with self.assertRaises(ValueError):
            lt.sha1_hash(b"a" * 19)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_init_str_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            lt.sha1_hash("a" * 20)

    def test_init_str(self) -> None:
        sha1 = lt.sha1_hash("a" * 20)
        self.assertEqual(sha1.to_bytes(), b"a" * 20)

    def test_equal(self) -> None:
        data = lib.get_random_bytes(20)
        sha1_1 = lt.sha1_hash(data)
        sha1_2 = lt.sha1_hash(data)

        self.assertEqual(sha1_1, sha1_2)
        self.assertEqual(hash(sha1_1), hash(sha1_2))

    def test_not_equal(self) -> None:
        sha1_1 = lt.sha1_hash(lib.get_random_bytes(20))
        sha1_2 = lt.sha1_hash(lib.get_random_bytes(20))

        self.assertNotEqual(sha1_1, sha1_2)

    def test_less_than(self) -> None:
        sha1_1 = lt.sha1_hash(b"\0" * 20)
        sha1_2 = lt.sha1_hash(b"\0" * 19 + b"\1")

        self.assertLess(sha1_1, sha1_2)

    def test_convert(self) -> None:
        data = lib.get_random_bytes(20)
        sha1 = lt.sha1_hash(data)

        self.assertEqual(str(sha1), data.hex())
        self.assertEqual(sha1.to_bytes(), data)

    def test_to_string(self) -> None:
        sha1 = lt.sha1_hash(b"a" * 20)
        self.assertEqual(sha1.to_string(), b"a" * 20)

    def test_clear(self) -> None:
        sha1 = lt.sha1_hash(lib.get_random_bytes(20))

        sha1.clear()

        self.assertEqual(sha1.to_bytes(), b"\0" * 20)

    def test_empty(self) -> None:
        sha1 = lt.sha1_hash()
        self.assertTrue(sha1.is_all_zeros())

    def test_is_all_zeros(self) -> None:
        sha1 = lt.sha1_hash(lib.get_random_bytes(20))
        self.assertFalse(sha1.is_all_zeros())

        sha1 = lt.sha1_hash(b"\0" * 20)
        self.assertTrue(sha1.is_all_zeros())
