import datetime
import tempfile
import unittest

import libtorrent as lt

from . import lib
from . import tdummy


class TorrentStatusTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = lt.session(lib.get_isolated_settings())
        self.tempdir = tempfile.TemporaryDirectory()
        self.torrent = tdummy.get_default()
        self.atp = self.torrent.atp()
        self.atp.save_path = self.tempdir.name
        self.handle = self.session.add_torrent(self.atp)
        self.status = self.handle.status()

    def tearDown(self) -> None:
        lib.cleanup_with_windows_fix(self.tempdir, timeout=5)

    def test_dunder(self) -> None:
        self.assertNotEqual(self.status, lt.torrent_status())

        self.assertEqual(lt.torrent_status(), lt.torrent_status())

    def test_properties(self) -> None:
        self.assertEqual(self.status.handle, self.handle)

        self.assertIsInstance(self.status.torrent_file, lt.torrent_info)
        assert self.status.torrent_file is not None
        self.assertEqual(
            self.status.torrent_file.info_section(),
            self.torrent.torrent_info().info_section(),
        )

        self.assertIsInstance(self.status.state, lt.torrent_status.states)
        if lt.api_version < 2:
            self.assertIsInstance(self.status.paused, bool)
            self.assertIsInstance(self.status.stop_when_ready, bool)
            self.assertIsInstance(self.status.auto_managed, bool)
            self.assertIsInstance(self.status.sequential_download, bool)

        self.assertIsInstance(self.status.is_seeding, bool)
        self.assertIsInstance(self.status.is_finished, bool)
        self.assertIsInstance(self.status.has_metadata, bool)

        self.assertIsInstance(self.status.progress, float)
        self.assertIsInstance(self.status.progress_ppm, int)

        self.assertIsInstance(self.status.next_announce, datetime.timedelta)
        if lt.api_version < 2:
            self.assertIsInstance(self.status.announce_interval, datetime.timedelta)

        self.assertIsInstance(self.status.current_tracker, str)

        self.assertIsInstance(self.status.total_download, int)
        self.assertIsInstance(self.status.total_upload, int)
        self.assertIsInstance(self.status.total_payload_download, int)
        self.assertIsInstance(self.status.total_payload_upload, int)
        self.assertIsInstance(self.status.total_failed_bytes, int)
        self.assertIsInstance(self.status.total_redundant_bytes, int)

        self.assertIsInstance(self.status.download_rate, int)
        self.assertIsInstance(self.status.upload_rate, int)
        self.assertIsInstance(self.status.download_payload_rate, int)
        self.assertIsInstance(self.status.upload_payload_rate, int)

        self.assertIsInstance(self.status.num_seeds, int)
        self.assertIsInstance(self.status.num_peers, int)
        self.assertIsInstance(self.status.num_complete, int)
        self.assertIsInstance(self.status.num_incomplete, int)
        self.assertIsInstance(self.status.list_seeds, int)
        self.assertIsInstance(self.status.list_peers, int)
        self.assertIsInstance(self.status.connect_candidates, int)

        self.assertEqual(self.status.pieces, [False] * len(self.torrent.pieces))
        self.assertIsInstance(self.status.verified_pieces, list)
        self.assertIsInstance(self.status.num_pieces, int)

        self.assertIsInstance(self.status.total_done, int)
        self.assertIsInstance(self.status.total_wanted_done, int)
        self.assertIsInstance(self.status.total_wanted, int)

        self.assertIsInstance(self.status.distributed_full_copies, int)
        self.assertIsInstance(self.status.distributed_fraction, int)
        self.assertIsInstance(self.status.distributed_copies, float)

        self.assertIsInstance(self.status.block_size, int)

        self.assertIsInstance(self.status.num_uploads, int)
        self.assertIsInstance(self.status.num_connections, int)

        self.assertIsInstance(self.status.uploads_limit, int)
        self.assertIsInstance(self.status.connections_limit, int)

        self.assertIsInstance(self.status.storage_mode, lt.storage_mode_t)

        self.assertIsInstance(self.status.up_bandwidth_queue, int)
        self.assertIsInstance(self.status.down_bandwidth_queue, int)

        self.assertIsInstance(self.status.all_time_upload, int)
        self.assertIsInstance(self.status.all_time_download, int)

        self.assertIsInstance(self.status.seed_rank, int)
        self.assertIsInstance(self.status.has_incoming, bool)

        if lt.api_version < 2:
            self.assertIsInstance(self.status.seed_mode, bool)
            self.assertIsInstance(self.status.upload_mode, bool)
            self.assertIsInstance(self.status.share_mode, bool)
            self.assertIsInstance(self.status.super_seeding, bool)

            self.assertIsInstance(self.status.active_time, int)
            self.assertIsInstance(self.status.finished_time, int)
            self.assertIsInstance(self.status.seeding_time, int)
            self.assertIsInstance(self.status.last_scrape, int)

            self.assertIsInstance(self.status.error, str)

            self.assertIsInstance(self.status.priority, int)

            self.assertIsInstance(self.status.time_since_upload, int)
            self.assertIsInstance(self.status.time_since_download, int)

        self.assertIsInstance(self.status.errc, lt.error_code)
        self.assertIsInstance(self.status.error_file, int)

        self.assertIsInstance(self.status.name, str)
        self.assertIsInstance(self.status.save_path, str)

        self.assertIsInstance(self.status.added_time, int)
        self.assertIsInstance(self.status.completed_time, int)
        self.assertIsInstance(self.status.last_seen_complete, int)

        self.assertIsInstance(self.status.queue_position, int)
        self.assertIsInstance(self.status.need_save_resume_data, int)

        if lt.api_version < 2:
            self.assertIsInstance(self.status.ip_filter_applies, bool)
        self.assertIsInstance(self.status.moving_storage, bool)
        if lt.api_version < 2:
            self.assertIsInstance(self.status.is_loaded, bool)

        self.assertIsInstance(self.status.announcing_to_trackers, bool)
        self.assertIsInstance(self.status.announcing_to_lsd, bool)
        self.assertIsInstance(self.status.announcing_to_dht, bool)

        if lt.api_version < 3:
            self.assertIsInstance(self.status.info_hash, lt.sha1_hash)
            self.assertEqual(self.status.info_hash, self.torrent.sha1_hash)
        self.assertIsInstance(self.status.info_hashes, lt.info_hash_t)
        self.assertEqual(self.status.info_hashes.v1, self.torrent.sha1_hash)

        self.assertIsInstance(self.status.last_upload, (type(None), datetime.datetime))
        self.assertIsInstance(
            self.status.last_download, (type(None), datetime.datetime)
        )

        self.assertIsInstance(self.status.active_duration, datetime.timedelta)
        self.assertIsInstance(self.status.finished_duration, datetime.timedelta)
        self.assertIsInstance(self.status.seeding_duration, datetime.timedelta)

        self.assertIsInstance(self.status.flags, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.need_save_resume, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.paused, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.stop_when_ready, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.auto_managed, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.sequential_download, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.announce_interval, datetime.timedelta)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.seed_mode, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.upload_mode, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.share_mode, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.super_seeding, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.active_time, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.finished_time, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.seeding_time, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.last_scrape, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.error, str)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.priority, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.time_since_upload, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.time_since_download, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.ip_filter_applies, bool)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.is_loaded, bool)
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(self.status.info_hash, lt.sha1_hash)


class EnumTest(unittest.TestCase):
    def test_states(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.torrent_status.states.queued_for_checking, int)
        self.assertIsInstance(lt.torrent_status.states.checking_files, int)
        self.assertIsInstance(lt.torrent_status.states.downloading_metadata, int)
        self.assertIsInstance(lt.torrent_status.states.downloading, int)
        self.assertIsInstance(lt.torrent_status.states.finished, int)
        self.assertIsInstance(lt.torrent_status.states.seeding, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.torrent_status.states.allocating, int)
        self.assertIsInstance(lt.torrent_status.states.checking_resume_data, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_states_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.torrent_status.states.queued_for_checking, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.torrent_status.states.allocating, int)
