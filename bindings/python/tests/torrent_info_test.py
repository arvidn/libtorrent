import os
import tempfile
from typing import AnyStr
import unittest

import libtorrent as lt

from . import lib
from . import tdummy


class FileEntryTest(unittest.TestCase):
    def test_attributes(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                fe = lt.file_entry()

            fe.path = os.path.join("path", "file.txt")
            path = fe.path
            self.assertEqual(path, os.path.join("path", "file.txt"))

            fe.symlink_path = os.path.join("path", "other.txt")
            self.assertEqual(fe.symlink_path, os.path.join("path", "other.txt"))

            fe.filehash = lt.sha1_hash(b"a" * 20)
            self.assertEqual(fe.filehash, lt.sha1_hash(b"a" * 20))

            fe.mtime = 123456
            self.assertEqual(fe.mtime, 123456)

            with self.assertWarns(DeprecationWarning):
                self.assertFalse(fe.pad_file)
            with self.assertWarns(DeprecationWarning):
                self.assertFalse(fe.executable_attribute)
            with self.assertWarns(DeprecationWarning):
                self.assertFalse(fe.hidden_attribute)
            with self.assertWarns(DeprecationWarning):
                self.assertFalse(fe.symlink_attribute)

            with self.assertWarns(DeprecationWarning):
                self.assertEqual(fe.offset, 0)
            with self.assertWarns(DeprecationWarning):
                self.assertEqual(fe.size, 0)


class InfoHashTest(unittest.TestCase):
    def test_sha1(self) -> None:
        sha1 = lt.sha1_hash(lib.get_random_bytes(20))

        ih = lt.info_hash_t(sha1)
        self.assertEqual(ih.get(lt.protocol_version.V1), sha1)
        self.assertEqual(ih.get(lt.protocol_version.V2), lt.sha1_hash())
        self.assertEqual(ih.get_best(), sha1)
        self.assertTrue(ih.has(lt.protocol_version.V1))
        self.assertFalse(ih.has(lt.protocol_version.V2))
        self.assertTrue(ih.has_v1())
        self.assertFalse(ih.has_v2())
        self.assertEqual(ih.v1, sha1)
        self.assertEqual(ih.v2, lt.sha256_hash())

        self.assertEqual(ih, lt.info_hash_t(sha1))
        self.assertEqual(hash(ih), hash(lt.info_hash_t(sha1)))
        self.assertNotEqual(ih, lt.info_hash_t())

    def test_sha256(self) -> None:
        sha256 = lt.sha256_hash(lib.get_random_bytes(32))
        sha256_trunc = lt.sha1_hash(sha256.to_bytes()[:20])

        ih = lt.info_hash_t(sha256)
        self.assertEqual(ih.get(lt.protocol_version.V1), lt.sha1_hash())
        self.assertEqual(ih.get(lt.protocol_version.V2), sha256_trunc)
        self.assertEqual(ih.get_best(), sha256_trunc)
        self.assertFalse(ih.has(lt.protocol_version.V1))
        self.assertTrue(ih.has(lt.protocol_version.V2))
        self.assertFalse(ih.has_v1())
        self.assertTrue(ih.has_v2())
        self.assertEqual(ih.v1, lt.sha1_hash())
        self.assertEqual(ih.v2, sha256)

        self.assertEqual(ih, lt.info_hash_t(sha256))
        self.assertEqual(hash(ih), hash(lt.info_hash_t(sha256)))
        self.assertNotEqual(ih, lt.info_hash_t())

    def test_dual(self) -> None:
        sha1 = lt.sha1_hash(lib.get_random_bytes(20))
        sha256 = lt.sha256_hash(lib.get_random_bytes(32))
        sha256_trunc = lt.sha1_hash(sha256.to_bytes()[:20])

        ih = lt.info_hash_t(sha1, sha256)
        self.assertEqual(ih.get(lt.protocol_version.V1), sha1)
        self.assertEqual(ih.get(lt.protocol_version.V2), sha256_trunc)
        self.assertEqual(ih.get_best(), sha256_trunc)
        self.assertTrue(ih.has(lt.protocol_version.V1))
        self.assertTrue(ih.has(lt.protocol_version.V2))
        self.assertTrue(ih.has_v1())
        self.assertTrue(ih.has_v2())
        self.assertEqual(ih.v1, sha1)
        self.assertEqual(ih.v2, sha256)

        self.assertEqual(ih, lt.info_hash_t(sha1, sha256))
        self.assertEqual(hash(ih), hash(lt.info_hash_t(sha1, sha256)))
        self.assertNotEqual(ih, lt.info_hash_t())


class ConstructorTest(unittest.TestCase):
    def do_test_filename(self, name: AnyStr) -> None:
        entry = {
            b"info": {
                b"name": b"test.txt",
                b"piece length": 16384,
                b"pieces": lib.get_random_bytes(20),
                b"length": 1024,
            }
        }
        data = lt.bencode(entry)
        with tempfile.TemporaryDirectory() as tempdir_str:
            tempdir: AnyStr
            if isinstance(name, str):
                tempdir = tempdir_str
            else:
                tempdir = os.fsencode(tempdir_str)
            path = os.path.join(tempdir, name)
            with open(path, mode="wb") as fp:
                fp.write(data)
            ti = lt.torrent_info(path)
        self.assertEqual(ti.name(), "test.txt")

    def test_filename(self) -> None:
        # ascii str
        self.do_test_filename("test.torrent")

        # non-ascii str
        self.do_test_filename("test-\u1234.torrent")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_filename_non_unicode_str(self) -> None:
        # non-unicode str
        self.do_test_filename(os.fsdecode(b"test-\xff.torrent"))

    def test_dict(self) -> None:
        entry = {
            b"info": {
                b"name": b"test.txt",
                b"piece length": 16384,
                b"pieces": lib.get_random_bytes(20),
                b"length": 1024,
            }
        }
        ti = lt.torrent_info(entry)
        self.assertEqual(ti.name(), "test.txt")

    def test_dict_with_str_keys(self) -> None:
        with self.assertWarns(DeprecationWarning):
            ti = lt.torrent_info(
                {
                    "info": {  # type: ignore
                        "name": "test.txt",
                        "length": 1024,
                        "piece length": 16384,
                        "pieces": "aaaaaaaaaaaaaaaaaaaa",
                    }
                }
            )
        self.assertEqual(ti.name(), "test.txt")

    def test_copy_constructor(self) -> None:
        ti = tdummy.get_default().torrent_info()
        other = lt.torrent_info(ti)
        self.assertEqual(ti.info_hashes(), other.info_hashes())

    def test_info_hash(self) -> None:
        sha1 = lt.sha1_hash(lib.get_random_bytes(20))
        ti = lt.torrent_info(lt.info_hash_t(sha1))
        self.assertEqual(ti.info_hashes(), lt.info_hash_t(sha1))

    def test_sha1_hash(self) -> None:
        sha1 = lt.sha1_hash(lib.get_random_bytes(20))
        ti = lt.torrent_info(sha1)
        self.assertEqual(ti.info_hash(), sha1)


class ConstructorWithLimitsTest(unittest.TestCase):
    def test_load_decode_depth_limit(self) -> None:
        with self.assertRaises(RuntimeError):
            lt.torrent_info(
                {
                    b"test": {b"test": {b"test": {b"test": {b"test": {}}}}},
                    b"info": {
                        b"name": b"test_torrent",
                        b"length": 1234,
                        b"piece length": 16 * 1024,
                        b"pieces": b"aaaaaaaaaaaaaaaaaaaa",
                    },
                },
                {"max_decode_depth": 1},
            )

    def test_load_max_pieces_limit(self) -> None:
        with self.assertRaises(RuntimeError):
            lt.torrent_info(
                {
                    b"info": {
                        b"name": b"test_torrent",
                        b"length": 1234000,
                        b"piece length": 16 * 1024,
                        b"pieces": b"aaaaaaaaaaaaaaaaaaaa",
                    }
                },
                {"max_pieces": 1},
            )

    def test_load_max_buffer_size_limit(self) -> None:
        with self.assertRaises(RuntimeError):
            lt.torrent_info(
                {
                    b"info": {
                        b"name": b"test_torrent",
                        b"length": 1234000,
                        b"piece length": 16 * 1024,
                        b"pieces": b"aaaaaaaaaaaaaaaaaaaa",
                    }
                },
                {"max_buffer_size": 1},
            )


class FieldTest(unittest.TestCase):
    def setUp(self) -> None:
        self.ti = tdummy.get_default().torrent_info()

    def test_trackers(self) -> None:
        self.ti.add_tracker("http://example0.com", 0, lt.tracker_source.source_client)
        with self.assertRaises(TypeError):
            self.ti.add_tracker(
                b"http://example1.com",  # type: ignore
                1,
                int(lt.tracker_source.source_torrent),  # type: ignore
            )

        self.assertEqual([t.url for t in self.ti.trackers()], ["http://example0.com"])
        self.assertEqual([t.tier for t in self.ti.trackers()], [0])
        self.assertEqual(
            [t.source for t in self.ti.trackers()], [lt.tracker_source.source_client]
        )

    def test_add_tracker_defaults(self) -> None:
        self.ti.add_tracker("http://example.com")

        self.assertEqual([t.url for t in self.ti.trackers()], ["http://example.com"])
        self.assertEqual([t.tier for t in self.ti.trackers()], [0])
        self.assertEqual(
            [t.source for t in self.ti.trackers()], [lt.tracker_source.source_client]
        )

    def test_web_seeds(self) -> None:
        # empty, and ascii str
        web_seeds = [
            {"url": "http://example1.com", "auth": ""},
            {"url": "http://example2.com", "auth": "password"},
        ]
        self.ti.set_web_seeds(web_seeds)
        self.assertEqual(self.ti.web_seeds(), web_seeds)

        def do_test(auth: AnyStr, auth_check: str) -> None:
            self.ti.set_web_seeds([{"url": "http://example.com", "auth": auth}])
            self.assertEqual(
                self.ti.web_seeds(), [{"url": "http://example.com", "auth": auth_check}]
            )

        # ascii bytes
        do_test(b"password", "password")

        # non-ascii str
        do_test("\u1234", "\u1234")

        # non-ascii bytes
        do_test("\u1234".encode(), "\u1234")

        # non-unicode
        with self.assertRaises(ValueError):
            do_test(b"\xff", "dummy")

    @unittest.skip("need to implement default empty auth")
    def test_web_seeds_default_auth(self) -> None:
        web_seeds = [{"url": "http://example.com"}]
        self.ti.set_web_seeds(web_seeds)
        self.assertEqual(self.ti.web_seeds(), web_seeds)

    def test_add_url_seed(self) -> None:
        self.ti.add_url_seed("http://example1.com")
        self.ti.add_url_seed("http://example2.com", "password", [("header", "value")])

    def test_add_http_seed(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.ti.add_http_seed("http://example1.com")
        with self.assertWarns(DeprecationWarning):
            self.ti.add_http_seed(
                "http://example2.com", "password", [("header", "value")]
            )

    def test_name(self) -> None:
        def generate(name: bytes) -> lt.torrent_info:
            return lt.torrent_info(
                {
                    b"info": {
                        b"name": name,
                        b"pieces": lib.get_random_bytes(20),
                        b"piece length": 16384,
                        b"length": 1024,
                    }
                }
            )

        # ascii
        self.assertEqual(generate(b"test.txt").name(), "test.txt")

        # non-ascii
        self.assertEqual(generate("\u1234.txt".encode()).name(), "\u1234.txt")

        # non-unicode
        self.assertEqual(generate(b"\xff.txt").name(), "_.txt")

    def test_creator(self) -> None:
        def generate(creator: bytes) -> lt.torrent_info:
            return lt.torrent_info(
                {
                    b"created by": creator,
                    b"info": {
                        b"name": b"test.txt",
                        b"pieces": lib.get_random_bytes(20),
                        b"piece length": 16384,
                        b"length": 1024,
                    },
                }
            )

        # ascii
        self.assertEqual(generate(b"test").creator(), "test")

        # non-ascii
        self.assertEqual(generate("test-\u1234".encode()).creator(), "test-\u1234")

        # non-unicode
        self.assertEqual(generate(b"test-\xff").creator(), "test-_")

    def test_comment(self) -> None:
        def generate(comment: bytes) -> lt.torrent_info:
            return lt.torrent_info(
                {
                    b"comment": comment,
                    b"info": {
                        b"name": b"test.txt",
                        b"pieces": lib.get_random_bytes(20),
                        b"piece length": 16384,
                        b"length": 1024,
                    },
                }
            )

        # ascii
        self.assertEqual(generate(b"test").comment(), "test")

        # non-ascii
        self.assertEqual(generate("\u1234".encode()).comment(), "\u1234")

        # non-unicode
        self.assertEqual(generate(b"test-\xff").comment(), "test-_")

    def test_collections(self) -> None:
        def generate(collection: bytes) -> lt.torrent_info:
            return lt.torrent_info(
                {
                    b"collections": [collection],
                    b"info": {
                        b"name": b"test.txt",
                        b"pieces": lib.get_random_bytes(20),
                        b"piece length": 16384,
                        b"length": 1024,
                    },
                }
            )

        # ascii
        self.assertEqual(generate(b"test").collections(), ["test"])

        # non-ascii
        self.assertEqual(generate("\u1234".encode()).collections(), ["\u1234"])

        # non-unicode
        with self.assertRaises(ValueError):
            generate(b"\xff").collections()

    def test_hash_for_piece_invalid(self) -> None:
        ti = lt.torrent_info(
            {
                b"info": {
                    b"name": b"test.txt",
                    b"pieces": lib.get_random_bytes(20),
                    b"piece length": 16384,
                    b"length": 1024,
                }
            }
        )
        with self.assertRaises(IndexError):
            ti.hash_for_piece(1)
        with self.assertRaises(IndexError):
            ti.hash_for_piece(-1)

    def test_info_hash(self) -> None:
        self.assertFalse(self.ti.info_hash().is_all_zeros())
        self.assertEqual(self.ti.info_hashes(), lt.info_hash_t(self.ti.info_hash()))

    def test_is_i2p(self) -> None:
        ti = lt.torrent_info(
            {
                b"announce": b"http://example.i2p",
                b"info": {
                    b"name": b"test.txt",
                    b"pieces": lib.get_random_bytes(20),
                    b"piece length": 16384,
                    b"length": 1024,
                },
            }
        )
        self.assertTrue(ti.is_i2p())

    def test_normal_fixed_fields(self) -> None:
        piece = lib.get_random_bytes(20)
        similar = lib.get_random_bytes(20)
        info = {
            b"name": b"test.txt",
            b"pieces": piece,
            b"piece length": 16384,
            b"length": 1024,
            b"similar": [similar],
        }
        ti = lt.torrent_info({b"creation date": 12345, b"info": info})

        self.assertEqual(ti.total_size(), 1024)
        self.assertEqual(ti.piece_length(), 16384)
        self.assertEqual(ti.num_pieces(), 1)
        self.assertEqual(ti.piece_size(-1), 16384)
        self.assertEqual(ti.piece_size(0), 1024)
        self.assertEqual(ti.piece_size(1), 16384)
        self.assertEqual(ti.hash_for_piece(0), piece)
        self.assertEqual(ti.num_files(), 1)
        self.assertTrue(ti.is_valid())
        self.assertFalse(ti.priv())
        self.assertEqual(ti.info_section(), lt.bencode(info))
        self.assertEqual(ti.creation_date(), 12345)
        self.assertEqual(ti.similar_torrents(), [lt.sha1_hash(similar)])

    def test_metadata(self) -> None:
        with self.assertWarns(DeprecationWarning):
            metadata = self.ti.metadata()
        self.assertEqual(metadata, self.ti.info_section())
        with self.assertWarns(DeprecationWarning):
            size = self.ti.metadata_size()
        self.assertEqual(size, len(metadata))

    def test_merkle_tree(self) -> None:
        with self.assertWarns(DeprecationWarning):
            self.assertFalse(self.ti.is_merkle_torrent())
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.ti.merkle_tree(), [])

        tree = [lib.get_random_bytes(20)]
        with self.assertWarns(DeprecationWarning):
            self.ti.set_merkle_tree(tree)
        with self.assertWarns(DeprecationWarning):
            self.assertTrue(self.ti.is_merkle_torrent())
        with self.assertWarns(DeprecationWarning):
            self.assertEqual(self.ti.merkle_tree(), tree)

    def test_nodes(self) -> None:
        self.ti.add_node("node1", 1234)
        self.assertEqual(self.ti.nodes(), [("node1", 1234)])

    def test_rename_file(self) -> None:
        # ascii str
        self.ti.rename_file(0, "ascii-str.txt")
        self.assertEqual(self.ti.files().file_path(0), "ascii-str.txt")

        # ascii bytes
        self.ti.rename_file(0, b"ascii-bytes.txt")  # type: ignore
        self.assertEqual(self.ti.files().file_path(0), "ascii-bytes.txt")

        # non-ascii str
        self.ti.rename_file(0, "\u1234-str.txt")
        self.assertEqual(self.ti.files().file_path(0), "\u1234-str.txt")

        # non-ascii bytes
        self.ti.rename_file(0, "\u1234-bytes.txt".encode())  # type: ignore
        self.assertEqual(self.ti.files().file_path(0), "\u1234-bytes.txt")

        # non-unicode
        self.ti.rename_file(0, b"\xff.txt")  # type: ignore
        with self.assertRaises(ValueError):
            self.ti.files().file_path(0)

    def test_files_and_remap(self) -> None:
        ti = lt.torrent_info(
            {
                b"info": {
                    b"name": b"test.txt",
                    b"pieces": lib.get_random_bytes(20),
                    b"piece length": 16384,
                    b"length": 1024,
                },
            }
        )

        self.assertEqual(ti.name(), "test.txt")
        self.assertEqual(ti.files().file_path(0), "test.txt")
        self.assertEqual(ti.orig_files().file_path(0), "test.txt")

        fs = lt.file_storage()
        fs.add_file(os.path.join("path", "remapped1.txt"), 512)
        fs.add_file(os.path.join("path", "remapped2.txt"), 512)

        ti.remap_files(fs)

        self.assertEqual(ti.name(), "path")
        self.assertEqual(ti.files().num_files(), 2)
        self.assertEqual(ti.files().file_path(0), os.path.join("path", "remapped1.txt"))
        self.assertEqual(ti.files().file_path(1), os.path.join("path", "remapped2.txt"))

        self.assertEqual(ti.orig_files().num_files(), 1)
        self.assertEqual(ti.orig_files().file_path(0), "test.txt")

    def test_map(self) -> None:
        pr = self.ti.map_file(0, 100, 200)
        self.assertEqual(pr.piece, 0)
        self.assertEqual(pr.start, 100)
        self.assertEqual(pr.length, 200)

        slices = self.ti.map_block(0, 100, 200)
        self.assertEqual(len(slices), 1)
        file_slice = slices[0]
        self.assertEqual(file_slice.file_index, 0)
        self.assertEqual(file_slice.offset, 100)
        self.assertEqual(file_slice.size, 200)

    def test_file_at(self) -> None:
        if lt.api_version < 2:
            with self.assertWarns(DeprecationWarning):
                fe = self.ti.file_at(0)
            self.assertEqual(fe.path, self.ti.files().file_path(0))
