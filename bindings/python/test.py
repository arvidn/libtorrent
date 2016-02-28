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

		ses = lt.session({'alert_mask': lt.alert.category_t.all_categories})
		shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'base.torrent'), '.')
		ti = lt.torrent_info('base.torrent');
		h = ses.add_torrent({'ti': ti, 'save_path': os.getcwd()})
		st = h.status()
		time.sleep(1)
		ses.remove_torrent(h)
		ses.wait_for_alert(1000) # milliseconds
		alerts = ses.pop_alerts()
		for a in alerts:
			print(a.message())

		print(st.next_announce)
		self.assertEqual(st.name, 'temp')
		print(st.errc.message())
		print(st.pieces)
		print(st.last_seen_complete)
		print(st.completed_time)
		print(st.progress)
		print(st.num_pieces)
		print(st.distributed_copies)
		print(st.paused)
		print(st.info_hash)
		self.assertEqual(st.save_path, os.getcwd())

class test_bencoder(unittest.TestCase):

	def test_bencode(self):

		encoded = lt.bencode({'a': 1, 'b': [1,2,3], 'c': 'foo'})
		self.assertEqual(encoded, b'd1:ai1e1:bli1ei2ei3ee1:c3:fooe')

	def test_bdecode(self):

		encoded = b'd1:ai1e1:bli1ei2ei3ee1:c3:fooe'
		decoded = lt.bdecode(encoded)
		self.assertEqual(decoded, {b'a': 1, b'b': [1,2,3], b'c': b'foo'})

class test_sha1hash(unittest.TestCase):

	def test_sha1hash(self):
		h = 'a0'*20
		s = lt.sha1_hash(binascii.unhexlify(h))
		self.assertEqual(h, str(s))


class test_session(unittest.TestCase):
	def test_post_session_stats(self):
		s = lt.session({'alert_mask': lt.alert.category_t.stats_notification})
		s.post_session_stats()
		a = s.wait_for_alert(1000)
		self.assertTrue(isinstance(a, lt.session_stats_alert))
		self.assertTrue(isinstance(a.values, dict))
		self.assertTrue(len(a.values) > 0)

if __name__ == '__main__':
	print(lt.__version__)
	unittest.main()

