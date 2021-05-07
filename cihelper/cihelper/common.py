import logging
import multiprocessing
import os
import pathlib
import subprocess
import sys
from typing import Sequence
from typing import Union

import appdirs

_LOG = logging.getLogger(__name__)


def configure_logger() -> None:
    logging.basicConfig(stream=sys.stdout, level=logging.DEBUG)


def get_jobs() -> int:
    return multiprocessing.cpu_count() + 1


_AnyPath = Union[str, os.PathLike]


# The real check_call has a huge list of parameters, just copy the ones we use
def check_call(cmd: Sequence[_AnyPath], *, cwd: _AnyPath) -> None:
    _LOG.info("+ %s", cmd)
    subprocess.check_call(cmd, cwd=cwd, stdout=sys.stdout, stderr=sys.stderr)


def get_appdirs() -> appdirs.AppDirs:
    return appdirs.AppDirs(appname="cihelper")


def get_base() -> pathlib.Path:
    base = os.environ.get("CIHELPER_BASE")
    if base:
        return pathlib.Path(base)
    # boost's bootstrap.sh (and maybe others) break if run from a dir with
    # spaces in the name. On OSX, user_data_dir includes "Application Support".
    # work around this by trying several dirs.
    dirs = get_appdirs()
    for path in (
        dirs.user_data_dir,
        dirs.user_cache_dir,
        dirs.user_state_dir,
        dirs.user_config_dir,
    ):
        if " " not in path:
            return pathlib.Path(path)
    # fallback to user_data_dir
    return pathlib.Path(dirs.user_data_dir)
