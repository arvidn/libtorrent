import os
import pathlib
import random
import tempfile
from typing import Any
from typing import Callable
from typing import Dict
import unittest

import libtorrent as lt

from . import lib
from . import tdummy


class EnumTest(unittest.TestCase):
    def test_move_flags_t(self) -> None:
        self.assertIsInstance(lt.move_flags_t.always_replace_files, int)
        self.assertIsInstance(lt.move_flags_t.fail_if_exist, int)
        self.assertIsInstance(lt.move_flags_t.dont_replace, int)

    def test_deprecated_move_flags_t(self) -> None:
        self.assertIsInstance(lt.deprecated_move_flags_t.always_replace_files, int)
        self.assertIsInstance(lt.deprecated_move_flags_t.fail_if_exist, int)
        self.assertIsInstance(lt.deprecated_move_flags_t.dont_replace, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_depr_is_depr(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.deprecated_move_flags_t.always_replace_files, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.deprecated_move_flags_t.fail_if_exist, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.deprecated_move_flags_t.dont_replace, int)

    def test_static_vars(self) -> None:
        self.assertIsInstance(lt.torrent_handle.ignore_min_interval, int)
        self.assertIsInstance(lt.torrent_handle.overwrite_existing, int)
        self.assertIsInstance(lt.torrent_handle.piece_granularity, int)
        self.assertIsInstance(lt.torrent_handle.graceful_pause, int)
        self.assertIsInstance(lt.torrent_handle.flush_disk_cache, int)
        self.assertIsInstance(lt.torrent_handle.save_info_dict, int)
        self.assertIsInstance(lt.torrent_handle.only_if_modified, int)
        self.assertIsInstance(lt.torrent_handle.alert_when_available, int)
        self.assertIsInstance(lt.torrent_handle.query_distributed_copies, int)
        self.assertIsInstance(lt.torrent_handle.query_accurate_download_counters, int)
        self.assertIsInstance(lt.torrent_handle.query_last_seen_complete, int)
        self.assertIsInstance(lt.torrent_handle.query_pieces, int)
        self.assertIsInstance(lt.torrent_handle.query_verified_pieces, int)

    def test_file_open_mode(self) -> None:
        self.assertIsInstance(lt.file_open_mode.read_only, int)
        self.assertIsInstance(lt.file_open_mode.write_only, int)
        self.assertIsInstance(lt.file_open_mode.read_write, int)
        self.assertIsInstance(lt.file_open_mode.rw_mask, int)
        self.assertIsInstance(lt.file_open_mode.sparse, int)
        self.assertIsInstance(lt.file_open_mode.no_atime, int)
        self.assertIsInstance(lt.file_open_mode.random_access, int)
        self.assertIsInstance(lt.file_open_mode.locked, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_file_open_mode_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.file_open_mode.locked, int)

    def test_file_progress_flags_t(self) -> None:
        self.assertIsInstance(lt.file_progress_flags_t.piece_granularity, int)

    def test_add_piece_flags_t(self) -> None:
        self.assertIsInstance(lt.add_piece_flags_t.overwrite_existing, int)

    def test_pause_flags_t(self) -> None:
        self.assertIsInstance(lt.pause_flags_t.graceful_pause, int)

    def test_save_resume_flags_t(self) -> None:
        self.assertIsInstance(lt.save_resume_flags_t.flush_disk_cache, int)
        self.assertIsInstance(lt.save_resume_flags_t.save_info_dict, int)
        self.assertIsInstance(lt.save_resume_flags_t.only_if_modified, int)

    def test_reannounce_flags_t(self) -> None:
        self.assertIsInstance(lt.reannounce_flags_t.ignore_min_interval, int)

    def test_deadline_flags_t(self) -> None:
        self.assertIsInstance(lt.deadline_flags_t.alert_when_available, int)

    def test_status_flags_t(self) -> None:
        self.assertIsInstance(lt.status_flags_t.query_distributed_copies, int)
        self.assertIsInstance(lt.status_flags_t.query_accurate_download_counters, int)
        self.assertIsInstance(lt.status_flags_t.query_last_seen_complete, int)
        self.assertIsInstance(lt.status_flags_t.query_pieces, int)
        self.assertIsInstance(lt.status_flags_t.query_verified_pieces, int)


class TorrentHandleTest(unittest.TestCase):
    def setUp(self) -> None:
        # Create a session and add a handle
        self.session = lt.session(lib.get_isolated_settings())
        self.dir = tempfile.TemporaryDirectory()
        self.torrent = tdummy.get_default()
        self.atp = self.torrent.atp()
        self.atp.save_path = self.dir.name
        self.handle = self.session.add_torrent(self.atp)

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.dir, timeout=5)


class DunderTest(TorrentHandleTest):
    def test_eq_and_hash(self) -> None:
        # get_torrents() will return a distinct handle object, but it should
        # compare equally to our existing handle
        other = self.session.get_torrents()[0]
        self.assertEqual(self.handle, other)

        self.assertEqual(hash(self.handle), hash(other))

        # Ensure set membership works
        handles = set((self.handle,))
        self.assertIn(other, handles)

        # Ensure dict key membership works
        handle_dict = {self.handle: 1}
        self.assertEqual(handle_dict[other], 1)

    def test_neq(self) -> None:
        other = lt.torrent_handle()
        self.assertNotEqual(self.handle, other)

    def test_lt(self) -> None:
        other = lt.torrent_handle()
        exactly_one = (self.handle < other) ^ (other < self.handle)
        self.assertTrue(exactly_one)


class PeerTestTest(TorrentHandleTest):
    def test_connect_and_get_info(self) -> None:
        # Create a peer, to ensure get_peer_info returns something
        seed = lt.session(lib.get_isolated_settings())
        seed.apply_settings({"close_redundant_connections": False})
        with tempfile.TemporaryDirectory() as seed_dir:
            atp = self.torrent.atp()
            atp.save_path = seed_dir
            seed.add_torrent(atp)

            self.session.apply_settings({"close_redundant_connections": False})
            self.handle.connect_peer(("127.0.0.1", seed.listen_port()))

            for _ in lib.loop_until_timeout(5, msg="heck"):
                if self.handle.get_peer_info():
                    break

            peer_info = self.handle.get_peer_info()[0]
            self.assertEqual(peer_info.ip, ("127.0.0.1", seed.listen_port()))


class StatusTest(TorrentHandleTest):
    def test_status(self) -> None:
        status = self.handle.status()
        self.assertIsInstance(status, lt.torrent_status)

        # We should query everything by default
        self.assertNotEqual(status.pieces, [])

        # Specify flags
        status = self.handle.status(flags=0)
        self.assertEqual(status.pieces, [])

    def test_alternate_methods(self) -> None:
        self.assertFalse(self.handle.have_piece(0))

    def test_deprecated_methods(self) -> None:
        status = self.handle.status()

        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.handle.name(), status.name)
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.handle.save_path(), status.save_path)
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.handle.is_finished(), False)
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.handle.is_seed(), False)


class DownloadQueueTest(TorrentHandleTest):
    def test_download_queue(self) -> None:
        # TODO: how do we simulate download queue to test this?
        self.handle.get_download_queue()


class FileProgressTest(TorrentHandleTest):
    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5995")
    def test_file_progress(self) -> None:
        self.assertEqual(self.handle.file_progress(), [0])
        self.assertEqual(
            self.handle.file_progress(flags=lt.file_progress_flags_t.piece_granularity),
            [0],
        )


class TrackersTest(TorrentHandleTest):
    def test_trackers(self) -> None:
        self.handle.add_tracker({"url": "http://127.1.2.3"})
        # This populates the endpoints list
        self.handle.scrape_tracker()

        # Various parts of the tracker stats conform to a particular status
        # layout
        def check(entry: Dict[str, Any]) -> None:
            self.assertIsInstance(entry["message"], str)
            self.assertIsInstance(entry["last_error"]["value"], int)
            self.assertIsInstance(entry["last_error"]["category"], str)
            self.assertIsInstance(entry["next_announce"], (int, type(None)))
            self.assertIsInstance(entry["min_announce"], (int, type(None)))
            self.assertIsInstance(entry["scrape_incomplete"], int)
            self.assertIsInstance(entry["scrape_complete"], int)
            self.assertIsInstance(entry["scrape_downloaded"], int)
            self.assertIsInstance(entry["fails"], int)
            self.assertIsInstance(entry["updating"], bool)
            self.assertIsInstance(entry["start_sent"], bool)
            self.assertIsInstance(entry["complete_sent"], bool)

        trackers = self.handle.trackers()
        self.assertEqual(len(trackers), 1)
        tr = trackers[0]

        self.assertIsInstance(tr["url"], str)
        self.assertIsInstance(tr["trackerid"], str)
        self.assertIsInstance(tr["tier"], int)
        self.assertIsInstance(tr["fail_limit"], int)
        self.assertIsInstance(tr["source"], int)
        self.assertIsInstance(tr["verified"], bool)
        self.assertIsInstance(tr["send_stats"], bool)
        # The overall tracker result has the status layout
        check(tr)
        self.assertEqual(len(tr["endpoints"]), 1)
        ip = tr["endpoints"][0]["local_address"]
        self.assertIsInstance(ip, tuple)
        self.assertEqual(len(ip), 2)
        self.assertIsInstance(ip[0], str)
        self.assertIsInstance(ip[1], int)
        # The endpoint has the status layout
        check(tr["endpoints"][0])
        # The per-info-hash structures have the status layout
        check(tr["endpoints"][0]["info_hashes"][0])
        check(tr["endpoints"][0]["info_hashes"][1])

    def get_input_entry(self, ae: Dict[str, Any]) -> Dict[str, Any]:
        return {k: ae[k] for k in ("url", "tier", "fail_limit")}

    def test_add_tracker(self) -> None:
        entry_dict = {"url": "http://127.1.2.3", "tier": 2, "fail_limit": 3}
        self.handle.add_tracker(entry_dict)
        self.assertEqual(
            [self.get_input_entry(ae) for ae in self.handle.trackers()], [entry_dict]
        )

    def test_replace_trackers(self) -> None:
        # list of announce_entry
        ae = lt.announce_entry("http://127.1.2.3")
        ae.tier = 2
        ae.fail_limit = 3
        self.handle.replace_trackers([ae])

        # list of dicts
        entry_dict = {"url": "http://127.1.2.3", "tier": 2, "fail_limit": 3}
        self.assertEqual(
            [self.get_input_entry(ae) for ae in self.handle.trackers()], [entry_dict]
        )

    def test_error(self) -> None:
        with self.assertRaises(KeyError):
            self.handle.add_tracker({})
        with self.assertRaises(KeyError):
            self.handle.replace_trackers([{}])


class UrlSeedTest(TorrentHandleTest):
    def test_url_seeds(self) -> None:
        self.assertEqual(self.handle.url_seeds(), [])
        self.handle.add_url_seed("http://127.1.2.3")
        self.assertEqual(self.handle.url_seeds(), ["http://127.1.2.3"])
        self.handle.remove_url_seed("http://127.1.2.3")
        self.assertEqual(self.handle.url_seeds(), [])

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/6136")
    def test_http_seeds(self) -> None:
        self.assertEqual(self.handle.http_seeds(), [])
        self.handle.add_http_seed("http://127.1.2.3")
        self.assertEqual(self.handle.http_seeds(), ["http://127.1.2.3"])
        self.handle.remove_http_seed("http://127.1.2.3")
        self.assertEqual(self.handle.http_seeds(), [])

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_http_seeds_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.handle.add_http_seed("http://127.1.2.3")
        with self.assertWarns(DeprecationWarning):
            self.handle.remove_http_seed("http://127.1.2.3")
        with self.assertWarns(DeprecationWarning):
            self.handle.http_seeds()


class TorrentFileTest(TorrentHandleTest):
    def test_torrent_file(self) -> None:
        self.assertEqual(
            self.handle.torrent_file().info_section(),
            self.torrent.torrent_info().info_section(),
        )
        with self.assertWarns(DeprecationWarning):
            self.assertTrue(self.handle.has_metadata())
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(
                self.handle.get_torrent_info().info_section(),
                self.torrent.torrent_info().info_section(),
            )

    def test_set_metadata(self) -> None:
        self.session.remove_torrent(self.handle)
        self.atp.ti = None
        self.atp.info_hashes = lt.info_hash_t(self.torrent.sha1_hash)

        handle = self.session.add_torrent(self.atp)
        with self.assertWarns(DeprecationWarning):
            self.assertFalse(handle.has_metadata())
        handle.set_metadata(lt.bencode(self.torrent.info))
        with self.assertWarns(DeprecationWarning):
            self.assertTrue(handle.has_metadata())


class IsValidTest(TorrentHandleTest):
    def test_is_valid(self) -> None:
        # Existing handle should be valid
        self.assertTrue(self.handle.is_valid())

        # New handle should be not valid
        self.assertFalse(lt.torrent_handle().is_valid())

        # If we remove our handle, it should eventually become invalid
        self.session.remove_torrent(self.handle)
        for _ in lib.loop_until_timeout(5, msg="invalidate"):
            if not self.handle.is_valid():
                break


class ClearErrorTest(TorrentHandleTest):
    def test_clear_error(self) -> None:
        self.handle.clear_error()
        # TODO: is there a reliable way to create an error condition, that
        # we can reliably clear?


class QueuePositionTest(TorrentHandleTest):
    def test_queue_position(self) -> None:
        self.handle.queue_position_up()
        self.handle.queue_position_down()
        self.handle.queue_position_top()
        self.handle.queue_position_bottom()
        self.assertIsInstance(self.handle.queue_position(), int)


TDUMMY_7BIT = tdummy.Torrent.single_file(
    piece_length=16384,
    name=b"test.txt",
    length=16384 * 9 + 1000,
    data=bytes(random.getrandbits(7) for _ in range(16384 * 9 + 1000)),
)


class AddPieceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.session = lt.session(lib.get_isolated_settings())
        # Use 7-bit data to allow testing deprecated path
        self.dummy = TDUMMY_7BIT
        atp = self.dummy.atp()
        atp.save_path = self.dir.name
        self.handle = self.session.add_torrent(atp)
        self.file_path = pathlib.Path(self.dir.name) / os.fsdecode(
            self.dummy.files[0].path
        )

        # add_piece() does not work in the checking_* states
        for _ in lib.loop_until_timeout(5, msg="checking"):
            if self.handle.status().state not in (
                lt.torrent_status.checking_files,
                lt.torrent_status.checking_resume_data,
            ):
                break

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.dir, timeout=5)

    def wait_until_finished(self) -> None:
        # wait until progress is 1.0
        for _ in lib.loop_until_timeout(5, msg="progress"):
            if self.handle.status().progress == 1.0:
                break

        # wait until data is written to disk
        for _ in lib.loop_until_timeout(5, msg="file write"):
            if not self.file_path.is_file():
                continue
            if self.file_path.read_bytes() == self.dummy.data:
                break

    def test_bytes(self) -> None:
        for i, data in enumerate(self.dummy.pieces):
            self.handle.add_piece(i, data, 0)

        self.wait_until_finished()

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_str_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.handle.add_piece(0, "0" * self.dummy.piece_length, 0)

    def test_str(self) -> None:
        for i, data in enumerate(self.dummy.pieces):
            self.handle.add_piece(i, data.decode(), 0)

        self.wait_until_finished()

    @unittest.skip("TODO: why doesn't this work?")
    def test_overwrite_existing(self) -> None:
        # the logic handling overwrite_existing short-circuits if all pieces
        # are present. We need to only test with one piece
        self.handle.add_piece(0, self.dummy.pieces[0], 0)

        for _ in lib.loop_until_timeout(5, msg="check piece"):
            if self.handle.status().progress > 0:
                break

        # add_piece without overwrite_existing should be a no-op
        self.handle.add_piece(0, b"\0" * len(self.dummy.pieces[0]), 0)

        self.assertGreater(self.handle.status().progress, 0)

        # add_piece with overwrite_existing should undo our progress
        self.handle.add_piece(
            0,
            b"\0" * len(self.dummy.pieces[0]),
            lt.add_piece_flags_t.overwrite_existing,
        )

        for _ in lib.loop_until_timeout(5, msg="undo progress"):
            if self.handle.status().progress == 0:
                break


class ReadPieceTest(TorrentHandleTest):
    def test_read_piece(self) -> None:
        # read_piece_alert is more thoroughly tested in alerts_test.py
        self.handle.read_piece(0)


class PieceDeadlineTest(TorrentHandleTest):
    def test_piece_deadline(self) -> None:
        # TODO: when we have a way to deterministically read piece deadlines,
        # we should test using that

        self.handle.set_piece_deadline(0, 0)
        self.handle.set_piece_deadline(
            0, 0, flags=lt.deadline_flags_t.alert_when_available
        )

        self.handle.reset_piece_deadline(0)

        self.handle.clear_piece_deadlines()


class AvailabilityTest(TorrentHandleTest):
    def test_piece_availability(self) -> None:
        self.assertEqual(
            self.handle.piece_availability(), [0] * len(self.torrent.pieces)
        )


class PrioritiesTest(TorrentHandleTest):
    def test_piece_priorities(self) -> None:
        # Single piece priority
        self.handle.piece_priority(0, 2)
        self.assertEqual(self.handle.piece_priority(0), 2)

        # List of priorities
        prio_list = self.handle.get_piece_priorities()
        self.assertEqual(len(prio_list), len(self.torrent.pieces))
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.handle.piece_priorities(), prio_list)
        prio_list = [2] * len(prio_list)
        self.handle.prioritize_pieces(prio_list)

        # [(piece, priority)]
        self.handle.prioritize_pieces([(0, 3)])
        prio_list[0] = 3
        self.assertEqual(self.handle.get_piece_priorities(), prio_list)

    def test_file_priorities(self) -> None:
        # Single file priority
        self.handle.file_priority(0, 2)
        # TODO: is this supposed to be immediate?
        for _ in lib.loop_until_timeout(5, msg="prio"):
            if self.handle.file_priority(0) == 2:
                break

        # List of priorities
        prio_list = [2] * len(self.torrent.files)
        self.handle.prioritize_files(prio_list)
        self.assertEqual(self.handle.get_file_priorities(), prio_list)
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.handle.file_priorities(), prio_list)

    def test_set_priority(self) -> None:
        # No way to test this
        with self.assertWarns(DeprecationWarning):
            self.handle.set_priority(0)


class FileStatusTest(TorrentHandleTest):
    def test_file_status(self) -> None:
        # TODO: how do we force this to be tested?
        self.assertEqual(self.handle.file_status(), [])


class ResumeDataTest(TorrentHandleTest):
    def test_resume_data(self) -> None:
        self.assertTrue(self.handle.need_save_resume_data())
        self.handle.save_resume_data()
        self.assertFalse(self.handle.need_save_resume_data())
        self.handle.save_resume_data(flags=lt.save_resume_flags_t.save_info_dict)
        with self.assertWarns(DeprecationWarning):
            resume_data = self.handle.write_resume_data()
        # This should parse as resume data
        lt.read_resume_data(lt.bencode(resume_data))


class ForceReannounceTest(TorrentHandleTest):
    def test_scrape_tracker(self) -> None:
        self.handle.add_tracker({"url": "http://127.1.2.3"})
        self.handle.scrape_tracker()
        self.assertEqual(len(self.handle.trackers()[0]["endpoints"]), 1)

        self.handle.replace_trackers([])
        self.handle.add_tracker({"url": "http://127.1.2.3"})
        self.handle.scrape_tracker(index=0)
        self.assertEqual(len(self.handle.trackers()[0]["endpoints"]), 1)

    def do_test_force_reannounce(self, reannounce: Callable[[], Any]) -> None:
        self.handle.replace_trackers([])
        self.handle.add_tracker({"url": "http://127.1.2.3"})
        self.handle.scrape_tracker()  # updates endpoints

        next_announce = self.handle.trackers()[0]["endpoints"][0]["next_announce"]
        # this requires a really long timeout for some reason
        for _ in lib.loop_until_timeout(120, msg="next_announce update"):
            reannounce()
            if (
                self.handle.trackers()[0]["endpoints"][0]["next_announce"]
                > next_announce
            ):
                break

    def test_force_reannounce(self) -> None:
        self.do_test_force_reannounce(lambda: self.handle.force_reannounce())

        self.do_test_force_reannounce(lambda: self.handle.force_reannounce(seconds=1))
        self.do_test_force_reannounce(
            lambda: self.handle.force_reannounce(seconds=1, tracker_idx=0)
        )
        self.do_test_force_reannounce(
            lambda: self.handle.force_reannounce(
                seconds=1,
                tracker_idx=0,
                flags=lt.reannounce_flags_t.ignore_min_interval,
            )
        )

    def test_force_dht_announce(self) -> None:
        # dht_announce_alert is tested more thoroughly in alert_test.py
        self.handle.force_dht_announce()


class FlushCacheTest(TorrentHandleTest):
    def test_flush_cache(self) -> None:
        # cache_flushed_alert is tested more thoroughly in alert_test.py
        self.handle.flush_cache()


class LimitTest(TorrentHandleTest):
    def test_limits(self) -> None:
        self.handle.set_upload_limit(1)
        self.assertEqual(self.handle.upload_limit(), 1)
        self.handle.set_download_limit(1)
        self.assertEqual(self.handle.download_limit(), 1)
        self.handle.set_max_uploads(1)
        self.assertEqual(self.handle.max_uploads(), 1)
        self.handle.set_max_connections(1)
        self.assertEqual(self.handle.max_connections(), 1)


class MoveStorageTest(TorrentHandleTest):
    def wait_for_save_path(self, save_path: str) -> None:
        for _ in lib.loop_until_timeout(5, msg="storage moved"):
            if self.handle.status().save_path == save_path:
                break

    def test_ascii_str(self) -> None:
        self.handle.move_storage(os.path.join(self.dir.name, "test"))
        self.wait_for_save_path(os.path.join(self.dir.name, "test"))

    def test_ascii_bytes(self) -> None:
        self.handle.move_storage(os.path.join(os.fsencode(self.dir.name), b"test"))
        self.wait_for_save_path(os.path.join(self.dir.name, "test"))

    def test_non_ascii_str(self) -> None:
        self.handle.move_storage(os.path.join(self.dir.name, "\u1234"))
        self.wait_for_save_path(os.path.join(self.dir.name, "\u1234"))

    def test_non_ascii_bytes(self) -> None:
        self.handle.move_storage(os.fsencode(os.path.join(self.dir.name, "\u1234")))
        self.wait_for_save_path(os.path.join(self.dir.name, "\u1234"))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_surrogate_str(self) -> None:
        self.handle.move_storage(os.path.join(self.dir.name, "\udcff"))
        self.wait_for_save_path(os.path.join(self.dir.name, "\udcff"))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_surrogate_bytes(self) -> None:
        self.handle.move_storage(os.fsencode(os.path.join(self.dir.name, "\udcff")))
        self.wait_for_save_path(os.path.join(self.dir.name, "\udcff"))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_non_unicode_str(self) -> None:
        self.handle.move_storage(os.path.join(self.dir.name, os.fsdecode(b"\xff")))
        self.wait_for_save_path(os.path.join(self.dir.name, os.fsdecode(b"\xff")))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_non_unicode_bytes(self) -> None:
        self.handle.move_storage(os.path.join(os.fsencode(self.dir.name), b"\xff"))
        self.wait_for_save_path(os.path.join(self.dir.name, os.fsdecode(b"\xff")))

    def test_args(self) -> None:
        new = os.path.join(self.dir.name, "new")
        self.handle.move_storage(new, flags=lt.move_flags_t.always_replace_files)
        self.wait_for_save_path(new)


class InfoHashTest(TorrentHandleTest):
    def test_info_hash(self) -> None:
        self.assertEqual(self.handle.info_hash(), self.torrent.sha1_hash)
        self.assertEqual(
            self.handle.info_hashes(), lt.info_hash_t(self.torrent.sha1_hash)
        )

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_info_hash_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.handle.info_hash()


class ForceRecheckTest(TorrentHandleTest):
    def test_force_recheck(self) -> None:
        # Wait until we're done checking
        for _ in lib.loop_until_timeout(5, msg="checking"):
            if self.handle.status().state not in (
                lt.torrent_status.checking_files,
                lt.torrent_status.checking_resume_data,
            ):
                break

        # Write the data
        path = pathlib.Path(self.dir.name) / os.fsdecode(self.torrent.files[0].path)
        path.write_bytes(self.torrent.files[0].data)

        # Force recheck
        self.handle.force_recheck()

        # We should eventually be complete
        for _ in lib.loop_until_timeout(5, msg="complete"):
            if self.handle.status().progress == 1.0:
                break


class RenameFileTest(TorrentHandleTest):
    def test_rename_file(self) -> None:
        path = os.path.join("dir", "file.txt")
        self.handle.rename_file(0, path)

        for _ in lib.loop_until_timeout(5, msg="rename"):
            if self.handle.torrent_file().files().file_path(0) == path:
                break

        # Test rename with bytes
        self.handle.rename_file(0, b"file2.txt")
        for _ in lib.loop_until_timeout(5, msg="rename"):
            if self.handle.torrent_file().files().file_path(0) == "file2.txt":
                break

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_bytes_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.handle.rename_file(0, b"file.txt")


class CertificateTest(TorrentHandleTest):
    def test_set_ssl_certificate(self) -> None:
        # TODO: how do we test these? It looks like it's supposed to post
        # torrent_error_alert on failures, but I don't see any.
        cert_path = os.path.join(self.dir.name, "cert")
        privkey_path = os.path.join(self.dir.name, "privkey")
        dhparam_path = os.path.join(self.dir.name, "dhparam")

        self.handle.set_ssl_certificate(cert_path, privkey_path, dhparam_path)
        self.handle.set_ssl_certificate(
            cert_path, privkey_path, dhparam_path, passphrase=b"passphrase"
        )
        self.handle.set_ssl_certificate(
            cert_path, privkey_path, dhparam_path, passphrase="passphrase"
        )

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5988")
    def test_passphrase_str_deprecated(self) -> None:
        cert_path = os.path.join(self.dir.name, "cert")
        privkey_path = os.path.join(self.dir.name, "privkey")
        dhparam_path = os.path.join(self.dir.name, "dhparam")
        with self.assertWarns(DeprecationWarning):
            self.handle.set_ssl_certificate(
                cert_path, privkey_path, dhparam_path, passphrase="passphrase"
            )


class FlagsTest(TorrentHandleTest):
    def test_flags(self) -> None:
        # For clarity
        self.assertTrue(self.handle.flags() & lt.torrent_flags.apply_ip_filter)
        self.assertFalse(self.handle.flags() & lt.torrent_flags.disable_dht)

        # Test with mask
        self.handle.set_flags(
            lt.torrent_flags.disable_dht,
            lt.torrent_flags.disable_dht | lt.torrent_flags.apply_ip_filter,
        )
        self.assertFalse(self.handle.flags() & lt.torrent_flags.apply_ip_filter)
        self.assertTrue(self.handle.flags() & lt.torrent_flags.disable_dht)

        # No mask means flags == mask
        self.handle.set_flags(lt.torrent_flags.apply_ip_filter)
        self.assertTrue(self.handle.flags() & lt.torrent_flags.apply_ip_filter)

        # Test unset_flags
        self.handle.unset_flags(lt.torrent_flags.disable_dht)
        self.assertFalse(self.handle.flags() & lt.torrent_flags.disable_dht)

    def test_old_apply_ip_filter(self) -> None:
        self.assertTrue(self.handle.flags() & lt.torrent_flags.apply_ip_filter)
        with self.assertWarns(DeprecationWarning):
            self.handle.apply_ip_filter(False)
        self.assertFalse(self.handle.flags() & lt.torrent_flags.apply_ip_filter)

    def test_old_auto_managed(self) -> None:
        self.assertTrue(self.handle.flags() & lt.torrent_flags.auto_managed)
        with self.assertWarns(DeprecationWarning):
            self.handle.auto_managed(False)
        self.assertFalse(self.handle.flags() & lt.torrent_flags.auto_managed)
        with self.assertWarns(DeprecationWarning):
            self.assertFalse(self.handle.is_auto_managed())

    def test_old_paused(self) -> None:
        for _ in lib.loop_until_timeout(5, msg="unpause"):
            if not (self.handle.flags() & lt.torrent_flags.paused):
                break
        with self.assertWarns(DeprecationWarning):
            self.assertFalse(self.handle.is_paused())

    def test_old_sequential_download(self) -> None:
        self.assertFalse(self.handle.flags() & lt.torrent_flags.sequential_download)
        with self.assertWarns(DeprecationWarning):
            self.handle.set_sequential_download(True)
        self.assertTrue(self.handle.flags() & lt.torrent_flags.sequential_download)

    def test_old_share_mode(self) -> None:
        self.assertFalse(self.handle.flags() & lt.torrent_flags.share_mode)
        with self.assertWarns(DeprecationWarning):
            self.handle.set_share_mode(True)
        self.assertTrue(self.handle.flags() & lt.torrent_flags.share_mode)

    def test_old_stop_when_ready(self) -> None:
        self.assertFalse(self.handle.flags() & lt.torrent_flags.stop_when_ready)
        with self.assertWarns(DeprecationWarning):
            self.handle.stop_when_ready(True)
        # stop_when_ready is a one-shot flag, which gets unset when it fires.
        # Just check the torrent is paused.
        self.assertTrue(self.handle.flags() & lt.torrent_flags.paused)

    def test_old_super_seeding(self) -> None:
        # super_seeding turns itself off when done checking. To prevent
        # flakiness, wait until we're done checking
        for _ in lib.loop_until_timeout(5, msg="checking"):
            if self.handle.status().state not in (
                lt.torrent_status.checking_files,
                lt.torrent_status.checking_resume_data,
            ):
                break

        self.assertFalse(self.handle.flags() & lt.torrent_flags.super_seeding)
        with self.assertWarns(DeprecationWarning):
            self.handle.super_seeding(True)
        self.assertTrue(self.handle.flags() & lt.torrent_flags.super_seeding)
        with self.assertWarns(DeprecationWarning):
            self.assertTrue(self.handle.super_seeding())

    def test_old_upload_mode(self) -> None:
        self.assertFalse(self.handle.flags() & lt.torrent_flags.upload_mode)
        with self.assertWarns(DeprecationWarning):
            self.handle.set_upload_mode(True)
        self.assertTrue(self.handle.flags() & lt.torrent_flags.upload_mode)

    def test_pause(self) -> None:
        for _ in lib.loop_until_timeout(5, msg="unpause"):
            if not (self.handle.flags() & lt.torrent_flags.paused):
                break

        # No-args
        self.handle.pause()
        self.assertTrue(self.handle.flags() & lt.torrent_flags.paused)

        self.handle.resume()
        self.assertFalse(self.handle.flags() & lt.torrent_flags.paused)

        # flags
        self.handle.pause(flags=lt.pause_flags_t.graceful_pause)
        # This normally isn't instantaneous, but currently *is* instantaneous
        # if no peers are connected
        self.assertTrue(self.handle.flags() & lt.torrent_flags.paused)


class DeprecatedFeaturesTest(TorrentHandleTest):
    def test_dead_features(self) -> None:
        # These all do nothing
        with self.assertWarns(DeprecationWarning):
            self.handle.set_peer_upload_limit(("127.1.2.3", 1234), 1)
        with self.assertWarns(DeprecationWarning):
            self.handle.set_peer_download_limit(("127.1.2.3", 1234), 1)
        with self.assertWarns(DeprecationWarning):
            self.handle.set_ratio(0.1)

    def test_set_tracker_login(self) -> None:
        # This is technically still functional, but no simple way to test it
        with self.assertWarns(DeprecationWarning):
            self.handle.set_tracker_login("username", "password")

    def test_use_interface(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.handle.use_interface("test")
        self.assertEqual(self.session.get_settings()["outgoing_interfaces"], "test")
