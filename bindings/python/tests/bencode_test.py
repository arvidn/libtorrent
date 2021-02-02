import libtorrent as lt

import unittest


class test_bencoder(unittest.TestCase):

    def test_bencode(self):

        encoded = lt.bencode({'a': 1, 'b': [1, 2, 3], 'c': 'foo'})
        self.assertEqual(encoded, b'd1:ai1e1:bli1ei2ei3ee1:c3:fooe')

    def test_bdecode(self):

        encoded = b'd1:ai1e1:bli1ei2ei3ee1:c3:fooe'
        decoded = lt.bdecode(encoded)
        self.assertEqual(decoded, {b'a': 1, b'b': [1, 2, 3], b'c': b'foo'})
