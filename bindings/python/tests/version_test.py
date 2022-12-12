import unittest

import libtorrent as lt


class VersionTest(unittest.TestCase):
    def test_version(self) -> None:
        self.assertIsInstance(lt.__version__, str)

    @unittest.skip("need to implement this")
    def test_version_tuple(self) -> None:
        self.assertIsInstance(lt.__version_info__, tuple)  # type: ignore

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.version, str)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.version_major, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.version_minor, int)

    def test_old_attrs(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.version, str)
            self.assertIsInstance(lt.version_major, int)
            self.assertIsInstance(lt.version_minor, int)
