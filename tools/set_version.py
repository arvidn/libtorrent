#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import glob
import os
import re
import sys


def v(version):
    ret = ()
    for i in version:
        if i < 10:
            ret = ret + (chr(ord('0') + i),)
        else:
            ret = ret + (chr(ord('A') + i - 10),)
    return ret


def substitute_file(name):
    subst = ''

    with open(name) as f:

        for line in f:
            if '#define LIBTORRENT_VERSION_MAJOR' in line and name.endswith('.hpp'):
                line = f'#define LIBTORRENT_VERSION_MAJOR {version[0]}\n'
            elif '#define LIBTORRENT_VERSION_MINOR' in line and name.endswith('.hpp'):
                line = f'#define LIBTORRENT_VERSION_MINOR {version[1]}\n'
            elif '#define LIBTORRENT_VERSION_TINY' in line and name.endswith('.hpp'):
                line = f'#define LIBTORRENT_VERSION_TINY {version[2]}\n'
            elif '#define LIBTORRENT_VERSION ' in line and name.endswith('.hpp'):
                line = f'#define LIBTORRENT_VERSION "{version[0]}.{version[1]}.{version[2]}.{version[3]}"\n'
            elif '#define LIBTORRENT_REVISION ' in line and name.endswith('.hpp'):
                line = f'#define LIBTORRENT_REVISION "{revision}"\n'
            elif 'AC_INIT([libtorrent-rasterbar]' in line and name.endswith('.ac'):
                line = f'AC_INIT([libtorrent-rasterbar],[{version[0]}.{version[1]}.{version[2]}],[arvid@libtorrent.org],\n'
            elif '\tVERSION "' in line and "${" not in line and name.endswith('.txt'):
                line = f'\tVERSION "{version[0]}.{version[1]}.{version[2]}"\n'
            elif ':Version: ' in line and (name.endswith('.rst') or name.endswith('.py')):
                line = f':Version: {version[0]}.{version[1]}.{version[2]}\n'
            elif 'VERSION=' in line and name.endswith('build_dist.sh'):
                line = f'VERSION={version[0]}.{version[1]}.{version[2]}\n'
            elif 'version=' in line and name.endswith('setup.py'):
                line = f'    version="{version[0]}.{version[1]}.{version[2]}",\n'
            elif '"-LT' in line and name.endswith('settings_pack.cpp'):
                line = re.sub('"-LT[0-9A-Za-z]{4}-"', f'"-LT{"".join(v(version))}-"', line)
            elif 'local FULL_VERSION = ' in line and name == 'Jamfile':
                line = f'\tlocal FULL_VERSION = {version[0]}.{version[1]}.{version[2]} ;\n'

            subst += line

    with open(name, 'w+') as f:
        f.write(subst)


version = (int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))
revision = os.popen('git log -1 --format=format:%h').read().strip()


substitute_file('CMakeLists.txt')
substitute_file('Jamfile')
substitute_file('build_dist.sh')
substitute_file('configure.ac')
substitute_file('bindings/python/setup.py')
substitute_file('docs/gen_reference_doc.py')
substitute_file('include/libtorrent/version.hpp')
substitute_file('src/settings_pack.cpp')
for i in glob.glob('docs/*.rst'):
    substitute_file(i)
