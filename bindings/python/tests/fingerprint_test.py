import unittest

import libtorrent as lt


class GenerateFingerprintTest(unittest.TestCase):
    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5985")
    def test_generate(self) -> None:
        # full version
        self.assertEqual(
            lt.generate_fingerprint_bytes(b"ABCD", 1, 2, 3, 4), b"-AB1234-"
        )

        # short name
        self.assertEqual(
            lt.generate_fingerprint_bytes(b"A", 1, 2, 3, 4), b"-A\x001234-"
        )

        # major.minor
        self.assertEqual(lt.generate_fingerprint_bytes(b"ABCD", 1, 2), b"-AB1200-")

        # high versions
        self.assertEqual(
            lt.generate_fingerprint_bytes(b"ABCD", 1000, 2000, 3000, 4000), b"unknown"
        )

        # version < 0
        self.assertEqual(
            lt.generate_fingerprint_bytes(b"ABCD", -1, -1, -1, -1), b"-AB0000-"
        )

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_deprecations(self) -> None:
        with self.assertWarns(DeprecationWarning):
            lt.generate_fingerprint("ABCD", 1, 2, 3, 4)

    def test_generate_str(self) -> None:
        # full version
        self.assertEqual(lt.generate_fingerprint("ABCD", 1, 2, 3, 4), "-AB1234-")

        # short name
        self.assertEqual(lt.generate_fingerprint("A", 1, 2, 3, 4), "---1234-")

        # version < 0
        self.assertEqual(lt.generate_fingerprint("ABCD", 1, 2, -1, -1), "-AB1200-")


class FingerprintTest(unittest.TestCase):
    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecations(self) -> None:
        with self.assertWarns(DeprecationWarning):
            lt.fingerprint("AB", 1, 2, 3, 4)

    def test_fingerprint(self) -> None:
        fprint = lt.fingerprint("AB", 1, 2, 3, 4)
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(str(fprint), "-AB1234-")
        # self.assertEqual(fprint.major_version, 1)
        # self.assertEqual(fprint.minor_version, 2)
        # self.assertEqual(fprint.revision_version, 3)
        # self.assertEqual(fprint.tag_version, 4)

        # short names behave differently
        fprint = lt.fingerprint("A", 1, 2, 3, 4)
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(str(fprint), "-A\x001234-")

    @unittest.skip("fingerprint.<attr> broke")
    def test_fingerprint_broken(self) -> None:
        fprint = lt.fingerprint("AB", 1, 2, 3, 4)
        self.assertEqual(fprint.major_version, 1)
        self.assertEqual(fprint.minor_version, 2)
        self.assertEqual(fprint.revision_version, 3)
        self.assertEqual(fprint.tag_version, 4)
