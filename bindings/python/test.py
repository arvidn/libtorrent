#!/usr/bin/env python

import libtorrent as lt

import unittest
import time
import os
import shutil
import binascii

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

class test_alerts(unittest.TestCase):

	def test_alert(self):

		ses = lt.session()
		sett = lt.session_settings()
		sett.alert_mask = 0xffffffff
		ses.set_alert_mask(0xfffffff)
		shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'base.torrent'), '.')
		ti = lt.torrent_info('base.torrent');
		h = ses.add_torrent({'ti': ti, 'save_path': '.'})
		time.sleep(1)
		ses.remove_torrent(h)
		alerts = ses.pop_alerts()
		for a in alerts:
			print a.message()

class test_bencoder(unittest.TestCase):

	def test_bencode(self):

		encoded = lt.bencode({'a': 1, 'b': [1,2,3], 'c': 'foo'})
		self.assertEqual(encoded, 'd1:ai1e1:bli1ei2ei3ee1:c3:fooe')

	def test_bdecode(self):

		encoded = 'd1:ai1e1:bli1ei2ei3ee1:c3:fooe'
		decoded = lt.bdecode(encoded)
		self.assertEqual(decoded, {'a': 1, 'b': [1,2,3], 'c': 'foo'})

class test_sha1hash(unittest.TestCase):

	def test_sha1hash(self):
		h = 'a0'*20
		s = lt.sha1_hash(binascii.unhexlify(h))
		self.assertEqual(h, str(s))


if __name__ == '__main__':
	print lt.__version__
	unittest.main()

