import errno
import os
import pickle
import unittest

import libtorrent as lt

ALL_CATEGORIES = (
    lt.generic_category(),
    lt.system_category(),
    lt.libtorrent_category(),
    lt.upnp_category(),
    lt.http_category(),
    lt.socks_category(),
    lt.bdecode_category(),
    lt.i2p_category(),
)


class ErrorCategoryTest(unittest.TestCase):
    def test_equal(self) -> None:
        self.assertEqual(lt.generic_category(), lt.generic_category())
        self.assertNotEqual(lt.generic_category(), lt.system_category())

    def test_accessors(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(lt.get_libtorrent_category(), lt.libtorrent_category())
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(lt.get_upnp_category(), lt.upnp_category())
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(lt.get_http_category(), lt.http_category())
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(lt.get_socks_category(), lt.socks_category())
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(lt.get_bdecode_category(), lt.bdecode_category())
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(lt.get_i2p_category(), lt.i2p_category())

    def test_name(self) -> None:
        self.assertEqual(lt.generic_category().name(), "generic")
        self.assertEqual(lt.system_category().name(), "system")
        self.assertEqual(lt.libtorrent_category().name(), "libtorrent")
        self.assertEqual(lt.upnp_category().name(), "upnp")
        self.assertEqual(lt.http_category().name(), "http")
        self.assertEqual(lt.socks_category().name(), "socks")
        self.assertEqual(lt.bdecode_category().name(), "bdecode")
        self.assertEqual(lt.i2p_category().name(), "i2p error")

    def test_message(self) -> None:
        for category in ALL_CATEGORIES:
            self.assertIsInstance(category.message(1), str)


class ErrorCodeTest(unittest.TestCase):
    def test_empty(self) -> None:
        ec = lt.error_code()
        self.assertEqual(ec.value(), 0)

    def test_init(self) -> None:
        ec = lt.error_code(1, lt.generic_category())
        self.assertEqual(ec.value(), 1)
        self.assertEqual(ec.category(), lt.generic_category())

    def test_message(self) -> None:
        ec = lt.error_code(errno.ENOENT, lt.generic_category())
        self.assertEqual(ec.message(), os.strerror(errno.ENOENT))

    def test_value(self) -> None:
        ec = lt.error_code(errno.ENOENT, lt.generic_category())
        self.assertEqual(ec.value(), errno.ENOENT)

    def test_clear(self) -> None:
        ec = lt.error_code(errno.ENOENT, lt.generic_category())
        ec.clear()
        self.assertEqual(ec.value(), 0)
        self.assertEqual(ec.category(), lt.system_category())

    def test_assign(self) -> None:
        ec = lt.error_code(errno.ENOENT, lt.generic_category())
        ec.assign(123, lt.libtorrent_category())
        self.assertEqual(ec.value(), 123)
        self.assertEqual(ec.category(), lt.libtorrent_category())

    def test_pickle(self) -> None:
        ec = lt.error_code(errno.ENOENT, lt.generic_category())
        ec = pickle.loads(pickle.dumps(ec))
        self.assertEqual(ec.value(), errno.ENOENT)
        self.assertEqual(ec.category(), lt.generic_category())
