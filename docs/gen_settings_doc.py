#!/usr/bin/env python3

from typing import Dict
from typing import List
from typing import Sequence
from typing import Set

f = open("../include/libtorrent/settings_pack.hpp")

out = open("settings.rst", "w+")
all_names: Set[str] = set()


def ljust(values: Sequence[str], widths: Sequence[int]) -> Sequence[str]:
    return [value.ljust(width) for value, width in zip(values, widths)]


def render_section(
    names: List[str], description: str, type: str, default_values: List[str]
) -> None:
    widths = (
        max(len(v) for v in names + ["name"]),
        max(len(v) for v in (type, "type")),
        max(len(v) for v in default_values + ["default"]),
    )

    # add link targets for the rest of the manual to reference
    for name in names:
        out.write(f".. _{name}:\n\n")
        for part in name.split("_"):
            all_names.add(part)

    if names:
        out.write(".. raw:: html\n\n")
        for name in names:
            out.write(f'\t<a name="{name}"></a>\n')
        out.write("\n")

    separator = "+-" + "-+-".join("-" * w for w in widths) + "-+\n"

    # build a table for the settings, their type and default value
    out.write(separator)

    out.write("| ")
    out.write(" | ".join(ljust(("name", "type", "default"), widths)))
    out.write(" |\n")
    out.write(separator.replace("-", "="))
    for name, default_value in zip(names, default_values):
        out.write("| ")
        out.write(" | ".join(ljust((name, type, default_value), widths)))
        out.write(" |\n")

        out.write(separator)
    out.write("\n")
    out.write(f"{description}\n")


mode = ""

# parse out default values for settings
f2 = open("../src/settings_pack.cpp")
def_map: Dict[str, str] = {}
for line in f2:
    line = line.strip()
    if (
        not line.startswith("SET(")
        and not line.startswith("SET_NOPREV(")
        and not line.startswith("DEPRECATED_SET(")
    ):
        continue

    args = line.split("(")[1].split(",")
    if args[1].strip()[0] == '"':
        default = ",".join(args[1:]).strip()[1:].split('"')[0].strip()
    else:
        default = args[1].strip()
    def_map[args[0]] = default
    print("%s = %s" % (args[0], default))

description = ""
names: List[str] = []

for line in f:
    if "enum string_types" in line:
        mode = "string"
    if "enum bool_types" in line:
        mode = "bool"
    if "enum int_types" in line:
        mode = "int"
    if "#if TORRENT_ABI_VERSION == 1" in line:
        mode += "skip"
    if "#if TORRENT_ABI_VERSION <= 2" in line:
        mode += "skip"
    if "#if TORRENT_ABI_VERSION <= 3" in line:
        mode += "skip"
    if "#endif" in line:
        mode = mode[0:-4]

    if mode == "":
        continue
    if mode[-4:] == "skip":
        continue

    line = line.lstrip()

    if line == "" and len(names) > 0:
        if description == "":
            for n in names:
                print('WARNING: no description for "%s"' % n)
        elif description.strip() != "hidden":
            default_values = []
            for n in names:
                default_values.append(def_map[n])
            render_section(names, description, mode, default_values)
        description = ""
        names = []

    if line.startswith("};"):
        mode = ""
        continue

    if line.startswith("//"):
        if line[2] == " ":
            description += line[3:]
        else:
            description += line[2:]
        continue

    line = line.strip()
    if line.endswith(","):
        line = line[:-1]  # strip trailing comma
        if "=" in line:
            line = line.split("=")[0].strip()
        if line.endswith("_internal"):
            continue

        names.append(line)

dictionary = open("hunspell/settings.dic", "w+")
for w in sorted(all_names):
    dictionary.write(w + "\n")
dictionary.close()
out.close()
f.close()
