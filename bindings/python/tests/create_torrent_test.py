import contextlib
import os
import tempfile
from typing import AnyStr
from typing import cast
from typing import Iterator
from typing import List
from typing import TYPE_CHECKING
import unittest
import unittest.mock

import libtorrent as lt

from . import lib

if TYPE_CHECKING:
    from libtorrent import TorrentFileDict
    from libtorrent import TorrentFileInfoDict
else:
    TorrentFileDict = dict


class EnumTest(unittest.TestCase):
    def test_file_storage_static_vars(self) -> None:
        self.assertIsInstance(lt.file_storage.flag_pad_file, int)
        self.assertIsInstance(lt.file_storage.flag_hidden, int)
        self.assertIsInstance(lt.file_storage.flag_executable, int)
        self.assertIsInstance(lt.file_storage.flag_symlink, int)

    def test_file_flags_t(self) -> None:
        self.assertIsInstance(lt.file_flags_t.flag_pad_file, int)
        self.assertIsInstance(lt.file_flags_t.flag_hidden, int)
        self.assertIsInstance(lt.file_flags_t.flag_executable, int)
        self.assertIsInstance(lt.file_flags_t.flag_symlink, int)

    def test_create_torrent_static_vars(self) -> None:
        if lt.api_version < 3:
            self.assertIsInstance(lt.create_torrent.optimize_alignment, int)
            self.assertIsInstance(lt.create_torrent.merkle, int)
        self.assertIsInstance(lt.create_torrent.v1_only, int)
        self.assertIsInstance(lt.create_torrent.v2_only, int)
        self.assertIsInstance(lt.create_torrent.canonical_files, int)
        self.assertIsInstance(lt.create_torrent.modification_time, int)
        self.assertIsInstance(lt.create_torrent.symlinks, int)

    def test_create_torrent_flags_t(self) -> None:
        if lt.api_version < 3:
            self.assertIsInstance(lt.create_torrent_flags_t.optimize_alignment, int)
            self.assertIsInstance(lt.create_torrent_flags_t.merkle, int)
        if lt.api_version < 2:
            self.assertIsInstance(lt.create_torrent_flags_t.optimize, int)
        self.assertIsInstance(lt.create_torrent_flags_t.v2_only, int)
        self.assertIsInstance(lt.create_torrent_flags_t.modification_time, int)
        self.assertIsInstance(lt.create_torrent_flags_t.symlinks, int)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5967")
    def test_deprecated(self) -> None:
        if lt.api_version < 3:
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.create_torrent.optimize_alignment, int)
            with self.assertWarns(DeprecationWarning):
                self.assertIsInstance(lt.create_torrent_flags_t.optimize_alignment, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.create_torrent.merkle, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.create_torrent_flags_t.optimize, int)
        with self.assertWarns(DeprecationWarning):
            self.assertIsInstance(lt.create_torrent_flags_t.merkle, int)


def _build_layout(
    files: List[tuple],
    flags: int = lt.create_torrent.v1_only,
    piece_size: int = 0,
) -> "lt.torrent_info":
    entries = []
    for item in files:
        path, size = item[0], item[1]
        symlink = item[2] if len(item) > 2 else ""
        item_flags = int(lt.file_flags_t.flag_symlink) if symlink else 0
        entries.append(
            lt.create_file_entry(path, size, flags=item_flags, symlink=symlink)
        )
    ct = lt.create_torrent(entries, piece_size, flags=flags)
    for i in range(ct.num_pieces()):
        ct.set_hash(i, lib.get_random_bytes(20))
    entry = ct.generate()
    return lt.torrent_info(entry)


class FileStorageTest(unittest.TestCase):
    def test_is_valid(self) -> None:
        ti = _build_layout([("file.txt", 1024)])
        self.assertTrue(ti.layout().is_valid())

    def test_num_files(self) -> None:
        ti = _build_layout([("file.txt", 1024)])
        self.assertEqual(ti.layout().num_files(), 1)

    def test_add_file(self) -> None:
        if lt.api_version < 5:
            ti = _build_layout([(os.path.join("path", "file1.txt"), 1024)])
            fs = ti.layout()
            fs.add_file(os.path.join("path", "file2.txt"), 512)
            self.assertEqual(fs.num_files(), 2)
            self.assertEqual(fs.file_path(1), os.path.join("path", "file2.txt"))
            self.assertEqual(fs.file_size(1), 512)

            ti = _build_layout([(os.path.join("path", "file1.txt"), 1024)])
            fs = ti.layout()
            fs.add_file(os.path.join("path", "file2.txt"), 512, linkpath="file1.txt")
            self.assertEqual(fs.symlink(1), os.path.join("path", "file1.txt"))

    def test_add_file_bytes(self) -> None:
        if lt.api_version < 5:
            ti = _build_layout([(os.path.join("path", "file1.txt"), 1024)])
            fs = ti.layout()
            with self.assertWarns(DeprecationWarning):
                fs.add_file(os.path.join(b"path", b"file2.txt"), 512)  # type: ignore
            self.assertEqual(fs.file_path(1), os.path.join("path", "file2.txt"))

            ti = _build_layout([(os.path.join("path", "file1.txt"), 1024)])
            fs = ti.layout()
            with self.assertWarns(DeprecationWarning):
                fs.add_file(
                    os.path.join("path", "file2.txt"),
                    512,
                    linkpath=b"file1.txt",
                )
            self.assertEqual(fs.symlink(1), os.path.join("path", "file1.txt"))

    def test_symlink(self) -> None:
        ti = _build_layout([(os.path.join("path", "test.txt"), 1024)])
        self.assertEqual(ti.layout().symlink(0), "")

        ti = _build_layout(
            [
                (os.path.join("path", "real.txt"), 1024),
                (os.path.join("path", "test.txt"), 0, "real.txt"),
            ],
            flags=lt.create_torrent.v1_only | lt.create_torrent.symlinks,
        )
        self.assertEqual(ti.layout().symlink(1), os.path.join("path", "real.txt"))

    def test_symlink_invalid(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        fs = ti.layout()
        with self.assertRaises(IndexError):
            fs.symlink(1)
        with self.assertRaises(IndexError):
            fs.symlink(-1)

    def test_file_path(self) -> None:
        ti = _build_layout([(os.path.join("path", "test.txt"), 1024)])
        fs = ti.layout()
        self.assertEqual(fs.file_path(0), os.path.join("path", "test.txt"))
        self.assertEqual(
            fs.file_path(0, save_path="base"), os.path.join("base", "path", "test.txt")
        )

    def test_file_path_invalid(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        fs = ti.layout()
        with self.assertRaises(IndexError):
            fs.file_path(1)
        with self.assertRaises(IndexError):
            fs.file_path(-1)

    def test_file_name(self) -> None:
        ti = _build_layout([(os.path.join("path", "test.txt"), 1024)])
        self.assertEqual(ti.layout().file_name(0), "test.txt")

    def test_file_name_invalid(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        fs = ti.layout()
        with self.assertRaises(IndexError):
            fs.file_name(1)
        with self.assertRaises(IndexError):
            fs.file_name(-1)

    def test_file_size(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        self.assertEqual(ti.layout().file_size(0), 1024)

    def test_file_size_invalid(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        fs = ti.layout()
        with self.assertRaises(IndexError):
            fs.file_size(1)
        with self.assertRaises(IndexError):
            fs.file_size(-1)

    def test_file_offset(self) -> None:
        ti = _build_layout(
            [
                (os.path.join("path", "test1.txt"), 1024),
                (os.path.join("path", "test2.txt"), 1024),
            ]
        )
        fs = ti.layout()
        self.assertEqual(fs.file_offset(0), 0)
        self.assertEqual(fs.file_offset(1), 1024)

    def test_file_offset_invalid(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        fs = ti.layout()
        with self.assertRaises(IndexError):
            fs.file_offset(1)
        with self.assertRaises(IndexError):
            fs.file_offset(-1)

    def test_file_flags(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        self.assertEqual(ti.layout().file_flags(0), 0)

    def test_file_flags_invalid(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        fs = ti.layout()
        with self.assertRaises(IndexError):
            fs.file_flags(1)
        with self.assertRaises(IndexError):
            fs.file_flags(-1)

    def test_total_size(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        self.assertEqual(ti.layout().total_size(), 1024)

    def test_piece_length(self) -> None:
        ti = _build_layout([("test.txt", 1024)], piece_size=16384)
        self.assertEqual(ti.layout().piece_length(), 16384)

    def test_piece_size(self) -> None:
        ti = _build_layout([("test.txt", 1024)], piece_size=16384)
        fs = ti.layout()
        self.assertEqual(fs.piece_size(0), 1024)
        with self.assertRaises(IndexError):
            fs.piece_size(1)

    def test_name(self) -> None:
        ti = _build_layout([(os.path.join("path", "test.txt"), 1024)])
        self.assertEqual(ti.layout().name(), "path")

    def test_hash(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        self.assertIsInstance(ti.layout().hash(0), lt.sha1_hash)

    def test_hash_invalid(self) -> None:
        ti = _build_layout([("test.txt", 1024)])
        fs = ti.layout()
        with self.assertRaises(IndexError):
            fs.hash(1)


class CreateTorrentTest(unittest.TestCase):
    def test_init_torrent_info(self) -> None:
        entry = TorrentFileDict(
            {
                b"info": {
                    b"name": b"test.txt",
                    b"piece length": 16384,
                    b"pieces": lib.get_random_bytes(20),
                    b"length": 1024,
                }
            }
        )
        ti = lt.torrent_info(entry)
        with self.assertWarns(DeprecationWarning):
            ct = lt.create_torrent(ti)
        # The output of generate() will have extra stuff like "creation date",
        # and the info dict will actually be in preformatted form. We can
        # only compare the bencoded forms
        self.assertEqual(lt.bencode(ct.generate()[b"info"]), lt.bencode(entry[b"info"]))

    def test_args(self) -> None:
        fs = [lt.create_file_entry(os.path.join("path", "test1.txt"), 1024)]

        lt.create_torrent(fs)
        lt.create_torrent(fs, piece_size=0)
        lt.create_torrent(fs, flags=lt.create_torrent_flags_t.v2_only)

    def test_args_deprecated(self) -> None:
        fs = _build_layout([(os.path.join("path", "test1.txt"), 1024)]).layout()

        with self.assertWarns(DeprecationWarning):
            lt.create_torrent(fs)
        with self.assertWarns(DeprecationWarning):
            lt.create_torrent(fs, piece_size=0)
        with self.assertWarns(DeprecationWarning):
            lt.create_torrent(fs, flags=lt.create_torrent_flags_t.v2_only)

    def test_generate(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertIsInstance(entry, dict)
        # Ensure this parses
        lt.torrent_info(entry)

    def test_single_file(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(entry[b"info"][b"name"], b"test1.txt")

    def test_files(self) -> None:
        fs = [lt.create_file_entry(os.path.join("path", "test1.txt"), 1024)]
        ct = lt.create_torrent(fs)
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(len(cast("TorrentFileInfoDict", entry[b"info"])[b"files"]), 1)

    def test_files2(self) -> None:
        fs = [lt.create_file_entry(os.path.join("path", "test1.txt"), 1024)]
        fs.append(lt.create_file_entry(os.path.join("path", "test2.txt"), 1024))
        ct = lt.create_torrent(fs)
        ct.set_hash(0, lib.get_random_bytes(20))
        ct.set_hash(1, lib.get_random_bytes(20))
        entry = ct.generate()
        # there are 2 pad-files
        self.assertEqual(len(cast("TorrentFileInfoDict", entry[b"info"])[b"files"]), 4)

    def test_comment(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_comment("test")
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(entry[b"comment"], b"test")

    def test_creator(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_creator("test")
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(entry[b"created by"], b"test")

    def test_set_hash_invalid(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        with self.assertRaises(IndexError):
            ct.set_hash(1, lib.get_random_bytes(20))
        with self.assertRaises(IndexError):
            ct.set_hash(-1, lib.get_random_bytes(20))

    def test_set_hash_short(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        with self.assertRaises(ValueError):
            ct.set_hash(0, lib.get_random_bytes(19))

    def test_set_hash_long_deprecated(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        with self.assertWarns(DeprecationWarning):
            ct.set_hash(0, lib.get_random_bytes(21))

    def test_set_file_hash(self) -> None:
        if lt.api_version < 3:
            fs = [lt.create_file_entry("test1.txt", 1024)]
            ct = lt.create_torrent(fs)
            with self.assertWarns(DeprecationWarning):
                ct.set_file_hash(0, lib.get_random_bytes(20))
            ct.set_hash(0, lib.get_random_bytes(20))
            entry = ct.generate()
            self.assertIn(b"sha1", entry[b"info"])

    def test_set_file_hash_invalid(self) -> None:
        if lt.api_version < 3:
            fs = [lt.create_file_entry("test1.txt", 1024)]
            ct = lt.create_torrent(fs)
            with self.assertWarns(DeprecationWarning):
                with self.assertRaises(IndexError):
                    ct.set_file_hash(1, lib.get_random_bytes(20))
            with self.assertWarns(DeprecationWarning):
                with self.assertRaises(IndexError):
                    ct.set_file_hash(-1, lib.get_random_bytes(20))

    def test_set_file_hash_short(self) -> None:
        if lt.api_version < 3:
            fs = [lt.create_file_entry("test1.txt", 1024)]
            ct = lt.create_torrent(fs)
            with self.assertWarns(DeprecationWarning):
                with self.assertRaises(ValueError):
                    ct.set_file_hash(0, lib.get_random_bytes(19))

    def test_set_file_hash_long_deprecated(self) -> None:
        if lt.api_version < 3:
            fs = [lt.create_file_entry("test1.txt", 1024)]
            ct = lt.create_torrent(fs)
            with self.assertWarns(DeprecationWarning):
                ct.set_file_hash(0, lib.get_random_bytes(21))

    def test_url_seed(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.add_url_seed("http://example.com")
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(entry[b"url-list"], b"http://example.com")

    def test_http_seed_deprecated(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        with self.assertWarns(DeprecationWarning):
            ct.add_http_seed("http://example.com")

    def test_add_node(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.add_node("1.2.3.4", 5678)
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(entry[b"nodes"], [[b"1.2.3.4", 5678]])

    def test_add_tracker(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.add_tracker("http://example.com")
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(entry[b"announce"], b"http://example.com")

    def test_add_tracker_args(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.add_tracker("http://1.com")
        ct.add_tracker("http://2.com", tier=100)
        ct.set_hash(0, lib.get_random_bytes(20))
        entry = ct.generate()
        self.assertEqual(
            entry[b"announce-list"],
            [[b"http://1.com"], [b"http://2.com"]],
        )

    def test_priv(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_priv(True)
        self.assertTrue(ct.priv())

    def test_num_pieces(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        self.assertEqual(ct.num_pieces(), 1)

    def test_piece_length(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        self.assertEqual(ct.piece_length(), 16384)

    def test_piece_size(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        with self.assertRaises(IndexError):
            ct.piece_size(-1)
        self.assertEqual(ct.piece_size(0), 1024)
        with self.assertRaises(IndexError):
            ct.piece_size(1)

    def test_root_cert(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_hash(0, lib.get_random_bytes(20))
        ct.set_root_cert("test")
        entry = ct.generate()
        self.assertEqual(entry[b"info"][b"ssl-cert"], b"test")

    def test_collections(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_hash(0, lib.get_random_bytes(20))
        ct.add_collection("ascii-str")
        ct.add_collection("non-ascii-str-\u1234")
        entry = ct.generate()
        self.assertEqual(
            entry[b"info"][b"collections"],
            [
                b"ascii-str",
                "non-ascii-str-\u1234".encode(),
            ],
        )

    def test_similar(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        ct.set_hash(0, lib.get_random_bytes(20))
        sha1 = lt.sha1_hash(lib.get_random_bytes(20))
        ct.add_similar_torrent(sha1)
        entry = ct.generate()
        self.assertEqual(entry[b"info"][b"similar"], [sha1.to_bytes()])


@contextlib.contextmanager
def get_tempdir_stack(*dirnames: AnyStr) -> Iterator[List[AnyStr]]:
    assert dirnames
    paths: List[AnyStr] = []
    with tempfile.TemporaryDirectory() as tempdir:
        top: AnyStr
        if isinstance(dirnames[0], bytes):
            top = os.fsencode(tempdir)
        else:
            top = tempdir
        for dirname in dirnames:
            top = os.path.join(top, dirname)
            os.mkdir(top)
            paths.append(top)
        yield paths


class ListTest(unittest.TestCase):
    def test_args_no_pred(self) -> None:
        with tempfile.TemporaryDirectory() as path:
            lt.list_files(path)
            lt.list_files(path, flags=lt.create_torrent_flags_t.v2_only)
        # The overloads are:
        # list_files(path, flags=0), and
        # list_files(path, predicate, flags=0)

    def test_args_pred_positional(self) -> None:
        with tempfile.TemporaryDirectory() as path:
            # Write some random data to two files
            file_paths = [os.path.join(path, name) for name in ("a.txt", "b.txt")]
            for file_path in file_paths:
                with open(file_path, mode="wb") as fp:
                    fp.write(lib.get_random_bytes(1024))
            calls = [unittest.mock.call(file_path) for file_path in file_paths]

            pred = unittest.mock.Mock(return_value=True)
            fs = lt.list_files(path, pred)
            pred.assert_has_calls(calls, any_order=True)
            self.assertEqual(len(fs), 2)

    def test_args_pred_kwargs(self) -> None:
        with tempfile.TemporaryDirectory() as path:
            # Write some random data to two files
            file_paths = [os.path.join(path, name) for name in ("a.txt", "b.txt")]
            for file_path in file_paths:
                with open(file_path, mode="wb") as fp:
                    fp.write(lib.get_random_bytes(1024))
            calls = [unittest.mock.call(file_path) for file_path in file_paths]

            pred = unittest.mock.Mock(return_value=True)
            fs = lt.list_files(
                path,
                pred,
                flags=lt.create_torrent_flags_t.v2_only,
            )
            pred.assert_has_calls(calls, any_order=True)
            self.assertEqual(len(fs), 2)


class AddFilesTest(unittest.TestCase):
    def test_args_no_pred(self) -> None:
        fs = _build_layout([("existing.txt", 1024)]).layout()
        with tempfile.TemporaryDirectory() as path:
            with self.assertWarns(DeprecationWarning):
                lt.add_files(fs, path)
            with self.assertWarns(DeprecationWarning):
                lt.add_files(fs, path, flags=lt.create_torrent_flags_t.v2_only)

        # The overloads are:
        # add_files(file_storage, path, flags=0), and
        # add_files(file_storage, path, predicate, flags=0)
        # Note that a form like add_files(file_storage, path, 0) gets the
        # second overload, and raises TypeError due to treating the int as
        # a callable

    def test_args_pred_positional(self) -> None:
        with tempfile.TemporaryDirectory() as path:
            # Write some random data to two files
            file_paths = [os.path.join(path, name) for name in ("a.txt", "b.txt")]
            for file_path in file_paths:
                with open(file_path, mode="wb") as fp:
                    fp.write(lib.get_random_bytes(1024))
            calls = [unittest.mock.call(file_path) for file_path in file_paths]

            fs = _build_layout([("existing.txt", 1024)]).layout()
            pred = unittest.mock.Mock(return_value=True)
            with self.assertWarns(DeprecationWarning):
                lt.add_files(fs, path, pred)
            pred.assert_has_calls(calls, any_order=True)
            self.assertEqual(fs.num_files(), 3)

    def test_args_pred_kwargs(self) -> None:
        with tempfile.TemporaryDirectory() as path:
            # Write some random data to two files
            file_paths = [os.path.join(path, name) for name in ("a.txt", "b.txt")]
            for file_path in file_paths:
                with open(file_path, mode="wb") as fp:
                    fp.write(lib.get_random_bytes(1024))
            calls = [unittest.mock.call(file_path) for file_path in file_paths]

            fs = _build_layout([("existing.txt", 1024)]).layout()
            pred = unittest.mock.Mock(return_value=True)
            with self.assertWarns(DeprecationWarning):
                lt.add_files(
                    fs,
                    path,
                    pred,
                    flags=lt.create_torrent_flags_t.v2_only,
                )
            pred.assert_has_calls(calls, any_order=True)
            self.assertEqual(fs.num_files(), 3)

    # We don't bother testing how file_storage sanitizes paths; we assume
    # this is exercised in C++ tests. We just test that various path forms
    # can be used as input to add_files()

    def test_path_ascii_str(self) -> None:
        with get_tempdir_stack("test") as (tempdir,):
            with open(os.path.join(tempdir, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(fs[0].filename, os.path.join("test", "test.txt"))

    def test_path_ascii_bytes(self) -> None:
        with get_tempdir_stack(b"test") as (tempdir,):
            with open(os.path.join(tempdir, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(fs[0].filename, os.path.join("test", "test.txt"))

    def test_path_unicode_str(self) -> None:
        with get_tempdir_stack("\u1234") as (tempdir,):
            with open(os.path.join(tempdir, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(os.path.split(fs[0].filename)[1], "test.txt")

    def test_path_unicode_bytes(self) -> None:
        with get_tempdir_stack(os.fsencode("\u1234")) as (tempdir,):
            with open(os.path.join(tempdir, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(os.path.split(fs[0].filename)[1], "test.txt")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_path_non_unicode_str(self) -> None:
        with get_tempdir_stack(os.fsdecode(b"\xff")) as (tempdir,):
            with open(os.path.join(tempdir, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(fs[0].filename, "test.txt")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_path_non_unicode_bytes(self) -> None:
        with get_tempdir_stack(b"\xff") as (tempdir,):
            with open(os.path.join(tempdir, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(fs[0].filename, "test.txt")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_path_surrogate_str(self) -> None:
        with get_tempdir_stack("\udcff") as (tempdir,):
            with open(os.path.join(tempdir, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(fs[0].filename, "test.txt")

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_path_surrogate_bytes(self) -> None:
        with get_tempdir_stack(os.fsencode("\udcff")) as (tempdir,):
            with open(os.path.join(tempdir, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(tempdir)
        self.assertEqual(fs[0].filename, "test.txt")


class SetPieceHashesTest(unittest.TestCase):
    def test_args(self) -> None:
        callback = unittest.mock.Mock()
        with get_tempdir_stack("outer", "inner") as (outer, inner):
            with open(os.path.join(inner, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = lt.list_files(inner)
            ct = lt.create_torrent(fs)

            # path str
            lt.set_piece_hashes(ct, outer)

            # path str and callback
            lt.set_piece_hashes(ct, outer, callback)
            calls = [unittest.mock.call(0)]
            callback.assert_has_calls(calls)

    def test_exceptions(self) -> None:
        fs = [lt.create_file_entry("test1.txt", 1024)]
        ct = lt.create_torrent(fs)
        with tempfile.TemporaryDirectory() as path:
            with self.assertRaises(RuntimeError):
                lt.set_piece_hashes(ct, path)
            with self.assertRaises(RuntimeError):
                lt.set_piece_hashes(ct, path, lambda _: None)

    # We don't bother testing how file_storage sanitizes paths; we assume
    # this is exercised in C++ tests. We just test that various path forms
    # can be used as input to add_files() and set_piece_hashes().

    # Note that add_files actually uses the *parent path* of its input,
    # so the last element of its input will get sanitized. Thus:
    #  add_files(fs, b"/tmp/\xff/")
    #  set_piece_hashes(fs, b"/tmp")
    # fails, because set_piece_hashes will search for sanitized files
    # that don't exist.

    # To avoid this, ensure the last path element is ascii for our tests. We
    # end up testing with a structure like b"/tmp/\xff/test/test.txt"

    def test_path_ascii_str(self) -> None:
        with get_tempdir_stack("test", "test") as (outer, inner):
            with open(os.path.join(inner, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)

    def test_path_ascii_bytes(self) -> None:
        with get_tempdir_stack(b"test", b"test") as (outer, inner):
            with open(os.path.join(inner, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)

    def test_path_non_ascii_str(self) -> None:
        with get_tempdir_stack("\u1234", "test") as (outer, inner):
            with open(os.path.join(inner, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)

    def test_path_non_ascii_bytes(self) -> None:
        with get_tempdir_stack(os.fsencode("\u1234"), b"test") as (outer, inner):
            with open(os.path.join(inner, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_path_surrogate_str(self) -> None:
        with get_tempdir_stack("\udcff", "test") as (outer, inner):
            with open(os.path.join(inner, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_surrogate_paths()
    def test_path_surrogate_bytes(self) -> None:
        with get_tempdir_stack(os.fsencode("\udcff"), b"test") as (outer, inner):
            with open(os.path.join(inner, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_path_non_unicode_str(self) -> None:
        with get_tempdir_stack(os.fsdecode("\xff"), "test") as (outer, inner):
            with open(os.path.join(inner, "test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)

    @unittest.skip("https://github.com/arvidn/libtorrent/issues/5984")
    @lib.uses_non_unicode_paths()
    def test_path_non_unicode_bytes(self) -> None:
        with get_tempdir_stack(b"\xff", b"test") as (outer, inner):
            with open(os.path.join(inner, b"test.txt"), mode="wb") as fp:
                fp.write(lib.get_random_bytes(1024))
            fs = [lt.create_file_entry(os.path.join("test", "test.txt"), 1024)]
            ct = lt.create_torrent(fs)
            lt.set_piece_hashes(ct, outer)
