import libtorrent as lt

import unittest
import binascii


class test_sha1hash(unittest.TestCase):

    def test_sha1hash(self):
        h = 'a0' * 20
        s = lt.sha1_hash(binascii.unhexlify(h))
        self.assertEqual(h, str(s))
