#!/usr/bin/env python
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

from __future__ import print_function

import libtorrent as lt

import unittest
import time
import datetime
import os
import shutil
import binascii
import subprocess as sub
import sys
import inspect
import pickle

# include terminal interface for travis parallel executions of scripts which use
# terminal features: fix multiple stdin assignment at termios.tcgetattr
if os.name != 'nt':
    import pty

HAVE_DEPRECATED_APIS = hasattr(lt, 'version')

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


class test_session_stats(unittest.TestCase):

    def test_unique(self):
        l = lt.session_stats_metrics()
        self.assertTrue(len(l) > 40)
        idx = set()
        for m in l:
            self.assertTrue(m.value_index not in idx)
            idx.add(m.value_index)

    def test_find_idx(self):
        self.assertEqual(lt.find_metric_idx("peer.error_peers"), 0)


class test_torrent_handle(unittest.TestCase):

    def setup(self):
        self.ses = lt.session({
            'alert_mask': lt.alert.category_t.all_categories,
            'enable_dht': False})
        self.ti = lt.torrent_info('url_seed_multi.torrent')
        self.h = self.ses.add_torrent({
            'ti': self.ti, 'save_path': os.getcwd()})

    def test_torrent_handle(self):
        self.setup()
        self.assertEqual(self.h.file_priorities(), [4, 4])
        self.assertEqual(self.h.piece_priorities(), [4])

        self.h.prioritize_files([0, 1])
        self.assertEqual(self.h.file_priorities(), [0, 1])

        self.h.prioritize_pieces([0])
        self.assertEqual(self.h.piece_priorities(), [0])

        # also test the overload that takes a list of piece->priority mappings
        self.h.prioritize_pieces([(0, 1)])
        self.assertEqual(self.h.piece_priorities(), [1])
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
        # wait a bit until the endpoints list gets populated
        while len(tracker_list[0]['endpoints']) == 0:
            time.sleep(0.1)
            tracker_list = [tracker for tracker in self.h.trackers()]
        pickled_trackers = pickle.dumps(tracker_list)
        unpickled_trackers = pickle.loads(pickled_trackers)
        self.assertEqual(unpickled_trackers[0]['url'], 'udp://tracker1.com')
        self.assertEqual(unpickled_trackers[0]['endpoints'][0]['last_error']['value'], 0)

    def test_file_status(self):
        self.setup()
        l = self.h.file_status()
        print(l)

    def test_piece_deadlines(self):
        self.setup()
        self.h.clear_piece_deadlines()

    def test_status_last_uploaded_dowloaded(self):
        # we want to check at seconds precision but can't control session
        # time, wait for next full second to prevent second increment
        time.sleep(1 - datetime.datetime.now().microsecond / 1000000.0)

        sessionStart = datetime.datetime.now().replace(microsecond=0)
        self.setup()
        st = self.h.status()
        # last upload and download times are at session start time
        self.assertLessEqual(abs(st.last_upload - sessionStart), datetime.timedelta(seconds=1))
        self.assertLessEqual(abs(st.last_download - sessionStart), datetime.timedelta(seconds=1))

    def test_serialize_trackers(self):
        """Test to ensure the dict contains only python built-in types"""
        self.setup()
        self.h.add_tracker({'url':'udp://tracker1.com'})
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
        self.assertEqual(ti.info_hash(), self.ti.info_hash())
        # make sure we can compare torrent_status objects
        st2 = self.h.status()
        self.assertEqual(st2, st)

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
        self.assertEqual(tp.info_hash, lt.sha1_hash('abababababababababab'))
        self.assertEqual(tp.file_priorities, [0, 1, 1])
        self.assertEqual(tp.peers, [('1.1.1.1', 1), ('2.2.2.2', 2)])

        ses = lt.session({'alert_mask': lt.alert.category_t.all_categories})
        h = ses.add_torrent(tp)

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
        self.ses = lt.session({'alert_mask': lt.alert.category_t.all_categories,
            'enable_dht': False})
        try:
            self.h = self.ses.add_torrent({'unexpected-key-name': ''})
            self.assertFalse('should have thrown an exception')
        except KeyError as e:
            print(e)

    def test_torrent_parameter(self):
        self.ses = lt.session({'alert_mask': lt.alert.category_t.all_categories,
            'enable_dht': False})
        self.ti = lt.torrent_info('url_seed_multi.torrent');
        self.h = self.ses.add_torrent({
            'ti': self.ti,
            'save_path': os.getcwd(),
            'trackers': ['http://test.com/announce'],
            'dht_nodes': [('1.2.3.4', 6881), ('4.3.2.1', 6881)],
            'file_priorities': [1,1],
            'http_seeds': ['http://test.com/file3'],
            'url_seeds': ['http://test.com/announce-url'],
            'peers': [('5.6.7.8', 6881)],
            'banned_peers': [('8.7.6.5', 6881)],
            'renamed_files': { 0: 'test.txt', 2: 'test.txt' }
            })
        self.st = self.h.status()
        self.assertEqual(self.st.save_path, os.getcwd())
        trackers = self.h.trackers();
        self.assertEqual(len(trackers), 1)
        self.assertEqual(trackers[0].get('url'), 'http://test.com/announce')
        self.assertEqual(trackers[0].get('tier'), 0)
        self.assertEqual(self.h.file_priorities(), [1,1])
        self.assertEqual(self.h.http_seeds(),['http://test.com/file3'])
        # url_seeds was already set, test that it did not got overwritten
        self.assertEqual(self.h.url_seeds(),
            ['http://test.com/announce-url/', 'http://test.com/file/'])
        self.assertEqual(self.h.piece_priorities(),[4])
        self.assertEqual(self.ti.merkle_tree(),[])
        self.assertEqual(self.st.verified_pieces,[])

class test_torrent_info(unittest.TestCase):

    def test_bencoded_constructor(self):
        info = lt.torrent_info({'info': {
            'name': 'test_torrent', 'length': 1234,
            'piece length': 16 * 1024,
            'pieces': 'aaaaaaaaaaaaaaaaaaaa'}})

        self.assertEqual(info.num_files(), 1)

        f = info.files()
        self.assertEqual(f.file_path(0), 'test_torrent')
        self.assertEqual(f.file_size(0), 1234)
        self.assertEqual(info.total_size(), 1234)

    def test_metadata(self):
        ti = lt.torrent_info('base.torrent')

        self.assertTrue(len(ti.metadata()) != 0)
        self.assertTrue(len(ti.hash_for_piece(0)) != 0)

    def test_web_seeds(self):
        ti = lt.torrent_info('base.torrent')

        ws = [{'url': 'http://foo/test', 'auth': '', 'type': 0},
              {'url': 'http://bar/test', 'auth': '', 'type': 1}]
        ti.set_web_seeds(ws)
        web_seeds = ti.web_seeds()
        self.assertEqual(len(ws), len(web_seeds))
        for i in range(len(web_seeds)):
            self.assertEqual(web_seeds[i]["url"], ws[i]["url"])
            self.assertEqual(web_seeds[i]["auth"], ws[i]["auth"])
            self.assertEqual(web_seeds[i]["type"], ws[i]["type"])

    def test_iterable_files(self):
        # the file_strage object is only iterable for backwards compatibility
        if not HAVE_DEPRECATED_APIS:
            return

        lt.session({'alert_mask': lt.alert.category_t.all_categories,
                    'enable_dht': False})
        ti = lt.torrent_info('url_seed_multi.torrent')
        files = ti.files()

        idx = 0
        expected = ['bar.txt', 'var.txt']
        for f in files:
            print(f.path)

            self.assertEqual(os.path.split(f.path)[1], expected[idx])
            self.assertEqual(os.path.split(f.path)[0],
                             os.path.join('temp', 'foo'))
            idx += 1


    def test_announce_entry(self):
        ae = lt.announce_entry('test')
        self.assertEquals(ae.url, 'test')
        self.assertEquals(ae.tier, 0)
        self.assertEquals(ae.verified, False)
        self.assertEquals(ae.source, 0)

class test_alerts(unittest.TestCase):

    def test_alert(self):

        ses = lt.session({'alert_mask': lt.alert.category_t.all_categories,
                          'enable_dht': False})
        ti = lt.torrent_info('base.torrent')
        h = ses.add_torrent({'ti': ti, 'save_path': os.getcwd()})
        st = h.status()
        time.sleep(1)
        ses.remove_torrent(h)
        ses.wait_for_alert(1000)  # milliseconds
        alerts = ses.pop_alerts()
        for a in alerts:
            print(a.message())
            for field_name in dir(a):
                if field_name.startswith('__'):
                    continue
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
        if HAVE_DEPRECATED_APIS:
            print(st.paused)
        print(st.info_hash)
        print(st.seeding_duration)
        print(st.last_upload)
        print(st.last_download)
        self.assertEqual(st.save_path, os.getcwd())

    def test_pop_alerts(self):
        ses = lt.session({'alert_mask': lt.alert.category_t.all_categories,
                          'enable_dht': False})
        ses.async_add_torrent(
            {"ti": lt.torrent_info("base.torrent"), "save_path": "."})

# this will cause an error (because of duplicate torrents) and the
# torrent_info object created here will be deleted once the alert goes out
# of scope. When that happens, it will decrement the python object, to allow
# it to release the object.
# we're trying to catch the error described in this post, with regards to
# torrent_info.
# https://mail.python.org/pipermail/cplusplus-sig/2007-June/012130.html
        ses.async_add_torrent(
            {"ti": lt.torrent_info("base.torrent"), "save_path": "."})
        time.sleep(1)
        for i in range(0, 10):
            alerts = ses.pop_alerts()
            for a in alerts:
                print(a.message())
            time.sleep(0.1)


class test_bencoder(unittest.TestCase):

    def test_bencode(self):

        encoded = lt.bencode({'a': 1, 'b': [1, 2, 3], 'c': 'foo'})
        self.assertEqual(encoded, b'd1:ai1e1:bli1ei2ei3ee1:c3:fooe')

    def test_bdecode(self):

        encoded = b'd1:ai1e1:bli1ei2ei3ee1:c3:fooe'
        decoded = lt.bdecode(encoded)
        self.assertEqual(decoded, {b'a': 1, b'b': [1, 2, 3], b'c': b'foo'})


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

class test_session(unittest.TestCase):

    def test_add_torrent(self):
        s = lt.session({'alert_mask': lt.alert.category_t.stats_notification, 'enable_dht': False})
        h = s.add_torrent({'ti': lt.torrent_info('base.torrent'),
            'save_path': '.',
            'dht_nodes': [('1.2.3.4', 6881), ('4.3.2.1', 6881)],
            'http_seeds': ['http://test.com/seed'],
            'peers': [('5.6.7.8', 6881)],
            'banned_peers': [('8.7.6.5', 6881)],
            'file_priorities': [1,1,1,2,0]})

    def test_apply_settings(self):

        s = lt.session({'enable_dht': False})
        s.apply_settings({'num_want': 66, 'user_agent': 'test123'})
        self.assertEqual(s.get_settings()['num_want'], 66)
        self.assertEqual(s.get_settings()['user_agent'], 'test123')

    def test_post_session_stats(self):
        s = lt.session({'alert_mask': lt.alert.category_t.stats_notification,
                        'enable_dht': False})

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

    def test_unknown_settings(self):
        try:
            s = lt.session({'unexpected-key-name': 42})
            self.assertFalse('should have thrown an exception')
        except KeyError as e:
            print(e)

    def test_fingerprint(self):
        self.assertEqual(lt.generate_fingerprint('LT', 0, 1, 2, 3), '-LT0123-')
        self.assertEqual(lt.generate_fingerprint('..', 10, 1, 2, 3), '-..A123-')

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


class test_example_client(unittest.TestCase):

    def test_execute_client(self):
        if os.name == 'nt':
            # TODO: fix windows includes of client.py
            return
        my_stdin = sys.stdin
        if os.name != 'nt':
            master_fd, slave_fd = pty.openpty()
            # slave_fd fix multiple stdin assignment at termios.tcgetattr
            my_stdin = slave_fd

        process = sub.Popen(
            [sys.executable,"client.py","url_seed_multi.torrent"],
            stdin=my_stdin, stdout=sub.PIPE, stderr=sub.PIPE)
        # python2 has no Popen.wait() timeout
        time.sleep(5)
        returncode = process.poll()
        if returncode == None:
            # this is an expected use-case
            process.kill()
        err = process.stderr.read().decode("utf-8")
        self.assertEqual('', err, 'process throw errors: \n' + err)
        # check error code if process did unexpected end
        if returncode != None:
            # in case of error return: output stdout if nothing was on stderr
            if returncode != 0:
                print("stdout:\n" + process.stdout.read().decode("utf-8"))
            self.assertEqual(returncode, 0, "returncode: " + str(returncode) + "\n"
                + "stderr: empty\n"
                + "some configuration does not output errors like missing module members,"
                + "try to call it manually to get the error message\n")

    def test_execute_simple_client(self):
        process = sub.Popen(
            [sys.executable,"simple_client.py","url_seed_multi.torrent"],
            stdout=sub.PIPE, stderr=sub.PIPE)
        # python2 has no Popen.wait() timeout
        time.sleep(5)
        returncode = process.poll()
        if returncode == None:
            # this is an expected use-case
            process.kill()
        err = process.stderr.read().decode("utf-8")
        self.assertEqual('', err, 'process throw errors: \n' + err)
        # check error code if process did unexpected end
        if returncode != None:
            # in case of error return: output stdout if nothing was on stderr
            if returncode != 0:
                print("stdout:\n" + process.stdout.read().decode("utf-8"))
            self.assertEqual(returncode, 0, "returncode: " + str(returncode) + "\n"
                + "stderr: empty\n"
                + "some configuration does not output errors like missing module members,"
                + "try to call it manually to get the error message\n")

    def test_execute_make_torrent(self):
        process = sub.Popen(
            [sys.executable,"make_torrent.py","url_seed_multi.torrent",
            "http://test.com/test"], stdout=sub.PIPE, stderr=sub.PIPE)
        returncode = process.wait()
        # python2 has no Popen.wait() timeout
        err = process.stderr.read().decode("utf-8")
        self.assertEqual('', err, 'process throw errors: \n' + err)
        # in case of error return: output stdout if nothing was on stderr
        if returncode != 0:
            print("stdout:\n" + process.stdout.read().decode("utf-8"))
        self.assertEqual(returncode, 0, "returncode: " + str(returncode) + "\n"
            + "stderr: empty\n"
            + "some configuration does not output errors like missing module members,"
            + "try to call it manually to get the error message\n")

    def test_default_settings(self):

        default = lt.default_settings()
        print(default)

class test_operation_t(unittest.TestCase):

    def test_enum(self):
        self.assertEqual(lt.operation_name(lt.operation_t.sock_accept), "sock_accept")
        self.assertEqual(lt.operation_name(lt.operation_t.unknown), "unknown")
        self.assertEqual(lt.operation_name(lt.operation_t.mkdir), "mkdir")
        self.assertEqual(lt.operation_name(lt.operation_t.partfile_write), "partfile_write")
        self.assertEqual(lt.operation_name(lt.operation_t.hostname_lookup), "hostname_lookup")

if __name__ == '__main__':
    print(lt.__version__)
    shutil.copy(os.path.join('..', '..', 'test', 'test_torrents',
                             'url_seed_multi.torrent'), '.')
    shutil.copy(os.path.join('..', '..', 'test', 'test_torrents',
                             'base.torrent'), '.')
    shutil.copy(os.path.join('..', '..', 'test', 'test_torrents',
                             'unordered.torrent'), '.')
    unittest.main()
