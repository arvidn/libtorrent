#!/usr/bin/env python3

from distutils import log
import distutils.debug
import distutils.sysconfig
import os
import pathlib
import sys
import sysconfig
import tempfile
import subprocess

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


def b2_escape(value):
    value = value.replace("\\", "\\\\")
    value = value.replace('"', '\\"')
    return f'"{value}"'


def write_b2_python_config(config):
    write = config.write
    # b2 keys python environments by X.Y version, breaking ties by matching
    # a property list, called the "condition" of the environment. To ensure
    # b2 always picks the environment we define here, we define a special
    # feature for the condition and include that in the build request.

    # Note that we might try to reuse a property we know will be set, like
    # <define>TORRENT_FOO. But python.jam actually modifies the build request
    # in this case, so that <define>TORRENT_FOO becomes something like
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

    write("using python")
    write(f" : {sysconfig.get_python_version()}")
    write(f" : {b2_escape(sys.executable)}")
    write(" : ")
    write(" ".join(b2_escape(path) for path in includes))
    write(" :")  # libraries
    write(" : <libtorrent-python>on")

    # Note that all else being equal, we'd like to exactly control the output
    # filename, so distutils can find it. However:
    # 1. We can only control part of the filename; the prefix is controlled by
    #    our Jamfile and the final suffix is controlled by python.jam.
    # 2. Debian patched python.jam to disregard the configured ext_suffix
    #    anyway; they always override it with the same sysconfig var we use,
    #    found by invoking the executable.

    # So instead of applying an arbitrary name, we just try to guarantee that
    # b2 produces a name that distutils would expect, on all platforms. In
    # other words we apply debian's override everywhere, and hope no other
    # overrides ever disagree with us.

    # Note that sysconfig and distutils.sysconfig disagree here, especially on
    # windows.
    ext_suffix = distutils.sysconfig.get_config_var("EXT_SUFFIX")

    # python.jam appends the platform-specific final suffix on its own. I can't
    # find a consistent value from sysconfig or distutils.sysconfig for this.
    for plat_suffix in (".pyd", ".dll", ".so", ".sl"):
        if ext_suffix.endswith(plat_suffix):
            ext_suffix = ext_suffix[: -len(plat_suffix)]
            break
    write(f" : {b2_escape(ext_suffix)}")
    write(" ;\n")


BuildExtBase = _build_ext_lib.build_ext


class LibtorrentBuildExt(BuildExtBase):

    user_options = BuildExtBase.user_options + [
        (
            "libtorrent-link=",
            None,
            "how to link to libtorrent ('static', 'shared' or 'prebuilt')",
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

        if os.name == "nt":
            self.libtorrent_link = "static"
        else:
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
        args = []

        if os.name == "nt":
            self.toolset = get_msvc_toolset()
            self.boost_link = "static"
            args.append('--abbreviate-paths')

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

        # Jamfile hacks to ensure we select the python environment defined in
        # our project-config.jam
        args.append("libtorrent-python=on")
        args.append(f"python={sysconfig.get_python_version()}")

        # Our goal is to produce an artifact at this path. If we do this, the
        # distutils build system will skip trying to build it.
        target = pathlib.Path(self.get_ext_fullpath("libtorrent")).absolute()
        self.announce(f"target: {target}")

        # b2 doesn't provide a way to signal the name or paths of its outputs.
        # We try to convince python.jam to name its output file like our target
        # and copy it to our target directory. See comments in
        # write_b2_python_config for limitations on controlling the filename.

        # Jamfile hack to copy the module to our target directory
        args.append(f"python-install-path={target.parent}")
        args.append("install_module")

        # We use a "project-config.jam" to instantiate a python environment
        # to exactly match the running one.
        python_binding_dir = pathlib.Path(__file__).parent.absolute()
        config = tempfile.NamedTemporaryFile(mode="w+", delete=False)
        try:
            write_b2_python_config(config)
            config.seek(0)
            log.info("project-config.jam contents:")
            log.info(config.read())
            config.close()
            args.append(f"--project-config={config.name}")

            log.info(" ".join(["b2"] + args))
            subprocess.run(["b2"] + args, cwd=python_binding_dir, check=True)
        finally:
            # If we errored while writing config, windows may complain about
            # unlinking a file "in use"
            config.close()
            os.unlink(config.name)


setuptools.setup(
    name="python-libtorrent",
    version="2.0.2",
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
