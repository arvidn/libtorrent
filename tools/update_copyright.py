#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import glob
import copyright
import os


def update_file(name):
    if os.path.split(name)[1] in ['puff.cpp', 'puff.hpp', 'sha1.cpp', 'sha1.hpp', 'route.h']:
        return

    new_header = copyright.get_authors(name)
    subst = ''
    f = open(name)

    substitution_state = 0
    for line in f:
        if substitution_state == 0 and line.strip() == '/*':
            subst = '/*\n\n'
            substitution_state += 1
            continue
        elif substitution_state == 1:
            if line.strip().lower().startswith('copyright'):
                existing_author = line.split(',')[-1]
                if existing_author not in new_header and not existing_author.strip() == 'Not Committed Yet':
                    print('preserving: %s' % line)
                    subst += line
            elif line.strip() == '*/':
                subst += new_header + '''All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/
'''
                substitution_state += 1
            continue

        subst += line

    f.close()
    open(name, 'w+').write(subst)


for i in glob.glob('src/*.cpp') + \
        glob.glob('include/libtorrent/*.hpp') + \
        glob.glob('include/libtorrent/aux_/*.hpp') + \
        glob.glob('include/libtorrent/extensions/*.hpp') + \
        glob.glob('include/libtorrent/kademlia/*.hpp') + \
        glob.glob('src/kademlia/*.cpp') + \
        glob.glob('examples/*.cpp') + \
        glob.glob('examples/*.hpp') + \
        glob.glob('tools/*.cpp') + \
        glob.glob('test/*.cpp') + \
        glob.glob('test/*.hpp') + \
        glob.glob('fuzzers/src/*.cpp') + \
        glob.glob('fuzzers/src/*.hpp'):
    update_file(i)
