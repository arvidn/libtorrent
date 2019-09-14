#!/usr/bin/env python
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import shutil
import glob


def clean():
    to_delete = [
        'session_stats',
        'libtorrent_logs*',
        'round_trip_ms.log',
        'dht.log',
        'upnp.log',
        'natpmp.log',
        'bin',
        'build-aux',
        '.deps',
        'test_tmp_*',
        'bjam_build.*.xml',
        '*.exe',
        '*.pdb',
        '*.pyd',
        'dist',
        'build',
        '.libs',
        '*.cpp.orig',
        '*.cpp.rej',
        '*.hpp.orig',
        '*.hpp.rej',
        '*.gcov',
        '*.gcno',
        '*.gcda',
        'Makefile.in',
        'Makefile',
        'lib*.a',
        'Jamfile.rej',
        'Jamfile.orig',
        '*.o',
        '*.lo',
        'autom4te.cache',
        'configure',
        'config.report',
        'config.log',
        '.lib',
    ]

    directories = [
        'examples',
        'test',
        '.',
        'tools',
        'src',
        'simulation',
        'fuzzers',
        os.path.join('src', 'kademlia'),
        os.path.join('include', 'libtorrent'),
        os.path.join('include', os.path.join('libtorrent', '_aux')),
        os.path.join('include', os.path.join('libtorrent', 'kademlia')),
        os.path.join('bindings', 'python'),
        os.path.join('bindings', os.path.join('python', 'src')),
        os.path.join('bindings', 'c'),
        os.path.join('bindings', os.path.join('c', 'src')),
        os.path.join('simulation', 'libsimulator')
    ]

    for d in directories:
        for f in to_delete:
            path = os.path.join(d, f)
            entries = glob.glob(path)
            for p in entries:
                try:
                    shutil.rmtree(p)
                    print(p)
                except Exception:
                    try:
                        os.remove(p)
                        print(p)
                    except Exception as e:
                        print(p, e)


if __name__ == "__main__":
    clean()
