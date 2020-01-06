#!/bin/python
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import sys

# no processing of the first file
sys.stdout.write(open(sys.argv[1], 'r').read())
sys.stderr.write('joining %s\n' % sys.argv[1])

for name in sys.argv[2:]:
    sys.stdout.write('\n')
    sys.stderr.write('joining %s\n' % name)
    f = open(name, 'r')
    for l in f:
        # strip out the table of contents from subsequent files
        if '.. contents::' in l:
            in_directive = True
            continue
        if ':Author:' in l:
            continue
        if ':Version:' in l:
            continue

        if l[0] in ' \t' and in_directive:
            continue
        in_directive = False
        sys.stdout.write(l)
