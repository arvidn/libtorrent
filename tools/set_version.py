#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import re
import sys
from typing import Callable
from typing import Dict
from typing import Tuple

v = (int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))


def format_fingerprint(version: Tuple[int, int, int, int]) -> str:
    ret = ""
    for i in version:
        if i < 10:
            ret += chr(ord("0") + i)
        else:
            ret += chr(ord("A") + i - 10)
    return ret


def fv(v: Tuple[int, int, int, int]) -> str:
    return f"{v[0]}.{v[1]}.{v[2]}.{v[3]}"


rev = os.popen("git log -1 --format=format:%h").read().strip()


def substitute_file(name: str, subs: Dict[str, Callable[[str], str]]) -> None:
    subst = ""
    with open(name) as f:
        for line in f:
            for match, sub in subs.items():
                if match in line:
                    line = sub(line)
            subst += line

    with open(name, "w+") as f:
        f.write(subst)


tab = "\t"
nl = "\n"

substitute_file(
    "include/libtorrent/version.hpp",
    {
        "constexpr int version_major = ":
            lambda ln: f"{tab}constexpr int version_major = {v[0]};{nl}",
        "constexpr int version_minor = ":
            lambda ln: f"{tab}constexpr int version_minor = {v[1]};{nl}",
        "constexpr int version_tiny = ":
            lambda ln: f"{tab}constexpr int version_tiny = {v[2]};{nl}",
        "constexpr std::uint64_t version_revision = ":
            lambda ln: f"{tab}constexpr std::uint64_t version_revision = 0x{rev};{nl}",
        "constexpr char const* version_str = ":
            lambda ln: f'{tab}constexpr char const* version_str = "{fv(v)}";{nl}',
        "#define LIBTORRENT_VERSION_MAJOR":
            lambda ln: f"#define LIBTORRENT_VERSION_MAJOR {v[0]}{nl}",
        "#define LIBTORRENT_VERSION_MINOR":
            lambda ln: f"#define LIBTORRENT_VERSION_MINOR {v[1]}{nl}",
        "#define LIBTORRENT_VERSION_TINY":
            lambda ln: f"#define LIBTORRENT_VERSION_TINY {v[2]}{nl}",
        "#define LIBTORRENT_VERSION ":
            lambda ln: f'#define LIBTORRENT_VERSION "{fv(v)}"{nl}',
        "#define LIBTORRENT_REVISION ":
            lambda ln: f'#define LIBTORRENT_REVISION "{rev}"{nl}',
    },
)

substitute_file(
    "Makefile",
    {
        "VERSION=": lambda ln: f"VERSION={v[0]}.{v[1]}.{v[2]}{nl}",
    },
)

substitute_file(
    "bindings/python/setup.cfg",
    {
        "version = ": lambda ln: f"version = {v[0]}.{v[1]}.{v[2]}{nl}",
    },
)
substitute_file(
    "src/settings_pack.cpp",
    {
        '"-LT': lambda ln: re.sub(
            '"-LT[0-9A-Za-z]{4}-"', f'"-LT{format_fingerprint(v)}-"', ln
        ),
    },
)
substitute_file(
    "test/test_settings_pack.cpp",
    {
        '"libtorrent/': lambda ln: re.sub(
            '"libtorrent/\\d+\\.\\d+\\.\\d+\\.\\d+"',
            f'"libtorrent/{v[0]}.{v[1]}.{v[2]}.{v[3]}"', ln
        ),
    },
)
substitute_file(
    "docs/header.rst",
    {
        ":Version: ": lambda ln: f":Version: {v[0]}.{v[1]}.{v[2]}{nl}",
    },
)
substitute_file(
    "docs/hunspell/libtorrent.dic",
    {
        "LT": lambda ln: re.sub(
            "LT[0-9A-Za-z]{4}", f"LT{format_fingerprint(v)}", ln),
    },
)
substitute_file(
    "Jamfile",
    {
        "VERSION = ": lambda ln: f"VERSION = {v[0]}.{v[1]}.{v[2]} ;{nl}",
    },
)
