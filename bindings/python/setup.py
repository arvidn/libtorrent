#!/usr/bin/env python

from distutils.core import setup, Extension
from distutils.sysconfig import get_config_vars
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
			file_ext = '.pyd'
			# See https://wiki.python.org/moin/WindowsCompilers for a table of msvc versions
			# used for each python version
			# Specify the full version number for 9.0 and 10.0 because apparently
			# older versions of boost don't support only specifying the major number and
			# there was only one version of msvc with those majors.
			# Only specify the major for msvc-14 so that 14.1, 14.11, etc can be used.
			# Hopefully people building with msvc-14 are using a new enough version of boost
			# for this to work.
			if sys.version_info[0:2] in ((2, 6), (2, 7), (3, 0), (3, 1), (3, 2)):
				toolset = ' toolset=msvc-9.0'
			elif sys.version_info[0:2] in ((3, 3), (3, 4)):
				toolset = ' toolset=msvc-10.0'
			elif sys.version_info[0:2] in ((3, 5), (3, 6)):
				toolset = ' toolset=msvc-14'
			else:
				# unknown python version, lets hope the user has the right version of msvc configured
				toolset = ' toolset=msvc'

		parallel_builds = ' -j%d' % multiprocessing.cpu_count()
		if sys.maxsize > 2**32:
			address_model = ' address-model=64'
		else:
			address_model = ' address-model=32'
		# add extra quoting around the path to prevent bjam from parsing it as a list
		# if the path has spaces
		os.environ['LIBTORRENT_PYTHON_INTERPRETER'] = '"' + sys.executable + '"'

		# build libtorrent using bjam and build the installer with distutils
		cmdline = 'b2 libtorrent-link=static boost-link=static release optimization=space stage_module --abbreviate-paths' + address_model + toolset + parallel_builds
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
	cfg_vars = get_config_vars()
	for key, value in cfg_vars.items():
		if isinstance(value, str):
			cfg_vars[key] = value.replace('-Wstrict-prototypes', '')

	source_list = os.listdir(os.path.join(os.path.dirname(__file__), "src"))
	source_list = [os.path.abspath(os.path.join(os.path.dirname(__file__), "src", s)) for s in source_list if s.endswith(".cpp")]

	if extra_cmd:
		flags = flags_parser()
		# ldflags must be parsed first to ensure the correct library search path order
		extra_link = flags.parse(ldflags)
		extra_compile = flags.parse(extra_cmd)

		# for some reason distutils uses the CC environment variable to determine
		# the compiler to use for C++
		if 'CXX' in os.environ:
			os.environ['CC'] = os.environ['CXX']
		if 'CXXFLAGS' in os.environ:
			os.environ['CFLAGS'] = os.environ['CXXFLAGS']

		ext = [Extension('libtorrent',
			sources = sorted(source_list),
			language='c++',
			include_dirs = flags.include_dirs,
			library_dirs = flags.library_dirs,
			extra_link_args = extra_link + arch(),
			extra_compile_args = extra_compile + arch() + target_specific(),
			libraries = ['torrent-rasterbar'] + flags.libraries)]

setup(name = 'python-libtorrent',
	version = '1.1.7',
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
