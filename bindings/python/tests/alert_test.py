import datetime
import errno
import functools
import http.server
import logging
import os
import pathlib
import ssl
import sys
import tempfile
import threading
from typing import Any
from typing import Callable
from typing import Dict
from typing import Optional
from typing import Tuple
from typing import Type
from typing import TypeVar
import unittest
import time

import libtorrent as lt

from . import lib
from . import tdummy


class EnumTest(unittest.TestCase):
    def test_category_t(self) -> None:
        self.assertIsInstance(lt.alert.category_t.error_notification, int)
        self.assertIsInstance(lt.alert.category_t.peer_notification, int)
        self.assertIsInstance(lt.alert.category_t.port_mapping_notification, int)
        self.assertIsInstance(lt.alert.category_t.storage_notification, int)
        self.assertIsInstance(lt.alert.category_t.tracker_notification, int)
        self.assertIsInstance(lt.alert.category_t.connect_notification, int)
        self.assertIsInstance(lt.alert.category_t.status_notification, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.alert.category_t.debug_notification, int)
            self.assertIsInstance(lt.alert.category_t.progress_notification, int)
        self.assertIsInstance(lt.alert.category_t.ip_block_notification, int)
        self.assertIsInstance(lt.alert.category_t.performance_warning, int)
        self.assertIsInstance(lt.alert.category_t.dht_notification, int)
        if lt.api_version < 3:
            self.assertIsInstance(lt.alert.category_t.stats_notification, int)
        self.assertIsInstance(lt.alert.category_t.session_log_notification, int)
        self.assertIsInstance(lt.alert.category_t.torrent_log_notification, int)
        self.assertIsInstance(lt.alert.category_t.peer_log_notification, int)
        self.assertIsInstance(lt.alert.category_t.incoming_request_notification, int)
        self.assertIsInstance(lt.alert.category_t.dht_log_notification, int)
        self.assertIsInstance(lt.alert.category_t.dht_operation_notification, int)
        self.assertIsInstance(lt.alert.category_t.port_mapping_log_notification, int)
        self.assertIsInstance(lt.alert.category_t.picker_log_notification, int)
        self.assertIsInstance(lt.alert.category_t.file_progress_notification, int)
        self.assertIsInstance(lt.alert.category_t.piece_progress_notification, int)
        self.assertIsInstance(lt.alert.category_t.upload_notification, int)
        self.assertIsInstance(lt.alert.category_t.block_progress_notification, int)
        self.assertIsInstance(lt.alert.category_t.all_categories, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_category_t_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.alert.category_t.debug_notification, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.alert.category_t.progress_notification, int)
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.alert.category_t.stats_notification, int)

    def test_alert_category(self) -> None:
        self.assertIsInstance(lt.alert_category.error, int)
        self.assertIsInstance(lt.alert_category.peer, int)
        self.assertIsInstance(lt.alert_category.port_mapping, int)
        self.assertIsInstance(lt.alert_category.storage, int)
        self.assertIsInstance(lt.alert_category.tracker, int)
        self.assertIsInstance(lt.alert_category.connect, int)
        self.assertIsInstance(lt.alert_category.status, int)
        self.assertIsInstance(lt.alert_category.ip_block, int)
        self.assertIsInstance(lt.alert_category.performance_warning, int)
        self.assertIsInstance(lt.alert_category.dht, int)
        self.assertIsInstance(lt.alert_category.stats, int)
        self.assertIsInstance(lt.alert_category.session_log, int)
        self.assertIsInstance(lt.alert_category.torrent_log, int)
        self.assertIsInstance(lt.alert_category.peer_log, int)
        self.assertIsInstance(lt.alert_category.incoming_request, int)
        self.assertIsInstance(lt.alert_category.dht_log, int)
        self.assertIsInstance(lt.alert_category.dht_operation, int)
        self.assertIsInstance(lt.alert_category.port_mapping_log, int)
        self.assertIsInstance(lt.alert_category.picker_log, int)
        self.assertIsInstance(lt.alert_category.file_progress, int)
        self.assertIsInstance(lt.alert_category.piece_progress, int)
        self.assertIsInstance(lt.alert_category.upload, int)
        self.assertIsInstance(lt.alert_category.block_progress, int)
        self.assertIsInstance(lt.alert_category.all, int)

    def test_operation_t(self) -> None:
        self.assertIsInstance(lt.operation_t.unknown, int)
        self.assertIsInstance(lt.operation_t.bittorrent, int)
        self.assertIsInstance(lt.operation_t.iocontrol, int)
        self.assertIsInstance(lt.operation_t.getpeername, int)
        self.assertIsInstance(lt.operation_t.getname, int)
        self.assertIsInstance(lt.operation_t.alloc_recvbuf, int)
        self.assertIsInstance(lt.operation_t.alloc_sndbuf, int)
        self.assertIsInstance(lt.operation_t.file_write, int)
        self.assertIsInstance(lt.operation_t.file_read, int)
        self.assertIsInstance(lt.operation_t.file, int)
        self.assertIsInstance(lt.operation_t.sock_write, int)
        self.assertIsInstance(lt.operation_t.sock_read, int)
        self.assertIsInstance(lt.operation_t.sock_open, int)
        self.assertIsInstance(lt.operation_t.sock_bind, int)
        self.assertIsInstance(lt.operation_t.available, int)
        self.assertIsInstance(lt.operation_t.encryption, int)
        self.assertIsInstance(lt.operation_t.connect, int)
        self.assertIsInstance(lt.operation_t.ssl_handshake, int)
        self.assertIsInstance(lt.operation_t.get_interface, int)
        self.assertIsInstance(lt.operation_t.sock_listen, int)
        self.assertIsInstance(lt.operation_t.sock_bind_to_device, int)
        self.assertIsInstance(lt.operation_t.sock_accept, int)
        self.assertIsInstance(lt.operation_t.parse_address, int)
        self.assertIsInstance(lt.operation_t.enum_if, int)
        self.assertIsInstance(lt.operation_t.file_stat, int)
        self.assertIsInstance(lt.operation_t.file_copy, int)
        self.assertIsInstance(lt.operation_t.file_fallocate, int)
        self.assertIsInstance(lt.operation_t.file_hard_link, int)
        self.assertIsInstance(lt.operation_t.file_remove, int)
        self.assertIsInstance(lt.operation_t.file_rename, int)
        self.assertIsInstance(lt.operation_t.file_open, int)
        self.assertIsInstance(lt.operation_t.mkdir, int)
        self.assertIsInstance(lt.operation_t.check_resume, int)
        self.assertIsInstance(lt.operation_t.exception, int)
        self.assertIsInstance(lt.operation_t.alloc_cache_piece, int)
        self.assertIsInstance(lt.operation_t.partfile_move, int)
        self.assertIsInstance(lt.operation_t.partfile_read, int)
        self.assertIsInstance(lt.operation_t.partfile_write, int)
        self.assertIsInstance(lt.operation_t.hostname_lookup, int)
        self.assertIsInstance(lt.operation_t.symlink, int)
        self.assertIsInstance(lt.operation_t.handshake, int)
        self.assertIsInstance(lt.operation_t.sock_option, int)

    def test_operation_name(self) -> None:
        # Programmatically check that operation_name works on all operation_t.* values
        for name in dir(lt.operation_t):
            value = getattr(lt.operation_t, name)
            if not isinstance(value, lt.operation_t):
                continue
            self.assertIsInstance(lt.operation_name(value), str)

    def test_listen_succeeded_alert_socket_type_t(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.tcp, int)
            self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.tcp_ssl, int)
            self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.udp, int)
            self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.i2p, int)
            self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.socks5, int)
            self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.utp_ssl, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_listen_succeeded_alert_socket_type_t_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.tcp, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    lt.listen_succeeded_alert_socket_type_t.tcp_ssl, int
                )
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.udp, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.listen_succeeded_alert_socket_type_t.i2p, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    lt.listen_succeeded_alert_socket_type_t.socks5, int
                )
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    lt.listen_succeeded_alert_socket_type_t.utp_ssl, int
                )

    def test_listen_failed_alert_socket_type_t(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.tcp, int)
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.tcp_ssl, int)
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.udp, int)
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.i2p, int)
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.socks5, int)
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.utp_ssl, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_listen_failed_alert_socket_type_t_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.tcp, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.tcp_ssl, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.udp, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.i2p, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.socks5, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.listen_failed_alert_socket_type_t.utp_ssl, int)

    def test_socket_type_t(self) -> None:
        self.assertIsInstance(lt.socket_type_t.tcp, int)
        self.assertIsInstance(lt.socket_type_t.socks5, int)
        self.assertIsInstance(lt.socket_type_t.http, int)
        self.assertIsInstance(lt.socket_type_t.utp, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.socket_type_t.udp, int)
        self.assertIsInstance(lt.socket_type_t.i2p, int)
        self.assertIsInstance(lt.socket_type_t.tcp_ssl, int)
        self.assertIsInstance(lt.socket_type_t.socks5_ssl, int)
        self.assertIsInstance(lt.socket_type_t.http_ssl, int)
        self.assertIsInstance(lt.socket_type_t.utp_ssl, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_socket_type_t_deprecated(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.socket_type_t.udp, int)

    def test_reason_t(self) -> None:
        self.assertIsInstance(lt.reason_t.ip_filter, int)
        self.assertIsInstance(lt.reason_t.port_filter, int)
        self.assertIsInstance(lt.reason_t.i2p_mixed, int)
        self.assertIsInstance(lt.reason_t.privileged_ports, int)
        self.assertIsInstance(lt.reason_t.utp_disabled, int)
        self.assertIsInstance(lt.reason_t.tcp_disabled, int)
        self.assertIsInstance(lt.reason_t.invalid_local_interface, int)

    def test_performance_warning_t(self) -> None:
        self.assertIsInstance(
            lt.performance_warning_t.outstanding_disk_buffer_limit_reached, int
        )
        self.assertIsInstance(
            lt.performance_warning_t.outstanding_request_limit_reached, int
        )
        self.assertIsInstance(lt.performance_warning_t.upload_limit_too_low, int)
        self.assertIsInstance(lt.performance_warning_t.download_limit_too_low, int)
        self.assertIsInstance(
            lt.performance_warning_t.send_buffer_watermark_too_low, int
        )
        self.assertIsInstance(
            lt.performance_warning_t.too_many_optimistic_unchoke_slots, int
        )
        if lt.api_version < 2:
            self.assertIsInstance(
                lt.performance_warning_t.bittyrant_with_no_uplimit, int
            )
        self.assertIsInstance(lt.performance_warning_t.too_high_disk_queue_limit, int)
        self.assertIsInstance(lt.performance_warning_t.too_few_outgoing_ports, int)
        self.assertIsInstance(lt.performance_warning_t.too_few_file_descriptors, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_performance_warning_t_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    lt.performance_warning_t.bittyrant_with_no_uplimit, int
                )

    def test_stats_channel(self) -> None:
        if lt.api_version < 3:
            self.assertIsInstance(lt.stats_channel.upload_payload, int)
            self.assertIsInstance(lt.stats_channel.upload_protocol, int)
            self.assertIsInstance(lt.stats_channel.upload_ip_protocol, int)
            if lt.api_version < 2:
                self.assertIsInstance(lt.stats_channel.upload_dht_protocol, int)
                self.assertIsInstance(lt.stats_channel.upload_tracker_protocol, int)
            self.assertIsInstance(lt.stats_channel.download_payload, int)
            self.assertIsInstance(lt.stats_channel.download_protocol, int)
            self.assertIsInstance(lt.stats_channel.download_ip_protocol, int)
            if lt.api_version < 2:
                self.assertIsInstance(lt.stats_channel.download_dht_protocol, int)
                self.assertIsInstance(lt.stats_channel.download_tracker_protocol, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_stats_channel_deprecated(self) -> None:
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.stats_channel.upload_payload, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.stats_channel.upload_protocol, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.stats_channel.upload_ip_protocol, int)
            if lt.api_version < 2:
                with self.assertWarns(DeprecationWarning):
                    self.assertIsInstance(lt.stats_channel.upload_dht_protocol, int)
                with self.assertWarns(DeprecationWarning):
                    self.assertIsInstance(lt.stats_channel.upload_tracker_protocol, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.stats_channel.download_payload, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.stats_channel.download_protocol, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.stats_channel.download_ip_protocol, int)
            if lt.api_version < 2:
                with self.assertWarns(DeprecationWarning):
                    self.assertIsInstance(lt.stats_channel.download_dht_protocol, int)
                with self.assertWarns(DeprecationWarning):
                    self.assertIsInstance(
                        lt.stats_channel.download_tracker_protocol, int
                    )

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_kind_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.kind.tracker_no_anonymous, int)

    def test_close_reason_t(self) -> None:
        self.assertIsInstance(lt.close_reason_t.blocked, int)
        self.assertIsInstance(lt.close_reason_t.corrupt_pieces, int)
        self.assertIsInstance(lt.close_reason_t.duplicate_peer_id, int)
        self.assertIsInstance(lt.close_reason_t.encryption_error, int)
        self.assertIsInstance(lt.close_reason_t.invalid_allow_fast_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_bitfield_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_cancel_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_choke_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_dht_port_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_dont_have_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_extended_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_have_all_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_have_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_have_none_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_info_hash, int)
        self.assertIsInstance(lt.close_reason_t.invalid_interested_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_message_id, int)
        self.assertIsInstance(lt.close_reason_t.invalid_metadata, int)
        self.assertIsInstance(lt.close_reason_t.invalid_metadata_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_metadata_offset, int)
        self.assertIsInstance(lt.close_reason_t.invalid_metadata_request_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_not_interested_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_pex_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_piece_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_reject_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_request_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_suggest_message, int)
        self.assertIsInstance(lt.close_reason_t.invalid_unchoke_message, int)
        self.assertIsInstance(lt.close_reason_t.message_too_big, int)
        self.assertIsInstance(lt.close_reason_t.metadata_too_big, int)
        self.assertIsInstance(lt.close_reason_t.no_memory, int)
        self.assertIsInstance(lt.close_reason_t.none, int)
        self.assertIsInstance(lt.close_reason_t.not_interested_upload_only, int)
        self.assertIsInstance(lt.close_reason_t.peer_churn, int)
        self.assertIsInstance(lt.close_reason_t.pex_message_too_big, int)
        self.assertIsInstance(lt.close_reason_t.pex_too_frequent, int)
        self.assertIsInstance(lt.close_reason_t.port_blocked, int)
        self.assertIsInstance(lt.close_reason_t.protocol_blocked, int)
        self.assertIsInstance(lt.close_reason_t.request_when_choked, int)
        self.assertIsInstance(lt.close_reason_t.self_connection, int)
        self.assertIsInstance(lt.close_reason_t.timed_out_activity, int)
        self.assertIsInstance(lt.close_reason_t.timed_out_handshake, int)
        self.assertIsInstance(lt.close_reason_t.timed_out_interest, int)
        self.assertIsInstance(lt.close_reason_t.timed_out_request, int)
        self.assertIsInstance(lt.close_reason_t.timeout, int)
        self.assertIsInstance(lt.close_reason_t.too_many_connections, int)
        self.assertIsInstance(lt.close_reason_t.too_many_files, int)
        self.assertIsInstance(lt.close_reason_t.torrent_removed, int)
        self.assertIsInstance(lt.close_reason_t.upload_to_upload, int)

    def test_dht_module_t(self) -> None:
        self.assertIsInstance(lt.dht_module_t.node, int)
        self.assertIsInstance(lt.dht_module_t.routing_table, int)
        self.assertIsInstance(lt.dht_module_t.rpc_manager, int)
        self.assertIsInstance(lt.dht_module_t.tracker, int)
        self.assertIsInstance(lt.dht_module_t.traversal, int)

    def test_picker_flags_t(self) -> None:
        self.assertIsInstance(lt.picker_flags_t.backup1, int)
        self.assertIsInstance(lt.picker_flags_t.backup2, int)
        self.assertIsInstance(lt.picker_flags_t.end_game, int)
        self.assertIsInstance(lt.picker_flags_t.extent_affinity, int)
        self.assertIsInstance(lt.picker_flags_t.partial_ratio, int)
        self.assertIsInstance(lt.picker_flags_t.prefer_contiguous, int)
        self.assertIsInstance(lt.picker_flags_t.prio_sequential_pieces, int)
        self.assertIsInstance(lt.picker_flags_t.prioritize_partials, int)
        self.assertIsInstance(lt.picker_flags_t.random_pieces, int)
        self.assertIsInstance(lt.picker_flags_t.rarest_first, int)
        self.assertIsInstance(lt.picker_flags_t.rarest_first_partials, int)
        self.assertIsInstance(lt.picker_flags_t.reverse_pieces, int)
        self.assertIsInstance(lt.picker_flags_t.reverse_rarest_first, int)
        self.assertIsInstance(lt.picker_flags_t.reverse_sequential, int)
        self.assertIsInstance(lt.picker_flags_t.sequential_pieces, int)
        self.assertIsInstance(lt.picker_flags_t.suggested_pieces, int)
        self.assertIsInstance(lt.picker_flags_t.time_critical, int)


_A = TypeVar("_A", bound=lt.alert)


def wait_for(
    session: lt.session,
    alert_type: Type[_A],
    *,
    timeout: float,
    prefix: Optional[str] = None,
) -> _A:
    # Return the first alert of type _A, but log all alerts.
    result: Optional[_A] = None
    for _ in lib.loop_until_timeout(timeout, msg=alert_type.__name__):
        for alert in session.pop_alerts():
            if prefix:
                logging.debug("%s: %s: %s", prefix, alert.what(), alert.message())
            else:
                logging.debug("%s: %s", alert.what(), alert.message())
            if result is None and isinstance(alert, alert_type):
                result = alert
        if result is not None:
            return result
    raise AssertionError("unreachable")


def wait_until_done_checking(handle: lt.torrent_handle, *, timeout: float) -> None:
    for _ in lib.loop_until_timeout(5, msg="checking"):
        st = handle.status().state
        if st not in (
            lt.torrent_status.checking_files,
            lt.torrent_status.checking_resume_data,
        ):
            break


def remove_handles_and_wait(session: lt.session) -> None:
    handles = session.get_torrents()
    for handle in handles:
        session.remove_torrent(handle)
    for _ in lib.loop_until_timeout(5, msg="clear all handles"):
        if not any(handle.is_valid() for handle in handles):
            break


class AlertTest(unittest.TestCase):
    ALERT_MASK = 0

    def setUp(self) -> None:
        super().setUp()
        self.settings = lib.get_isolated_settings()
        self.settings["alert_mask"] = self.ALERT_MASK
        self.session = lt.session(self.settings)
        self.endpoint = ("127.0.0.1", self.session.listen_port())
        self.endpoint_str = f"{self.endpoint[0]}:{self.endpoint[1]}"

    def tearDown(self) -> None:
        super().tearDown()
        # we do this because sessions writing data can collide with
        # cleaning up temporary directories. session.abort() isn't bound
        remove_handles_and_wait(self.session)

    def assert_alert(self, alert: lt.alert, category: int, what: str) -> None:
        self.assertEqual(alert.category(), category)
        self.assertEqual(alert.what(), what)
        self.assertIsInstance(alert.message(), str)
        self.assertNotEqual(alert.message(), "")
        self.assertEqual(str(alert), alert.message())


if lt.api_version < 2:
    maybe_progress_notification = lt.alert.category_t.progress_notification
else:
    maybe_progress_notification = 0


class TorrentAlertTest(AlertTest):
    def setUp(self) -> None:
        super().setUp()
        self.dir = tempfile.TemporaryDirectory()
        self.torrent = tdummy.get_default()
        self.atp = self.torrent.atp()
        self.atp.flags &= ~lt.torrent_flags.auto_managed
        self.atp.flags &= ~lt.torrent_flags.paused
        self.atp.save_path = self.dir.name
        ti = self.atp.ti
        assert ti is not None
        self.file_path = os.path.join(self.dir.name, ti.files().file_path(0))
        self.torrent_name = ti.name()

    def tearDown(self) -> None:
        super().tearDown()
        lib.cleanup_with_windows_fix(self.dir, timeout=5)

    def assert_torrent_alert(
        self, alert: lt.torrent_alert, handle: lt.torrent_handle
    ) -> None:
        self.assertEqual(alert.handle, handle)
        self.assertEqual(alert.torrent_name, self.torrent_name)


class UseAfterFreeTest(TorrentAlertTest):
    # In theory, we shouldn't hold a reference to an alert after the next
    # pop_alerts(), because the underlying data gets freed. In reality, strict
    # reference management is infeasible in python.

    # For example, note that every exception references the stack frame where it
    # was raised, which references all the locals. So if your alert-handling
    # code uses a local variable for an alert, and any exception is raised by
    # lower-level code and handled by upper-level code, you'll get a reference
    # to an alert you probably didn't intend, and can't feasibly remove.

    # For another example, consider:
    #    logging.info("alert: %s", alert)
    # Seems like this shouldn't cause a lasting reference to the alert, but *it
    # depends on logger configuration. pytest's default logger holds references
    # to all log entries *including arguments* for the lifetime of the test
    # runner.

    # We don't have a defense against code that aggressively introspects all
    # live objects. Hopefully this is too rare to matter

    # Instead, we test some basic accesses that are likely to happen to
    # arbitrary python objects.

    def setUp(self) -> None:
        super().setUp()
        self.session.add_torrent(self.atp)
        self.alert = wait_for(self.session, lt.add_torrent_alert, timeout=5)
        self.session.pop_alerts()
        # underlying alert data has now been freed

    def test_repr(self) -> None:
        # repr() is used to display results on an interactive command line;
        # let's ensure that doesn't constantly crash. It's also used as a "poor
        # man's introspection" in some other contexts.
        self.assertIsInstance(repr(self.alert), str)

    def test_release(self) -> None:
        # This just tests releasing the python object after the underlying
        # object was freed. Technically this is done by other tests, but I want
        # to explicitly spell out that this is something we need to support
        pass


class TorrentAddedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_torrent_added_alert(self) -> None:
        if lt.api_version < 2:
            handle = self.session.add_torrent(self.atp)
            alert = wait_for(self.session, lt.torrent_added_alert, timeout=5)

            self.assert_alert(alert, lt.alert_category.status, "torrent_added")
            self.assert_torrent_alert(alert, handle)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertTrue(issubclass(lt.torrent_added_alert, lt.torrent_alert))


class TorrentRemovedAlertTest(TorrentAlertTest):
    def test_torrent_removed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.session.remove_torrent(handle)
        alert = wait_for(self.session, lt.torrent_removed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "torrent_removed")
        self.assert_torrent_alert(alert, handle)
        if lt.api_version < 3:
            self.assertEqual(alert.info_hash, self.torrent.sha1_hash)
        self.assertEqual(alert.info_hashes.v1, self.torrent.sha1_hash)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.session.remove_torrent(handle)
        alert = wait_for(self.session, lt.torrent_removed_alert, timeout=5)
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.info_hash, self.torrent.sha1_hash)


class ReadPieceAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.piece_progress

    def test_read_piece(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add_piece() does not work in the checking_* states
        wait_until_done_checking(handle, timeout=5)

        handle.add_piece(0, self.torrent.pieces[0], 0)
        # read_piece() does not work until we have the piece
        self.alert = wait_for(self.session, lt.piece_finished_alert, timeout=5)
        handle.read_piece(0)
        alert = wait_for(self.session, lt.read_piece_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "read_piece")
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.error.value(), 0)
        if lt.api_version < 2:
            self.assertEqual(alert.ec.value(), 0)
        self.assertEqual(alert.buffer, self.torrent.pieces[0])
        self.assertEqual(alert.piece, 0)
        self.assertEqual(alert.size, len(self.torrent.pieces[0]))

    def test_error(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.set_piece_deadline(0, 0, flags=lt.deadline_flags_t.alert_when_available)
        # setting piece priority to 0 cancels a read_piece
        handle.piece_priority(0, 0)
        alert = wait_for(self.session, lt.read_piece_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "read_piece")
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.error.value(), errno.ECANCELED)
        self.assertEqual(alert.error.category(), lt.generic_category())
        if lt.api_version < 2:
            self.assertEqual(alert.ec.value(), errno.ECANCELED)
            self.assertEqual(alert.ec.category(), lt.generic_category())

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.set_piece_deadline(0, 0, flags=lt.deadline_flags_t.alert_when_available)
        # setting piece priority to 0 cancels a read_piece
        handle.piece_priority(0, 0)
        alert = wait_for(self.session, lt.read_piece_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.ec.value(), errno.ECANCELED)


class PeerAlertTest(TorrentAlertTest):
    def setUp(self) -> None:
        super().setUp()
        self.session.apply_settings({"close_redundant_connections": False})

        self.peer = lt.session(lib.get_isolated_settings())
        self.peer.apply_settings({"close_redundant_connections": False, "alert_mask": 0xffffffff})
        self.peer_dir = tempfile.TemporaryDirectory()
        self.peer_atp = self.torrent.atp()
        self.peer_atp.save_path = self.peer_dir.name
        self.peer_endpoint = ("127.0.0.1", self.peer.listen_port())
        self.peer_endpoint_str = f"{self.peer_endpoint[0]}:{self.peer_endpoint[1]}"
        self.peer_fingerprint = self.peer.get_settings()["peer_fingerprint"].encode()

    def tearDown(self) -> None:
        super().tearDown()
        remove_handles_and_wait(self.peer)
        lib.cleanup_with_windows_fix(self.peer_dir, timeout=5)

    def assert_peer_alert(
        self,
        alert: lt.peer_alert,
        endpoint: Tuple[str, int],
        *,
        pid: Optional[lt.sha1_hash] = None,
        fingerprint: Optional[bytes] = None,
    ) -> None:
        if lt.api_version < 2:
            self.assertEqual(alert.ip, endpoint)
        self.assertEqual(alert.endpoint, endpoint)
        if pid is not None:
            self.assertEqual(alert.pid, pid)
        if fingerprint is not None:
            self.assertEqual(alert.pid.to_bytes()[: len(fingerprint)], fingerprint)


class LambdaRequestHandler(http.server.BaseHTTPRequestHandler):
    default_request_version = "HTTP/1.1"

    def __init__(
        self, get_data: Callable[[], bytes], *args: Any, **kwargs: Any
    ) -> None:
        self.get_data = get_data
        super().__init__(*args, **kwargs)

    def do_GET(self) -> None:
        data = self.get_data()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


class TrackerAlertTest(TorrentAlertTest):
    RESPONSE = None

    def setUp(self) -> None:
        super().setUp()
        # TODO: this is currently necessary. Is it a bug?
        self.session.apply_settings({"ssrf_mitigation": False})
        self.tracker_response: Dict[bytes, Any] = {}
        self.tracker = http.server.HTTPServer(
            ("127.0.0.1", 0),
            functools.partial(
                LambdaRequestHandler, lambda: lt.bencode(self.tracker_response)
            ),
        )
        ctx = self.get_tracker_ssl_ctx()
        if ctx is not None:
            self.tracker.socket = ctx.wrap_socket(self.tracker.socket, server_side=True)
        self.tracker_thread = threading.Thread(target=self.tracker.serve_forever)
        self.tracker_thread.start()
        # HTTPServer.server_name seems to resolve to things like
        # "localhost.localdomain"
        port = self.tracker.server_port
        proto = "http" if ctx is None else "https"
        self.tracker_url = f"{proto}://127.0.0.1:{port}/announce"

    def tearDown(self) -> None:
        super().tearDown()
        self.tracker.shutdown()
        # Explicitly clean up server sockets, to avoid ResourceWarning
        self.tracker.server_close()

    def get_tracker_ssl_ctx(self) -> Optional[ssl.SSLContext]:
        return None

    def assert_tracker_alert(self, alert: lt.tracker_alert) -> None:
        if lt.api_version < 2:
            self.assertEqual(alert.url, self.tracker_url)
        self.assertEqual(alert.tracker_url(), self.tracker_url)
        endpoint = alert.local_endpoint
        self.assertIsInstance(endpoint, tuple)
        self.assertEqual(len(endpoint), 2)
        self.assertEqual(endpoint[0], "127.0.0.1")
        self.assertIsInstance(endpoint[1], int)


class TrackerErrorAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.tracker

    def test_tracker_error_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.tracker_response = {
            b"failure reason": b"test",
        }
        handle.add_tracker({"url": self.tracker_url})
        alert = wait_for(self.session, lt.tracker_error_alert, timeout=5)

        self.assert_alert(
            alert, lt.alert_category.tracker | lt.alert_category.error, "tracker_error"
        )
        self.assert_torrent_alert(alert, handle)
        self.assert_tracker_alert(alert)
        if lt.api_version < 2:
            self.assertEqual(alert.msg, "test")
            self.assertEqual(alert.status_code, -1)
        self.assertEqual(alert.error_message(), "test")
        self.assertEqual(alert.failure_reason(), "test")
        self.assertEqual(alert.times_in_row, 1)
        self.assertEqual(alert.error.category(), lt.libtorrent_category())
        self.assertEqual(alert.error.value(), 173)  # "tracker sent a failure message"

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.tracker_response = {
            b"failure reason": b"test",
        }
        handle.add_tracker({"url": self.tracker_url})
        alert = wait_for(self.session, lt.tracker_error_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.url, self.tracker_url)
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.msg, "test")
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.status_code, -1)


class TrackerWarningAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.tracker

    def test_tracker_warning_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.tracker_response = {b"warning message": b"test"}
        handle.add_tracker({"url": self.tracker_url})
        alert = wait_for(self.session, lt.tracker_warning_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.tracker | lt.alert_category.error,
            "tracker_warning",
        )
        self.assert_torrent_alert(alert, handle)
        self.assert_tracker_alert(alert)
        # the actual warning message is not bound
        self.assertIn("warning: test", alert.message())


class TrackerReplyAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.tracker

    def test_tracker_reply_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.tracker_response = {b"peers": [{b"ip": b"127.0.0.1", b"port": 65535}]}
        handle.add_tracker({"url": self.tracker_url})
        alert = wait_for(self.session, lt.tracker_reply_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.tracker, "tracker_reply")
        self.assert_torrent_alert(alert, handle)
        self.assert_tracker_alert(alert)
        self.assertEqual(alert.num_peers, 1)


class TrackerAnnounceAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.tracker

    def test_tracker_reply_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.add_tracker({"url": self.tracker_url})
        alert = wait_for(self.session, lt.tracker_announce_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.tracker, "tracker_announce")
        self.assert_torrent_alert(alert, handle)
        self.assert_tracker_alert(alert)


class HashFailedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_hash_failed_alert_test(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add_piece() does not work in the checking_* states
        wait_until_done_checking(handle, timeout=5)
        # add_piece() with bad data
        handle.add_piece(0, b"a" * len(self.torrent.pieces[0]), 0)

        alert = wait_for(self.session, lt.hash_failed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "hash_failed")
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.piece_index, 0)


class PeerBanAlertTest(PeerAlertTest):
    @unittest.skip("TODO: Probably need to mock a bad actor peer")
    def test_peer_ban_alert(self) -> None:
        # peer_ban_alert is raised when libtorrent finds a peer sending
        # data that fails hash check. how do we convince the peer session
        # to do this?
        raise NotImplementedError


class PeerErrorAlertTest(PeerAlertTest):
    @unittest.skip("TODO: Probably need to mock a bad actor peer")
    def test_peer_error_alert(self) -> None:
        # peer_error_alert is raised in various cases of peers doing invalid
        # protocol things
        raise NotImplementedError


class InvalidRequestAlertTest(PeerAlertTest):
    @unittest.skip("TODO: Probably need to mock a bad actor peer")
    def test_invalid_request_alert(self) -> None:
        # peer_error_alert is raised in various cases of peers doing invalid
        # protocol things
        raise NotImplementedError


class TorrentErrorAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    @unittest.skipIf(sys.platform == "win32", "TODO: make this work on windows")
    def test_torrent_error_alert(self) -> None:
        # create the target file as a directory
        os.mkdir(self.file_path)
        handle = self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.torrent_error_alert, timeout=5)

        self.assert_alert(
            alert, lt.alert_category.error | lt.alert_category.status, "torrent_error"
        )
        self.assert_torrent_alert(alert, handle)
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)
        # NB: posix storage results in different error codes than mmap storage
        self.assertIsInstance(alert.error.category(), lt.error_category)


class TorrentFinishedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_torrent_finished_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add_piece() doesn't work in checking state
        wait_until_done_checking(handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)

        alert = wait_for(self.session, lt.torrent_finished_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "torrent_finished")
        self.assert_torrent_alert(alert, handle)


class PieceFinishedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.piece_progress

    def test_piece_finished_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add_piece() doesn't work in checking state
        wait_until_done_checking(handle, timeout=5)
        handle.add_piece(0, self.torrent.pieces[0], 0)

        alert = wait_for(self.session, lt.piece_finished_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.piece_progress | maybe_progress_notification,
            "piece_finished",
        )
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.piece_index, 0)


class BlockFinishedAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.block_progress | 0xffffffffff

    def test_block_finished_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        # add_piece() doesn't work in checking state
        wait_until_done_checking(peer_handle, timeout=5)
        peer_handle.add_piece(0, self.torrent.pieces[0], 0)
        wait_for(self.peer, lt.piece_finished_alert, timeout=10, prefix="PEER")
        handle.connect_peer(self.peer_endpoint)
        for i in range(10):
            for alert in self.peer.pop_alerts():
                logging.debug("PEER: %s: %s", alert.what(), alert.message())
            time.sleep(1.0)

        alert = wait_for(self.session, lt.block_finished_alert, timeout=10, prefix="MAIN")

        self.assert_alert(
            alert,
            lt.alert_category.block_progress | maybe_progress_notification,
            "block_finished",
        )
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.piece_index, 0)
        self.assertEqual(alert.block_index, 0)


class BlockDownloadingAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.block_progress

    def test_block_downloading_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        # add_piece() doesn't work in checking state
        wait_until_done_checking(peer_handle, timeout=5)
        peer_handle.add_piece(0, self.torrent.pieces[0], 0)
        handle.connect_peer(self.peer_endpoint)

        alert = wait_for(self.session, lt.block_downloading_alert, timeout=10)

        self.assert_alert(
            alert,
            lt.alert_category.block_progress | maybe_progress_notification,
            "block_downloading",
        )
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.piece_index, 0)
        self.assertEqual(alert.block_index, 0)
        if lt.api_version < 2:
            self.assertIsInstance(alert.peer_speedmsg, str)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        # add_piece() doesn't work in checking state
        wait_until_done_checking(peer_handle, timeout=5)
        peer_handle.add_piece(0, self.torrent.pieces[0], 0)
        handle.connect_peer(self.peer_endpoint)

        alert = wait_for(self.session, lt.block_downloading_alert, timeout=10)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(alert.peer_speedmsg, str)


class StorageMovedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_storage_moved_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        wait_until_done_checking(handle, timeout=5)
        new = os.path.join(self.dir.name, "new")
        handle.move_storage(new)

        alert = wait_for(self.session, lt.storage_moved_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "storage_moved")
        self.assert_torrent_alert(alert, handle)
        if lt.api_version < 2:
            self.assertEqual(alert.path, new)
        self.assertEqual(alert.storage_path(), new)
        self.assertEqual(alert.old_path(), self.dir.name)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        wait_until_done_checking(handle, timeout=5)
        new = os.path.join(self.dir.name, "new")
        handle.move_storage(new)

        alert = wait_for(self.session, lt.storage_moved_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.path, new)


class StorageMovedFailedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_storage_moved_failed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add some data, otherwise storage_moved_failed_alert isn't triggered
        wait_until_done_checking(handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        # create a file at the new path
        new = os.path.join(self.dir.name, "new")
        with open(new, mode="wb") as fp:
            fp.write(b"data")
        handle.move_storage(new)

        alert = wait_for(self.session, lt.storage_moved_failed_alert, timeout=5)

        print(f"{alert.error.message()} : {alert.error.category().name()}")

        self.assert_alert(alert, lt.alert_category.storage, "storage_moved_failed")
        self.assert_torrent_alert(alert, handle)
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)
        self.assertTrue(
            alert.error.category() == lt.system_category()
            or alert.error.category() == lt.generic_category()
        )
        self.assertEqual(alert.file_path(), self.file_path)
        self.assertTrue(
            alert.op in [lt.operation_t.file_open, lt.operation_t.file_rename]
        )
        if lt.api_version < 2:
            self.assertTrue(alert.operation in ["file_open", "file_rename"])

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add some data, otherwise storage_moved_failed_alert isn't triggered
        wait_until_done_checking(handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        # create a file at the new path
        new = os.path.join(self.dir.name, "new")
        with open(new, mode="wb") as fp:
            fp.write(b"data")
        handle.move_storage(new)

        alert = wait_for(self.session, lt.storage_moved_failed_alert, timeout=5)

        with self.assertWarns(DeprecationWarning):
            self.assertEqual(alert.operation, "file_rename")


class TorrentDeletedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_torrent_deleted_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add some data, otherwise storage_moved_failed_alert isn't triggered
        wait_until_done_checking(handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        self.session.remove_torrent(
            handle, option=lt.session.delete_files | lt.session.delete_partfile
        )

        alert = wait_for(self.session, lt.torrent_deleted_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "torrent_deleted")
        self.assert_torrent_alert(alert, handle)
        if lt.api_version < 2:
            self.assertEqual(alert.info_hash, self.torrent.sha1_hash)
        self.assertEqual(alert.info_hashes.v1, self.torrent.sha1_hash)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        # add some data, otherwise storage_moved_failed_alert isn't triggered
        wait_until_done_checking(handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        self.session.remove_torrent(
            handle, option=lt.session.delete_files | lt.session.delete_partfile
        )

        alert = wait_for(self.session, lt.torrent_deleted_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.info_hash, self.torrent.sha1_hash)


class TorrentPausedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_torrent_paused_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.pause()

        alert = wait_for(self.session, lt.torrent_paused_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "torrent_paused")
        self.assert_torrent_alert(alert, handle)


class TorrentCheckedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_torrent_checked_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.torrent_checked_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "torrent_checked")
        self.assert_torrent_alert(alert, handle)


class UrlSeedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.peer

    def test_url_seed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.add_url_seed("test://test")

        alert = wait_for(self.session, lt.url_seed_alert, timeout=5)

        self.assert_alert(
            alert, lt.alert_category.peer | lt.alert_category.error, "url_seed"
        )
        self.assert_torrent_alert(alert, handle)
        if lt.api_version < 2:
            self.assertEqual(alert.url, "test://test")
            self.assertIsInstance(alert.msg, str)
        self.assertEqual(alert.error.category(), lt.libtorrent_category())
        self.assertEqual(alert.error.value(), 24)  # unsupported URL protocol
        self.assertEqual(alert.server_url(), "test://test")
        # Seems to be always empty
        self.assertEqual(alert.error_message(), "")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.add_url_seed("test://test")

        alert = wait_for(self.session, lt.url_seed_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.url, "test://test")
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(alert.msg, str)


class FileErrorAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    @unittest.skipIf(sys.platform == "win32", "TODO: make this work on windows")
    def test_file_error_alert(self) -> None:
        # create the target file as a directory
        os.mkdir(self.file_path)
        handle = self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.file_error_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.error
            | lt.alert_category.storage
            | lt.alert_category.status,
            "file_error",
        )
        self.assert_torrent_alert(alert, handle)
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)
        # NB: posix storage results in different error codes than mmap storage
        self.assertIsInstance(alert.error.category(), lt.error_category)
        self.assertEqual(alert.filename(), self.file_path)
        if lt.api_version < 2:
            self.assertEqual(alert.file, self.file_path)
            self.assertEqual(alert.msg, alert.error.message())

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        # create the target file as a directory
        os.mkdir(self.file_path)
        self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.file_error_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.file, self.file_path)
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.msg, alert.error.message())


class MetadataFailedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.error

    def test_metadata_failed_alert(self) -> None:
        self.atp.ti = None
        self.atp.name = self.torrent_name
        self.atp.info_hashes = lt.info_hash_t(self.torrent.sha1_hash)
        handle = self.session.add_torrent(self.atp)
        handle.set_metadata(b"invalid")

        alert = wait_for(self.session, lt.metadata_failed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.error, "metadata_failed")
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.error.category(), lt.libtorrent_category())
        self.assertEqual(alert.error.value(), 30)  # invalid metadata received


class MetadataReceivedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_metadata_failed_alert(self) -> None:
        self.atp.ti = None
        self.atp.name = self.torrent_name
        self.atp.info_hashes = lt.info_hash_t(self.torrent.sha1_hash)
        handle = self.session.add_torrent(self.atp)
        handle.set_metadata(lt.bencode(self.torrent.info))

        alert = wait_for(self.session, lt.metadata_received_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "metadata_received")
        self.assert_torrent_alert(alert, handle)


class ListenFailedAlertTest(AlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_listen_failed_alert(self) -> None:
        self.session.apply_settings({"listen_interfaces": "does-not-exist"})

        alert = wait_for(self.session, lt.listen_failed_alert, timeout=5)

        self.assert_alert(
            alert, lt.alert_category.status | lt.alert_category.error, "listen_failed"
        )
        self.assertEqual(alert.address, "0.0.0.0")
        self.assertEqual(alert.port, 0)
        self.assertEqual(alert.listen_interface(), "does-not-exist")
        self.assertEqual(alert.error.category(), lt.libtorrent_category())
        self.assertEqual(alert.error.value(), 32)  # parse error
        self.assertEqual(alert.op, lt.operation_t.parse_address)
        if lt.api_version < 2:
            self.assertEqual(alert.endpoint, ("0.0.0.0", 0))
            self.assertEqual(alert.operation, 0)
            self.assertIsInstance(alert.sock_type, lt.listen_failed_alert_socket_type_t)
        self.assertIsInstance(alert.socket_type, lt.socket_type_t)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        self.session.apply_settings({"listen_interfaces": "does-not-exist"})

        alert = wait_for(self.session, lt.listen_failed_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.endpoint, ("0.0.0.0", 0))
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.operation, 0)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    alert.sock_type, lt.listen_failed_alert_socket_type_t
                )


class ListenSucceededAlertTest(AlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_listen_succeeded_alert(self) -> None:
        alert = wait_for(self.session, lt.listen_succeeded_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "listen_succeeded")
        if lt.api_version < 2:
            self.assertEqual(alert.endpoint, ("127.0.0.1", self.session.listen_port()))
            self.assertIsInstance(
                alert.sock_type, lt.listen_succeeded_alert_socket_type_t
            )
        self.assertEqual(alert.address, "127.0.0.1")
        self.assertEqual(alert.port, self.session.listen_port())
        self.assertIsInstance(alert.socket_type, lt.socket_type_t)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        alert = wait_for(self.session, lt.listen_succeeded_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(
                    alert.endpoint, ("127.0.0.1", self.session.listen_port())
                )
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    alert.sock_type, lt.listen_failed_alert_socket_type_t
                )


class PortmapErrorAlertTest(AlertTest):
    @unittest.skip("TODO: how do we test this in isolation?")
    def test_portmap_error_alert(self) -> None:
        raise NotImplementedError


class PortmapAlertTest(AlertTest):
    @unittest.skip("TODO: how do we test this in isolation?")
    def test_portmap_alert(self) -> None:
        raise NotImplementedError


class PortmapLogAlertTest(AlertTest):
    @unittest.skip("TODO: how do we test this in isolation?")
    def test_portmap_log_alert(self) -> None:
        raise NotImplementedError


class FastresumeRejectedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_fastresume_rejected_alert(self) -> None:
        # setting atp.have_pieces makes libtorrent think of the atp as
        # "resume data", triggering the alert
        self.atp.have_pieces = [True] * len(self.torrent.pieces)
        # create the target file as a directory
        os.mkdir(self.file_path)
        handle = self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.fastresume_rejected_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.error | lt.alert_category.status,
            "fastresume_rejected",
        )
        self.assert_torrent_alert(alert, handle)
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)
        self.assertEqual(alert.error.category(), lt.libtorrent_category())
        self.assertEqual(alert.file_path(), self.file_path)
        self.assertEqual(alert.op, lt.operation_t.check_resume)
        if lt.api_version < 2:
            self.assertEqual(alert.operation, "check_resume")
            self.assertIsInstance(alert.msg, str)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        # setting atp.have_pieces makes libtorrent think of the atp as
        # "resume data", triggering the alert
        self.atp.have_pieces = [True] * len(self.torrent.pieces)
        # create the target file as a directory
        os.mkdir(self.file_path)
        self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.fastresume_rejected_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.operation, "check_resume")
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(alert.msg, str)


class PeerBlockedAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.ip_block

    def test_peer_blocked_alert(self) -> None:
        # block all ips
        ipf = lt.ip_filter()
        ipf.add_rule("0.0.0.0", "255.255.255.255", 1)
        self.session.set_ip_filter(ipf)
        # add the torrent
        handle = self.session.add_torrent(self.atp)
        # add on the peer
        self.peer.add_torrent(self.peer_atp)
        # connect us to peer
        handle.connect_peer(self.peer_endpoint)

        alert = wait_for(self.session, lt.peer_blocked_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.ip_block, "peer_blocked")
        self.assert_torrent_alert(alert, handle)
        self.assert_peer_alert(alert, self.peer_endpoint, pid=lt.sha1_hash())
        if lt.api_version < 2:
            self.assertEqual(alert.ip, self.peer_endpoint)
        self.assertEqual(alert.reason, lt.reason_t.ip_filter)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        # block all ips
        ipf = lt.ip_filter()
        ipf.add_rule("0.0.0.0", "255.255.255.255", 1)
        self.session.set_ip_filter(ipf)
        # add the torrent
        handle = self.session.add_torrent(self.atp)
        # add on the peer
        self.peer.add_torrent(self.peer_atp)
        # connect us to peer
        handle.connect_peer(self.peer_endpoint)

        alert = wait_for(self.session, lt.peer_blocked_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.ip_block, "peer_blocked")
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(alert.ip, self.peer_endpoint)


class ScrapeReplyAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.tracker

    def test_scrape_reply_alert(self) -> None:
        self.tracker_response = {
            b"files": {
                self.torrent.sha1_hash.to_bytes(): {
                    b"complete": 123,
                    b"incomplete": 234,
                    b"downloaded": 345,
                    b"downloaders": 456,
                }
            }
        }
        handle = self.session.add_torrent(self.atp)
        handle.add_tracker({"url": self.tracker_url})
        handle.scrape_tracker()

        alert = wait_for(self.session, lt.scrape_reply_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.tracker, "scrape_reply")
        self.assert_torrent_alert(alert, handle)
        self.assert_tracker_alert(alert)
        self.assertEqual(alert.complete, 123)
        self.assertEqual(alert.incomplete, 234)


class ScrapeFailedAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.tracker

    def test_scrape_failed_alert(self) -> None:
        # leave the default empty tracker reply, to trigger an error
        handle = self.session.add_torrent(self.atp)
        handle.add_tracker({"url": self.tracker_url})
        handle.scrape_tracker()

        alert = wait_for(self.session, lt.scrape_failed_alert, timeout=5)

        self.assert_alert(
            alert, lt.alert_category.tracker | lt.alert_category.error, "scrape_failed"
        )
        self.assert_torrent_alert(alert, handle)
        self.assert_tracker_alert(alert)
        if lt.api_version < 2:
            self.assertEqual(alert.msg, alert.error.message())
        self.assertEqual(alert.error_message(), "")
        self.assertEqual(alert.error.category(), lt.libtorrent_category())
        self.assertEqual(alert.error.value(), 174)  # invalid files entry


class UdpErrorAlertTest(AlertTest):
    ALERT_MASK = lt.alert_category.error

    @unittest.skip("TODO: how do we test this?")
    def test_udp_error_alert(self) -> None:
        raise NotImplementedError


class ExternalIpAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_external_ip_alert(self) -> None:
        self.tracker_response = {
            b"external ip": b"\x01\x02\x03\x04",
        }
        handle = self.session.add_torrent(self.atp)
        handle.add_tracker({"url": self.tracker_url})

        alert = wait_for(self.session, lt.external_ip_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "external_ip")
        self.assertEqual(alert.external_address, "1.2.3.4")


class SaveResumeDataAlertTest(TorrentAlertTest):
    def test_save_resume_data_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.save_resume_data()

        alert = wait_for(self.session, lt.save_resume_data_alert, timeout=5)
        self.assert_alert(alert, lt.alert_category.storage, "save_resume_data")
        self.assert_torrent_alert(alert, handle)
        self.assertIsInstance(alert.params, lt.add_torrent_params)
        self.assertEqual(alert.params.info_hashes.v1, self.torrent.sha1_hash)
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                atp = lt.read_resume_data(lt.bencode(alert.resume_data))
            self.assertEqual(atp.info_hashes.v1, self.torrent.sha1_hash)


class FileCompletedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.file_progress

    def test_file_completed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        wait_until_done_checking(handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)

        alert = wait_for(self.session, lt.file_completed_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.file_progress | maybe_progress_notification,
            "file_completed",
        )
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.index, 0)


class FileRenamedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_file_renamed_alert_test(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.rename_file(0, "other.txt")

        alert = wait_for(self.session, lt.file_renamed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "file_renamed")
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.index, 0)
        if lt.api_version < 2:
            self.assertEqual(alert.name, "other.txt")
        self.assertEqual(alert.new_name(), "other.txt")
        ti = self.atp.ti
        assert ti is not None
        self.assertEqual(alert.old_name(), ti.files().file_path(0))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.rename_file(0, "other.txt")

        alert = wait_for(self.session, lt.file_renamed_alert, timeout=5)

        with self.assertWarns(DeprecationWarning):
            self.assertEqual(alert.name, "other.txt")


class FileRenameFailedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_file_rename_failed_alert_test(self) -> None:
        handle = self.session.add_torrent(self.atp)
        wait_until_done_checking(handle, timeout=5)
        # add the data
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        # mkdir at the new path
        new = os.path.join(self.dir.name, "other.txt")
        os.mkdir(new)
        # attempt to rename over the dir
        handle.rename_file(0, "other.txt")

        alert = wait_for(self.session, lt.file_rename_failed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "file_rename_failed")
        self.assert_torrent_alert(alert, handle)
        self.assertEqual(alert.index, 0)
        # NB: posix storage results in different error codes than mmap storage
        self.assertIsInstance(alert.error.category(), lt.error_category)
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)


class TorrentResumedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_torrent_resumed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.pause()
        handle.resume()

        alert = wait_for(self.session, lt.torrent_resumed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "torrent_resumed")
        self.assert_torrent_alert(alert, handle)


class StateChangedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.status

    def test_state_changed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.state_changed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "state_changed")
        self.assert_torrent_alert(alert, handle)
        self.assertIsInstance(alert.state, lt.torrent_status.states)
        self.assertIsInstance(alert.prev_state, lt.torrent_status.states)
        self.assertNotEqual(alert.state, alert.prev_state)


class StateUpdateAlertTest(TorrentAlertTest):
    def test_state_update_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.session.post_torrent_updates()

        alert = wait_for(self.session, lt.state_update_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "state_update")
        self.assertEqual(len(alert.status), 1)
        self.assertEqual(alert.status[0].handle, handle)


class I2pAlertTest(AlertTest):
    ALERT_MASK = lt.alert_category.error

    def test_i2p_alert(self) -> None:
        self.session.apply_settings({"i2p_hostname": "127.1.2.3"})

        alert = wait_for(self.session, lt.i2p_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.error, "i2p")
        self.assertEqual(alert.error.category(), lt.system_category())
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)


class DhtAlertTest(PeerAlertTest):
    def setUp(self) -> None:
        super().setUp()
        # Configure the peer to start dht, but with no routers. Add the dht
        # category to their alert mask, so we can wait for them to be done
        # bootstrapping
        peer_alert_mask = self.peer.get_settings()["alert_mask"]
        self.peer.apply_settings(
            {"enable_dht": True, "alert_mask": peer_alert_mask | lt.alert_category.dht}
        )
        # We will ping our routers during bootstrap, so make sure we wait for
        # the peer to be done bootstrapping before we bootstrap ourselves
        wait_for(self.peer, lt.dht_bootstrap_alert, timeout=5, prefix="peer")

        alert_mask = self.session.get_settings()["alert_mask"]
        self.session.apply_settings(
            {
                "enable_dht": True,
                "dht_bootstrap_nodes": self.peer_endpoint_str,
                "alert_mask": alert_mask | lt.alert_category.dht,
            }
        )
        # DHT functions won't work until our bootstrap is done
        self.bootstrap_alert = wait_for(self.session, lt.dht_bootstrap_alert, timeout=5)


class DhtReplyAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    @unittest.skip("TODO: why isn't this triggered?")
    def test_dht_reply_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        self.peer.add_torrent(self.peer_atp)
        handle.force_dht_announce()

        alert = wait_for(self.session, lt.dht_reply_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_reply")
        self.assertEqual(alert.num_peers, 1)


class DhtAnnounceAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    def test_dht_announce_alert(self) -> None:
        self.session.add_torrent(self.atp)
        self.peer.add_torrent(self.peer_atp)

        alert = wait_for(self.session, lt.dht_announce_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_announce")
        # The peer returns us as a peer, so we may get announces from ourselves
        self.assertIn(alert.ip, (self.peer_endpoint[0], self.endpoint[0]))
        self.assertIn(alert.port, (self.peer_endpoint[1], self.endpoint[1]))
        self.assertEqual(alert.info_hash, self.torrent.sha1_hash)


class DhtGetPeersAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    def test_dht_get_peers_alert(self) -> None:
        self.session.add_torrent(self.atp)
        self.peer.add_torrent(self.peer_atp)

        alert = wait_for(self.session, lt.dht_get_peers_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_get_peers")
        # DHT nodes apparently send lots of get_peers requests, not always for
        # a torrent's info-hash. We could wait for one that actually matches,
        # but I think this is sufficient
        self.assertIsInstance(alert.info_hash, lt.sha1_hash)
        self.assertFalse(alert.info_hash.is_all_zeros())


class PeerUnsnubbedAlertTest(PeerAlertTest):
    @unittest.skip("TODO: how do we test this?")
    def test_peer_unsnubbed_alert(self) -> None:
        raise NotImplementedError


class PeerSnubbedAlertTest(PeerAlertTest):
    @unittest.skip("TODO: how do we test this?")
    def test_peer_snubbed_alert(self) -> None:
        raise NotImplementedError


class PeerConnectAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.connect

    def test_peer_connect_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.connect_peer(self.peer_endpoint)
        alert = wait_for(self.session, lt.peer_connect_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.connect, "peer_connect")
        self.assert_torrent_alert(alert, handle)
        self.assert_peer_alert(alert, self.peer_endpoint, pid=lt.sha1_hash())

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.connect_peer(self.peer_endpoint)
        alert = wait_for(self.session, lt.peer_connect_alert, timeout=5)
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(alert.ip, self.peer_endpoint)


class PeerDisconnectedAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.connect

    def test_peer_disconnected_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        handle.connect_peer(self.peer_endpoint)
        wait_for(self.session, lt.peer_connect_alert, timeout=5)
        self.peer.remove_torrent(peer_handle)

        alert = wait_for(self.session, lt.peer_disconnected_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.connect, "peer_disconnected")
        self.assert_torrent_alert(alert, handle)
        self.assert_peer_alert(alert, self.peer_endpoint, pid=lt.sha1_hash())
        self.assertIsInstance(alert.socket_type, lt.socket_type_t)
        self.assertIsInstance(alert.op, lt.operation_t)
        self.assertIsInstance(alert.error.category(), lt.error_category)
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)
        self.assertIsInstance(alert.reason, lt.close_reason_t)
        if lt.api_version < 2:
            self.assertEqual(alert.msg, alert.error.message())

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        if lt.api_version < 2:
            handle = self.session.add_torrent(self.atp)
            peer_handle = self.peer.add_torrent(self.peer_atp)
            handle.connect_peer(self.peer_endpoint)
            self.peer.remove_torrent(peer_handle)

            alert = wait_for(self.session, lt.peer_disconnected_alert, timeout=15)

            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.msg, alert.error.message())


class RequestDroppedAlertTest(PeerAlertTest):
    @unittest.skip("TODO: is this dead?")
    def test_request_dropped_alert(self) -> None:
        raise NotImplementedError


class BlockTimeoutAlertTest(PeerAlertTest):
    @unittest.skip("TODO: how do we test this?")
    def test_block_timeout_alert(self) -> None:
        raise NotImplementedError


class UnwantedBlockAlertTest(PeerAlertTest):
    @unittest.skip("TODO: how do we test this?")
    def test_unwanted_block_alert(self) -> None:
        raise NotImplementedError


class TorrentDeleteFailedAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_torrent_delete_failed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        wait_until_done_checking(handle, timeout=5)
        # add the data
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        # wait until written
        for _ in lib.loop_until_timeout(5, msg="write"):
            try:
                with open(self.file_path, mode="rb") as fp:
                    if fp.read() == self.torrent.files[0].data:
                        break
            except FileNotFoundError:
                pass

        # Change the file to a non-empty directory, so libtorrent won't be able
        # to delete it.
        for _ in lib.loop_until_timeout(5):
            # On Windows, we expect anti-virus software to prevent us from
            # deleting the file for a short time.
            try:
                os.unlink(self.file_path)
                break
            except PermissionError:
                pass
        os.mkdir(self.file_path)
        with open(os.path.join(self.file_path, "test.txt"), mode="wb") as fp:
            pass

        # TODO: https://github.com/arvidn/libtorrent/issues/6158 : we'd like to
        # supply delete_files|delete_partfile here for safety, but it doesn't
        # currently work.
        self.session.remove_torrent(handle, option=lt.session.delete_files)

        alert = wait_for(self.session, lt.torrent_delete_failed_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.storage | lt.alert_category.error,
            "torrent_delete_failed",
        )
        self.assert_torrent_alert(alert, handle)
        if lt.api_version < 2:
            self.assertEqual(alert.msg, alert.error.message())
        error_value = alert.error.value()
        # On non-Windows, the error value is an errno. On Windows it's a
        # "winerror" value. This uses python's winerror-to-errno translation on
        # Windows only.
        errno_value = OSError(error_value, "", None, error_value).errno
        self.assertEqual(errno_value, errno.ENOTEMPTY)
        self.assertEqual(alert.error.category(), lt.system_category())
        if lt.api_version < 3:
            self.assertEqual(alert.info_hash, self.torrent.sha1_hash)
        self.assertEqual(alert.info_hashes.v1, self.torrent.sha1_hash)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        wait_until_done_checking(handle, timeout=5)
        # add the data
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        # wait until written
        for _ in lib.loop_until_timeout(5, msg="write"):
            with open(self.file_path, mode="rb") as fp:
                if fp.read() == self.torrent.files[0].data:
                    break

        # Change the file to a non-empty directory, so libtorrent won't be able
        # to delete it.
        for _ in lib.loop_until_timeout(5):
            # On Windows, we expect anti-virus software to prevent us from
            # deleting the file for a short time.
            try:
                os.unlink(self.file_path)
                break
            except PermissionError:
                pass
        os.mkdir(self.file_path)
        with open(os.path.join(self.file_path, "test.txt"), mode="wb") as fp:
            pass

        # remove the torrent and delete the torrent
        # TODO: https://github.com/arvidn/libtorrent/issues/6158 : we'd like to
        # supply delete_files|delete_partfile here for safety, but it doesn't
        # currently work.
        self.session.remove_torrent(handle, option=lt.session.delete_files)

        alert = wait_for(self.session, lt.torrent_delete_failed_alert, timeout=5)

        with self.assertWarns(DeprecationWarning):
            self.assertEqual(alert.msg, alert.error.message())
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(alert.info_hash, self.torrent.sha1_hash)


class SaveResumeDataFailedAlertTest(TorrentAlertTest):
    def test_save_resume_data_failed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.save_resume_data()
        handle.save_resume_data(flags=lt.save_resume_flags_t.only_if_modified)

        alert = wait_for(self.session, lt.save_resume_data_failed_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.storage | lt.alert_category.error,
            "save_resume_data_failed",
        )
        self.assert_torrent_alert(alert, handle)
        if lt.api_version < 2:
            self.assertEqual(alert.msg, alert.error.message())
        self.assertEqual(alert.error.category(), lt.libtorrent_category())
        self.assertEqual(alert.error.value(), 143)  # not modified since last save

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.save_resume_data()
        handle.save_resume_data(flags=lt.save_resume_flags_t.only_if_modified)

        alert = wait_for(self.session, lt.save_resume_data_failed_alert, timeout=5)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(alert.msg, alert.error.message())


class PerformanceAlertTest(AlertTest):
    ALERT_MASK = lt.alert_category.performance_warning

    def test_performance_alert(self) -> None:
        # create some inconsistent settings
        self.session.apply_settings(
            {"num_optimistic_unchoke_slots": 100, "unchoke_slots_limit": 100}
        )

        alert = wait_for(self.session, lt.performance_alert, timeout=5)
        self.assert_alert(alert, lt.alert_category.performance_warning, "performance")
        self.assertEqual(
            alert.warning_code,
            lt.performance_warning_t.too_many_optimistic_unchoke_slots,
        )
        self.assertFalse(alert.handle.is_valid())


class StatsAlert(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.stats

    def test_stats_alert(self) -> None:
        if lt.api_version < 3:
            handle = self.session.add_torrent(self.atp)

            alert = wait_for(self.session, lt.stats_alert, timeout=5)

            self.assert_alert(alert, lt.alert_category.stats, "stats")
            self.assert_torrent_alert(alert, handle)
            self.assertEqual(alert.transferred, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
            self.assertGreater(alert.interval, 0)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertTrue(issubclass(lt.stats_alert, lt.torrent_alert))


class CacheFlushedAlert(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_cache_flushed_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.flush_cache()

        alert = wait_for(self.session, lt.cache_flushed_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "cache_flushed")
        self.assert_torrent_alert(alert, handle)


class AnonymousModeAlertTest(TorrentAlertTest):
    @unittest.skip("TODO: is this dead?")
    def test_anonymous_mode_alert(self) -> None:
        raise NotImplementedError


class IncomingConnectionAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.peer

    def test_incoming_connection_alert(self) -> None:
        self.session.add_torrent(self.atp)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        peer_handle.connect_peer(self.endpoint)

        alert = wait_for(self.session, lt.incoming_connection_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.peer, "incoming_connection")
        self.assertIsInstance(alert.socket_type, lt.socket_type_t)
        if lt.api_version < 2:
            self.assertEqual(alert.ip, self.peer_endpoint)
        self.assertEqual(alert.endpoint, self.peer_endpoint)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        self.session.add_torrent(self.atp)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        peer_handle.connect_peer(self.endpoint)

        alert = wait_for(self.session, lt.incoming_connection_alert, timeout=5)

        with self.assertWarns(DeprecationWarning):
            self.assertEqual(alert.ip, self.peer_endpoint)


class TorrentNeedCertAlertTest(TorrentAlertTest):
    @unittest.skip("TODO: how do we test this?")
    def test_torrent_need_cert_alert(self) -> None:
        raise NotImplementedError


class AddTorrentAlertTest(TorrentAlertTest):
    def test_torrent_alert_properties(self) -> None:
        handle = self.session.add_torrent(self.atp)

        alert = wait_for(self.session, lt.add_torrent_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "add_torrent")
        self.assert_torrent_alert(alert, handle)


class DhtOutgoingGetPeersAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    def test_outgoing_get_peers_alert(self) -> None:
        self.session.add_torrent(self.atp)
        self.peer.add_torrent(self.peer_atp)

        alert = wait_for(self.session, lt.dht_outgoing_get_peers_alert, timeout=10)

        self.assert_alert(alert, lt.alert_category.dht, "dht_outgoing_get_peers")
        self.assertIsInstance(alert.info_hash, lt.sha1_hash)
        self.assertFalse(alert.info_hash.is_all_zeros())
        self.assertIsInstance(alert.obfuscated_info_hash, lt.sha1_hash)
        self.assertFalse(alert.obfuscated_info_hash.is_all_zeros())
        # Our peer may return us as a peer, so we may get alerts for querying
        # ourselves
        if lt.api_version < 2:
            self.assertIn(alert.ip, (self.peer_endpoint, self.endpoint))
        self.assertIn(alert.endpoint, (self.peer_endpoint, self.endpoint))

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        self.session.add_torrent(self.atp)
        self.peer.add_torrent(self.peer_atp)

        alert = wait_for(self.session, lt.dht_outgoing_get_peers_alert, timeout=10)

        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIn(alert.ip, (self.peer_endpoint, self.endpoint))


class LogAlertTest(AlertTest):
    ALERT_MASK = lt.alert_category.session_log

    def test_log_alert(self) -> None:
        alert = wait_for(self.session, lt.log_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.session_log, "log")
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(alert.msg(), str)
            with self.assertWarns(DeprecationWarning):
                self.assertNotEqual(alert.msg(), "")
        self.assertIsInstance(alert.log_message(), str)
        self.assertNotEqual(alert.log_message(), "")


class PeerLogAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.peer_log

    def test_peer_log_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.connect_peer(self.peer_endpoint)

        alert = wait_for(self.session, lt.peer_log_alert, timeout=5)
        self.assert_alert(alert, lt.alert_category.peer_log, "peer_log")
        self.assert_torrent_alert(alert, handle)
        self.assert_peer_alert(alert, self.peer_endpoint, pid=lt.sha1_hash())
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(alert.msg(), str)
            with self.assertWarns(DeprecationWarning):
                self.assertNotEqual(alert.msg(), "")
        self.assertIsInstance(alert.log_message(), str)
        self.assertNotEqual(alert.log_message(), "")


class PickerLogAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.picker_log

    def test_picker_log_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        wait_until_done_checking(peer_handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            peer_handle.add_piece(0, piece, 0)
        handle.connect_peer(self.peer_endpoint)

        alert = wait_for(self.session, lt.picker_log_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.picker_log, "picker_log")
        self.assert_torrent_alert(alert, handle)
        self.assert_peer_alert(
            alert, self.peer_endpoint, fingerprint=self.peer_fingerprint
        )
        self.assertIsInstance(alert.picker_flags, int)
        self.assertEqual(alert.blocks(), [(0, 0)])


class LsdErrorAlertTest(AlertTest):
    @unittest.skip("TODO: how do we test this?")
    def test_lsd_error_alert(self) -> None:
        raise NotImplementedError


class DhtStatsAlertTest(DhtAlertTest):
    def test_dht_stats_alert(self) -> None:
        self.session.post_dht_stats()

        alert = wait_for(self.session, lt.dht_stats_alert, timeout=5)
        self.assert_alert(alert, 0, "dht_stats")
        self.assertEqual(alert.active_requests, [])
        self.assertEqual(alert.routing_table, [{"num_nodes": 0, "num_replacements": 0}])


class DhtLogAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht_log

    def test_dht_alert_log_alert(self) -> None:
        # Generate some logs
        self.session.dht_announce(lt.sha1_hash(b"a" * 20))

        alert = wait_for(self.session, lt.dht_log_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht_log, "dht_log")
        self.assertIsInstance(alert.module, lt.dht_module_t)
        self.assertIsInstance(alert.log_message(), str)
        self.assertNotEqual(alert.log_message(), "")


class DhtPktAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht_log

    def test_dht_pkt_alert(self) -> None:
        # Generate some packets
        self.session.dht_announce(lt.sha1_hash(b"a" * 20))

        alert = wait_for(self.session, lt.dht_pkt_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht_log, "dht_pkt")
        self.assertIsInstance(alert.pkt_buf, bytes)
        self.assertNotEqual(alert.pkt_buf, b"")
        # Ensure it's in bencoded form
        lt.bencode(alert.pkt_buf)


class DhtImmutableItemAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    def test_dht_immutable_item_alert(self) -> None:
        item = {b"test": b"test"}
        sha1 = self.session.dht_put_immutable_item(item)
        # Wait for the put to complete, or the get will short-circuit
        wait_for(self.session, lt.dht_put_alert, timeout=5)
        self.session.dht_get_immutable_item(sha1)

        alert = wait_for(self.session, lt.dht_immutable_item_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_immutable_item")
        self.assertEqual(alert.target, sha1)
        self.assertEqual(alert.item, item)


class DhtMutableItemAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    def test_dht_mutable_item_alert(self) -> None:
        # NB: libtorrent uses private keys in hased-and-exponentiated form, not
        # concatenated <seed><pubkey> form used by some implementations
        pk = bytes.fromhex(
            "77ff84905a91936367c01360803104f92432fcd904a43511876df5cdf3e7e548"
        )
        sk = bytes.fromhex(
            "e06d3183d14159228433ed599221b80bd0a5ce8352e4bdf0262f76786ef1c74d"
            "b7e7a9fea2c0eb269d61e3b38e450a22e754941ac78479d6c54e1faf6037881d"
        )
        data = b"test"
        salt = b"salt"
        # Put the item, and wait for success
        self.session.dht_put_mutable_item(sk, pk, data, salt)
        wait_for(self.session, lt.dht_put_alert, timeout=5)
        self.session.dht_get_mutable_item(pk, salt)

        alert = wait_for(self.session, lt.dht_mutable_item_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_mutable_item")
        self.assertEqual(alert.key, pk)
        self.assertEqual(alert.item, data)
        self.assertEqual(
            alert.signature,
            bytes.fromhex(
                "aeaa269b37bab7f87126c8d0cc31fb9c88ea81f0d0d5338166d103be8ea16f16"
                "2d017ce812926a24dc697beb6915bec986e9eb0a37f262150cd90f2add54f805"
            ),
        )
        self.assertEqual(alert.salt, salt)
        self.assertEqual(alert.seq, 1)
        self.assertFalse(alert.authoritative)


class DhtPutAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    def test_dht_put_alert_with_immutable(self) -> None:
        item = {b"test": b"test"}
        sha1 = self.session.dht_put_immutable_item(item)

        alert = wait_for(self.session, lt.dht_put_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_put")
        self.assertEqual(alert.target, sha1)
        self.assertEqual(alert.public_key, b"\0" * 32)
        self.assertEqual(alert.signature, b"\0" * 64)
        self.assertEqual(alert.salt, b"")
        self.assertEqual(alert.seq, 0)
        # In our current setup, our peer returns us as a neighbor of itself,
        # so we end up forwarding the put request to ourselves, so
        # num_success is actually 2. This may change in the future, but it
        # should at least be 1.
        self.assertGreater(alert.num_success, 0)

    def test_dht_put_alert_with_mutable(self) -> None:
        # NB: libtorrent uses private keys in hased-and-exponentiated form, not
        # concatenated <seed><pubkey> form used by some implementations
        pk = bytes.fromhex(
            "77ff84905a91936367c01360803104f92432fcd904a43511876df5cdf3e7e548"
        )
        sk = bytes.fromhex(
            "e06d3183d14159228433ed599221b80bd0a5ce8352e4bdf0262f76786ef1c74d"
            "b7e7a9fea2c0eb269d61e3b38e450a22e754941ac78479d6c54e1faf6037881d"
        )
        data = b"test"
        salt = b"salt"
        self.session.dht_put_mutable_item(sk, pk, data, salt)

        alert = wait_for(self.session, lt.dht_put_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_put")
        self.assertEqual(alert.target, lt.sha1_hash())
        self.assertEqual(alert.public_key, pk)
        self.assertEqual(
            alert.signature,
            bytes.fromhex(
                "aeaa269b37bab7f87126c8d0cc31fb9c88ea81f0d0d5338166d103be8ea16f16"
                "2d017ce812926a24dc697beb6915bec986e9eb0a37f262150cd90f2add54f805"
            ),
        )
        self.assertEqual(alert.salt, salt)
        self.assertEqual(alert.seq, 1)
        # In our current setup, our peer returns us as a neighbor of itself,
        # so we end up forwarding the put request to ourselves, so
        # num_success is actually 2. This may change in the future, but it
        # should at least be 1.
        self.assertGreater(alert.num_success, 0)


class SessionStatsAlertTest(AlertTest):
    def test_session_stats_alert(self) -> None:
        self.session.post_session_stats()

        alert = wait_for(self.session, lt.session_stats_alert, timeout=5)

        self.assert_alert(alert, 0, "session_stats")
        self.assertIsInstance(alert.values, dict)
        self.assertEqual(
            set(alert.values.keys()),
            set(metric.name for metric in lt.session_stats_metrics()),
        )
        self.assertEqual(
            [v for v in alert.values.values() if not isinstance(v, int)], []
        )


class SessionStatsHeaderAlertTest(AlertTest):
    def test_session_stats_alert(self) -> None:
        self.session.post_session_stats()

        alert = wait_for(self.session, lt.session_stats_header_alert, timeout=5)

        self.assert_alert(alert, 0, "session_stats_header")


class DhtGetPeersReplyAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht_operation

    def test_get_peers_dht_reply_alert(self) -> None:
        self.session.add_torrent(self.atp)
        # Wait for the peer to reflect the announce back to us
        wait_for(self.session, lt.dht_announce_alert, timeout=5)
        self.session.dht_get_peers(self.torrent.sha1_hash)

        alert = wait_for(self.session, lt.dht_get_peers_reply_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht_operation, "dht_get_peers_reply")
        self.assertEqual(alert.info_hash, self.torrent.sha1_hash)
        self.assertEqual(alert.num_peers(), 1)
        self.assertEqual(alert.peers(), [self.endpoint])


class BlockUploadedAlertTest(PeerAlertTest):
    ALERT_MASK = lt.alert_category.upload

    def test_block_uploaded_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        wait_until_done_checking(handle, timeout=5)
        for i, piece in enumerate(self.torrent.pieces):
            handle.add_piece(i, piece, 0)
        peer_handle = self.peer.add_torrent(self.peer_atp)
        peer_handle.connect_peer(self.endpoint)

        alert = wait_for(self.session, lt.block_uploaded_alert, timeout=5)

        self.assert_alert(
            alert,
            lt.alert_category.upload | maybe_progress_notification,
            "block_uploaded",
        )
        self.assertIsInstance(alert.block_index, int)
        self.assertIsInstance(alert.piece_index, int)


class AlertsDroppedAlertTest(AlertTest):
    def test_alerts_dropped_alert(self) -> None:
        self.session.apply_settings({"alert_queue_size": 1})
        self.session.pop_alerts()
        for _ in range(100):
            self.session.post_session_stats()

        alert = wait_for(self.session, lt.alerts_dropped_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.error, "alerts_dropped")
        # The alert types aren't mapped anywhere, but this should indicate
        # session_stats_alert
        self.assertEqual(alert.dropped_alerts.index(True), 70)


class Socks5AlertTest(AlertTest):
    ALERT_MASK = lt.alert_category.error

    @unittest.skipIf(sys.platform == "darwin", "TODO: doesn't fire on osx")
    def test_socks5_alert(self) -> None:
        self.session.apply_settings(
            {
                "proxy_type": lt.proxy_type_t.socks5,
                "proxy_hostname": "127.1.2.3",
                "proxy_port": 12345,
            }
        )

        alert = wait_for(self.session, lt.socks5_alert, timeout=15)

        self.assert_alert(alert, lt.alert_category.error, "socks5")
        self.assertIsInstance(alert.error.value(), int)
        self.assertNotEqual(alert.error.value(), 0)
        self.assertEqual(alert.error.category(), lt.system_category())
        self.assertEqual(alert.op, lt.operation_t.connect)
        self.assertEqual(alert.ip, ("127.1.2.3", 12345))


class FilePrioAlertTest(TorrentAlertTest):
    ALERT_MASK = lt.alert_category.storage

    def test_file_prio_alert(self) -> None:
        handle = self.session.add_torrent(self.atp)
        handle.file_priority(0, 0)

        alert = wait_for(self.session, lt.file_prio_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.storage, "file_prio")
        self.assert_torrent_alert(alert, handle)


class DhtLiveNodesAlertTest(DhtAlertTest):
    def test_dht_live_nodes_alert(self) -> None:
        self.session.dht_live_nodes(self.torrent.sha1_hash)

        alert = wait_for(self.session, lt.dht_live_nodes_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.dht, "dht_live_nodes")
        self.assertEqual(alert.node_id, self.torrent.sha1_hash)
        self.assertEqual(alert.num_nodes, 0)
        self.assertEqual(alert.nodes, [])


class DhtSampleInfohashesAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht_operation

    def test_dht_sample_infohashes_alert(self) -> None:
        self.session.add_torrent(self.peer_atp)
        # Wait for the peer to reflect the announce back to us
        wait_for(self.session, lt.dht_announce_alert, timeout=5)
        self.session.dht_sample_infohashes(self.peer_endpoint, lt.sha1_hash())

        alert = wait_for(self.session, lt.dht_sample_infohashes_alert, timeout=5)

        self.assert_alert(
            alert, lt.alert_category.dht_operation, "dht_sample_infohashes"
        )
        self.assertEqual(alert.endpoint, self.peer_endpoint)
        self.assertEqual(
            alert.interval,
            datetime.timedelta(
                seconds=self.session.get_settings()["dht_sample_infohashes_interval"]
            ),
        )
        self.assertEqual(alert.num_infohashes, 1)
        self.assertEqual(alert.num_samples, 1)
        self.assertEqual(alert.samples, [self.torrent.sha1_hash])
        self.assertEqual(alert.num_nodes, 1)
        # TODO: how do we get our node id, to make this simpler?
        self.assertEqual(len(alert.nodes), 1)
        self.assertEqual(alert.nodes[0]["endpoint"], self.endpoint)
        self.assertIsInstance(alert.nodes[0]["nid"], lt.sha1_hash)
        self.assertFalse(alert.nodes[0]["nid"].is_all_zeros())


class DhtBootstrapAlertTest(DhtAlertTest):
    ALERT_MASK = lt.alert_category.dht

    def test_dht_bootstrap_alert(self) -> None:
        self.assert_alert(self.bootstrap_alert, lt.alert_category.dht, "dht_bootstrap")


class SSLTrackerAlertTest(TrackerAlertTest):
    ALERT_MASK = lt.alert_category.status

    def get_tracker_ssl_ctx(self) -> ssl.SSLContext:
        ctx = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
        cert_path = (
            pathlib.Path(__file__).parent.parent.parent.parent
            / "test"
            / "ssl"
            / "server.pem"
        )
        ctx.load_cert_chain(cert_path)
        return ctx

    def setUp(self) -> None:
        super().setUp()

        # I couldn't get validation to work on all platforms. Setting
        # SSL_CERT_FILE to our self-signed cert works on linux and mac, but
        # not on Windows.
        self.session.apply_settings({"validate_https_trackers": False})

    def test_external_ip_alert_via_ssl_tracker(self) -> None:
        self.tracker_response = {
            b"external ip": b"\x01\x02\x03\x04",
        }
        handle = self.session.add_torrent(self.atp)
        handle.add_tracker({"url": self.tracker_url})

        alert = wait_for(self.session, lt.external_ip_alert, timeout=5)

        self.assert_alert(alert, lt.alert_category.status, "external_ip")
        self.assertEqual(alert.external_address, "1.2.3.4")
