import libtorrent as lt

import unittest


class test_create_torrent(unittest.TestCase):

    def test_from_torrent_info(self):
        ti = lt.torrent_info('unordered.torrent')
        print(ti.ssl_cert())
        ct = lt.create_torrent(ti)
        entry = ct.generate()
        content = lt.bencode(entry).strip()
        with open('unordered.torrent', 'rb') as f:
            file_content = bytearray(f.read().strip())
            print(content)
            print(file_content)
            print(entry)
            self.assertEqual(content, file_content)

    def test_from_scratch(self):
        fs = lt.file_storage()
        fs.add_file('test/file1', 1000)
        fs.add_file('test/file2', 2000)
        ct = lt.create_torrent(fs)
        ct.add_url_seed('foo')
        ct.add_http_seed('bar')
        ct.add_tracker('bar')
        ct.set_root_cert('1234567890')
        ct.add_collection('1337')
        for i in range(ct.num_pieces()):
            ct.set_hash(i, b'abababababababababab')
        entry = ct.generate()
        print(entry)
