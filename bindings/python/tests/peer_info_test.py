import unittest

import libtorrent as lt


class EnumTest(unittest.TestCase):
    def test_enum(self) -> None:
        self.assertIsInstance(lt.peer_info.interesting, int)
        self.assertIsInstance(lt.peer_info.choked, int)
        self.assertIsInstance(lt.peer_info.remote_interested, int)
        self.assertIsInstance(lt.peer_info.remote_choked, int)
        self.assertIsInstance(lt.peer_info.supports_extensions, int)
        self.assertIsInstance(lt.peer_info.local_connection, int)
        self.assertIsInstance(lt.peer_info.outgoing_connection, int)
        self.assertIsInstance(lt.peer_info.handshake, int)
        self.assertIsInstance(lt.peer_info.connecting, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.peer_info.queued, int)
        self.assertIsInstance(lt.peer_info.on_parole, int)
        self.assertIsInstance(lt.peer_info.seed, int)
        self.assertIsInstance(lt.peer_info.optimistic_unchoke, int)
        self.assertIsInstance(lt.peer_info.snubbed, int)
        self.assertIsInstance(lt.peer_info.upload_only, int)
        self.assertIsInstance(lt.peer_info.endgame_mode, int)
        self.assertIsInstance(lt.peer_info.holepunched, int)
        self.assertIsInstance(lt.peer_info.rc4_encrypted, int)
        self.assertIsInstance(lt.peer_info.plaintext_encrypted, int)

        self.assertIsInstance(lt.peer_info.standard_bittorrent, int)
        self.assertIsInstance(lt.peer_info.web_seed, int)
        self.assertIsInstance(lt.peer_info.http_seed, int)

        self.assertIsInstance(lt.peer_info.tracker, int)
        self.assertIsInstance(lt.peer_info.dht, int)
        self.assertIsInstance(lt.peer_info.pex, int)
        self.assertIsInstance(lt.peer_info.lsd, int)
        self.assertIsInstance(lt.peer_info.resume_data, int)

        self.assertIsInstance(lt.peer_info.bw_idle, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.peer_info.bw_torrent, int)
            self.assertIsInstance(lt.peer_info.bw_global, int)
        self.assertIsInstance(lt.peer_info.bw_limit, int)
        self.assertIsInstance(lt.peer_info.bw_network, int)
        self.assertIsInstance(lt.peer_info.bw_disk, int)
