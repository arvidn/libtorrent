import hashlib
import pathlib
import random
import sys
import tempfile
from typing import Set
import unittest

import libtorrent as lt

from . import lib


class TestMakeTorrent(unittest.TestCase):
    maxDiff = None

    def setUp(self) -> None:
        # path relative to this file
        self.script_path = pathlib.Path(__file__).parent / ".." / "make_torrent.py"

        self.tempdir = tempfile.TemporaryDirectory()
        self.tempdir_path = pathlib.Path(self.tempdir.name)

        self.out_path = self.tempdir_path / "out.torrent"
        self.tracker_url = "http://test-tracker.com"

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def test_single_file(self) -> None:
        data = bytes(random.getrandbits(8) for _ in range(1024))
        name = "test.txt"
        file_path = self.tempdir_path / name
        file_path.write_bytes(data)

        proc = lib.run_script(
            [sys.executable, str(self.script_path), str(file_path), self.tracker_url],
            cwd=self.tempdir_path,
        )

        self.assertEqual(proc.stderr, "")

        ti = lt.torrent_info(str(self.out_path))
        self.assertEqual(list(tr.url for tr in ti.trackers()), [self.tracker_url])
        fs = ti.layout()
        self.assertEqual(fs.num_files(), 1)
        self.assertEqual(fs.file_name(0), name)
        self.assertEqual(hashlib.sha1(data).digest(), ti.hash_for_piece(0))

    def test_directory(self) -> None:
        data1 = bytes(random.getrandbits(8) for _ in range(1024))
        data2 = bytes(random.getrandbits(8) for _ in range(1024))
        (self.tempdir_path / "test1.txt").write_bytes(data1)
        (self.tempdir_path / "test2.txt").write_bytes(data2)

        proc = lib.run_script(
            [
                sys.executable,
                str(self.script_path),
                str(self.tempdir_path),
                self.tracker_url,
            ],
            cwd=self.tempdir_path,
        )

        self.assertEqual(proc.stderr, "")

        ti = lt.torrent_info(str(self.out_path))
        self.assertEqual(list(tr.url for tr in ti.trackers()), [self.tracker_url])
        fs = ti.layout()
        non_pad_filenames: Set[str] = set()
        for i in range(fs.num_files()):
            if fs.file_flags(i) & lt.file_flags_t.flag_pad_file:
                continue
            non_pad_filenames.add(fs.file_name(i))
        self.assertEqual(non_pad_filenames, {"test1.txt", "test2.txt"})
