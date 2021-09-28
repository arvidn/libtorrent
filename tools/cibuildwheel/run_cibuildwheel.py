#!/usr/bin/env python

import logging
import os
import runpy
import shlex
import subprocess
import sys
from typing import List

_LOG = logging.getLogger(__name__)


def quote(s: str) -> str:
    if sys.platform == "win32":
        return subprocess.list2cmdline([s])
    else:
        return shlex.quote(s)


def set_env(name: str, value: str) -> None:
    _LOG.info("set %s = %s", name, value)
    os.environ[name] = value


def append_inner_env(name: str, value: str) -> None:
    existing = os.environ.get("CIBW_ENVIRONMENT", "")
    set_env("CIBW_ENVIRONMENT", f"{existing} {name}={value}")


def setup_env() -> None:
    # cibuildwheel allows many ways of selecting builds. For simplicity, we
    # only support selecting a single build via CIBW_BUILD.
    build = os.environ.get("CIBW_BUILD")
    assert build is not None, "CIBW_BUILD must be specified"
    assert " " not in build, "CIBW_BUILD must specify exactly one build"
    assert "*" not in build, "CIBW_BUILD must specify exactly one build"

    # As of 2.2.0a1, CIBW_BUILD does not supercede CIBW_ARCHS.
    set_env("CIBW_ARCHS", "all")

    # Per https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html,
    # we need gcc 6.1 for stable C++14 ABI.
    # manylinux1 has gcc 4.8.2.
    # manylinux2010 and manylinux2014 have a backported gcc at version
    # 8+. Their glibcs are old; they achieve support for newer gcc by
    # statically linking backported newer glibc functions.
    # manylinux_2_24 has a "plain" install of gcc 6.x.

    set_env("CIBW_MANYLINUX_X86_64_IMAGE", "manylinux2010")
    set_env("CIBW_MANYLINUX_I686_IMAGE", "manylinux2010")

    # manylinux2014 is the oldest available for these archs.
    set_env("CIBW_MANYLINUX_AARCH64_IMAGE", "manylinux2014")
    set_env("CIBW_MANYLINUX_PPC64LE_IMAGE", "manylinux2014")
    set_env("CIBW_MANYLINUX_S390X_IMAGE", "manylinux2014")

    # musllinux_1_1 is the latest available for these archs.
    set_env("CIBW_MUSLLINUX_X86_64_IMAGE", "musllinux_1_1")
    set_env("CIBW_MUSLLINUX_I686_IMAGE", "musllinux_1_1")
    set_env("CIBW_MUSLLINUX_AARCH64_IMAGE", "musllinux_1_1")
    set_env("CIBW_MUSLLINUX_PPC64LE_IMAGE", "musllinux_1_1")
    set_env("CIBW_MUSLLINUX_S390X_IMAGE", "musllinux_1_1")

    # This would normally be in root files like setup.cfg or setup.py, but all
    # our files are in bindings/python so cibuildwheel can't figure it out on
    # its own
    set_env("CIBW_PROJECT_REQUIRES_PYTHON", ">=3.6")
    # Per https://cibuildwheel.readthedocs.io/en/stable/cpp_standards/, 10.9
    # is required for full C++14 support
    set_env("MACOSX_DEPLOYMENT_TARGET", "10.9")

    # Set up tests.
    # NB: cibuildwheel expands {project} to the project root.
    test_command_str = "cd '{project}/bindings/python' && python test.py"
    set_env("CIBW_TEST_COMMAND", f"bash -c {quote(test_command_str)}")

    # TODO: support pre-existing boost in $BOOST_ROOT, for mac and windows.
    boost_version = "1.76.0"
    boost_root = "c:/boost" if sys.platform == "win32" else "/tmp/boost"
    boost_build_path = f"{boost_root}/tools/build"

    before_all_commands = [
        "./tools/cibuildwheel/setup_boost.sh "
        f"{quote(boost_version)} {quote(boost_root)}",
    ]
    before_test_commands: List[str] = []

    if sys.platform == "win32":
        # Install openssl from chocolatey
        options = ""
        if build.endswith("win32"):
            options = "--x86"
        before_all_commands.append(f"choco install {options} openssl")
        # Uninstall openssl prior to testing
        before_test_commands.append("choco uninstall openssl")

    if sys.platform == "linux":
        # Currently, we link against libutil and libdl both dynamically and
        # statically. libutil.a and libdl.a are not installed by default on
        # all of the centos manylinux containers.
        if "manylinux" in build:
            before_all_commands.append("yum install -y glibc-static")

        # Our openssl build script will honor existing system openssl, if it's
        # new enough.
        if "musllinux" in build:
            before_all_commands.append("apk add openssl-dev openssl-libs-static")

        before_all_commands.append("./tools/cibuildwheel/setup_openssl.sh")

    before_all_command_str = " && ".join(before_all_commands)
    set_env("CIBW_BEFORE_ALL", f"bash -c {quote(before_all_command_str)}")

    # cibuildwheel's environment variable expansion seems to stomp on \, so
    # with CIBW_ENVIRONMENT=PATH="c:\foo;$PATH", PATH becomes "c:foo;...".
    # Not sure if the intent is that we just manipulate PATH directly on
    # Windows since we run nakedly. That seems the best answer for now
    # so that's what we do.
    if sys.platform == "win32":
        set_env("BOOST_ROOT", boost_root)
        set_env("BOOST_BUILD_PATH", boost_build_path)
        set_env("PATH", os.pathsep.join([boost_root, os.environ["PATH"]]))
    else:
        append_inner_env("BOOST_ROOT", boost_root)
        append_inner_env("BOOST_BUILD_PATH", boost_build_path)
        path = os.pathsep.join([boost_root, "$PATH"])
        # Quote PATH to protect expansion.
        append_inner_env("PATH", f'"{path}"')


def run() -> None:
    logging.basicConfig(stream=sys.stdout, level=logging.DEBUG)
    setup_env()
    runpy.run_module("cibuildwheel", run_name="__main__")


if __name__ == "__main__":
    run()
