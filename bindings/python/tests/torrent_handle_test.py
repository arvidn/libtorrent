import libtorrent as lt

import unittest
import time
import datetime
import os
import pickle


settings = {
    'alert_mask': lt.alert.category_t.all_categories,
    'enable_dht': False, 'enable_lsd': False, 'enable_natpmp': False,
    'enable_upnp': False, 'listen_interfaces': '0.0.0.0:0', 'file_pool_size': 1}

class test_torrent_handle(unittest.TestCase):

    def setup(self):
        self.ses = lt.session(settings)
        self.ti = lt.torrent_info('url_seed_multi.torrent')
        self.h = self.ses.add_torrent({
            'ti': self.ti, 'save_path': os.getcwd(),
            'flags': lt.torrent_flags.default_flags})

    def test_add_torrent_error(self):
        self.ses = lt.session(settings)
        self.ti = lt.torrent_info('url_seed_multi.torrent')
        with self.assertRaises(RuntimeError):
            self.ses.add_torrent({'ti': self.ti, 'save_path': os.getcwd(), 'info_hashes': b'abababababababababab'})

    def test_move_storage(self):
        self.setup()
        self.h.move_storage(u'test-dir')
        self.h.move_storage(b'test-dir2')
        self.h.move_storage('test-dir3')
        self.h.move_storage(u'test-dir', flags=lt.move_flags_t.dont_replace)
        self.h.move_storage(u'test-dir', flags=2)
        self.h.move_storage(b'test-dir2', flags=2)
        self.h.move_storage('test-dir3', flags=2)

    def test_torrent_handle(self):
        self.setup()
        self.assertEqual(self.h.get_file_priorities(), [4, 4])
        self.assertEqual(self.h.get_piece_priorities(), [4])

        self.h.prioritize_files([0, 1])
        # workaround for asynchronous priority update
        time.sleep(1)
        self.assertEqual(self.h.get_file_priorities(), [0, 1])

        self.h.prioritize_pieces([0])
        self.assertEqual(self.h.get_piece_priorities(), [0])

        # also test the overload that takes a list of piece->priority mappings
        self.h.prioritize_pieces([(0, 1)])
        self.assertEqual(self.h.get_piece_priorities(), [1])
        self.h.connect_peer(('127.0.0.1', 6881))
        self.h.connect_peer(('127.0.0.2', 6881), source=4)
        self.h.connect_peer(('127.0.0.3', 6881), flags=2)
        self.h.connect_peer(('127.0.0.4', 6881), flags=2, source=4)

        torrent_files = self.h.torrent_file()
        print(torrent_files.map_file(0, 0, 0).piece)

        print(self.h.queue_position())

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
        # wait a bit until the endpoints list gets populated
        while len(self.h.trackers()[0]['endpoints']) == 0:
            time.sleep(0.1)

        trackers = self.h.trackers()
        self.assertEqual(trackers[0]['url'], 'udp://tracker1.com')
        # this is not necessarily 0, it could also be (EHOSTUNREACH) if the
        # local machine doesn't support the address family
        expect_value = trackers[0]['endpoints'][0]['info_hashes'][0]['last_error']['value']
        pickled_trackers = pickle.dumps(trackers)
        unpickled_trackers = pickle.loads(pickled_trackers)
        self.assertEqual(unpickled_trackers[0]['url'], 'udp://tracker1.com')
        self.assertEqual(unpickled_trackers[0]['endpoints'][0]['info_hashes'][0]['last_error']['value'], expect_value)

    def test_file_status(self):
        self.setup()
        status = self.h.file_status()
        print(status)

    def test_piece_deadlines(self):
        self.setup()
        self.h.clear_piece_deadlines()

    def test_status_last_uploaded_dowloaded(self):
        # we want to check at seconds precision but can't control session
        # time, wait for next full second to prevent second increment
        time.sleep(1 - datetime.datetime.now().microsecond / 1000000.0)

        self.setup()
        st = self.h.status()
        for attr in dir(st):
            print('%s: %s' % (attr, getattr(st, attr)))
        # last upload and download times are at session start time
        self.assertEqual(st.last_upload, None)
        self.assertEqual(st.last_download, None)

    def test_serialize_trackers(self):
        """Test to ensure the dict contains only python built-in types"""
        self.setup()
        self.h.add_tracker({'url': 'udp://tracker1.com'})
        tr = self.h.trackers()[0]
        # wait a bit until the endpoints list gets populated
        while len(tr['endpoints']) == 0:
            time.sleep(0.1)
            tr = self.h.trackers()[0]
        import json
        print(json.dumps(self.h.trackers()[0]))

    def test_torrent_status(self):
        self.setup()
        st = self.h.status()
        ti = st.handle
        self.assertEqual(ti.info_hashes(), self.ti.info_hashes())
        # make sure we can compare torrent_status objects
        st2 = self.h.status()
        self.assertEqual(st2, st)
        print(st2)

    def test_read_resume_data(self):

        resume_data = lt.bencode({
            'file-format': 'libtorrent resume file',
            'info-hash': 'abababababababababab',
            'name': 'test',
            'save_path': '.',
            'peers': '\x01\x01\x01\x01\x00\x01\x02\x02\x02\x02\x00\x02',
            'file_priority': [0, 1, 1]})
        tp = lt.read_resume_data(resume_data)

        self.assertEqual(tp.name, 'test')
        self.assertEqual(tp.info_hashes.v1, lt.sha1_hash('abababababababababab'))
        self.assertEqual(tp.file_priorities, [0, 1, 1])
        self.assertEqual(tp.peers, [('1.1.1.1', 1), ('2.2.2.2', 2)])

        ses = lt.session(settings)
        h = ses.add_torrent(tp)
        for attr in dir(tp):
            print('%s: %s' % (attr, getattr(tp, attr)))

        h.connect_peer(('3.3.3.3', 3))

        for i in range(0, 10):
            alerts = ses.pop_alerts()
            for a in alerts:
                print(a.message())
            time.sleep(0.1)

    def test_scrape(self):
        self.setup()
        # this is just to make sure this function can be called like this
        # from python
        self.h.scrape_tracker()

    def test_unknown_torrent_parameter(self):
        self.ses = lt.session(settings)
        try:
            self.h = self.ses.add_torrent({'unexpected-key-name': ''})
            self.assertFalse('should have thrown an exception')
        except KeyError as e:
            print(e)

    def test_torrent_parameter(self):
        self.ses = lt.session(settings)
        self.ti = lt.torrent_info('url_seed_multi.torrent')
        self.h = self.ses.add_torrent({
            'ti': self.ti,
            'save_path': os.getcwd(),
            'trackers': ['http://test.com/announce'],
            'dht_nodes': [('1.2.3.4', 6881), ('4.3.2.1', 6881)],
            'file_priorities': [1, 1],
            'url_seeds': ['http://test.com/announce-url'],
            'peers': [('5.6.7.8', 6881)],
            'banned_peers': [('8.7.6.5', 6881)],
            'renamed_files': {0: 'test.txt', 2: 'test.txt'}
        })
        self.st = self.h.status()
        self.assertEqual(self.st.save_path, os.getcwd())
        trackers = self.h.trackers()
        self.assertEqual(len(trackers), 1)
        self.assertEqual(trackers[0].get('url'), 'http://test.com/announce')
        self.assertEqual(trackers[0].get('tier'), 0)
        self.assertEqual(self.h.get_file_priorities(), [1, 1])
        # url_seeds was already set, test that it did not get overwritten
        self.assertEqual(self.h.url_seeds(),
                         ['http://test.com/announce-url/', 'http://test.com/file/'])
        # piece priorities weren't set explicitly, but they were updated by the
        # file priorities being set
        self.assertEqual(self.h.get_piece_priorities(), [1])
        self.assertEqual(self.st.verified_pieces, [])
