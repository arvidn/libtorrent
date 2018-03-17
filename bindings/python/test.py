#!/usr/bin/env python

import libtorrent as lt

import unittest
import time
import os
import shutil
import binascii
import inspect
import pickle

class test_create_torrent(unittest.TestCase):

	def test_from_torrent_info(self):
		ti = lt.torrent_info('unordered.torrent')
		ct = lt.create_torrent(ti, True)
		entry = ct.generate()
		content = lt.bencode(entry).strip()
		with open('unordered.torrent', 'rb') as f:
			file_content = bytearray(f.read().strip())
			print(content)
			print(file_content)
			print(entry)
			self.assertEqual(content, file_content)

class test_session_stats(unittest.TestCase):

	def test_unique(self):
		l = lt.session_stats_metrics()
		self.assertTrue(len(l) > 40);
		idx = set()
		for m in l:
			self.assertTrue(m.value_index not in idx)
			idx.add(m.value_index)

	def test_find_idx(self):
		self.assertEqual(lt.find_metric_idx("peer.error_peers"), 0)

class test_torrent_handle(unittest.TestCase):

	def setup(self):
		self.ses = lt.session({'alert_mask': lt.alert.category_t.all_categories, 'enable_dht': False})
		self.ti = lt.torrent_info('url_seed_multi.torrent');
		self.h = self.ses.add_torrent({'ti': self.ti, 'save_path': os.getcwd()})

	def test_torrent_handle(self):
		self.setup()
		self.assertEqual(self.h.file_priorities(), [4,4])
		self.assertEqual(self.h.piece_priorities(), [4])

		self.h.prioritize_files([0,1])
		self.assertEqual(self.h.file_priorities(), [0,1])

		self.h.prioritize_pieces([0])
		self.assertEqual(self.h.piece_priorities(), [0])

		# also test the overload that takes a list of piece->priority mappings
		self.h.prioritize_pieces([(0, 1)])
		self.assertEqual(self.h.piece_priorities(), [1])
		self.h.connect_peer(('127.0.0.1', 6881))
		self.h.connect_peer(('127.0.0.2', 6881), source=4)
		self.h.connect_peer(('127.0.0.3', 6881), flags=2)
		self.h.connect_peer(('127.0.0.4', 6881), flags=2, source=4)

	def test_torrent_handle_in_set(self):
		self.setup()
		torrents = set()
		torrents.add(self.h)

		# get another instance of a torrent_handle that represents the same
		# torrent. Make sure that when we add it to a set, it just replaces the
		# existing object
		t = self.ses.get_torrents()
		self.assertEqual(len(t), 1)
		for h in t:
			torrents.add(h)

		self.assertEqual(len(torrents), 1)

	def test_torrent_handle_in_dict(self):
		self.setup()
		torrents = {}
		torrents[self.h] = 'foo'

		# get another instance of a torrent_handle that represents the same
		# torrent. Make sure that when we add it to a dict, it just replaces the
		# existing object
		t = self.ses.get_torrents()
		self.assertEqual(len(t), 1)
		for h in t:
			torrents[h] = 'bar'

		self.assertEqual(len(torrents), 1)
		self.assertEqual(torrents[self.h], 'bar')

	def test_replace_trackers(self):
		self.setup()
		trackers = []
		for idx, tracker_url in enumerate(('udp://tracker1.com', 'udp://tracker2.com')):
			tracker = lt.announce_entry(tracker_url)
			tracker.tier = idx
			tracker.fail_limit = 2
			trackers.append(tracker)
		self.h.replace_trackers(trackers)
		new_trackers = self.h.trackers()
		self.assertEqual(new_trackers[0]['url'], 'udp://tracker1.com')
		self.assertEqual(new_trackers[1]['tier'], 1)
		self.assertEqual(new_trackers[1]['fail_limit'], 2)

	def test_pickle_trackers(self):
		"""Test lt objects convertors are working and trackers can be pickled"""
		self.setup()
		tracker = lt.announce_entry('udp://tracker1.com')
		tracker.tier = 0
		tracker.fail_limit = 1
		trackers = [tracker]
		self.h.replace_trackers(trackers)
		tracker_list = [tracker for tracker in self.h.trackers()]
		pickled_trackers = pickle.dumps(tracker_list)
		unpickled_trackers = pickle.loads(pickled_trackers)
		self.assertEqual(unpickled_trackers[0]['url'], 'udp://tracker1.com')
		self.assertEqual(unpickled_trackers[0]['last_error']['value'], 0)

	def test_file_status(self):
		self.setup()
		l = self.h.file_status()
		print(l)

	def test_piece_deadlines(self):
		self.setup()
		self.h.clear_piece_deadlines()

	def test_torrent_status(self):
		self.setup()
		st = self.h.status()
		ti = st.handle;
		self.assertEqual(ti.info_hash(), self.ti.info_hash())
		# make sure we can compare torrent_status objects
		st2 = self.h.status()
		self.assertEqual(st2, st)

	def test_serialize_trackers(self):
		"""Test to ensure the dict contains only python built-in types"""
		self.setup()
		self.h.add_tracker({'url':'udp://tracker1.com'})
		tr = self.h.trackers()[0]
		# wait a bit until a valid timestamp appears
		while tr['next_announce'] == None:
			time.sleep(0.1)
			tr = self.h.trackers()[0]
		import json
		print(json.dumps(self.h.trackers()[0]))

	def test_scrape(self):
		self.setup()
		# this is just to make sure this function can be called like this
		# from python
		self.h.scrape_tracker()

	def test_cache_info(self):
		self.setup()
		cs = self.ses.get_cache_info(self.h)
		self.assertEqual(cs.pieces, [])

class test_torrent_info(unittest.TestCase):

	def test_bencoded_constructor(self):
		info = lt.torrent_info({ 'info': {'name': 'test_torrent', 'length': 1234,
			'piece length': 16 * 1024,
			'pieces': 'aaaaaaaaaaaaaaaaaaaa'}})

		self.assertEqual(info.num_files(), 1)

		f = info.files()
		self.assertEqual(f.file_path(0), 'test_torrent')
		self.assertEqual(f.file_name(0), 'test_torrent')
		self.assertEqual(f.file_size(0), 1234)
		self.assertEqual(info.total_size(), 1234)

	def test_metadata(self):
		ti = lt.torrent_info('base.torrent');

		self.assertTrue(len(ti.metadata()) != 0)
		self.assertTrue(len(ti.hash_for_piece(0)) != 0)

	def test_web_seeds(self):
		ti = lt.torrent_info('base.torrent');

		ws = [{'url': 'http://foo/test', 'auth': '', 'type': 0},
			{'url': 'http://bar/test', 'auth': '', 'type': 1} ]
		ti.set_web_seeds(ws)
		web_seeds = ti.web_seeds()
		self.assertEqual(len(ws), len(web_seeds))
		for i in range(len(web_seeds)):
			self.assertEqual(web_seeds[i]["url"], ws[i]["url"])
			self.assertEqual(web_seeds[i]["auth"], ws[i]["auth"])
			self.assertEqual(web_seeds[i]["type"], ws[i]["type"])

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
			print(f.path)

			self.assertEqual(os.path.split(f.path)[1], expected[idx])
			self.assertEqual(os.path.split(os.path.split(f.path)[0]), ('temp', 'foo'))
			idx += 1

	def test_announce_entry(self):
		ae = lt.announce_entry('test')
		self.assertEquals(ae.can_announce(False), True)
		self.assertEquals(ae.scrape_incomplete, -1)
		self.assertEquals(ae.next_announce, None)
		self.assertEquals(ae.last_error.value(), 0)

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
			if a.what() == 'add_torrent_alert':
				self.assertEquals(a.torrent_name, 'temp')
			print(a.message())
			for field_name in dir(a):
				if field_name.startswith('__'): continue
				field = getattr(a, field_name)
				if callable(field):
					print('  ', field_name, ' = ', field())
				else:
					print('  ', field_name, ' = ', field)

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

class test_magnet_link(unittest.TestCase):

	def test_parse_magnet_uri(self):
		ses = lt.session({})
		magnet = 'magnet:?xt=urn:btih:C6EIF4CCYDBTIJVG3APAGM7M4NDONCTI'
		p = lt.parse_magnet_uri(magnet)
		p['save_path'] = '.'
		h = ses.add_torrent(p)
		self.assertEqual(str(h.info_hash()), '178882f042c0c33426a6d81e0333ece346e68a68')

class test_peer_class(unittest.TestCase):

	def test_peer_class_ids(self):
		s = lt.session({'enable_dht': False})

		print('global_peer_class_id:', lt.session.global_peer_class_id)
		print('tcp_peer_class_id:', lt.session.tcp_peer_class_id)
		print('local_peer_class_id:', lt.session.local_peer_class_id)

		print('global: ', s.get_peer_class(s.global_peer_class_id))
		print('tcp: ', s.get_peer_class(s.local_peer_class_id))
		print('local: ', s.get_peer_class(s.local_peer_class_id))

	def test_peer_class(self):
		s = lt.session({'enable_dht': False})

		c = s.create_peer_class('test class')
		print('new class: ', s.get_peer_class(c))

		nfo = s.get_peer_class(c)
		self.assertEqual(nfo['download_limit'], 0)
		self.assertEqual(nfo['upload_limit'], 0)
		self.assertEqual(nfo['ignore_unchoke_slots'], False)
		self.assertEqual(nfo['connection_limit_factor'], 100)
		self.assertEqual(nfo['download_priority'], 1)
		self.assertEqual(nfo['upload_priority'], 1)
		self.assertEqual(nfo['label'], 'test class')

		nfo['download_limit'] = 1337
		nfo['upload_limit'] = 1338
		nfo['ignore_unchoke_slots'] = True
		nfo['connection_limit_factor'] = 42
		nfo['download_priority'] = 2
		nfo['upload_priority'] = 3

		s.set_peer_class(c, nfo)

		nfo2 = s.get_peer_class(c)
		self.assertEqual(nfo, nfo2)

	def test_peer_class_filter(self):
		filt = lt.peer_class_type_filter()
		filt.add(lt.socket_type_t.tcp_socket, lt.session.global_peer_class_id);
		filt.remove(lt.socket_type_t.utp_socket, lt.session.local_peer_class_id);

		filt.disallow(lt.socket_type_t.tcp_socket, lt.session.global_peer_class_id);
		filt.allow(lt.socket_type_t.utp_socket, lt.session.local_peer_class_id);

	def test_peer_class_ip_filter(self):
		s = lt.session({'enable_dht': False})
		s.set_peer_class_type_filter(lt.peer_class_type_filter())
		s.set_peer_class_filter(lt.ip_filter())

class test_session(unittest.TestCase):

	def test_post_session_stats(self):
		s = lt.session({'alert_mask': lt.alert.category_t.stats_notification, 'enable_dht': False})
		s.post_session_stats()
		alerts = []
		# first the stats headers log line. but not if logging is disabled
		if 'log_alert' in [i[0] for i in inspect.getmembers(lt)]:
			s.wait_for_alert(1000)
			alerts = s.pop_alerts()
			a = alerts.pop(0)
			self.assertTrue(isinstance(a, lt.log_alert))
		# then the actual stats values
		if len(alerts) == 0:
			s.wait_for_alert(1000)
			alerts = s.pop_alerts()
		a = alerts.pop(0)
		self.assertTrue(isinstance(a, lt.session_stats_alert))
		self.assertTrue(isinstance(a.values, dict))
		self.assertTrue(len(a.values) > 0)

	def test_post_dht_stats(self):
		s = lt.session({'alert_mask': lt.alert.category_t.stats_notification, 'enable_dht': False})
		s.post_dht_stats()
		alerts = []
		# first the stats headers log line. but not if logging is disabled
		time.sleep(1)
		alerts = s.pop_alerts()
		a = alerts.pop(0)
		while not isinstance(a, lt.dht_stats_alert):
			a = alerts.pop(0
)
		self.assertTrue(isinstance(a, lt.dht_stats_alert))
		self.assertTrue(isinstance(a.active_requests, list))
		self.assertTrue(isinstance(a.routing_table, list))

	def test_unknown_settings(self):
		try:
			s = lt.session({'unexpected-key-name': 42})
			self.assertFalse('should have thrown an exception')
		except KeyError as e:
			print(e)

	def test_fingerprint(self):
		self.assertEqual(lt.generate_fingerprint('LT', 0, 1, 2, 3), '-LT0123-')
		self.assertEqual(lt.generate_fingerprint('..', 10, 1, 2, 3), '-..A123-')

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

	def test_min_memory_preset(self):
		min_mem = lt.min_memory_usage()
		print(min_mem)

		self.assertTrue('connection_speed' in min_mem)
		self.assertTrue('file_pool_size' in min_mem)

	def test_seed_mode_preset(self):
		seed_mode = lt.high_performance_seed()
		print(seed_mode)

		self.assertTrue('alert_queue_size' in seed_mode)
		self.assertTrue('connection_speed' in seed_mode)
		self.assertTrue('file_pool_size' in seed_mode)

	def test_default_settings(self):

		default = lt.default_settings()
		print(default)

if __name__ == '__main__':
	print(lt.__version__)
	shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'url_seed_multi.torrent'), '.')
	shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'base.torrent'), '.')
	shutil.copy(os.path.join('..', '..', 'test', 'test_torrents', 'unordered.torrent'), '.')
	unittest.main()

