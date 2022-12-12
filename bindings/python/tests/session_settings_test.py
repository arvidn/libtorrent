import unittest

import libtorrent as lt


class EnumTest(unittest.TestCase):
    def test_choking_algorithm_t(self) -> None:
        self.assertIsInstance(lt.choking_algorithm_t.fixed_slots_choker, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.choking_algorithm_t.auto_expand_choker, int)
            self.assertIsInstance(lt.choking_algorithm_t.bittyrant_choker, int)
        self.assertIsInstance(lt.choking_algorithm_t.rate_based_choker, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_choking_algorithm_t_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.choking_algorithm_t.auto_expand_choker, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.choking_algorithm_t.bittyrant_choker, int)

    def test_seed_choking_algorithm_t(self) -> None:
        self.assertIsInstance(lt.seed_choking_algorithm_t.round_robin, int)
        self.assertIsInstance(lt.seed_choking_algorithm_t.fastest_upload, int)
        self.assertIsInstance(lt.seed_choking_algorithm_t.anti_leech, int)

    def test_suggest_mode_t(self) -> None:
        self.assertIsInstance(lt.suggest_mode_t.no_piece_suggestions, int)
        self.assertIsInstance(lt.suggest_mode_t.suggest_read_cache, int)

    def test_io_buffer_mode_t(self) -> None:
        self.assertIsInstance(lt.io_buffer_mode_t.enable_os_cache, int)
        if lt.api_version < 2:
            self.assertIsInstance(
                lt.io_buffer_mode_t.disable_os_cache_for_aligned_files, int
            )
        self.assertIsInstance(lt.io_buffer_mode_t.disable_os_cache, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_io_buffer_mode_t_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(
                    lt.io_buffer_mode_t.disable_os_cache_for_aligned_files, int
                )

    def test_bandwidth_mixed_algo_t(self) -> None:
        self.assertIsInstance(lt.bandwidth_mixed_algo_t.prefer_tcp, int)
        self.assertIsInstance(lt.bandwidth_mixed_algo_t.peer_proportional, int)

    def test_enc_policy(self) -> None:
        self.assertIsInstance(lt.enc_policy.pe_forced, int)
        self.assertIsInstance(lt.enc_policy.pe_enabled, int)
        self.assertIsInstance(lt.enc_policy.pe_disabled, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.enc_policy.forced, int)
            self.assertIsInstance(lt.enc_policy.enabled, int)
            self.assertIsInstance(lt.enc_policy.disabled, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_enc_policy_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.enc_policy.forced, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.enc_policy.enabled, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.enc_policy.disabled, int)

    def test_enc_level(self) -> None:
        self.assertIsInstance(lt.enc_level.pe_rc4, int)
        self.assertIsInstance(lt.enc_level.pe_plaintext, int)
        self.assertIsInstance(lt.enc_level.pe_both, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.enc_level.rc4, int)
            self.assertIsInstance(lt.enc_level.plaintext, int)
            self.assertIsInstance(lt.enc_level.both, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_enc_level_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.enc_level.rc4, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.enc_level.plaintext, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.enc_level.both, int)

    def test_proxy_type_t(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.proxy_type_t.none, int)
            self.assertIsInstance(lt.proxy_type_t.socks4, int)
            self.assertIsInstance(lt.proxy_type_t.socks5, int)
            self.assertIsInstance(lt.proxy_type_t.socks5_pw, int)
            self.assertIsInstance(lt.proxy_type_t.http, int)
            self.assertIsInstance(lt.proxy_type_t.http_pw, int)
            self.assertIsInstance(lt.proxy_type_t.i2p_proxy, int)

    def test_proxy_type(self) -> None:
        if lt.api_version < 2:
            self.assertIsInstance(lt.proxy_type_t.proxy_type.none, int)
            self.assertIsInstance(lt.proxy_type_t.proxy_type.socks4, int)
            self.assertIsInstance(lt.proxy_type_t.proxy_type.socks5, int)
            self.assertIsInstance(lt.proxy_type_t.proxy_type.socks5_pw, int)
            self.assertIsInstance(lt.proxy_type_t.proxy_type.http, int)
            self.assertIsInstance(lt.proxy_type_t.proxy_type.http_pw, int)
            self.assertIsInstance(lt.proxy_type_t.proxy_type.i2p_proxy, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_proxy_type_deprecated(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.proxy_type_t.proxy_type.none, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.proxy_type_t.proxy_type.socks4, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.proxy_type_t.proxy_type.socks5, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.proxy_type_t.proxy_type.socks5_pw, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.proxy_type_t.proxy_type.http, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.proxy_type_t.proxy_type.http_pw, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.proxy_type_t.proxy_type.i2p_proxy, int)


class ProxySettingsTest(unittest.TestCase):
    def test_proxy_settings(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                ps = lt.proxy_type_t.proxy_settings()

            ps.hostname = "host"
            self.assertEqual(ps.hostname, "host")

            ps.port = 123
            self.assertEqual(ps.port, 123)

            ps.password = "pass"
            self.assertEqual(ps.password, "pass")

            ps.username = "user"
            self.assertEqual(ps.username, "user")

            ps.type = lt.proxy_type_t.http
            self.assertEqual(ps.type, lt.proxy_type_t.http)

            self.assertTrue(ps.proxy_peer_connections)
            ps.proxy_peer_connections = False
            self.assertFalse(ps.proxy_peer_connections)

            self.assertTrue(ps.proxy_hostnames)
            ps.proxy_hostnames = False
            self.assertFalse(ps.proxy_hostnames)


class DhtSettingsTest(unittest.TestCase):
    def test_dht_settings(self) -> None:
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                ds = lt.dht_settings()

            ds.max_peers_reply = 123
            self.assertEqual(ds.max_peers_reply, 123)

            ds.search_branching = 123
            self.assertEqual(ds.search_branching, 123)

            ds.max_fail_count = 123
            self.assertEqual(ds.max_fail_count, 123)

            ds.max_torrents = 123
            self.assertEqual(ds.max_torrents, 123)

            ds.max_dht_items = 123
            self.assertEqual(ds.max_dht_items, 123)

            self.assertTrue(ds.restrict_routing_ips)
            ds.restrict_routing_ips = False
            self.assertFalse(ds.restrict_routing_ips)

            self.assertTrue(ds.restrict_search_ips)
            ds.restrict_search_ips = False
            self.assertFalse(ds.restrict_search_ips)

            ds.max_torrent_search_reply = 123
            self.assertEqual(ds.max_torrent_search_reply, 123)

            self.assertTrue(ds.extended_routing_table)
            ds.extended_routing_table = False
            self.assertFalse(ds.extended_routing_table)

            self.assertTrue(ds.aggressive_lookups)
            ds.aggressive_lookups = False
            self.assertFalse(ds.aggressive_lookups)

            self.assertFalse(ds.privacy_lookups)
            ds.privacy_lookups = False
            self.assertFalse(ds.privacy_lookups)

            self.assertFalse(ds.enforce_node_id)
            ds.enforce_node_id = True
            self.assertTrue(ds.enforce_node_id)

            self.assertTrue(ds.ignore_dark_internet)
            ds.ignore_dark_internet = False
            self.assertFalse(ds.ignore_dark_internet)

            ds.block_timeout = 123
            self.assertEqual(ds.block_timeout, 123)

            ds.block_ratelimit = 123
            self.assertEqual(ds.block_ratelimit, 123)

            self.assertFalse(ds.read_only)
            ds.read_only = True
            self.assertTrue(ds.read_only)

            ds.item_lifetime = 123
            self.assertEqual(ds.item_lifetime, 123)


class PeSettingsTest(unittest.TestCase):
    def test_pe_settings(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                pe = lt.pe_settings()

            pe.out_enc_policy = 123
            self.assertEqual(pe.out_enc_policy, 123)

            pe.in_enc_policy = 123
            self.assertEqual(pe.in_enc_policy, 123)

            pe.allowed_enc_level = 123
            self.assertEqual(pe.allowed_enc_level, 123)

            self.assertFalse(pe.prefer_rc4)
            pe.prefer_rc4 = True
            self.assertTrue(pe.prefer_rc4)
