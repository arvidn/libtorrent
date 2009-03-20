import os
import sys

version = (int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))

def substitute_file(name):
	subst = ''
	f = open(name)
	for l in f:
		if '#define LIBTORRENT_VERSION_MAJOR' in l:
			l = '#define LIBTORRENT_VERSION_MAJOR %d\n' % version[0]
		elif '#define LIBTORRENT_VERSION_MINOR' in l:
			l = '#define LIBTORRENT_VERSION_MINOR %d\n' % version[1]
		if '#define LIBTORRENT_VERSION_TINY' in l:
			l = '#define LIBTORRENT_VERSION_TINY %d\n' % version[2]
		elif '#define LIBTORRENT_VERSION' in l:
			l = '#define LIBTORRENT_VERSION "%d.%d.%d.%d"\n' % (version[0], version[1], version[2], version[3])
		elif 'AC_INIT([libtorrent-rasterbar]' in l:
			l = 'AC_INIT([libtorrent-rasterbar], [%d.%d.%d], [arvid@cs.umu.se])\n' % (version[0], version[1], version[2])
		elif 'set (VERSION ' in l:
			l = 'set (VERSION "%d.%d.%d")\n' % (version[0], version[1], version[2])
		elif ':Version: ' in l:
			l = ':Version: %d.%d.%d\n' % (version[0], version[1], version[2])

		subst += l

	f.close()
	open(name, 'w+').write(subst)


substitute_file('include/libtorrent/version.hpp')
substitute_file('CMakeLists.txt')
substitute_file('configure.in')
substitute_file('docs/manual.rst')
substitute_file('docs/building.rst')
substitute_file('docs/features.rst')

