#!/usr/bin/env python

from distutils.core import setup, Extension
from distutils.sysconfig import get_config_var
import os
import platform
import sys
import shutil
import multiprocessing
import subprocess

class flags_parser:
	def __init__(self):
		self.include_dirs = []
		self.library_dirs = []
		self.libraries = []

	def parse(self, args):
		"""Parse out the -I -L -l directives and return a list of all other arguments"""
		ret = []
		for token in args.split():
			prefix = token[:2]
			if prefix == '-I':
				self.include_dirs.append(token[2:])
			elif prefix == '-L':
				self.library_dirs.append(token[2:])
			elif prefix == '-l':
				self.libraries.append(token[2:])
			else:
				ret.append(token)
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

if '--bjam' in sys.argv:

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
		cmdline = 'b2 libtorrent-link=static boost-link=static release optimization=space stage_module --abbreviate-paths' + toolset + parallel_builds
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
	# Remove the '-Wstrict-prototypes' compiler option, which isn't valid for C++.
	os.environ['OPT'] = ' '.join(
		flag for flag in get_config_var('OPT').split() if flag != '-Wstrict-prototypes')

	source_list = os.listdir(os.path.join(os.path.dirname(__file__), "src"))
	source_list = [os.path.abspath(os.path.join(os.path.dirname(__file__), "src", s)) for s in source_list if s.endswith(".cpp")]

	if extra_cmd:
		flags = flags_parser()
		# ldflags must be parsed first to ensure the correct library search path order
		extra_link = flags.parse(ldflags)
		extra_compile = flags.parse(extra_cmd)

		ext = [Extension('libtorrent',
			sources = source_list,
			language='c++',
			include_dirs = flags.include_dirs,
			library_dirs = flags.library_dirs,
			extra_link_args = extra_link + arch(),
			extra_compile_args = extra_compile + arch() + target_specific(),
			libraries = ['torrent-rasterbar'] + flags.libraries)]

setup(name = 'python-libtorrent',
	version = '1.1.0',
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
