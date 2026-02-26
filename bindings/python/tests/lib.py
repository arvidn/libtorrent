import os
import random
import sys
import tempfile
import time
from typing import Any
from typing import Callable
from typing import Iterator
from typing import TYPE_CHECKING
from typing import TypeVar
import unittest

if TYPE_CHECKING:
    from libtorrent import settings_pack
else:
    settings_pack = dict


def get_random_bytes(n: int) -> bytes:
    return bytes(random.getrandbits(8) for _ in range(n))


def get_isolated_settings() -> settings_pack:
    return {
        "enable_dht": False,
        "enable_lsd": False,
        "enable_natpmp": False,
        "enable_upnp": False,
        "listen_interfaces": "127.0.0.1:0",
        "dht_bootstrap_nodes": "",
    }


def loop_until_timeout(timeout: float, msg: str = "condition") -> Iterator[None]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        yield
    raise AssertionError(f"{msg} timed out")


_FT = TypeVar("_FT", bound=Callable[..., Any])


def uses_non_unicode_paths() -> Callable[[_FT], _FT]:
    supported = True
    try:
        os.fsdecode(b"\xff")
    except ValueError:
        supported = False
    return unittest.skipIf(
        not supported, "platform doesn't support non-unicode filenames"
    )


# Note that this happens to work on supported platforms (posix and windows),
# but unpaired surrogates *mean different things* on these platforms. On windows
# they're passed through, on posix they're treated as an escape for non-utf-8
# bytes
def uses_surrogate_paths() -> Callable[[_FT], _FT]:
    supported = True
    try:
        os.fsencode("\udcff")
    except ValueError:
        supported = False
    return unittest.skipIf(
        not supported, "platform doesn't support unpaired unicode surrogate filenames"
    )


def unlink_all_files(path: str) -> None:
    for dirpath, _, filenames in os.walk(path):
        for filename in filenames:
            filepath = os.path.join(dirpath, filename)
            os.unlink(filepath)


# In test cases where libtorrent writes torrent data in a temporary directory,
# cleaning up the tempdir on Windows CI sometimes fails with a PermissionError
# having WinError 5 (Access Denied). I can't repro this WinError in any way;
# holding an open file handle results in a different WinError. Seems to be a
# race condition which only happens with very short-lived tests which write
# data. Work around by cleaning up the tempdir in a loop.


# TODO: why is this necessary?
def cleanup_with_windows_fix(
    tempdir: tempfile.TemporaryDirectory, *, timeout: float
) -> None:
    # Clean up just the files, so we don't have to bother with depth-first
    # traversal
    for _ in loop_until_timeout(timeout, msg="PermissionError clear"):
        try:
            unlink_all_files(tempdir.name)
        except PermissionError:
            if sys.platform == "win32":
                # current release of mypy doesn't know about winerror
                # if exc.winerror == 5:
                continue
            raise
        break
    # This removes directories in depth-first traversal.
    # It also marks the tempdir as explicitly cleaned so it doesn't trigger a
    # ResourceWarning.
    tempdir.cleanup()
