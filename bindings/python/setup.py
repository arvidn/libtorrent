#!/usr/bin/env python

from distutils import sysconfig
from distutils.core import setup, Extension
import os
import platform
import sys
import shutil
import multiprocessing
import subprocess

def parse_cmd(cmdline, prefix, keep_prefix = False):
	ret = []
	for token in cmdline.split():
		if token[:len(prefix)] == prefix:
			if keep_prefix:
				ret.append(token)
			else:
				ret.append(token[len(prefix):])
	return ret

def arch():
	if platform.system() != 'Darwin': return []
	a = os.uname()[4]
	if a == 'Power Macintosh': a = 'ppc'
	return ['-arch', a]

def target_specific():

	if platform.system() != 'Darwin': return []

	# on mavericks, clang will fail when unknown arguments are
	# passed in. python distutils will pass in arguments it doesn't
	# know about
	return ['-Wno-error=unused-command-line-argument-hard-error-in-future']

try:
	with open('compile_flags') as _file:
		extra_cmd = _file.read()

except:
	extra_cmd = None

try:
	with open('link_flags') as _file:
		ldflags = _file.read()

except:
	ldflags = None

ext = None
packages = None

if '--bjam' in sys.argv or ldflags == None or extra_cmd == None:

	if '--bjam' in sys.argv:
		del sys.argv[sys.argv.index('--bjam')]

	if not '--help' in sys.argv \
		and not '--help-commands' in sys.argv:

		toolset = ''
		file_ext = '.so'

		if platform.system() == 'Windows':
			# msvc 9.0 (2008) is the official windows compiler for python 2.6
			# http://docs.python.org/whatsnew/2.6.html#build-and-c-api-changes
			toolset = ' msvc-9.0'
			file_ext = '.pyd'

		parallel_builds = ' -j%d' % multiprocessing.cpu_count()

		# build libtorrent using bjam and build the installer with distutils
		cmdline = 'b2 boost=source link=static geoip=static boost-link=static release optimization=space stage_module --abbreviate-paths' + toolset + parallel_builds
		print(cmdline)
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

	packages = ['libtorrent']

else:

	source_list = os.listdir(os.path.join(os.path.dirname(__file__), "src"))
	source_list = [os.path.join("src", s) for s in source_list if s.endswith(".cpp")]

	ext = [Extension('libtorrent',
		sources = source_list,
		language='c++',
		include_dirs = parse_cmd(extra_cmd, '-I'),
		library_dirs = parse_cmd(extra_cmd, '-L'),
		extra_link_args = ldflags.split() + arch(),
		extra_compile_args = parse_cmd(extra_cmd, '-D', True) + arch() \
			+ target_specific(),
		libraries = ['torrent-rasterbar'] + parse_cmd(extra_cmd, '-l'))]

setup(name = 'python-libtorrent',
	version = '1.0.7',
	author = 'Arvid Norberg',
	author_email = 'arvid@libtorrent.org',
	description = 'Python bindings for libtorrent-rasterbar',
	long_description = 'Python bindings for libtorrent-rasterbar',
	url = 'http://libtorrent.org',
	platforms = [platform.system() + '-' + platform.machine()],
	license = 'BSD',
	packages = packages,
	ext_modules = ext
)

