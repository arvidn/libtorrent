import libtorrent as lt

import unittest



class test_session_stats(unittest.TestCase):

    def test_add_torrent_params(self):
        atp = lt.add_torrent_params()

        for field_name in dir(atp):
            field = getattr(atp, field_name)
            print(field_name, field)
