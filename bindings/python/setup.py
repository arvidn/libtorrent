#!/usr/bin/env python


from distutils.core import setup, Extension
from distutils.sysconfig import get_config_vars
import os
import platform
import sys
import shutil
import multiprocessing


class flags_parser:
    def __init__(self):
        self.include_dirs = []
        self.library_dirs = []
        self.libraries = []

    def parse(self, args):
        """Parse out the -I -L -l directives

        Returns:
            list: All other arguments
        """
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
    if platform.system() == 'Darwin':
        __, __, machine = platform.mac_ver()
        if machine.startswith('ppc'):
            return ['-arch', machine]
    return []


def target_specific():
    if platform.system() == 'Darwin':
        # On mavericks, clang will fail when unknown arguments are passed in.
        # python distutils will pass in arguments it doesn't know about.
        return ['-Wno-error=unused-command-line-argument-hard-error-in-future']
    return []


try:
    with open('compile_flags') as _file:
        extra_cmd = _file.read()
except Exception:
    extra_cmd = None

try:
    with open('link_flags') as _file:
        ldflags = _file.read()
except Exception:
    ldflags = None

# this is to pull out compiler arguments from the CXX flags set up by the
# configure script. Specifically, the -std=c++11 flag is added to CXX and here
# we pull out everything starting from the first flag (i.e. something starting
# with a '-'). The actual command to call the compiler may be more than one
# word, for instance "ccache g++".
try:
    with open('compile_cmd') as _file:
        cmd = _file.read().split(' ')
        while len(cmd) > 0 and not cmd[0].startswith('-'):
            cmd = cmd[1:]
        extra_cmd += ' '.join(cmd)
except Exception:
    pass

ext = None
packages = None

if '--bjam' in sys.argv:
    del sys.argv[sys.argv.index('--bjam')]

    if '--help' not in sys.argv \
            and '--help-commands' not in sys.argv:

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
        cmdline = ('b2 libtorrent-link=static boost-link=static release '
                   'optimization=space stage_module --abbreviate-paths' +
                   address_model + toolset + parallel_builds)
        print(cmdline)
        if os.system(cmdline) != 0:
            print('build failed')
            sys.exit(1)

        try:
            os.mkdir('build')
        except Exception:
            pass
        try:
            shutil.rmtree('build/lib')
        except Exception:
            pass
        try:
            os.mkdir('build/lib')
        except Exception:
            pass
        try:
            os.mkdir('libtorrent')
        except Exception:
            pass
        shutil.copyfile('libtorrent' + file_ext,
                        'build/lib/libtorrent' + file_ext)

    packages = ['libtorrent']

else:
    # Remove '-Wstrict-prototypes' compiler option, which isn't valid for C++.
    cfg_vars = get_config_vars()
    for key, value in list(cfg_vars.items()):
        if isinstance(value, str):
            cfg_vars[key] = value.replace('-Wstrict-prototypes', '')

    src_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "src"))
    source_list = [os.path.join(src_dir, s) for s in os.listdir(src_dir) if s.endswith(".cpp")]

    flags = flags_parser()
    ext_extra = {}

    if ldflags:
        # ldflags parsed first to ensure the correct library search path order
        ext_extra["extra_link_args"] = flags.parse(ldflags) + arch()

    if extra_cmd:
        ext_extra["extra_compile_args"] = flags.parse(extra_cmd) + arch() + target_specific()

    ext = [Extension(
        'libtorrent',
        sources=sorted(source_list),
        language='c++',
        include_dirs=flags.include_dirs,
        library_dirs=flags.library_dirs,
        libraries=['torrent-rasterbar'] + flags.libraries,
        **ext_extra)
    ]

setup(
    name='python-libtorrent',
    version='1.2.6',
    author='Arvid Norberg',
    author_email='arvid@libtorrent.org',
    description='Python bindings for libtorrent-rasterbar',
    long_description='Python bindings for libtorrent-rasterbar',
    url='http://libtorrent.org',
    platforms=[platform.system() + '-' + platform.machine()],
    license='BSD',
    packages=packages,
    ext_modules=ext
)
