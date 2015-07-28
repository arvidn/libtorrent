#!/usr/bin/env python

# this name "py_libtorrent" is the name produced by the Jamfile. The setup.py
# will actually name it libtorrent for the distro. It's only called
# py_libtorrent here to be able to coexist with the main libtorrent.so on linux
# (which otherwise would have the same name in the same directory)
import py_libtorrent as lt

import unittest

# test torrent_info

class test_torrent_info(unittest.TestCase):

	def test_bencoded_constructor(self):
		info = lt.torrent_info({ 'info': {'name': 'test_torrent', 'length': 1234,
			'piece length': 16 * 1024,
			'pieces': 'aaaaaaaaaaaaaaaaaaaa'}})

		self.assertEqual(info.num_files(), 1)

		f = info.files()
		self.assertEqual(f.file_path(0), 'test_torrent')
		self.assertEqual(f.file_size(0), 1234)
		self.assertEqual(info.total_size(), 1234)

class test_bencoder(unittest.TestCase):

	def test_bencode(self):

		encoded = lt.bencode({'a': 1, 'b': [1,2,3], 'c': 'foo'})
		self.assertEqual(encoded, 'd1:ai1e1:bli1ei2ei3ee1:c3:fooe')

	def test_bdecode(self):

		encoded = 'd1:ai1e1:bli1ei2ei3ee1:c3:fooe'
		decoded = lt.bdecode(encoded)
		self.assertEqual(decoded, {'a': 1, 'b': [1,2,3], 'c': 'foo'})

if __name__ == '__main__':
    unittest.main()

