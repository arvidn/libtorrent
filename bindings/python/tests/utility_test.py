from typing import Any
import unittest

import libtorrent as lt


class BencodeTest(unittest.TestCase):
    def assert_bencoding(self, decoded: Any, encoded: bytes) -> None:
        self.assertEqual(lt.bencode(decoded), encoded)
        self.assertEqual(lt.bdecode(encoded), decoded)

    def test_expected(self) -> None:
        # top-level dict
        self.assert_bencoding(
            {b"a": 1, b"b": [1, 2, 3], b"c": b"foo"}, b"d1:ai1e1:bli1ei2ei3ee1:c3:fooe"
        )

        # top-level int
        self.assert_bencoding(123, b"i123e")

        # top-level bytes
        self.assert_bencoding(b"abc", b"3:abc")

        # top-level list
        self.assert_bencoding([123, b"abc"], b"li123e3:abce")

        # top-level preformatted
        self.assertEqual(lt.bencode((1, 2, 3)), b"\x01\x02\x03")

    def test_deprecations(self) -> None:
        # top-level str
        with self.assertWarns(DeprecationWarning):
            lt.bencode("abc")

    def test_nonstandard_types(self) -> None:
        # top-level str
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(lt.bencode("abc"), b"3:abc")

        # top-level float
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(lt.bencode(1.0), b"0:")  # type: ignore

        # top-level other object
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(lt.bencode(self), b"0:")  # type: ignore


class IdentifyClientTest(unittest.TestCase):
    def test_identify_client(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(lt.identify_client(lt.sha1_hash()), "Unknown")


class ClientFingerprintTest(unittest.TestCase):
    def test_identify_client(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                fprint = lt.client_fingerprint(lt.sha1_hash(b"-AB1200-............"))
            self.assertEqual(str(fprint), "-AB1200-")
