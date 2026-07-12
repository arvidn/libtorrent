#!/usr/bin/env python3
# vim: tabstop=4 noexpandtab shiftwidth=4 softtabstop=4

# Regression test for the "install" targets in the Jamfile and CMakeLists.txt:
# the installed include tree must contain only libtorrent's own public
# headers, and those headers must be self-sufficient, with no access to any
# vendored third-party headers.
#
# Usage:
#   check_installed_headers.py <install-prefix>

import argparse
import glob
import os
import shlex
import subprocess
import sys


def check_no_vendored_headers(include_dir: str) -> None:
    if not os.path.isdir(include_dir):
        sys.exit(f"error: {include_dir} does not exist")
    entries = sorted(os.listdir(include_dir))
    if entries != ["libtorrent"]:
        sys.exit(
            f"error: {include_dir} must contain exactly libtorrent/, found: {entries}"
        )
    print(f"OK: {include_dir} contains only libtorrent/")


def find_pc_file(prefix: str) -> str:
    # CMAKE_INSTALL_LIBDIR (and therefore where the .pc file ends up) varies
    # by distro/prefix -- e.g. lib, lib64, lib/x86_64-linux-gnu -- so search
    # for it rather than assuming a fixed layout.
    matches = glob.glob(
        os.path.join(prefix, "**", "pkgconfig", "libtorrent-rasterbar.pc"),
        recursive=True,
    )
    if len(matches) != 1:
        sys.exit(
            f"error: expected exactly one libtorrent-rasterbar.pc under {prefix}, "
            f"found {matches}"
        )
    return matches[0]


def check_public_headers_compile(prefix: str) -> None:
    pc_file = find_pc_file(prefix)
    cflags = shlex.split(
        subprocess.run(
            ["pkg-config", "--cflags", pc_file],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )

    header = os.path.join(prefix, "include", "libtorrent", "libtorrent.hpp")
    cxx = os.environ.get("CXX", "c++")
    subprocess.run([cxx, "-std=c++17", "-fsyntax-only", *cflags, header], check=True)
    print(f"OK: {header} compiles using only the installed headers")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="verify an installed libtorrent include/ tree contains "
        "only libtorrent's own headers, and that they are self-sufficient"
    )
    parser.add_argument("prefix", help="install prefix to check")
    args = parser.parse_args()

    prefix = os.path.abspath(args.prefix)
    check_no_vendored_headers(os.path.join(prefix, "include"))
    check_public_headers_compile(prefix)


if __name__ == "__main__":
    main()
