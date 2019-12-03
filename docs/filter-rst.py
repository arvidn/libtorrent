#!/usr/bin/env python
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
from __future__ import print_function

import sys


def indent(line):
    if line == '':
        return None
    end = 0
    for c in line:
        end += 1
        if " \t" not in c:
            return line[:end]
    return line


start_block = False
filter_indent = None

for line in open(sys.argv[1]):

    if line == '\n':
        continue

    if filter_indent:
        if line.startswith(filter_indent):
            continue
        else:
            filter_indent = None

    if line.strip().startswith('.. '):
        start_block = True
        continue

    if line.endswith('::\n'):
        start_block = True
        continue

    if start_block:
        filter_indent = indent(line)
        start_block = False
        continue

    sys.stdout.write(line)
