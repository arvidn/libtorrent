#!/usr/bin/env python3


from distutils.core import setup, Extension
import os
import platform
import sys
import shutil
import multiprocessing
import glob


def bjam_build():
    toolset = ''
    file_ext = '.so'

    if platform.system() == 'Windows':
        file_ext = '.pyd'
        # https://packaging.python.org/guides/packaging-binary-extensions/#binary-extensions-for-windows
        #
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
            toolset = ' toolset=msvc-14.2'  # libtorrent requires VS 2017 or newer
        else:
            # unknown python version, lets hope the user has the right version of msvc configured
            toolset = ' toolset=msvc'

        # on windows, just link all the dependencies together to keep it simple
        toolset += ' libtorrent-link=static boost-link=static'

    parallel_builds = ' -j%d' % multiprocessing.cpu_count()
    if sys.maxsize > 2**32:
        address_model = ' address-model=64'
    else:
        address_model = ' address-model=32'

    # add extra quoting around the path to prevent bjam from parsing it as a list
    # if the path has spaces
    os.environ['LIBTORRENT_PYTHON_INTERPRETER'] = '"' + sys.executable + '"'

    # build libtorrent using bjam and build the installer with distutils
    cmdline = ('b2 release optimization=space stage_module --hash' +
               address_model + toolset + parallel_builds)
    print(cmdline)
    if os.system(cmdline) != 0:
        print('build failed')
        sys.exit(1)

    try:
        os.mkdir('build')
    except FileExistsError:
        pass
    try:
        shutil.rmtree('build/lib')
    except FileNotFoundError:
        pass
    try:
        os.mkdir('build/lib')
    except FileExistsError:
        pass
    try:
        os.mkdir('libtorrent')
    except FileExistsError:
        pass
    for f in glob.glob('libtorrent*.' + file_ext):
        shutil.copyfile(f, 'build/lib/libtorrent' + file_ext)

    return None


def distutils_build():

    src_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "src"))
    source_list = [os.path.join(src_dir, s) for s in os.listdir(src_dir) if s.endswith(".cpp")]

    ext = [Extension(
        'libtorrent',
        sources=sorted(source_list),
        language='c++',
        include_dirs=['../../include'],
        library_dirs=[],
        libraries=['torrent-rasterbar'],
        extra_compile_args=['-DTORRENT_USE_OPENSSL', '-DTORRENT_USE_LIBCRYPTO',
                            '-DBOOST_ASIO_HAS_STD_CHRONO=1 -DBOOST_EXCEPTION_DISABLE',
                            '-DBOOST_ASIO_ENABLE_CANCELIO', '-DTORRENT_LINKING_SHARED',
                            '-DTORRENT_BUILDING_LIBRARY']
        )
    ]

    return ext


ext = None

if '--help' not in sys.argv \
        and '--help-commands' not in sys.argv:
    ext = bjam_build()
#    ext = distutils_build()

setup(
    name='python-libtorrent',
    version='2.0.1',
    author='Arvid Norberg',
    author_email='arvid@libtorrent.org',
    description='Python bindings for libtorrent-rasterbar',
    long_description='Python bindings for libtorrent-rasterbar',
    url='http://libtorrent.org',
    platforms=[platform.system() + '-' + platform.machine()],
    license='BSD',
    packages=['libtorrent'],
    ext_modules=ext
)
