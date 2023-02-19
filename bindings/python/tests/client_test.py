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

from . import lib
from . import tdummy

# import fails on windows
if os.name != "nt":
    import pty


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


class TestClient(unittest.TestCase):
    script_path: pathlib.Path
    tempdir_path: pathlib.Path
    torrent_path: pathlib.Path
    pty_master: Any
    pty_slave: Any
    torrent: tdummy.Torrent

    def setUp(self) -> None:
        if sys.platform != "linux":
            return

        # path relative to this file
        self.script_path = pathlib.Path(__file__).parent / ".." / "client.py"

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

        self.pty_master, self.pty_slave = pty.openpty()

    def tearDown(self) -> None:
        if sys.platform != "linux":
            return
        self.server.shutdown()
        self.server.server_close()
        self.tempdir.cleanup()
        os.close(self.pty_master)
        os.close(self.pty_slave)

    @unittest.skipIf(sys.platform != "linux", "pty control only works on linux")
    def test_download_from_web_seed(self) -> None:
        proc = subprocess.Popen(
            [
                sys.executable,
                str(self.script_path),
                "--port",
                "0",
                "--listen-interface",
                "127.0.0.1",
                str(self.torrent_path),
            ],
            cwd=self.tempdir_path,
            stdin=self.pty_slave,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )

        try:
            # wait for the file to be downloaded from the web seed
            file_path = self.tempdir_path / os.fsdecode(self.torrent.files[0].path)
            for _ in lib.loop_until_timeout(15):
                try:
                    if file_path.read_bytes() == self.torrent.data:
                        break
                except FileNotFoundError:
                    pass

            # send a 'q' command
            os.write(self.pty_master, b"q")
            # process should terminate on its own
            returncode = proc.wait(15)
        finally:
            proc.kill()

        # process should complete without errors or warnings
        self.assertEqual(returncode, 0)
        assert proc.stderr is not None  # helps mypy
        self.assertEqual(proc.stderr.read(), "")
        proc.stderr.close()

        # fastresume should be written
        fastresume = pathlib.Path(f"{file_path}.fastresume")
        lt.bdecode(fastresume.read_bytes())
