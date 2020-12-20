#!/usr/bin/env python3

import distutils.debug
import os
import pathlib
import platform
import sys
import sysconfig
import tempfile
import multiprocessing

import setuptools
import setuptools.command.build_ext as _build_ext_lib


def get_msvc_toolset():
    # Reference: https://wiki.python.org/moin/WindowsCompilers
    major_minor = sys.version_info[0:2]
    if major_minor in ((2, 6), (2, 7), (3, 0), (3, 1), (3, 2)):
        return "msvc-9.0"
    if major_minor in ((3, 3), (3, 4)):
        return "msvc-10.0"
    if major_minor in ((3, 5), (3, 6)):
        return "msvc-14.1"  # libtorrent requires VS 2017 or newer
    # unknown python version
    return "msvc"


def b2_bool(value):
    if value:
        return "on"
    return "off"


# Frustratingly, the "bdist_*" unconditionally (re-)run "build" without
# args, even ignoring "build_*" earlier on the same command line. This
# means "build_*" must be a no-op if some build output exists, even if that
# output might have been generated with different args (like
# "--define=FOO"). b2 does not know how to be "naively idempotent" like
# this; it will only generate outputs that exactly match the build request.
#
# It doesn't work to short-circuit initialize_options() / finalize_options(),
# as this doesn't play well with the way options are externally manipulated by
# distutils.
#
# It DOES work to short-circuit Distribution.reinitialize_command(), so we do
# that here.


class B2Distribution(setuptools.Distribution):
    def reinitialize_command(self, command, reinit_subcommands=0):
        if command == "build_ext":
            return self.get_command_obj("build_ext")
        return super().reinitialize_command(
            command, reinit_subcommands=reinit_subcommands
        )


# Various setuptools logic expects us to provide Extension instances for each
# extension in the distro.
class StubExtension(setuptools.Extension):
    def __init__(self, name):
        # An empty sources list ensures the base build_ext command won't build
        # anything
        super().__init__(name, sources=[])


def escape(string):
    return '"' + string.replace('\\', '\\\\') + '"'


def write_b2_python_config(target, config):
    write = config.write
    # b2 normally keys python environments by X.Y version, but since we may
    # have a duplicates, we also key on a special property. It's always-on, so
    # any python versions configured with this as a condition will always be
    # picked.
    # Note that python.jam actually modifies the condition for the build
    # request, so that <define>TORRENT_FOO becomes something like
    # <define>TORRENT_FOO,<python>3.7,<target-os>linux:... which causes chaos.
    # We should always define a custom feature for the condition.
    write("import feature ;\n")
    write("feature.feature libtorrent-python : on ;\n")

    # python.jam tries to determine correct include and library paths. Per
    # https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=691378 , include
    # detection is broken, but debian's fix is also broken (invokes a global
    # pythonX.Y instead of the passed interpreter)
    paths = sysconfig.get_paths()
    includes = [paths["include"], paths["platinclude"]]

    # Note that on debian, the extension suffix is overwritten, but it's
    # necessary everywhere else, or else b2 will just build "libtorrent.so".
    # python.jam appends SHLIB_SUFFIX on its own.
    target = target.name.split("libtorrent")[1]
    if os.name == "nt":
        suffix = sysconfig.get_config_var("EXT_SUFFIX")
    else:
        suffix = sysconfig.get_config_var("SHLIB_SUFFIX")
    if suffix != None and target.endswith(suffix):
        target = target[:-len(suffix)]

    using = f'using python : {sysconfig.get_python_version()} : {escape(sys.executable)} : {" ".join(escape(path) for path in includes)} : : <libtorrent-python>on : "{target}" ;\n'
    print(using)
    write(using)

BuildExtBase = _build_ext_lib.build_ext


class LibtorrentBuildExt(BuildExtBase):

    user_options = BuildExtBase.user_options + [
        (
            "libtorrent-link=",
            None,
            "how to link to libtorrent ('static' or 'shared')",
        ),
        (
            "boost-link=",
            None,
            "how to link to boost-python ('static' or 'shared')",
        ),
        ("toolset=", None, "b2 toolset"),
        ("pic", None, "whether to compile with -fPIC"),
        ("optimization=", None, "b2 optimization mode"),
        (
            "hash",
            None,
            "use a property hash for the build directory, rather than "
            "property subdirectories",
        ),
        ("cxxstd=", None, "boost cxxstd value (14, 17, 20, etc.)"),
    ]

    boolean_options = BuildExtBase.boolean_options + ["pic", "hash"]

    def initialize_options(self):
        self.libtorrent_link = None
        self.boost_link = None
        self.toolset = None
        self.pic = None
        self.optimization = None
        self.hash = None
        self.cxxstd = None
        return super().initialize_options()

    def run(self):
        # The current jamfile layout just supports one extension
        self.build_extension_with_b2()
        return super().run()

    def build_extension_with_b2(self):
        if os.name == "nt":
            self.toolset = get_msvc_toolset()
            self.libtorrent_link = "static"
            self.boost_link = "static"

        args = []

        if distutils.debug.DEBUG:
            args.append("--debug-configuration")
            args.append("--debug-building")
            args.append("--debug-generators")

        variant = "debug" if self.debug else "release"
        args.append(variant)
        bits = 64 if sys.maxsize > 2 ** 32 else 32
        args.append(f"address-model={bits}")

        if self.parallel:
            args.append(f"-j{self.parallel}")
        else:
            args.append(f"-j{multiprocessing.cpu_count()}")
        if self.libtorrent_link:
            args.append(f"libtorrent-link={self.libtorrent_link}")
        if self.boost_link:
            args.append(f"boost-link={self.boost_link}")
        if self.pic:
            args.append(f"libtorrent-python-pic={b2_bool(self.pic)}")
        if self.optimization:
            args.append(f"optimization={self.optimization}")
        if self.hash:
            args.append("--hash")
        if self.cxxstd:
            args.append(f"cxxstd={self.cxxstd}")

        # Jamfile hack to copy the module to our target directory
        target = pathlib.Path(self.get_ext_fullpath("libtorrent"))
        args.append(f"python-install-path={target.parent}")
        args.append("libtorrent-python=on")
        args.append("install_module")

        # We use a "project-config.jam" to instantiate a python environment
        # to exactly match the running one.
        config = tempfile.NamedTemporaryFile(mode="w+", delete=False)
        try:
            write_b2_python_config(target, config)
            config.seek(0)
            self.announce("project-config.jam contents:")
            self.announce(config.read())
            args.append(f"python={sysconfig.get_python_version()}")
            args.append(f"--project-config={config.name}")
            self.spawn(["b2"] + args)
        finally:
            config.close()
            os.unlink(config.name)


setuptools.setup(
    name="python-libtorrent",
    version="2.0.1",
    author="Arvid Norberg",
    author_email="arvid@libtorrent.org",
    description="Python bindings for libtorrent-rasterbar",
    long_description="Python bindings for libtorrent-rasterbar",
    url="http://libtorrent.org",
    license="BSD",
    ext_modules=[StubExtension("libtorrent")],
    cmdclass={
        "build_ext": LibtorrentBuildExt,
    },
    distclass=B2Distribution,
)
