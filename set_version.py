#! /usr/bin/env python
import os
import sys
import glob
import re

version = (int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))

def v(version):
	ret = ()
	for i in version:
		if i < 9: ret = ret + (chr(ord('0') + i),)
		else: ret = ret + (chr(ord('A') + i - 10),)
	return ret

def substitute_file(name):
	subst = ''
	f = open(name)
	for l in f:
		if '#define LIBTORRENT_VERSION_MAJOR' in l and name.endswith('.hpp'):
			l = '#define LIBTORRENT_VERSION_MAJOR %d\n' % version[0]
		elif '#define LIBTORRENT_VERSION_MINOR' in l and name.endswith('.hpp'):
			l = '#define LIBTORRENT_VERSION_MINOR %d\n' % version[1]
		elif '#define LIBTORRENT_VERSION_TINY' in l and name.endswith('.hpp'):
			l = '#define LIBTORRENT_VERSION_TINY %d\n' % version[2]
		elif '#define LIBTORRENT_VERSION ' in l and name.endswith('.hpp'):
			l = '#define LIBTORRENT_VERSION "%d.%d.%d.%d"\n' % (version[0], version[1], version[2], version[3])
		elif 'AC_INIT([libtorrent-rasterbar]' in l and name.endswith('.ac'):
			l = 'AC_INIT([libtorrent-rasterbar],[%d.%d.%d],[arvid@libtorrent.org],\n' % (version[0], version[1], version[2])
		elif 'set (VERSION ' in l and name.endswith('.txt'):
			l = 'set (VERSION "%d.%d.%d")\n' % (version[0], version[1], version[2])
		elif ':Version: ' in l and (name.endswith('.rst') or name.endswith('.py')):
			l = ':Version: %d.%d.%d\n' % (version[0], version[1], version[2])
		elif 'VERSION = ' in l and name.endswith('Jamfile'):
			l = 'VERSION = %d.%d.%d ;\n' % (version[0], version[1], version[2])
		elif 'version=' in l and name.endswith('setup.py'):
			l = "\tversion = '%d.%d.%d',\n" % (version[0], version[1], version[2])
		elif "version = '" in l and name.endswith('setup.py'):
			l = "\tversion = '%d.%d.%d',\n" % (version[0], version[1], version[2])
		elif '"-LT' in l and name.endswith('settings_pack.cpp'):
			l = re.sub('"-LT[0-9A-Za-z]{4}-"', '"-LT%c%c%c%c-"' % v(version), l)

		subst += l

	f.close()
	open(name, 'w+').write(subst)


substitute_file('include/libtorrent/version.hpp')
substitute_file('CMakeLists.txt')
substitute_file('configure.ac')
substitute_file('bindings/python/setup.py')
substitute_file('docs/gen_reference_doc.py')
substitute_file('src/settings_pack.cpp')
for i in glob.glob('docs/*.rst'):
	substitute_file(i)
substitute_file('Jamfile')


