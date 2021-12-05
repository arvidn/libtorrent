import ipaddress
from typing import List
from typing import Tuple
import unittest

import libtorrent as lt


# boost's address formatting is currently inconsistent between alpine linux and
# other environments, even when boost is built from source
def normalize(
    exported_filter: Tuple[List[Tuple[str, str]], List[Tuple[str, str]]]
) -> Tuple[List[Tuple[str, str]], List[Tuple[str, str]]]:
    def norm(addr: str) -> str:
        return str(ipaddress.ip_address(addr))

    def norm_list(filter_list: List[Tuple[str, str]]) -> List[Tuple[str, str]]:
        return [(norm(lo), norm(hi)) for lo, hi in filter_list]

    v4, v6 = exported_filter
    return (norm_list(v4), norm_list(v6))


class IpFilterTest(unittest.TestCase):
    maxDiff = None

    def test_empty(self) -> None:
        ipf = lt.ip_filter()
        self.assertEqual(ipf.access("0.1.2.3"), 0)
        self.assertEqual(ipf.access("::123"), 0)

        self.assertEqual(
            normalize(ipf.export_filter()),
            (
                [("0.0.0.0", "255.255.255.255")],
                [("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")],
            ),
        )

    def test_with_rule_ip4(self) -> None:
        ipf = lt.ip_filter()
        ipf.add_rule("0.0.0.0", "0.255.255.255", 123)
        self.assertEqual(ipf.access("0.1.2.3"), 123)
        self.assertEqual(ipf.access("1.2.3.4"), 0)
        self.assertEqual(ipf.access("::123"), 0)

        self.assertEqual(
            normalize(ipf.export_filter()),
            (
                [("0.0.0.0", "0.255.255.255"), ("1.0.0.0", "255.255.255.255")],
                [("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")],
            ),
        )

    def test_with_rule_ip6(self) -> None:
        ipf = lt.ip_filter()
        ipf.add_rule("::", "::ffff", 123)
        self.assertEqual(ipf.access("::123"), 123)
        self.assertEqual(ipf.access("1.2.3.4"), 0)
        self.assertEqual(ipf.access("::1:0"), 0)

        self.assertEqual(
            normalize(ipf.export_filter()),
            (
                [("0.0.0.0", "255.255.255.255")],
                [
                    ("::", "::ffff"),
                    ("::1:0", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                ],
            ),
        )

    def test_export(self) -> None:
        ipf = lt.ip_filter()
        self.assertEqual(
            normalize(ipf.export_filter()),
            (
                [("0.0.0.0", "255.255.255.255")],
                [("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")],
            ),
        )

        ipf = lt.ip_filter()
        ipf.add_rule("0.0.0.0", "0.255.255.255", 123)
        ipf.add_rule("::", "::ffff", 456)
        self.assertEqual(
            normalize(ipf.export_filter()),
            (
                [("0.0.0.0", "0.255.255.255"), ("1.0.0.0", "255.255.255.255")],
                [
                    ("::", "::ffff"),
                    ("::1:0", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                ],
            ),
        )
