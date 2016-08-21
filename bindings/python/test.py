#!/usr/bin/env python

import libtorrent as lt

import unittest
import time
import os
import shutil
import binascii


class test_create_torrent(unittest.TestCase):

	def test_from_torrent_info(self):
		ti = lt.torrent_info('unordered.torrent')
		ct = lt.create_torrent(ti)
		entry = ct.generate()
		content = lt.bencode(entry).strip()
		with open('unordered.torrent', 'rb') as f:
			file_content = bytearray(f.read().strip())
			print(content)
			print(file_content)
			print(entry)
			self.assertEqual(content, file_content)

class test_torrent_handle(unittest.TestCase):

	def test_torrent_handle(self):
		ses = lt.session({'alert_mask': lt.alert.category_t.all_categories, 'enable_dht': False})
		ti = lt.torrent_info('url_seed_multi.torrent');
		h = ses.add_torrent({'ti': ti, 'save_path': os.getcwd()})

		h.prioritize_files([0,1])
		self.assertEqual(h.file_priorities(), [0,1])

		h.prioritize_pieces([0])
		self.assertEqual(h.piece_priorities(), [0])

      # also test the overload that takes a list of piece->priority mappings
		h.prioritize_pieces([(0, 1)])
		self.assertEqual(h.piece_priorities(), [1])

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

	def test_metadata(self):
		ti = lt.torrent_info('base.torrent');

		self.assertTrue(len(ti.metadata()) != 0)
		self.assertTrue(len(ti.hash_for_piece(0)) != 0)

	def test_iterable_files(self):

		# this detects whether libtorrent was built with deprecated APIs
		# the file_strage object is only iterable for backwards compatibility
		if not hasattr(lt, 'version'): return

		ses = lt.session({'alert_mask': lt.alert.category_t.all_categories, 'enable_dht': False})
		ti = lt.torrent_info('url_seed_multi.torrent');
		files = ti.files()

		idx = 0
		expected = ['bar.txt', 'var.txt']
		for f in files:
			print f.path

			self.assertEqual(os.path.split(f.path)[1], expected[idx])
			self.assertEqual(os.path.split(os.path.split(f.path)[0]), ('temp', 'foo'))
			idx += 1

class test_alerts(unittest.TestCase):

	def test_alert(self):

		ses = lt.session({'alert_mask': lt.alert.category_t.all_categories, 'enable_dht': False})
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

	def test_pop_alerts(self):
		ses = lt.session({'alert_mask': lt.alert.category_t.all_categories, 'enable_dht': False})

		ses.async_add_torrent({"ti": lt.torrent_info("base.torrent"), "save_path": "."})
# this will cause an error (because of duplicate torrents) and the
# torrent_info object created here will be deleted once the alert goes out
# of scope. When that happens, it will decrement the python object, to allow
# it to release the object.
# we're trying to catch the error described in this post, with regards to
# torrent_info.
# https://mail.python.org/pipermail/cplusplus-sig/2007-June/012130.html
		ses.async_add_torrent({"ti": lt.torrent_info("base.torrent"), "save_path": "."})
		time.sleep(1)
		for i in range(0, 10):
			alerts = ses.pop_alerts()
			for a in alerts:
				print(a.message())
			time.sleep(0.1)

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
		s = lt.session({'alert_mask': lt.alert.category_t.stats_notification, 'enable_dht': False})
		s.post_session_stats()
		a = s.wait_for_alert(1000)
		self.assertTrue(isinstance(a, lt.session_stats_alert))
		self.assertTrue(isinstance(a.values, dict))
		self.assertTrue(len(a.values) > 0)

	def test_deprecated_settings(self):

		# this detects whether libtorrent was built with deprecated APIs
		if hasattr(lt, 'version'):
			s = lt.session({'enable_dht': False})
			sett = lt.session_settings()
			sett.num_want = 10;
			s.set_settings(sett)
			s.set_settings({'num_want': 33})
			self.assertEqual(s.get_settings()['num_want'], 33)

	def test_apply_settings(self):

		s = lt.session({'enable_dht': False})
		s.apply_settings({'num_want': 66, 'user_agent': 'test123'})
		self.assertEqual(s.get_settings()['num_want'], 66)
		self.assertEqual(s.get_settings()['user_agent'], 'test123')


if __name__ == '__main__':
	print(lt.__version__)
	shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'url_seed_multi.torrent'), '.')
	shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'base.torrent'), '.')
	shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'unordered.torrent'), '.')
	unittest.main()

