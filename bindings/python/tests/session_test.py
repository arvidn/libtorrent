import os
import pathlib
import socket
import sys
import tempfile
from typing import Any
from typing import Dict
import unittest

import libtorrent as lt

from . import lib
from . import tdummy


class ReadWriteSessionParamsTest(unittest.TestCase):
    def test_read_bdecoded(self) -> None:
        params = lt.session_params({"alert_mask": 123})
        bdecoded = lt.write_session_params(params)

        read_params = lt.read_session_params(bdecoded)
        self.assertIsInstance(read_params, lt.session_params)
        # self.assertEqual(read_params, params)
        self.assertEqual(read_params.settings, params.settings)

        read_params = lt.read_session_params(bdecoded, flags=0)
        self.assertIsInstance(read_params, lt.session_params)
        # self.assertEqual(read_params, lt.session_params())
        self.assertEqual(read_params.settings, lt.session_params().settings)

    def test_read_bencoded(self) -> None:
        params = lt.session_params({"alert_mask": 123})
        bencoded = lt.bencode(lt.write_session_params(params))

        read_params = lt.read_session_params(bencoded)
        self.assertIsInstance(read_params, lt.session_params)
        # self.assertEqual(read_params, params)
        self.assertEqual(read_params.settings, params.settings)

        read_params = lt.read_session_params(bencoded, flags=0)
        self.assertIsInstance(read_params, lt.session_params)
        # self.assertEqual(read_params, lt.session_params())
        self.assertEqual(read_params.settings, lt.session_params().settings)

    def test_write(self) -> None:
        params = lt.session_params({"alert_mask": 123})

        bdecoded = lt.write_session_params(params)
        self.assertIsInstance(bdecoded, dict)
        self.assertEqual(bdecoded, lt.write_session_params(params))

        bdecoded = lt.write_session_params(
            params, flags=lt.save_state_flags_t.save_settings
        )
        self.assertEqual(set(bdecoded.keys()), {b"settings"})

    def test_write_empty(self) -> None:
        bdecoded = lt.write_session_params(lt.session_params(), flags=0)
        self.assertIsInstance(bdecoded, dict)

    def test_round_trip(self) -> None:
        params = lt.session_params()
        params.settings = {"alert_mask": 123}

        read_params = lt.read_session_params(lt.write_session_params(params))
        self.assertIsInstance(read_params, lt.session_params)
        # self.assertEqual(params, read_params)
        self.assertEqual(params.settings, read_params.settings)


class SessionParamsTest(unittest.TestCase):
    def test_constructor(self) -> None:
        params = lt.session_params()
        # self.assertEqual(params, lt.session_params())
        self.assertEqual(params.settings, lt.session_params().settings)

        params = lt.session_params({"alert_mask": 123})
        expected = lt.session_params().settings
        expected["alert_mask"] = 123
        self.assertEqual(params.settings, expected)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_equality(self) -> None:
        self.assertEqual(lt.session_params(), lt.session_params())
        self.assertEqual(
            lt.session_params({"alert_mask": 123}),
            lt.session_params({"alert_mask": 123}),
        )

    def test_settings(self) -> None:
        params = lt.session_params()
        params.settings = {"alert_mask": 123}
        expected = lt.session_params().settings
        expected["alert_mask"] = 123
        self.assertEqual(params.settings, expected)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_settings_reference(self) -> None:
        params = lt.session_params()
        params.settings["alert_mask"] = 123
        self.assertEqual(params.settings["alert_mask"], 123)

    def test_ip_filter(self) -> None:
        params = lt.session_params()
        self.assertEqual(
            params.ip_filter.export_filter(),
            (
                [("0.0.0.0", "255.255.255.255")],
                [("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")],
            ),
        )
        ipf = lt.ip_filter()
        ipf.add_rule("128.0.0.0", "128.0.0.1", 123)
        params.ip_filter = ipf
        self.assertEqual(
            params.ip_filter.export_filter(),
            (
                [
                    ("0.0.0.0", "127.255.255.255"),
                    ("128.0.0.0", "128.0.0.1"),
                    ("128.0.0.2", "255.255.255.255"),
                ],
                [("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")],
            ),
        )

    def test_ip_filter_reference(self) -> None:
        params = lt.session_params()
        self.assertEqual(
            params.ip_filter.export_filter(),
            (
                [("0.0.0.0", "255.255.255.255")],
                [("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")],
            ),
        )
        params.ip_filter.add_rule("128.0.0.0", "128.0.0.1", 123)
        self.assertEqual(
            params.ip_filter.export_filter(),
            (
                [
                    ("0.0.0.0", "127.255.255.255"),
                    ("128.0.0.0", "128.0.0.1"),
                    ("128.0.0.2", "255.255.255.255"),
                ],
                [("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")],
            ),
        )

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_dht_state(self) -> None:
        params = lt.session_params()
        self.assertEqual(params.dht_state, lt.dht_state())
        state = lt.dht_state()
        state.nodes = [("127.0.0.1", 1234)]
        params.dht_state = state
        self.assertEqual(params.dht_state, state)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_dht_state_reference(self) -> None:
        params = lt.session_params()
        self.assertEqual(params.dht_state, lt.dht_state())
        params.dht_state.nodes = [("127.0.0.1", 1234)]
        self.assertEqual(params.dht_state.nodes, [("127.0.0.1", 1234)])


class DhtStateTest(unittest.TestCase):
    def test_constructor(self) -> None:
        state = lt.dht_state()
        self.assertIsInstance(state, lt.dht_state)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_equality(self) -> None:
        a = lt.dht_state()
        b = lt.dht_state()
        self.assertEqual(a, b)
        a.nodes = [("127.0.0.1", 1234)]
        self.assertNotEqual(a, b)
        b.nodes = [("127.0.0.1", 1234)]
        self.assertNotEqual(a, b)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_nids(self) -> None:
        state = lt.dht_state()
        self.assertEqual(state.nids, [])
        state.nids = [("127.0.0.1", lt.sha1_hash(b"a" * 20))]
        self.assertEqual(state.nids, [("127.0.0.1", lt.sha1_hash(b"a" * 20))])

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_nodes(self) -> None:
        state = lt.dht_state()
        self.assertEqual(state.nodes, [])
        state.nodes = [("127.0.0.1", 1234)]
        self.assertEqual(state.nodes, [("127.0.0.1", 1234)])

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6140")
    def test_nodes6(self) -> None:
        state = lt.dht_state()
        self.assertEqual(state.nodes6, [])
        state.nodes6 = [("::1", 1234)]
        self.assertEqual(state.nodes6, [("::1", 1234)])


class SessionStatusTest(unittest.TestCase):
    def test_fields(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                stat = lt.session_status()

            self.assertIsInstance(stat.has_incoming_connections, bool)

            self.assertIsInstance(stat.upload_rate, int)
            self.assertIsInstance(stat.download_rate, int)
            self.assertIsInstance(stat.total_download, int)
            self.assertIsInstance(stat.total_upload, int)

            self.assertIsInstance(stat.payload_upload_rate, int)
            self.assertIsInstance(stat.payload_download_rate, int)
            self.assertIsInstance(stat.total_payload_download, int)
            self.assertIsInstance(stat.total_payload_upload, int)

            self.assertIsInstance(stat.ip_overhead_upload_rate, int)
            self.assertIsInstance(stat.ip_overhead_download_rate, int)
            self.assertIsInstance(stat.total_ip_overhead_download, int)
            self.assertIsInstance(stat.total_ip_overhead_upload, int)

            self.assertIsInstance(stat.dht_upload_rate, int)
            self.assertIsInstance(stat.dht_download_rate, int)
            self.assertIsInstance(stat.total_dht_download, int)
            self.assertIsInstance(stat.total_dht_upload, int)

            self.assertIsInstance(stat.tracker_upload_rate, int)
            self.assertIsInstance(stat.tracker_download_rate, int)
            self.assertIsInstance(stat.total_tracker_download, int)
            self.assertIsInstance(stat.total_tracker_upload, int)

            self.assertIsInstance(stat.total_redundant_bytes, int)
            self.assertIsInstance(stat.total_failed_bytes, int)

            self.assertIsInstance(stat.num_peers, int)
            self.assertIsInstance(stat.num_unchoked, int)
            self.assertIsInstance(stat.allowed_upload_slots, int)

            self.assertIsInstance(stat.up_bandwidth_queue, int)
            self.assertIsInstance(stat.down_bandwidth_queue, int)

            self.assertIsInstance(stat.up_bandwidth_bytes_queue, int)
            self.assertIsInstance(stat.down_bandwidth_bytes_queue, int)

            self.assertIsInstance(stat.optimistic_unchoke_counter, int)
            self.assertIsInstance(stat.unchoke_counter, int)

            self.assertIsInstance(stat.dht_nodes, int)
            self.assertIsInstance(stat.dht_node_cache, int)
            self.assertIsInstance(stat.dht_torrents, int)
            self.assertIsInstance(stat.dht_global_nodes, int)
            self.assertEqual(stat.active_requests, [])
            self.assertIsInstance(stat.dht_total_allocations, int)

            with self.assertWarns(DeprecationWarning):
                utp = stat.utp_stats
            self.assertIsInstance(utp["num_idle"], int)
            self.assertIsInstance(utp["num_syn_sent"], int)
            self.assertIsInstance(utp["num_connected"], int)
            self.assertIsInstance(utp["num_fin_sent"], int)
            self.assertIsInstance(utp["num_close_wait"], int)

    def test_from_session(self) -> None:
        session = lt.session(lib.get_isolated_settings())
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                stat = session.status()
            self.assertIsInstance(stat, lt.session_status)


class AddTorrentParamsTest(unittest.TestCase):
    def test_fields(self) -> None:
        atp = lt.add_torrent_params()

        atp.version = 123
        self.assertEqual(atp.version, 123)
        atp.ti = lt.torrent_info(lt.sha1_hash())
        self.assertEqual(atp.ti.info_hashes().v1, lt.sha1_hash())
        atp.trackers = ["http://example.com/tr"]
        self.assertEqual(atp.trackers, ["http://example.com/tr"])
        atp.tracker_tiers = [1]
        self.assertEqual(atp.tracker_tiers, [1])
        atp.dht_nodes = [("0.1.2.3", 1234)]
        self.assertEqual(atp.dht_nodes, [("0.1.2.3", 1234)])
        atp.name = "test.txt"
        self.assertEqual(atp.name, "test.txt")
        atp.save_path = "."
        self.assertEqual(atp.save_path, ".")
        atp.storage_mode = lt.storage_mode_t.storage_mode_allocate
        self.assertEqual(atp.storage_mode, lt.storage_mode_t.storage_mode_allocate)
        atp.file_priorities = [1]
        self.assertEqual(atp.file_priorities, [1])
        atp.trackerid = "trackerid"
        self.assertEqual(atp.trackerid, "trackerid")
        atp.flags = lt.torrent_flags.default_flags
        self.assertEqual(atp.flags, lt.torrent_flags.default_flags)
        atp.max_uploads = 1
        self.assertEqual(atp.max_uploads, 1)
        atp.max_connections = 1
        self.assertEqual(atp.max_connections, 1)
        atp.upload_limit = 1024
        self.assertEqual(atp.upload_limit, 1024)
        atp.download_limit = 1024
        self.assertEqual(atp.download_limit, 1024)
        atp.total_uploaded = 1024
        self.assertEqual(atp.total_uploaded, 1024)
        atp.total_downloaded = 1024
        self.assertEqual(atp.total_downloaded, 1024)
        atp.active_time = 1234
        self.assertEqual(atp.active_time, 1234)
        atp.finished_time = 1234
        self.assertEqual(atp.finished_time, 1234)
        atp.seeding_time = 1234
        self.assertEqual(atp.seeding_time, 1234)
        atp.added_time = 1234
        self.assertEqual(atp.added_time, 1234)
        atp.completed_time = 1234
        self.assertEqual(atp.completed_time, 1234)
        atp.last_seen_complete = 1234
        self.assertEqual(atp.last_seen_complete, 1234)
        atp.last_download = 1234
        self.assertEqual(atp.last_download, 1234)
        atp.last_upload = 1234
        self.assertEqual(atp.last_upload, 1234)
        atp.num_complete = 10
        self.assertEqual(atp.num_complete, 10)
        atp.num_incomplete = 10
        self.assertEqual(atp.num_incomplete, 10)
        atp.num_downloaded = 10
        self.assertEqual(atp.num_downloaded, 10)
        atp.info_hashes = lt.info_hash_t(lt.sha1_hash())
        self.assertEqual(atp.info_hashes.v1, lt.sha1_hash())
        atp.url_seeds = ["http://example.com/seed"]
        self.assertEqual(atp.url_seeds, ["http://example.com/seed"])
        atp.peers = [("1.2.3.4", 4321)]
        self.assertEqual(atp.peers, [("1.2.3.4", 4321)])
        atp.banned_peers = [("2.3.4.5", 4321)]
        self.assertEqual(atp.banned_peers, [("2.3.4.5", 4321)])
        atp.unfinished_pieces = {}
        self.assertEqual(atp.unfinished_pieces, {})
        atp.have_pieces = [True, False]
        self.assertEqual(atp.have_pieces, [True, False])
        atp.verified_pieces = [True, False]
        self.assertEqual(atp.verified_pieces, [True, False])
        atp.piece_priorities = [1]
        self.assertEqual(atp.piece_priorities, [1])
        atp.renamed_files = {}
        self.assertEqual(atp.renamed_files, {})

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_name_assign_bytes_deprecated(self) -> None:
        atp = lt.add_torrent_params()

        with self.assertWarns(DeprecationWarning):
            atp.name = b"test.txt"  # type: ignore

    def test_name_assign_bytes(self) -> None:
        atp = lt.add_torrent_params()

        atp.name = b"test.txt"  # type: ignore
        self.assertEqual(atp.name, "test.txt")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_trackerid_assign_bytes_deprecated(self) -> None:
        atp = lt.add_torrent_params()
        with self.assertWarns(DeprecationWarning):
            atp.trackerid = b"trackerid"  # type: ignore

    def test_trackerid_assign_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.trackerid = b"trackerid"  # type: ignore
        self.assertEqual(atp.trackerid, "trackerid")

    def test_save_path_ascii_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = "test"
        self.assertEqual(atp.save_path, "test")
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], b"test")

    def test_save_path_ascii_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = b"test"  # type: ignore
        self.assertEqual(atp.save_path, "test")
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], b"test")

    def test_save_path_non_ascii_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = "\u1234"
        self.assertEqual(atp.save_path, "\u1234")
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], os.fsencode("\u1234"))

    def test_save_path_non_ascii_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = os.fsencode("\u1234")  # type: ignore
        self.assertEqual(atp.save_path, "\u1234")
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], os.fsencode("\u1234"))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_save_path_surrogate_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = "\udcff"
        self.assertEqual(atp.save_path, "\udcff")
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], os.fsencode("\udcff"))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_save_path_surrogate_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = os.fsencode("\udcff")  # type: ignore
        self.assertEqual(atp.save_path, "\udcff")
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], os.fsencode("\udcff"))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_save_path_non_unicode_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = os.fsdecode(b"\xff")
        self.assertEqual(atp.save_path, os.fsdecode(b"\xff"))
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], b"\xff")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_save_path_non_unicode_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.save_path = b"\xff"  # type: ignore
        self.assertEqual(atp.save_path, os.fsdecode(b"\xff"))
        self.assertEqual(lt.write_resume_data(atp)[b"save_path"], b"\xff")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_info_hash_deprecated(self) -> None:
        atp = lt.add_torrent_params()
        with self.assertWarns(DeprecationWarning):
            self.assertTrue(atp.info_hash.is_all_zeros())
        with self.assertWarns(DeprecationWarning):
            atp.info_hash = lt.sha1_hash(lib.get_random_bytes(20))

    def test_info_hash(self) -> None:
        atp = lt.add_torrent_params()
        if lt.api_version < 4:
            self.assertTrue(atp.info_hash.is_all_zeros())
        self.assertTrue(atp.info_hashes.v1.is_all_zeros())
        self.assertTrue(atp.info_hashes.v2.is_all_zeros())

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_http_seeds_deprecated(self) -> None:
        atp = lt.add_torrent_params()
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(atp.http_seeds, [])
        with self.assertWarns(DeprecationWarning):
            atp.http_seeds = ["http://example.com/seed"]

    def test_http_seeds(self) -> None:
        atp = lt.add_torrent_params()
        atp.http_seeds = ["http://example.com/seed"]
        self.assertEqual(atp.http_seeds, ["http://example.com/seed"])

    def test_unfinished_pieces(self) -> None:
        atp = lt.add_torrent_params()
        atp.unfinished_pieces = {}
        atp.unfinished_pieces = {1: [True, False]}

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_merkle_tree_deprecated(self) -> None:
        atp = lt.add_torrent_params()
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(atp.merkle_tree, [])
        with self.assertWarns(DeprecationWarning):
            atp.merkle_tree = [lt.sha1_hash()]

    def test_merkle_tree(self) -> None:
        atp = lt.add_torrent_params()
        atp.merkle_tree = [lt.sha1_hash()]
        self.assertEqual(atp.merkle_tree, [lt.sha1_hash()])

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_url_deprecated(self) -> None:
        atp = lt.add_torrent_params()
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(atp.url, "")
        with self.assertWarns(DeprecationWarning):
            atp.url = "http://example.com/torrent"

    def test_url(self) -> None:
        atp = lt.add_torrent_params()
        atp.url = "http://example.com/torrent"
        self.assertEqual(atp.url, "http://example.com/torrent")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_resume_data_deprecated(self) -> None:
        atp = lt.add_torrent_params()
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(atp.resume_data, [])
        with self.assertWarns(DeprecationWarning):
            atp.resume_data = ["a"]

    def test_resume_data(self) -> None:
        atp = lt.add_torrent_params()
        atp.resume_data = ["a"]
        self.assertEqual(atp.resume_data, ["a"])

    def test_renamed_files_ascii_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: "test.txt"}
        self.assertEqual(atp.renamed_files, {0: "test.txt"})

    def test_renamed_files_ascii_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: b"test.txt"}  # type: ignore
        self.assertEqual(atp.renamed_files, {0: "test.txt"})

    def test_renamed_files_non_ascii_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: "\u1234.txt"}
        self.assertEqual(atp.renamed_files, {0: "\u1234.txt"})

    def test_renamed_files_non_ascii_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: os.fsencode("\u1234.txt")}  # type: ignore
        self.assertEqual(atp.renamed_files, {0: "\u1234.txt"})

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_renamed_files_surrogate_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: "\udcff.txt"}
        self.assertEqual(atp.renamed_files, {0: "\udcff.txt"})

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_renamed_files_surrogate_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: os.fsencode("\udcff.txt")}  # type: ignore
        self.assertEqual(atp.renamed_files, {0: "\udcff.txt"})

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_renamed_files_non_unicode_str(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: os.fsdecode(b"\xff.txt")}
        self.assertEqual(atp.renamed_files, {0: os.fsdecode("\xff.txt")})

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_renamed_files_non_unicode_bytes(self) -> None:
        atp = lt.add_torrent_params()
        atp.renamed_files = {0: b"\xff.txt"}  # type: ignore
        self.assertEqual(atp.renamed_files, {0: os.fsdecode("\xff.txt")})


class EnumsTest(unittest.TestCase):
    def test_storage_mode_t(self) -> None:
        self.assertIsInstance(lt.storage_mode_t.storage_mode_allocate, int)
        self.assertIsInstance(lt.storage_mode_t.storage_mode_sparse, int)

    def test_options_t(self) -> None:
        self.assertIsInstance(lt.options_t.delete_files, int)

    def test_session_flags_t(self) -> None:
        self.assertIsInstance(lt.session_flags_t.paused, int)
        if lt.api_version < 3:
            self.assertIsInstance(lt.session_flags_t.add_default_plugins, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.session_flags_t.start_default_features, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_session_flags_t_deprecated(self) -> None:
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.session_flags_t.add_default_plugins, int)
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.session_flags_t.start_default_features, int)

    def test_torrent_flags(self) -> None:
        self.assertIsInstance(lt.torrent_flags.seed_mode, int)
        self.assertIsInstance(lt.torrent_flags.upload_mode, int)
        self.assertIsInstance(lt.torrent_flags.share_mode, int)
        self.assertIsInstance(lt.torrent_flags.apply_ip_filter, int)
        self.assertIsInstance(lt.torrent_flags.paused, int)
        self.assertIsInstance(lt.torrent_flags.auto_managed, int)
        self.assertIsInstance(lt.torrent_flags.duplicate_is_error, int)
        self.assertIsInstance(lt.torrent_flags.update_subscribe, int)
        self.assertIsInstance(lt.torrent_flags.super_seeding, int)
        self.assertIsInstance(lt.torrent_flags.sequential_download, int)
        self.assertIsInstance(lt.torrent_flags.stop_when_ready, int)
        if lt.api_version < 4:
            self.assertIsInstance(lt.torrent_flags.override_trackers, int)
            self.assertIsInstance(lt.torrent_flags.override_web_seeds, int)
        self.assertIsInstance(lt.torrent_flags.disable_dht, int)
        self.assertIsInstance(lt.torrent_flags.disable_lsd, int)
        self.assertIsInstance(lt.torrent_flags.disable_pex, int)
        self.assertIsInstance(lt.torrent_flags.no_verify_files, int)
        self.assertIsInstance(lt.torrent_flags.default_flags, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_atp_flags_t_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.add_torrent_params_flags_t.default_flags, int)

    def test_atp_flags_t(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.add_torrent_params_flags_t.flag_seed_mode, int)
            self.assertIsInstance(lt.add_torrent_params_flags_t.flag_upload_mode, int)
            self.assertIsInstance(lt.add_torrent_params_flags_t.flag_share_mode, int)
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_apply_ip_filter, int
            )
            self.assertIsInstance(lt.add_torrent_params_flags_t.flag_paused, int)
            self.assertIsInstance(lt.add_torrent_params_flags_t.flag_auto_managed, int)
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_duplicate_is_error, int
            )
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_update_subscribe, int
            )
            self.assertIsInstance(lt.add_torrent_params_flags_t.flag_super_seeding, int)
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_sequential_download, int
            )
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_stop_when_ready, int
            )
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_override_trackers, int
            )
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_override_web_seeds, int
            )
            self.assertIsInstance(lt.add_torrent_params_flags_t.flag_pinned, int)
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_override_resume_data, int
            )
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_merge_resume_trackers, int
            )
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_use_resume_save_path, int
            )
            self.assertIsInstance(
                lt.add_torrent_params_flags_t.flag_merge_resume_http_seeds, int
            )
            self.assertIsInstance(lt.add_torrent_params_flags_t.default_flags, int)

    def test_portmap_protocol(self) -> None:
        self.assertIsInstance(lt.portmap_protocol.none, int)
        self.assertIsInstance(lt.portmap_protocol.udp, int)
        self.assertIsInstance(lt.portmap_protocol.tcp, int)

    def test_portmap_transport(self) -> None:
        self.assertIsInstance(lt.portmap_transport.natpmp, int)
        self.assertIsInstance(lt.portmap_transport.upnp, int)

    def test_peer_class_type_filter_socket_type_t(self) -> None:
        pctfst = lt.peer_class_type_filter_socket_type_t
        self.assertIsInstance(pctfst.tcp_socket, int)
        self.assertIsInstance(pctfst.utp_socket, int)
        self.assertIsInstance(pctfst.ssl_tcp_socket, int)
        self.assertIsInstance(pctfst.ssl_utp_socket, int)
        self.assertIsInstance(pctfst.i2p_socket, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_protocol_type_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.protocol_type.udp, int)

    def test_protocol_type(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.protocol_type.udp, int)
            self.assertIsInstance(lt.protocol_type.tcp, int)

    def test_save_state_flags_t(self) -> None:
        self.assertIsInstance(lt.save_state_flags_t.all, int)
        self.assertIsInstance(lt.save_state_flags_t.save_settings, int)
        self.assertIsInstance(lt.save_state_flags_t.save_dht_state, int)
        if lt.api_version < 3:
            self.assertIsInstance(lt.save_state_flags_t.save_dht_settings, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.save_state_flags_t.save_encryption_settings, int)
            self.assertIsInstance(lt.save_state_flags_t.save_as_map, int)
            self.assertIsInstance(lt.save_state_flags_t.save_i2p_proxy, int)
            self.assertIsInstance(lt.save_state_flags_t.save_proxy, int)
            self.assertIsInstance(lt.save_state_flags_t.save_dht_proxy, int)
            self.assertIsInstance(lt.save_state_flags_t.save_peer_proxy, int)
            self.assertIsInstance(lt.save_state_flags_t.save_web_proxy, int)
            self.assertIsInstance(lt.save_state_flags_t.save_tracker_proxy, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_save_state_flags_t_deprecated(self) -> None:
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_dht_settings, int)
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    lt.save_state_flags_t.save_encryption_settings, int
                )
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_as_map, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_i2p_proxy, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_proxy, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_dht_proxy, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_peer_proxy, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_web_proxy, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.save_state_flags_t.save_tracker_proxy, int)

    def test_listen_on_flags_t(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.listen_on_flags_t.listen_reuse_address, int)
            self.assertIsInstance(lt.listen_on_flags_t.listen_no_system_port, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_listen_on_flags_t_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.listen_on_flags_t.listen_reuse_address, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.listen_on_flags_t.listen_no_system_port, int)

    def test_metric_type_t(self) -> None:
        self.assertIsInstance(lt.metric_type_t.counter, int)
        self.assertIsInstance(lt.metric_type_t.gauge, int)

    def test_session_static_vars(self) -> None:
        self.assertIsInstance(lt.session.tcp, int)
        self.assertIsInstance(lt.session.udp, int)

        self.assertIsInstance(lt.session.global_peer_class_id, int)
        self.assertIsInstance(lt.session.tcp_peer_class_id, int)
        self.assertIsInstance(lt.session.local_peer_class_id, int)

        self.assertIsInstance(lt.session.reopen_map_ports, int)

        self.assertIsInstance(lt.session.delete_files, int)
        self.assertIsInstance(lt.session.delete_partfile, int)

    def test_announce_flags_t(self) -> None:
        self.assertIsInstance(lt.announce_flags_t.seed, int)
        self.assertIsInstance(lt.announce_flags_t.implied_port, int)
        self.assertIsInstance(lt.announce_flags_t.ssl_torrent, int)


class PeerClassTypeFilterTest(unittest.TestCase):
    def test_filter(self) -> None:
        pctf = lt.peer_class_type_filter()
        tcp_socket = lt.peer_class_type_filter_socket_type_t.tcp_socket
        pctf.add(tcp_socket, 1)
        pctf.remove(tcp_socket, 1)
        pctf.allow(tcp_socket, 1)
        pctf.disallow(tcp_socket, 1)
        self.assertEqual(pctf.apply(tcp_socket, 1), 1)

    def test_enum_values(self) -> None:
        self.assertIsInstance(lt.peer_class_type_filter.tcp_socket, int)
        self.assertIsInstance(lt.peer_class_type_filter.utp_socket, int)
        self.assertIsInstance(lt.peer_class_type_filter.ssl_tcp_socket, int)
        self.assertIsInstance(lt.peer_class_type_filter.ssl_utp_socket, int)
        self.assertIsInstance(lt.peer_class_type_filter.i2p_socket, int)


class ResumeDataTest(unittest.TestCase):
    def do_test_round_trip(self, atp: lt.add_torrent_params) -> None:
        first = lt.write_resume_data(atp)
        second = lt.write_resume_data(
            lt.read_resume_data(lt.write_resume_data_buf(atp))
        )

        self.assertEqual(first, second)

    def test_round_trip(self) -> None:
        atp = lt.add_torrent_params()
        atp.name = "test"
        self.do_test_round_trip(atp)

    def test_limit_decode_depth(self) -> None:
        atp = lt.add_torrent_params()
        buf = lt.write_resume_data_buf(atp)
        with self.assertRaises(RuntimeError):
            lt.read_resume_data(buf, {"max_decode_depth": 1})

    def test_limit_decode_tokens(self) -> None:
        atp = lt.add_torrent_params()
        buf = lt.write_resume_data_buf(atp)
        with self.assertRaises(RuntimeError):
            lt.read_resume_data(buf, {"max_decode_tokens": 1})

    def test_limit_pieces(self) -> None:
        atp = lt.add_torrent_params()
        atp.ti = lt.torrent_info(
            {
                b"info": {
                    b"name": b"test.txt",
                    b"length": 1234000,
                    b"piece length": 16384,
                    b"pieces": b"aaaaaaaaaaaaaaaaaaaa" * (1234000 // 16384 + 1),
                }
            }
        )
        buf = lt.write_resume_data_buf(atp)
        with self.assertRaises(RuntimeError):
            lt.read_resume_data(buf, {"max_pieces": 1})


class ConstructorTest(unittest.TestCase):
    # A bunch of these technically break isolation, but they need to be tested.
    # We compensate by immediately setting isolated settings

    def test_args(self) -> None:
        # no-args

        session = lt.session()
        session.apply_settings(lib.get_isolated_settings())

        # positional args

        lt.session(lib.get_isolated_settings())
        lt.session(lib.get_isolated_settings(), 0)

        # kwargs

        lt.session(settings=lib.get_isolated_settings())

        if lt.api_version < 2:
            session = lt.session(flags=0)
        else:
            session = lt.session()
        session.apply_settings(lib.get_isolated_settings())

        lt.session(settings=lib.get_isolated_settings(), flags=0)

    def test_invalid_settings(self) -> None:
        with self.assertRaises(TypeError):
            lt.session({"alert_mask": "not-an-int"})
        with self.assertRaises(KeyError):
            lt.session({"not-a-setting": 123})

    def test_predefined_settings_packs(self) -> None:
        session = lt.session(lt.default_settings())
        session.apply_settings(lib.get_isolated_settings())

        session = lt.session(lt.high_performance_seed())
        session.apply_settings(lib.get_isolated_settings())

        session = lt.session(lt.min_memory_usage())
        session.apply_settings(lib.get_isolated_settings())

    def test_fingerprint(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                fingerprint = lt.fingerprint("AB", 1, 2, 3, 4)

            session = lt.session(fingerprint)
            session.apply_settings(lib.get_isolated_settings())

            session = lt.session(fingerprint, 0)
            session.apply_settings(lib.get_isolated_settings())

            session = lt.session(fingerprint, 0, 0)
            session.apply_settings(lib.get_isolated_settings())

            session = lt.session(fingerprint=fingerprint)
            session.apply_settings(lib.get_isolated_settings())

            session = lt.session(fingerprint=fingerprint, flags=0)
            session.apply_settings(lib.get_isolated_settings())

            session = lt.session(fingerprint=fingerprint, flags=0, alert_mask=0)
            session.apply_settings(lib.get_isolated_settings())

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_fingerprint_deprecated(self) -> None:
        if lt.api_version < 2:
            fingerprint = lt.fingerprint("AB", 1, 2, 3, 4)
            with self.assertWarns(DeprecationWarning):
                session = lt.session(fingerprint)
                session.apply_settings(lib.get_isolated_settings())
            with self.assertWarns(DeprecationWarning):
                session = lt.session(fingerprint, 0)
                session.apply_settings(lib.get_isolated_settings())
            with self.assertWarns(DeprecationWarning):
                session = lt.session(fingerprint, 0, 0)
                session.apply_settings(lib.get_isolated_settings())
            with self.assertWarns(DeprecationWarning):
                session = lt.session(fingerprint=fingerprint)
                session.apply_settings(lib.get_isolated_settings())
            with self.assertWarns(DeprecationWarning):
                session = lt.session(fingerprint=fingerprint, flags=0)
                session.apply_settings(lib.get_isolated_settings())
            with self.assertWarns(DeprecationWarning):
                session = lt.session(fingerprint=fingerprint, flags=0, alert_mask=0)
                session.apply_settings(lib.get_isolated_settings())


class DhtTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_functions(self) -> None:
        self.assertFalse(self.session.is_dht_running())

        # Should be "pretty safe" for isolation purposes
        endpoint = ("127.1.2.3", 65535)

        self.session.add_dht_node(endpoint)

        sha1 = lt.sha1_hash(b"a" * 20)

        self.session.dht_get_immutable_item(sha1)
        self.session.dht_get_mutable_item(b"a" * 32, b"salt")
        self.assertIsInstance(
            self.session.dht_put_immutable_item(b"test"), lt.sha1_hash
        )
        self.assertIsInstance(self.session.dht_put_immutable_item(12345), lt.sha1_hash)
        self.assertIsInstance(
            self.session.dht_put_immutable_item({b"a": 1}), lt.sha1_hash
        )
        self.assertIsInstance(
            self.session.dht_put_immutable_item([1, 2, 3]), lt.sha1_hash
        )
        self.session.dht_put_mutable_item(b"a" * 64, b"b" * 32, b"data", b"salt")
        self.session.dht_get_peers(sha1)
        self.session.dht_announce(sha1)
        self.session.dht_announce(sha1, 0, 0)
        self.session.dht_announce(sha1, port=0, flags=lt.announce_flags_t.seed)
        self.session.dht_live_nodes(sha1)
        self.session.dht_sample_infohashes(endpoint, sha1)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.session.add_dht_router(*endpoint)

        if lt.api_version < 3:
            dht_settings = self.session.get_dht_settings()
            self.session.set_dht_settings(dht_settings)

    def test_bad_args(self) -> None:
        with self.assertRaises(ValueError):
            self.session.dht_get_mutable_item(b"short", b"salt")
        with self.assertRaises(ValueError):
            self.session.dht_put_mutable_item(b"short", b"b" * 32, b"data", b"salt")
        with self.assertRaises(ValueError):
            self.session.dht_put_mutable_item(b"a" * 64, b"short", b"data", b"salt")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.session.get_dht_settings()
        with self.assertWarns(DeprecationWarning):
            lt.dht_settings()
        dht_settings = lt.dht_settings()
        with self.assertWarns(DeprecationWarning):
            self.session.set_dht_settings(dht_settings)
        with self.assertWarns(DeprecationWarning):
            self.session.dht_get_mutable_item("a" * 32, "salt")  # type: ignore
        with self.assertWarns(DeprecationWarning):
            self.session.dht_put_mutable_item(
                "a" * 64,  # type: ignore
                "b" * 32,  # type: ignore
                "data",  # type: ignore
                "salt",  # type: ignore
            )

    def test_dht_lookup(self) -> None:
        if lt.api_version < 2:
            lookup = lt.dht_lookup()
            self.assertIsInstance(lookup.branch_factor, int)
            self.assertIsInstance(lookup.outstanding_requests, int)
            self.assertIsInstance(lookup.response, int)
            self.assertIsInstance(lookup.timeouts, int)
            self.assertIsNone(lookup.type)  # Should be a str, for real lookups


class AlertHandlingTest(unittest.TestCase):
    def setUp(self) -> None:
        settings = lib.get_isolated_settings()
        settings["alert_mask"] = 0
        self.session = lt.session(settings)

    def test_wait_and_pop(self) -> None:
        # wait_for_alert() shouldn't return anything with no pending alerts
        self.assertIsNone(self.session.wait_for_alert(0))

        # Force an alert to fire
        self.session.post_torrent_updates()
        alert = self.session.wait_for_alert(10000)
        self.assertIsInstance(alert, lt.state_update_alert)
        alerts = self.session.pop_alerts()
        self.assertEqual(len(alerts), 1)
        self.assertIsInstance(alerts[0], lt.state_update_alert)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_set_alert_notify_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.session.set_alert_notify(lambda: None)

    @unittest.skipIf(sys.platform == "win32", "windows doesn't support pipes")
    def test_set_alert_fd_pipe(self) -> None:
        # Redundant sys.platform checks are to help mypy
        r, w = os.pipe()
        # Should always be non-blocking, or we'll block the event loop
        if sys.platform != "win32":
            os.set_blocking(w, False)

        self.session.set_alert_fd(w)

        # Pipe should initially be empty
        if sys.platform != "win32":
            os.set_blocking(r, False)
        with self.assertRaises(BlockingIOError):
            os.read(r, 1024)
        if sys.platform != "win32":
            os.set_blocking(r, True)

        # Force an alert to fire
        self.session.post_torrent_updates()

        # We should have data shortly
        data = os.read(r, 1024)
        self.assertGreater(len(data), 0)

    def test_set_alert_fd_socket(self) -> None:
        r, w = socket.socketpair()
        # Should always be non-blocking, or we'll block the event loop
        w.setblocking(False)

        self.session.set_alert_fd(w.fileno())

        # Pipe should initially be empty
        r.setblocking(False)
        with self.assertRaises(BlockingIOError):
            r.recv(1024)
        r.setblocking(True)

        # Force an alert to fire
        self.session.post_torrent_updates()

        # Should now have data
        data = r.recv(1024)
        self.assertGreater(len(data), 0)

        # Explicit cleanup to avoid ResourceWarning
        r.close()
        w.close()


class Test5155(unittest.TestCase):
    # We attempt to force a torrent_info to be allocated in python, but
    # released from C++.
    # See https://github.com/arvidn/libtorrent/issues/5155
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        settings = lib.get_isolated_settings()
        settings["alert_mask"] = 0
        self.session = lt.session(settings)

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.dir, timeout=5)

    def test_5155(self) -> None:
        # Ensure the torrent_info only is referenced by function locals
        def do_add() -> lt.torrent_handle:
            torrent = tdummy.Torrent.single_file(
                piece_length=16384, name=b"test.txt", length=16384 * 9 + 1000
            )
            atp = lt.add_torrent_params()
            atp.ti = torrent.torrent_info()
            atp.save_path = "."
            return self.session.add_torrent(atp)

        handle = do_add()
        # The bug depends on this exact sequence of alert management. I don't
        # have a good explanation for why
        self.session.wait_for_alert(10000)  # add_torrent_alert
        self.session.pop_alerts()
        self.session.remove_torrent(handle)
        self.session.wait_for_alert(10000)  # torrent_removed_alert
        self.session.pop_alerts()


class PostAlertsTest(unittest.TestCase):
    def setUp(self) -> None:
        settings = lib.get_isolated_settings()
        settings["alert_mask"] = 0
        self.session = lt.session(settings)

    def test_post_torrent_updates(self) -> None:
        # no args
        self.session.post_torrent_updates()
        self.assertIsInstance(self.session.wait_for_alert(10000), lt.state_update_alert)

        self.session.pop_alerts()
        # positional args
        self.session.post_torrent_updates(0)
        self.assertIsInstance(self.session.wait_for_alert(10000), lt.state_update_alert)

        self.session.pop_alerts()
        # kwargs
        self.session.post_torrent_updates(flags=0)
        self.assertIsInstance(self.session.wait_for_alert(10000), lt.state_update_alert)

    def test_post_dht_stats(self) -> None:
        self.session.post_dht_stats()
        self.assertIsInstance(self.session.wait_for_alert(10000), lt.dht_stats_alert)


class AddTorrentTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())
        self.torrent = tdummy.get_default()
        self.dir = tempfile.TemporaryDirectory()

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.dir, timeout=5)

    def test_old_style_with_wrong_args(self) -> None:
        with self.assertRaises(TypeError):
            self.session.add_torrent(  # type: ignore
                self.torrent.torrent_info(),
                resume_data=None,
            )

    def test_old_style(self) -> None:
        if lt.api_version < 2:
            ti = self.torrent.torrent_info()
            # positional args
            with self.assertWarns(DeprecationWarning):
                handle = self.session.add_torrent(ti, self.dir.name)
            self.assertIsInstance(handle, lt.torrent_handle)
            self.assertTrue(handle.is_valid())
            self.assertEqual(handle.status().save_path, self.dir.name)

            with self.assertWarns(DeprecationWarning):
                handle = self.session.add_torrent(
                    ti,
                    self.dir.name,
                    None,
                    lt.storage_mode_t.storage_mode_sparse,
                    False,
                )
            self.assertIsInstance(handle, lt.torrent_handle)
            self.assertTrue(handle.is_valid())

            # kwargs
            with self.assertWarns(DeprecationWarning):
                handle = self.session.add_torrent(
                    ti,
                    self.dir.name,
                    resume_data=None,
                    storage_mode=lt.storage_mode_t.storage_mode_sparse,
                    paused=False,
                )
            self.assertIsInstance(handle, lt.torrent_handle)
            self.assertTrue(handle.is_valid())

    def test_atp(self) -> None:
        atp = self.torrent.atp()
        atp.save_path = self.dir.name

        self.session.async_add_torrent(atp)
        handle = self.session.add_torrent(atp)
        self.assertIsInstance(handle, lt.torrent_handle)
        self.assertTrue(handle.is_valid())

    def test_dict_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            atp = {"save_path": ".", "ti": self.torrent.torrent_info()}
            self.session.add_torrent(atp)
        with self.assertWarns(DeprecationWarning):
            atp = {"save_path": ".", "ti": self.torrent.torrent_info()}
            self.session.async_add_torrent(atp)

    def do_test_dict(self, params: Dict[str, Any]) -> lt.torrent_handle:
        with self.assertWarns(DeprecationWarning):
            self.session.async_add_torrent(params)
        with self.assertWarns(DeprecationWarning):
            handle = self.session.add_torrent(params)
        self.assertIsInstance(handle, lt.torrent_handle)
        self.assertTrue(handle.is_valid())
        return handle

    def test_dict(self) -> None:
        ti = self.torrent.torrent_info()
        atp = {
            "ti": ti,
            "info_hashes": ti.info_hashes().v1.to_bytes(),
            "save_path": self.dir.name,
            "storage_mode": lt.storage_mode_t.storage_mode_allocate,
            "trackers": ["http://127.1.2.1/tr"],
            "url_seeds": ["http://127.1.2.2/us"],
            "http_seeds": ["http://127.1.2.3/hs"],
            "dht_nodes": [("127.1.2.4", 1234)],
            "banned_peers": [("127.1.2.5", 1234)],
            "peers": [("127.1.2.6", 1234)],
            "flags": lt.torrent_flags.sequential_download,
            "trackerid": "trackerid",
            "renamed_files": {0: "renamed.txt"},
            "file_priorities": [2],
        }

        if lt.api_version < 4:
            atp["info_hash"] = ti.info_hashes().v1.to_bytes()

        if lt.api_version < 2:
            atp["url"] = "http://127.1.2.7/u"

        handle = self.do_test_dict(atp)

        status = handle.status()
        self.assertEqual(handle.status().save_path, self.dir.name)
        self.assertEqual(status.storage_mode, lt.storage_mode_t.storage_mode_allocate)
        self.assertEqual(
            [tr["url"] for tr in handle.trackers()], ["http://127.1.2.1/tr"]
        )
        self.assertEqual(handle.url_seeds(), ["http://127.1.2.2/us"])
        # TODO: can we test dht nodes?
        # TODO: can we test banned_nodes?
        # TODO: why is this sometimes 2, sometimes 1?
        self.assertIn(status.list_peers, (1, 2))
        self.assertEqual(handle.flags(), lt.torrent_flags.sequential_download)
        # TODO: can we test trackerid?
        torrent_file = handle.torrent_file()
        assert torrent_file is not None
        # the torrent_info object is immutable now, and renamed files are
        # recorded in a separate object
        # self.assertEqual(torrent_file.files().file_path(0), "renamed.txt")
        self.assertEqual(handle.get_file_priorities(), [2])

    def test_dict_no_torrent_info_old(self) -> None:
        self.do_test_dict({"info_hashes": b"a" * 20, "save_path": self.dir.name})

    def test_no_torrent_info_old(self) -> None:
        atp = lt.add_torrent_params()
        atp.info_hashes = lt.info_hash_t(lt.sha1_hash(b"a" * 20))
        atp.save_path = self.dir.name
        self.session.add_torrent(atp)

    def test_dict_name(self) -> None:
        # This can only be tested *without* torrent info
        handle = self.do_test_dict(
            {"info_hashes": b"a" * 20, "save_path": self.dir.name, "name": "test-name"}
        )

        self.assertEqual(handle.status().name, "test-name")

    def test_dict_no_torrent_info_sha1(self) -> None:
        handle = self.do_test_dict(
            {"info_hashes": b"a" * 20, "save_path": self.dir.name}
        )
        self.assertEqual(handle.info_hashes().v1.to_bytes(), b"a" * 20)

    def test_dict_no_torrent_info_sha256(self) -> None:
        handle = self.do_test_dict(
            {"info_hashes": b"a" * 32, "save_path": self.dir.name}
        )

        self.assertEqual(handle.info_hashes().v2.to_bytes(), b"a" * 32)

    def test_dict_errors(self) -> None:
        with self.assertWarns(DeprecationWarning):
            with self.assertRaises(KeyError):
                self.session.add_torrent({"invalid-key": None})
        with self.assertWarns(DeprecationWarning):
            with self.assertRaises(KeyError):
                self.session.async_add_torrent({"invalid-key": None})

    def test_errors(self) -> None:
        atp = self.torrent.atp()
        atp.save_path = self.dir.name
        atp.flags |= lt.torrent_flags.duplicate_is_error

        self.session.add_torrent(atp)
        with self.assertRaises(RuntimeError):
            self.session.add_torrent(atp)


class StateTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def check_state(self, state: Dict[bytes, Any]) -> None:
        self.assertIsInstance(state[b"settings"], dict)
        self.assertIsInstance(state[b"dht"], dict)
        # we disable dht, so we don't expect b"dht state"

    def test_save(self) -> None:
        if lt.api_version < 3:
            self.check_state(self.session.save_state())
            self.check_state(self.session.save_state(flags=2**32 - 1))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.session.save_state()
        state = self.session.save_state()
        with self.assertWarns(DeprecationWarning):
            self.session.load_state(state)

    def test_load(self) -> None:
        state = self.session.save_state()
        self.session.load_state(state)
        self.session.load_state(state, flags=2**32 - 1)


class GetTorrentsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())
        self.dir = tempfile.TemporaryDirectory()
        self.torrent = tdummy.get_default()
        atp = self.torrent.atp()
        atp.save_path = self.dir.name
        self.handle = self.session.add_torrent(atp)

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.dir, timeout=5)

    def test_get_torrents(self) -> None:
        torrents = self.session.get_torrents()
        self.assertEqual(torrents, [self.handle])

    def test_find_torrent_sha1(self) -> None:
        handle = self.session.find_torrent(self.torrent.sha1_hash)
        self.assertEqual(handle, self.handle)


class PauseTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_pause(self) -> None:
        self.assertFalse(self.session.is_paused())
        self.session.pause()
        self.assertTrue(self.session.is_paused())
        self.session.resume()
        self.assertFalse(self.session.is_paused())


class ComponentsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_start_stop(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.session.start_upnp()
            with self.assertWarns(DeprecationWarning):
                self.session.stop_upnp()
            with self.assertWarns(DeprecationWarning):
                self.session.start_lsd()
            with self.assertWarns(DeprecationWarning):
                self.session.stop_lsd()
            with self.assertWarns(DeprecationWarning):
                self.session.start_natpmp()
            with self.assertWarns(DeprecationWarning):
                self.session.stop_natpmp()
            with self.assertWarns(DeprecationWarning):
                self.session.start_dht()
            with self.assertWarns(DeprecationWarning):
                self.session.stop_dht()


class PortsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_ports(self) -> None:
        # NB: this typically is a no-op with our isolated settings, so we
        # may not test the case where it actually works
        tags = self.session.add_port_mapping(lt.portmap_protocol.tcp, 65535, 65535)
        for tag in tags:
            self.session.delete_port_mapping(tag)
        self.session.delete_port_mapping(12345)
        self.session.reopen_network_sockets(0)
        self.assertTrue(self.session.is_listening())
        self.assertIsInstance(self.session.listen_port(), int)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.session.outgoing_ports(1024, 65535)

            # not sure how to correctly test str arg, because it raises
            # RuntimeError on an invalid interface

            # positional args
            with self.assertWarns(DeprecationWarning):
                self.session.listen_on(1024, 65535)
            with self.assertWarns(DeprecationWarning):
                self.session.listen_on(1024, 65535, None)
            with self.assertWarns(DeprecationWarning):
                self.session.listen_on(
                    1024, 65535, None, lt.listen_on_flags_t.listen_no_system_port
                )

            # kwargs
            with self.assertWarns(DeprecationWarning):
                self.session.listen_on(1024, 65535)
            with self.assertWarns(DeprecationWarning):
                self.session.listen_on(1024, 65535, interface=None)
            with self.assertWarns(DeprecationWarning):
                self.session.listen_on(
                    1024, 65535, flags=lt.listen_on_flags_t.listen_no_system_port
                )

            with self.assertWarns(DeprecationWarning):
                with self.assertRaises(RuntimeError):
                    self.session.listen_on(1024, 65535, "interface-does-not-exist")


class SettingsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def do_test_settings_pack(self, settings: Dict[str, Any]) -> None:
        settings = {**settings, **lib.get_isolated_settings()}
        self.session.apply_settings(settings)

    def test_settings_packs(self) -> None:
        self.do_test_settings_pack(lt.default_settings())
        self.do_test_settings_pack(lt.high_performance_seed())
        self.do_test_settings_pack(lt.min_memory_usage())

        self.assertIsInstance(self.session.get_settings(), dict)

    def test_old_settings(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.session.local_download_rate_limit(), int)
            with self.assertWarns(DeprecationWarning):
                self.session.set_local_download_rate_limit(0)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.session.local_upload_rate_limit(), int)
            with self.assertWarns(DeprecationWarning):
                self.session.set_local_upload_rate_limit(0)

            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.session.download_rate_limit(), int)
            with self.assertWarns(DeprecationWarning):
                self.session.set_download_rate_limit(0)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.session.upload_rate_limit(), int)
            with self.assertWarns(DeprecationWarning):
                self.session.set_upload_rate_limit(0)

            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.session.max_connections(), int)
            with self.assertWarns(DeprecationWarning):
                self.session.set_max_connections(0)

            with self.assertWarns(DeprecationWarning):
                self.session.set_max_uploads(0)
            with self.assertWarns(DeprecationWarning):
                self.session.set_max_half_open_connections(0)
            with self.assertWarns(DeprecationWarning):
                self.session.set_alert_queue_size_limit(0)
            with self.assertWarns(DeprecationWarning):
                self.session.set_alert_mask(0)

            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    self.session.proxy(), lt.proxy_type_t.proxy_settings
                )
            with self.assertWarns(DeprecationWarning):
                self.session.set_proxy(lt.proxy_type_t.proxy_settings())
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    self.session.dht_proxy(), lt.proxy_type_t.proxy_settings
                )
            with self.assertWarns(DeprecationWarning):
                self.session.set_dht_proxy(lt.proxy_type_t.proxy_settings())
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    self.session.peer_proxy(), lt.proxy_type_t.proxy_settings
                )
            with self.assertWarns(DeprecationWarning):
                self.session.set_peer_proxy(lt.proxy_type_t.proxy_settings())
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    self.session.tracker_proxy(), lt.proxy_type_t.proxy_settings
                )
            with self.assertWarns(DeprecationWarning):
                self.session.set_tracker_proxy(lt.proxy_type_t.proxy_settings())
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    self.session.web_seed_proxy(), lt.proxy_type_t.proxy_settings
                )
            with self.assertWarns(DeprecationWarning):
                self.session.set_web_seed_proxy(lt.proxy_type_t.proxy_settings())
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    self.session.i2p_proxy(), lt.proxy_type_t.proxy_settings
                )
            with self.assertWarns(DeprecationWarning):
                self.session.set_i2p_proxy(lt.proxy_type_t.proxy_settings())


class PeSettingsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_pe_settings(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.session.get_pe_settings(), lt.pe_settings)
            with self.assertWarns(DeprecationWarning):
                self.session.set_pe_settings(lt.pe_settings())


class IpFilterTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_ip_filter(self) -> None:
        self.assertIsInstance(self.session.get_ip_filter(), lt.ip_filter)
        self.session.set_ip_filter(lt.ip_filter())


def unlink_all_files(path: str) -> None:
    for dirpath, _, filenames in os.walk(path):
        for filename in filenames:
            os.unlink(os.path.join(dirpath, filename))


class RemoveTorrentTest(unittest.TestCase):
    def setUp(self) -> None:
        settings = lib.get_isolated_settings()
        settings["alert_mask"] = 0
        self.session = lt.session(settings)
        self.dir = tempfile.TemporaryDirectory()
        self.torrent = tdummy.get_default()
        self.file_path = pathlib.Path(self.dir.name) / os.fsdecode(
            self.torrent.files[0].path
        )
        atp = self.torrent.atp()
        atp.save_path = self.dir.name
        self.handle = self.session.add_torrent(atp)

        # add_piece() does not work in the checking_* states
        for _ in lib.loop_until_timeout(5, msg="checking"):
            if self.handle.status().state not in (
                lt.torrent_status.checking_files,
                lt.torrent_status.checking_resume_data,
            ):
                break

        # add synthetic data
        for i, data in enumerate(self.torrent.pieces):
            self.handle.add_piece(i, data, 0)

        # wait until data is written to disk
        for _ in lib.loop_until_timeout(5, msg="file write"):
            if not self.file_path.is_file():
                continue
            if self.file_path.read_bytes() == self.torrent.data:
                break

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.dir, timeout=5)

    def test_remove(self) -> None:
        self.session.remove_torrent(self.handle)
        # There's no good way to synchronize so that we test the data doesn't
        # *eventually* get deleted, but this at least makes the test more
        # readable
        self.assertEqual(self.file_path.read_bytes(), self.torrent.data)

    def test_remove_data(self) -> None:
        self.session.remove_torrent(self.handle, option=self.session.delete_files)
        for _ in lib.loop_until_timeout(5, msg="file delete"):
            if not self.file_path.is_file():
                break


class TorrentStatusTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())
        self.dir = tempfile.TemporaryDirectory()
        self.torrent = tdummy.get_default()
        atp = self.torrent.atp()
        atp.save_path = self.dir.name
        self.handle = self.session.add_torrent(atp)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6008")
    def test_get_torrents_status(self) -> None:
        # NB: the predicate function may suffer from being being "owned" by
        # libtorrent and inappropriately freed. Test against this by _not_
        # keeping a reference to the functions here

        status_list = self.session.get_torrent_status(lambda _: True)
        self.assertEqual(len(status_list), 1)
        status = status_list[0]
        # flags should default to 0, so no pieces
        self.assertEqual(status.pieces, [])

        status_list = self.session.get_torrent_status(
            lambda _: True, flags=lt.status_flags_t.query_pieces
        )
        status = status_list[0]
        self.assertGreater(len(status.pieces), 0)

        status_list = self.session.get_torrent_status(lambda _: False)
        self.assertEqual(len(status_list), 0)

        with self.assertRaises(TypeError):
            self.session.get_torrent_status(None)  # type: ignore
        with self.assertRaises(TypeError):
            self.session.get_torrent_status(lambda: True)  # type: ignore

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6008")
    def test_refresh_torrent_status(self) -> None:
        status_list = self.session.get_torrent_status(lambda _: True)
        updated_list = self.session.refresh_torrent_status(status_list)
        self.assertEqual(len(updated_list), 1)


class FieldsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_peer_id(self) -> None:
        sha1 = lt.sha1_hash(b"a" * 20)
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.session.set_peer_id(sha1)
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(self.session.id(), sha1)

    def test_num_connections(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.session.num_connections(), int)


class PeerClassTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_peer_class_filter(self) -> None:
        self.session.set_peer_class_filter(lt.ip_filter())

    def test_peer_class_type_filter(self) -> None:
        self.session.set_peer_class_type_filter(lt.peer_class_type_filter())

    def test_create_delete_peer_class(self) -> None:
        pc = self.session.create_peer_class("test")
        self.session.delete_peer_class(pc)

    def test_get_set_peer_class(self) -> None:
        pci = self.session.get_peer_class(0)
        self.assertIsInstance(pci["ignore_unchoke_slots"], bool)
        self.assertIsInstance(pci["connection_limit_factor"], int)
        self.assertIsInstance(pci["label"], str)
        self.assertIsInstance(pci["upload_limit"], int)
        self.assertIsInstance(pci["download_limit"], int)
        self.assertIsInstance(pci["upload_priority"], int)
        self.assertIsInstance(pci["download_priority"], int)

        self.session.set_peer_class(0, pci)


class ExtensionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())

    def test_extensions(self) -> None:
        self.session.add_extension(lt.create_smart_ban_plugin)
        self.session.add_extension(lt.create_ut_metadata_plugin)
        self.session.add_extension(lt.create_ut_pex_plugin)

        # Should not raise an error
        self.session.add_extension("does-not-exist")

        # TODO: can we test whether extensions are loaded?


class SessionStatsMetricsTest(unittest.TestCase):
    def test_session_stats_metrics(self) -> None:
        metrics = lt.session_stats_metrics()
        self.assertGreater(len(metrics), 0)
        metric = metrics[0]
        self.assertIsInstance(metric.name, str)
        self.assertIsInstance(metric.type, lt.metric_type_t)
        self.assertIsInstance(metric.value_index, int)

        self.assertEqual(lt.find_metric_idx(metric.name), 0)

        self.assertLess(lt.find_metric_idx("does-not-exist"), 0)


class SessionStateTest(unittest.TestCase):
    def setUp(self) -> None:
        self.settings = lib.get_isolated_settings()
        self.session = lt.session(self.settings)

    def test_session_state(self) -> None:
        # By default, everything should be included
        params = self.session.session_state()
        self.assertLessEqual(set(self.settings.items()), set(params.settings.items()))

        # With flags=0, nothing should be included
        params = self.session.session_state(flags=0)
        self.assertEqual(params.settings, lt.session_params().settings)
