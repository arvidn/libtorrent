import unittest

import libtorrent as lt


class GenerateFingerprintTest(unittest.TestCase):
    def test_generate(self) -> None:

        # full version
        self.assertEqual(
            lt.generate_fingerprint_bytes(b"AB", 1, 2, 3, 4),
            b"-AB1234-",
        )

        # long name
        with self.assertRaises(ValueError):
            lt.generate_fingerprint_bytes(b"ABCD", 1, 2, 3, 4),

        # short name
        with self.assertRaises(ValueError):
            lt.generate_fingerprint_bytes(b"A", 1, 2, 3, 4)

        # major only
        self.assertEqual(
            lt.generate_fingerprint_bytes(b"AB", 1),
            b"-AB1000-",
        )

        # high versions
        self.assertEqual(
            lt.generate_fingerprint_bytes(b"AB", 1000, 2000, 3000, 4000),
            b"-AB\x1f\x07\xef\xd7-",
        )

        # version < 0
        with self.assertRaises(ValueError):
            lt.generate_fingerprint_bytes(b"AB", -1, -1, -1, -1)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_deprecations(self) -> None:
        with self.assertWarns(DeprecationWarning):
            lt.generate_fingerprint("ABCD", 1, 2, 3, 4)

    def test_generate_str(self) -> None:
        # full version
        self.assertEqual(lt.generate_fingerprint("AB", 1, 2, 3, 4), "-AB1234-")

        # short name
        with self.assertRaises(ValueError):
            self.assertEqual(lt.generate_fingerprint("A", 1, 2, 3, 4), "---1234-")

        # version < 0
        with self.assertRaises(ValueError):
            self.assertEqual(lt.generate_fingerprint("AB", 1, 2, -1, -1), "-AB1200-")


class FingerprintTest(unittest.TestCase):
    def test_fingerprint(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                fprint = lt.fingerprint("AB", 1, 2, 3, 4)
            self.assertEqual(str(fprint), "-AB1234-")
            self.assertEqual(fprint.major_version, 1)
            self.assertEqual(fprint.minor_version, 2)
            self.assertEqual(fprint.revision_version, 3)
            self.assertEqual(fprint.tag_version, 4)

            # short names behave differently
            with self.assertWarns(DeprecationWarning):
                with self.assertRaises(ValueError):
                    fprint = lt.fingerprint("A", 1, 2, 3, 4)
