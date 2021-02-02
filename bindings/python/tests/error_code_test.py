import libtorrent as lt

import unittest


class test_error_code(unittest.TestCase):

    def test_error_code(self):

        a = lt.error_code()
        a = lt.error_code(10, lt.libtorrent_category())
        self.assertEqual(a.category().name(), 'libtorrent')

        self.assertEqual(lt.libtorrent_category().name(), 'libtorrent')
        self.assertEqual(lt.upnp_category().name(), 'upnp')
        self.assertEqual(lt.http_category().name(), 'http')
        self.assertEqual(lt.socks_category().name(), 'socks')
        self.assertEqual(lt.bdecode_category().name(), 'bdecode')
        self.assertEqual(lt.generic_category().name(), 'generic')
        self.assertEqual(lt.system_category().name(), 'system')
