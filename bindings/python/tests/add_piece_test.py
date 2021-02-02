import libtorrent as lt

import unittest
import time
import os
import tempfile

import dummy_data

settings = {
    'alert_mask': lt.alert.category_t.all_categories,
    'enable_dht': False, 'enable_lsd': False, 'enable_natpmp': False,
    'enable_upnp': False, 'listen_interfaces': '0.0.0.0:0', 'file_pool_size': 1}

class TestAddPiece(unittest.TestCase):

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.session = lt.session(settings)
        self.ti = lt.torrent_info(dummy_data.DICT)
        self.atp = lt.add_torrent_params()
        self.atp.ti = self.ti
        self.atp.save_path = self.dir.name
        self.handle = self.session.add_torrent(self.atp)
        self.wait_for(lambda: self.handle.status().state != lt.torrent_status.checking_files
                      and self.handle.status().state != lt.torrent_status.checking_resume_data, msg="checking")

    def wait_for(self, condition, msg="condition", timeout=5):
        deadline = time.time() + timeout
        while not condition():
            self.assertLess(time.time(), deadline, msg="%s timed out" % msg)
            time.sleep(0.1)

    def wait_until_torrent_finished(self):
        self.wait_for(lambda: self.handle.status().progress == 1.0, msg="progress")

        def file_written():
            with open(os.path.join(self.dir.name.encode(), dummy_data.NAME), mode="rb") as f:
                return f.read() == dummy_data.DATA

        self.wait_for(file_written, msg="file write")

    def test_with_str(self):
        for i, data in enumerate(dummy_data.PIECES):
            self.handle.add_piece(i, data.decode(), 0)

        self.wait_until_torrent_finished()

    def test_with_bytes(self):
        for i, data in enumerate(dummy_data.PIECES):
            self.handle.add_piece(i, data, 0)

        self.wait_until_torrent_finished()
