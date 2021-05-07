import logging
import os
import runpy
import shlex
import sys

from . import common
from . import env

_LOG = logging.getLogger(__name__)


def set_env(name: str, value: str) -> None:
    _LOG.info("set %s = %r", name, value)
    os.environ[name] = value


def append_inner_env(name: str, value: str) -> None:
    existing = os.environ.get("CIBW_ENVIRONMENT", "")
    set_env("CIBW_ENVIRONMENT", f"{existing} {name}={value}")


def setup_env() -> None:
    if sys.platform == "linux":
        # cibuildwheel will invoke docker.

        # Our homedir-based install paths will be different in the container.
        # Set this *now*, so we calculate other paths correctly
        set_env("CIHELPER_BASE", "/cihelper")
        # *Also* set in CIBW_ENVIRONMENT so paths are calculated the same
        # way inside docker
        append_inner_env("CIHELPER_BASE", "/cihelper")

        # We'll need to install cihelper in the container
        set_env(
            "CIBW_BEFORE_ALL",
            "python -m pip install ./cihelper && cihelper_boost_install",
        )

        # Per https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html,
        # we need gcc 6.1 for stable C++14 ABI.
        # Per https://gcc.gnu.org/onlinedocs/libstdc++/manual/abi.html and
        # https://github.com/mayeut/pep600_compliance, this requires
        # manylinux_2_24
        set_env("CIBW_MANYLINUX_X86_64_IMAGE", "manylinux_2_24")
        set_env("CIBW_MANYLINUX_I686_IMAGE", "manylinux_2_24")

        # Supply our env variables
        for name, value in env.get().items():
            # We've already figured exact values we want. We *don't* want
            # cibuildwheel to do any expansion. shlex-quote will defend against
            # this.
            append_inner_env(name, shlex.quote(value))

        # Supply our PATH entries
        path = list(env.get_path())
        if path:
            value = os.pathsep.join(path + ["$PATH"])
            # We *do* want cibuildwheel to do expansion, for $PATH. I can't find a
            # way to tell cibuildwheel to expand $PATH but not perform expansion
            # on the other paths we're supplying; hopefully they don't contain
            # variable names.
            append_inner_env("PATH", f'"{value}"')
    else:
        # cibuildwheel will build the wheel nakedly.
        # Do *not* install cihelper, because it's already installed (it's
        # running this script!)
        set_env("CIBW_BEFORE_ALL", "cihelper_boost_install")

        # cibuildwheel's environment variable expansion seems to stomp on \, so
        # with CIBW_ENVIRONMENT=PATH="c:\foo;$PATH", PATH becomes "c:foo;...".
        # Not sure if the intent is that we just manipulate PATH directly on
        # Windows since we run nakedly. That seems the best answer for now
        # so that's what we do.
        for name, value in env.get().items():
            set_env(name, value)
        path = list(env.get_path())
        if path:
            set_env("PATH", os.pathsep.join(path + [os.environ["PATH"]]))

    # This would normally be in root files like setup.cfg or setup.py, but all
    # our files are in bindings/python so cibuildwheel can't figure it out on
    # its own
    set_env("CIBW_PROJECT_REQUIRES_PYTHON", ">=3.6")
    # Per https://cibuildwheel.readthedocs.io/en/stable/cpp_standards/, 10.14
    # is required for full C++17 support
    set_env("MACOSX_DEPLOYMENT_TARGET", "10.14")


def run() -> None:
    setup_env()
    runpy.run_module("cibuildwheel", run_name="__main__")


def run_cmd() -> None:
    common.configure_logger()
    run()
