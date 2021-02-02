import libtorrent as lt

import unittest


class test_operation_t(unittest.TestCase):

    def test_enum(self):
        self.assertEqual(lt.operation_name(lt.operation_t.sock_accept), "sock_accept")
        self.assertEqual(lt.operation_name(lt.operation_t.unknown), "unknown")
        self.assertEqual(lt.operation_name(lt.operation_t.mkdir), "mkdir")
        self.assertEqual(lt.operation_name(lt.operation_t.partfile_write), "partfile_write")
        self.assertEqual(lt.operation_name(lt.operation_t.hostname_lookup), "hostname_lookup")
