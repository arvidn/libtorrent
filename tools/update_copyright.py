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
    added = False
    found = False
    for line in f:
        if line.strip() == '/*':
            substitution_state += 1
        elif substitution_state == 1:
            if line.strip().lower().startswith('copyright'):
                # remove the existing copyright
                found = True
                existing_author = line.split(',')[-1]
                if existing_author in new_header or existing_author.strip() == 'Not Committed Yet':
                    continue
                print('preserving: %s' % line)
            elif not added and found:
                subst += new_header
                added = True
        elif line.strip() == '*/':
            substitution_state += 1

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
