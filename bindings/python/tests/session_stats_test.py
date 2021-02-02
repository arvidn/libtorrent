import libtorrent as lt

import unittest


class test_session_stats(unittest.TestCase):

    def test_unique(self):
        metrics = lt.session_stats_metrics()
        self.assertTrue(len(metrics) > 40)
        idx = set()
        for m in metrics:
            self.assertTrue(m.value_index not in idx)
            idx.add(m.value_index)

    def test_find_idx(self):
        self.assertEqual(lt.find_metric_idx("peer.error_peers"), 0)
