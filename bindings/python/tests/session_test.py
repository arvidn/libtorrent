import libtorrent as lt

import unittest
import sys


settings = {
    'alert_mask': lt.alert.category_t.all_categories,
    'enable_dht': False, 'enable_lsd': False, 'enable_natpmp': False,
    'enable_upnp': False, 'listen_interfaces': '0.0.0.0:0', 'file_pool_size': 1}


class test_session(unittest.TestCase):

    def test_settings(self):
        sett = {'alert_mask': lt.alert.category_t.all_categories}
        s = lt.session(sett)
        sett = s.get_settings()
        self.assertEqual(sett['alert_mask'] & 0x7fffffff, 0x7fffffff)

    def test_add_torrent(self):
        s = lt.session(settings)
        s.add_torrent({'ti': lt.torrent_info('base.torrent'),
                       'save_path': '.',
                       'dht_nodes': [('1.2.3.4', 6881), ('4.3.2.1', 6881)],
                       'http_seeds': ['http://test.com/seed'],
                       'peers': [('5.6.7.8', 6881)],
                       'banned_peers': [('8.7.6.5', 6881)],
                       'file_priorities': [1, 1, 1, 2, 0]})

    def test_apply_settings(self):

        s = lt.session(settings)
        s.apply_settings({'num_want': 66, 'user_agent': 'test123'})
        self.assertEqual(s.get_settings()['num_want'], 66)
        self.assertEqual(s.get_settings()['user_agent'], 'test123')

    def test_post_session_stats(self):
        s = lt.session({'alert_mask': 0, 'enable_dht': False})
        s.post_session_stats()
        alerts = []
        # first the stats headers log line. but not if logging is disabled
        while len(alerts) == 0:
            s.wait_for_alert(1000)
            alerts = s.pop_alerts()

        while len(alerts) > 0:
            a = alerts.pop(0)
            print(a)
            if isinstance(a, lt.session_stats_header_alert):
                break
        self.assertTrue(isinstance(a, lt.session_stats_header_alert))
        # then the actual stats values
        while len(alerts) == 0:
            s.wait_for_alert(1000)
            alerts = s.pop_alerts()
        a = alerts.pop(0)
        print(a)
        self.assertTrue(isinstance(a, lt.session_stats_alert))
        self.assertTrue(isinstance(a.values, dict))
        self.assertTrue(len(a.values) > 0)

    def test_post_dht_stats(self):
        s = lt.session({'alert_mask': 0, 'enable_dht': False})
        s.post_dht_stats()
        alerts = []
        cnt = 0
        while len(alerts) == 0:
            s.wait_for_alert(1000)
            alerts = s.pop_alerts()
            cnt += 1
            if cnt > 60:
                print('no dht_stats_alert in 1 minute!')
                sys.exit(1)
        a = alerts.pop(0)
        self.assertTrue(isinstance(a, lt.dht_stats_alert))
        self.assertTrue(isinstance(a.active_requests, list))
        self.assertTrue(isinstance(a.routing_table, list))

    def test_unknown_settings(self):
        try:
            lt.session({'unexpected-key-name': 42})
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

    def test_default_settings(self):

        default = lt.default_settings()
        print(default)
