#!/usr/bin/env python

from distutils import sysconfig
from distutils.core import setup, Extension
import os
import platform
import sys
import shutil
import multiprocessing

if not '--help' in sys.argv \
	and not '--help-commands' in sys.argv:

	toolset = ''
	file_ext = '.so'

	if platform.system() == 'Windows':
		# msvc 9.0 (2008) is the official windows compiler for python 2.6
		# http://docs.python.org/whatsnew/2.6.html#build-and-c-api-changes
		toolset = ' msvc-9.0'
		file_ext = '.pyd'

	parallell_builds = ' -j%d' % multiprocessing.cpu_count()

	# build libtorrent using bjam and build the installer with distutils

	cmdline = 'bjam boost=source link=static geoip=static boost-link=static release optimization=space stage_module --abbreviate-paths' + toolset + parallell_builds
	print cmdline
	if os.system(cmdline) != 0:
		print('build failed')
		sys.exit(1)

	try: os.mkdir('build')
	except: pass
	try: shutil.rmtree('build/lib')
	except: pass
	try: os.mkdir('build/lib')
	except: pass
	try: os.mkdir('libtorrent')
	except: pass
	shutil.copyfile('libtorrent' + file_ext, 'build/lib/libtorrent' + file_ext)

setup( name='python-libtorrent',
	version='1.0.0',
	author = 'Arvid Norberg',
	author_email='arvid@rasterbar.com',
	description = 'Python bindings for libtorrent-rasterbar',
	long_description = 'Python bindings for libtorrent-rasterbar',
	url = 'http://libtorrent.org',
	platforms = [platform.system() + '-' + platform.machine()],
	license = 'BSD',
	packages = ['libtorrent'],
)

