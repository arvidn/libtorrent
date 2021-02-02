import libtorrent as lt

import unittest


class test_peer_info(unittest.TestCase):

    def test_peer_info_members(self):

        p = lt.peer_info()

        print(p.client)
        print(p.pieces)
        print(p.pieces)
        print(p.last_request)
        print(p.last_active)
        print(p.flags)
        print(p.source)
        print(p.pid)
        print(p.downloading_piece_index)
        print(p.ip)
        print(p.local_endpoint)
        print(p.read_state)
        print(p.write_state)
