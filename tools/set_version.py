#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import sys
import glob
import re

version = (int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))


def v(version):
    ret = ()
    for i in version:
        if i < 10:
            ret = ret + (chr(ord('0') + i),)
        else:
            ret = ret + (chr(ord('A') + i - 10),)
    return ret


revision = os.popen('git log -1 --format=format:%h').read().strip()


def substitute_file(name):
    subst = ''
    f = open(name)
    for line in f:
        if 'constexpr int version_major = ' in line and name.endswith('.hpp'):
            line = '\tconstexpr int version_major = %d;\n' % version[0]
        elif 'constexpr int version_minor = ' in line and name.endswith('.hpp'):
            line = '\tconstexpr int version_minor = %d;\n' % version[1]
        elif 'constexpr int version_tiny = ' in line and name.endswith('.hpp'):
            line = '\tconstexpr int version_tiny = %d;\n' % version[2]
        elif 'constexpr std::uint64_t version_revision = ' in line and name.endswith('.hpp'):
            line = '\tconstexpr std::uint64_t version_revision = 0x%s;\n' % revision
        elif 'constexpr char const* version_str = ' in line and name.endswith('.hpp'):
            line = '\tconstexpr char const* version_str = "%d.%d.%d.%d";\n' \
                % (version[0], version[1], version[2], version[3])
        elif '#define LIBTORRENT_VERSION_MAJOR' in line and name.endswith('.hpp'):
            line = '#define LIBTORRENT_VERSION_MAJOR %d\n' % version[0]
        elif '#define LIBTORRENT_VERSION_MINOR' in line and name.endswith('.hpp'):
            line = '#define LIBTORRENT_VERSION_MINOR %d\n' % version[1]
        elif '#define LIBTORRENT_VERSION_TINY' in line and name.endswith('.hpp'):
            line = '#define LIBTORRENT_VERSION_TINY %d\n' % version[2]
        elif '#define LIBTORRENT_VERSION ' in line and name.endswith('.hpp'):
            line = '#define LIBTORRENT_VERSION "%d.%d.%d.%d"\n' % (version[0], version[1], version[2], version[3])
        elif '#define LIBTORRENT_REVISION ' in line and name.endswith('.hpp'):
            line = '#define LIBTORRENT_REVISION "%s"\n' % revision
        elif 'AC_INIT([libtorrent-rasterbar]' in line and name.endswith('.ac'):
            line = 'AC_INIT([libtorrent-rasterbar],[%d.%d.%d],[arvid@libtorrent.org],\n' % (
                version[0], version[1], version[2])
        elif 'set (VERSION ' in line and name.endswith('.txt'):
            line = 'set (VERSION "%d.%d.%d")\n' % (version[0], version[1], version[2])
        elif ':Version: ' in line and (name.endswith('.rst') or name.endswith('.py')):
            line = ':Version: %d.%d.%d\n' % (version[0], version[1], version[2])
        elif line.startswith('VERSION = ') and name.endswith('Jamfile'):
            line = 'VERSION = %d.%d ;\n' % (version[0], version[1])
        elif 'VERSION=' in line and name.endswith('Makefile'):
            line = 'VERSION=%d.%d.%d\n' % (version[0], version[1], version[2])
        elif 'version=' in line and name.endswith('setup.py'):
            line = '    version="%d.%d.%d",\n' % (version[0], version[1], version[2])
        elif '"-LT' in line and name.endswith('settings_pack.cpp'):
            line = re.sub('"-LT[0-9A-Za-z]{4}-"', '"-LT%c%c%c%c-"' % v(version), line)
        elif 'local FULL_VERSION = ' in line and name == 'Jamfile':
            line = '\tlocal FULL_VERSION = %d.%d.%d ;\n' % (version[0], version[1], version[2])

        subst += line

    f.close()
    open(name, 'w+').write(subst)


substitute_file('include/libtorrent/version.hpp')
substitute_file('Makefile')
substitute_file('CMakeLists.txt')
substitute_file('bindings/python/setup.py')
substitute_file('docs/gen_reference_doc.py')
substitute_file('src/settings_pack.cpp')
for i in glob.glob('docs/*.rst'):
    substitute_file(i)
substitute_file('Jamfile')
