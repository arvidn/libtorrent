import functools
import http.server
import logging
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
from typing import Any
import unittest

import libtorrent as lt

from . import tdummy


class Handler(http.server.BaseHTTPRequestHandler):
    def __init__(self, *args: Any, serve_data: bytes, **kwargs: Any) -> None:
        self.serve_data = serve_data
        super().__init__(*args, **kwargs)

    def do_GET(self) -> None:
        logging.info("%s", self.requestline)
        self.send_response(200)
        self.send_header("content-length", str(len(self.serve_data)))
        self.end_headers()
        self.wfile.write(self.serve_data)


class TestSimpleClient(unittest.TestCase):
    def setUp(self) -> None:
        # path relative to this file
        self.script_path = pathlib.Path(__file__).parent / ".." / "simple_client.py"

        self.tempdir = tempfile.TemporaryDirectory()
        self.tempdir_path = pathlib.Path(self.tempdir.name)

        self.torrent = tdummy.get_default()

        # set up a web seed to serve dummy torrent data
        self.server = http.server.HTTPServer(
            ("127.0.0.1", 0), functools.partial(Handler, serve_data=self.torrent.data)
        )
        addr, port = self.server.server_address
        self.server_thread = threading.Thread(target=self.server.serve_forever)
        self.server_thread.start()

        # construct a .torrent to point to our web seed
        self.tdict = dict(self.torrent.dict)
        self.tdict[b"url-list"] = b"".join(
            [b"http://", str(addr).encode(), b":", str(port).encode()]
        )
        self.torrent_path = self.tempdir_path / "dummy.torrent"
        self.torrent_path.write_bytes(lt.bencode(self.tdict))

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.tempdir.cleanup()

    def test_download_from_web_seed(self) -> None:
        proc = subprocess.run(
            [sys.executable, str(self.script_path), str(self.torrent_path)],
            cwd=self.tempdir_path,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
            universal_newlines=True,
            timeout=20,
        )

        self.assertEqual(proc.stderr, "")
        file_path = self.tempdir_path / os.fsdecode(self.torrent.files[0].path)
        self.assertEqual(file_path.read_bytes(), self.torrent.data)
