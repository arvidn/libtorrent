import libtorrent as lt

import unittest
import binascii


class test_magnet_link(unittest.TestCase):

    def test_parse_magnet_uri(self):
        ses = lt.session({})
        magnet = 'magnet:?xt=urn:btih:C6EIF4CCYDBTIJVG3APAGM7M4NDONCTI'
        p = lt.parse_magnet_uri(magnet)
        self.assertEqual(str(p.info_hashes.v1), '178882f042c0c33426a6d81e0333ece346e68a68')
        p.save_path = '.'
        h = ses.add_torrent(p)
        self.assertEqual(str(h.info_hash()), '178882f042c0c33426a6d81e0333ece346e68a68')
        self.assertEqual(str(h.info_hashes().v1), '178882f042c0c33426a6d81e0333ece346e68a68')

    def test_parse_magnet_uri_dict(self):
        ses = lt.session({})
        magnet = 'magnet:?xt=urn:btih:C6EIF4CCYDBTIJVG3APAGM7M4NDONCTI'
        p = lt.parse_magnet_uri_dict(magnet)
        self.assertEqual(binascii.hexlify(p['info_hashes']), b'178882f042c0c33426a6d81e0333ece346e68a68')
        p['save_path'] = '.'
        h = ses.add_torrent(p)
        self.assertEqual(str(h.info_hash()), '178882f042c0c33426a6d81e0333ece346e68a68')
        self.assertEqual(str(h.info_hashes().v1), '178882f042c0c33426a6d81e0333ece346e68a68')
