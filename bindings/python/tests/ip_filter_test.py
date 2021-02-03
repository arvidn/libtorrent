import unittest

import libtorrent as lt


class IpFilterTest(unittest.TestCase):
    def test_empty(self) -> None:
        ipf = lt.ip_filter()
        self.assertEqual(ipf.access("0.1.2.3"), 0)
        self.assertEqual(ipf.access("::123"), 0)

        # self.assertEqual(ipf.export_filter(), "something")

    def test_with_rule_ip4(self) -> None:
        ipf = lt.ip_filter()
        ipf.add_rule("0.0.0.0", "0.255.255.255", 123)
        self.assertEqual(ipf.access("0.1.2.3"), 123)
        self.assertEqual(ipf.access("1.2.3.4"), 0)
        self.assertEqual(ipf.access("::123"), 0)

        # self.assertEqual(ipf.export_filter(), "something")

    # @unittest.skip("https://github.com/arvidn/libtorrent/issues/5995")
    def test_with_rule_ip6(self) -> None:
        ipf = lt.ip_filter()
        ipf.add_rule("::", "::ffff", 123)
        self.assertEqual(ipf.access("::123"), 123)
        self.assertEqual(ipf.access("1.2.3.4"), 0)
        self.assertEqual(ipf.access("::1:0"), 0)

        # self.assertEqual(ipf.export_filter(), "something")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5995")
    def test_export(self) -> None:
        # Seems the format of this may need discussion
        ipf = lt.ip_filter()
        self.assertEqual(ipf.export_filter(), "something")

        ipf = lt.ip_filter()
        ipf.add_rule("0.0.0.0", "0.255.255.255", 123)
        ipf.add_rule("::", "::ffff", 456)
        self.assertEqual(ipf.export_filter(), "something")
