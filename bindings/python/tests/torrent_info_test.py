import libtorrent as lt

import unittest


class test_torrent_info(unittest.TestCase):

    def test_non_ascii_file(self):
        ti = lt.torrent_info('base-\u745E\u5177.torrent')

        self.assertTrue(len(ti.info_section()) != 0)
        self.assertTrue(len(ti.hash_for_piece(0)) != 0)

    def test_bencoded_constructor(self):
        info = lt.torrent_info({'info': {
            'name': 'test_torrent', 'length': 1234,
            'piece length': 16 * 1024,
            'pieces': 'aaaaaaaaaaaaaaaaaaaa'}})

        self.assertEqual(info.num_files(), 1)

        f = info.files()
        self.assertEqual(f.file_path(0), 'test_torrent')
        self.assertEqual(f.file_name(0), 'test_torrent')
        self.assertEqual(f.file_size(0), 1234)
        self.assertEqual(info.total_size(), 1234)
        self.assertEqual(info.creation_date(), 0)

    def test_load_decode_depth_limit(self):
        self.assertRaises(RuntimeError, lambda: lt.torrent_info(
            {'test': {'test': {'test': {'test': {'test': {}}}}}, 'info': {
                'name': 'test_torrent', 'length': 1234,
                'piece length': 16 * 1024,
                'pieces': 'aaaaaaaaaaaaaaaaaaaa'}}, {'max_decode_depth': 1}))

    def test_load_max_pieces_limit(self):
        self.assertRaises(RuntimeError, lambda: lt.torrent_info(
            {'info': {
                'name': 'test_torrent', 'length': 1234000,
                'piece length': 16 * 1024,
                'pieces': 'aaaaaaaaaaaaaaaaaaaa'}}, {'max_pieces': 1}))

    def test_load_max_buffer_size_limit(self):
        self.assertRaises(RuntimeError, lambda: lt.torrent_info(
            {'info': {
                'name': 'test_torrent', 'length': 1234000,
                'piece length': 16 * 1024,
                'pieces': 'aaaaaaaaaaaaaaaaaaaa'}}, {'max_buffer_size': 1}))

    def test_info_section(self):
        ti = lt.torrent_info('base.torrent')

        self.assertTrue(len(ti.info_section()) != 0)
        self.assertTrue(len(ti.hash_for_piece(0)) != 0)

    def test_announce_entry(self):
        ae = lt.announce_entry('test')
        self.assertEqual(ae.url, 'test')
        self.assertEqual(ae.tier, 0)
        self.assertEqual(ae.verified, False)
        self.assertEqual(ae.source, 0)
