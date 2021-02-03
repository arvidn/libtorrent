import tempfile
import unittest

import libtorrent as lt

from . import lib


class ParseMagnetTest(unittest.TestCase):
    def setUp(self) -> None:
        self.info_hash_sha1 = lib.get_random_bytes(20).hex()
        self.info_hash_sha256 = lib.get_random_bytes(32).hex()

    def test_parse_to_atp(self) -> None:
        uri = f"magnet:?xt=urn:btih:{self.info_hash_sha1}"
        atp = lt.parse_magnet_uri(uri)
        self.assertEqual(str(atp.info_hash).lower(), self.info_hash_sha1)

    def test_parse_to_atp_error(self) -> None:
        with self.assertRaises(RuntimeError):
            lt.parse_magnet_uri("magnet:?")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5992")
    def test_parse_dict_deprecated(self) -> None:
        uri = f"magnet:?xt=urn:btih:{self.info_hash_sha1}"
        with self.assertWarns(DeprecationWarning):
            lt.parse_magnet_uri_dict(uri)

    def test_parse_dict_sha1(self) -> None:
        uri = (
            f"magnet:?xt=urn:btih:{self.info_hash_sha1}&"
            "dn=test.txt&"
            "tr=http://example.com/tr&"
            "ws=http://example.com/ws&"
            "so=0-2,4&"
            "x.pe=0.1.2.3:4567&"
            "dht=1.2.3.4:5678"
        )
        params = lt.parse_magnet_uri_dict(uri)
        self.assertEqual(
            params,
            {
                "dht_nodes": [("1.2.3.4", 5678)],
                "flags": lt.add_torrent_params_flags_t.default_flags,
                "info_hash": bytes.fromhex(self.info_hash_sha1),
                "info_hashes": bytes.fromhex(self.info_hash_sha1),
                "name": "test.txt",
                "save_path": "",
                "storage_mode": lt.storage_mode_t.storage_mode_sparse,
                "trackers": ["http://example.com/tr"],
                "url": "",
            },
        )

        # The dict is intended to be usable as argument to session.add_torrent()
        session = lt.session(lib.get_isolated_settings())
        with tempfile.TemporaryDirectory() as path:
            params["save_path"] = path
            with self.assertWarns(DeprecationWarning):
                handle = session.add_torrent(params)
            self.assertEqual(str(handle.info_hashes().v1), self.info_hash_sha1)
            self.assertEqual(handle.status().name, "test.txt")
            self.assertEqual(
                [t["url"] for t in handle.trackers()], ["http://example.com/tr"]
            )
            # self.assertEqual(handle.url_seeds(), ["http://example.com/ws"])
            # self.assertEqual(handle.file_priorities(), [4, 4, 4, 0, 4])
            # Can't test peers or dht

    @unittest.skip("need to parse more params")
    def test_parse_dict_sha1_broken(self) -> None:
        uri = (
            f"magnet:?xt=urn:btih:{self.info_hash_sha1}&"
            "dn=test.txt&"
            "tr=http://example.com/tr&"
            "ws=http://example.com/ws&"
            "so=0-2,4&"
            "x.pe=0.1.2.3:4567&"
            "dht=1.2.3.4:5678"
        )
        params = lt.parse_magnet_uri_dict(uri)
        self.assertEqual(
            params,
            {
                "dht_nodes": [("1.2.3.4", 5678)],
                "file_priorities": [4, 4, 4, 0, 4],
                "flags": lt.add_torrent_params_flags_t.default_flags,
                "info_hash": bytes.fromhex(self.info_hash_sha1),
                "info_hashes": bytes.fromhex(self.info_hash_sha1),
                "name": "test.txt",
                "save_path": "",
                "storage_mode": lt.storage_mode_t.storage_mode_sparse,
                "trackers": ["http://example.com/tr"],
                "url": "",
                "url_seeds": ["http://example.com/ws"],
            },
        )

        # The dict is intended to be usable as argument to session.add_torrent()
        session = lt.session(lib.get_isolated_settings())
        with tempfile.TemporaryDirectory() as path:
            params["save_path"] = path
            handle = session.add_torrent(params)
            self.assertEqual(str(handle.info_hashes().v1), self.info_hash_sha1)
            self.assertEqual(handle.name(), "test.txt")
            self.assertEqual(
                [t["url"] for t in handle.trackers()], ["http://example.com/tr"]
            )
            self.assertEqual(handle.url_seeds(), ["http://example.com/ws"])
            self.assertEqual(handle.file_priorities(), [4, 4, 4, 0, 4])
            # Can't test peers or dht

    def test_parse_dict_sha256(self) -> None:
        uri = (
            f"magnet:?xt=urn:btmh:1220{self.info_hash_sha256}&"
            "dn=test.txt&"
            "tr=http://example.com/tr&"
            "ws=http://example.com/ws&"
            "so=0-2,4&"
            "x.pe=0.1.2.3:4567&"
            "dht=1.2.3.4:5678"
        )
        params = lt.parse_magnet_uri_dict(uri)
        self.assertEqual(
            params,
            {
                "dht_nodes": [("1.2.3.4", 5678)],
                "flags": lt.add_torrent_params_flags_t.default_flags,
                "info_hash": bytes.fromhex(self.info_hash_sha256)[:20],
                "info_hashes": bytes.fromhex(self.info_hash_sha256),
                "name": "test.txt",
                "save_path": "",
                "storage_mode": lt.storage_mode_t.storage_mode_sparse,
                "trackers": ["http://example.com/tr"],
                "url": "",
            },
        )

        # The dict is intended to be usable as argument to session.add_torrent()
        session = lt.session(lib.get_isolated_settings())
        with tempfile.TemporaryDirectory() as path:
            params["save_path"] = path
            with self.assertWarns(DeprecationWarning):
                handle = session.add_torrent(params)
            # self.assertEqual(str(handle.info_hashes().v2), self.info_hash_sha256)
            self.assertEqual(handle.status().name, "test.txt")
            self.assertEqual(
                [t["url"] for t in handle.trackers()], ["http://example.com/tr"]
            )
            # self.assertEqual(handle.url_seeds(), ["http://example.com/ws"])
            # self.assertEqual(handle.file_priorities(), [4, 4, 4, 0, 4])
            # Can't test peers or dht

    @unittest.skip("need to parse more params")
    def test_parse_dict_sha256_broken(self) -> None:
        uri = (
            f"magnet:?xt=urn:btmh:1220{self.info_hash_sha256}&"
            "dn=test.txt&"
            "tr=http://example.com/tr&"
            "ws=http://example.com/ws&"
            "so=0-2,4&"
            "x.pe=0.1.2.3:4567&"
            "dht=1.2.3.4:5678"
        )
        params = lt.parse_magnet_uri_dict(uri)
        self.assertEqual(
            params,
            {
                "dht_nodes": [("1.2.3.4", 5678)],
                "file_priorities": [4, 4, 4, 0, 4],
                "flags": lt.add_torrent_params_flags_t.default_flags,
                "info_hash": bytes.fromhex(self.info_hash_sha256)[:20],
                "info_hashes": bytes.fromhex(self.info_hash_sha256),
                "name": "test.txt",
                "peers": [("0.1.2.3", 4567)],
                "save_path": "",
                "storage_mode": lt.storage_mode_t.storage_mode_sparse,
                "trackers": ["http://example.com/tr"],
                "url": "",
                "url_seeds": "http://example.com/ws",
            },
        )

        # The dict is intended to be usable as argument to session.add_torrent()
        session = lt.session(lib.get_isolated_settings())
        with tempfile.TemporaryDirectory() as path:
            params["save_path"] = path
            handle = session.add_torrent(params)
            self.assertEqual(str(handle.info_hashes().v2), self.info_hash_sha256)
            self.assertEqual(handle.name(), "test.txt")
            self.assertEqual(
                [t["url"] for t in handle.trackers()], ["http://example.com/tr"]
            )
            self.assertEqual(handle.url_seeds(), ["http://example.com/ws"])
            self.assertEqual(handle.file_priorities(), [4, 4, 4, 0, 4])
            # Can't test peers or dht

    def test_parse_dict_error(self) -> None:
        with self.assertRaises(RuntimeError):
            lt.parse_magnet_uri_dict("magnet:?")


class AddMagnetUriTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())
        self.dir = tempfile.TemporaryDirectory()
        self.info_hash_sha1 = lib.get_random_bytes(20).hex()

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.dir, timeout=5)

    def test_error(self) -> None:
        with self.assertWarns(DeprecationWarning):
            with self.assertRaises(RuntimeError):
                lt.add_magnet_uri(self.session, "magnet:?", {})

    def test_add(self) -> None:
        uri = f"magnet:?xt=urn:btih:{self.info_hash_sha1}"
        with self.assertWarns(DeprecationWarning):
            handle = lt.add_magnet_uri(self.session, uri, {"save_path": self.dir.name})
        self.assertEqual(str(handle.info_hashes().v1), self.info_hash_sha1)


class MakeMagnetUriTest(unittest.TestCase):
    def setUp(self) -> None:
        self.info_hash_sha1 = lib.get_random_bytes(20).hex()

    def test_torrent_info(self) -> None:
        ti = lt.torrent_info(lt.sha1_hash(bytes.fromhex(self.info_hash_sha1)))
        uri = lt.make_magnet_uri(ti)
        self.assertEqual(uri, f"magnet:?xt=urn:btih:{self.info_hash_sha1}")

    def test_torrent_handle(self) -> None:
        atp = lt.add_torrent_params()
        atp.info_hashes = lt.info_hash_t(
            lt.sha1_hash(bytes.fromhex(self.info_hash_sha1))
        )
        session = lt.session(lib.get_isolated_settings())
        with tempfile.TemporaryDirectory() as path:
            atp.save_path = path
            handle = session.add_torrent(atp)
            uri = lt.make_magnet_uri(handle)
        self.assertEqual(uri, f"magnet:?xt=urn:btih:{self.info_hash_sha1}")
