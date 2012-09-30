#! /usr/bin/env python
import os
import sys
import glob

version = (int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))

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
			l = 'AC_INIT([libtorrent-rasterbar],[%d.%d.%d],[arvid@cs.umu.se],\n' % (version[0], version[1], version[2])
		elif 'set (VERSION ' in l and name.endswith('.txt'):
			l = 'set (VERSION "%d.%d.%d")\n' % (version[0], version[1], version[2])
		elif ':Version: ' in l and name.endswith('.rst'):
			l = ':Version: %d.%d.%d\n' % (version[0], version[1], version[2])
		elif 'VERSION = ' in l and name.endswith('Jamfile'):
			l = 'VERSION = %d.%d.%d ;\n' % (version[0], version[1], version[2])

		subst += l

	f.close()
	open(name, 'w+').write(subst)


substitute_file('include/libtorrent/version.hpp')
substitute_file('CMakeLists.txt')
substitute_file('configure.ac')
for i in glob.glob('docs/*.rst'):
	substitute_file(i)
substitute_file('Jamfile')


