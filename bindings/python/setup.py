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
import contextlib
import warnings
import re
import shlex

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

    CONFIG_MODE_DISTUTILS = "distutils"
    CONFIG_MODE_B2 = "b2"
    CONFIG_MODES = (CONFIG_MODE_DISTUTILS, CONFIG_MODE_B2)

    user_options = BuildExtBase.user_options + [
        (
            "config-mode=",
            None,
            "'b2' or 'distutils' (default). "
            "In b2 mode, setup.py will just invoke b2 using --b2-args. "
            "It will not attempt to auto-configure b2 or override any "
            "args. "
            "In distutils mode, setup.py will attempt to configure "
            "and invoke b2 to match the expectations and behavior of "
            "distutils (libtorrent will be built against the invoking "
            "python, etc; note not all behaviors are currently supported). "
            "The feature set will match the version found on pypi. "
            "You can selectively override the auto-configuration in "
            "this mode with --no-autoconf or --b2-args. For example "
            "--b2-args=python=x.y or --no-autoconf=python will prevent "
            "python from being auto-configured. "
            "Note that --b2-args doesn't currently understand implicit features. "
            "Be sure to include their names, e.g. --b2-args=variant=debug",
        ),
        (
            "b2-args=",
            None,
            "The full argument string to pass to b2. This is parsed with shlex, to "
            "support arguments with spaces. For example: --b2-args 'variant=debug "
            '"my-feature=a value with spaces"\'',
        ),
        (
            "no-autoconf=",
            None,
            "Space-separated list of b2 arguments that should not be "
            "auto-configured in distutils mode.",
        ),
        (
            "libtorrent-link=",
            None,
            "(DEPRECATED; use --b2-args=libtorrent-link=...) ",
        ),
        (
            "boost-link=",
            None,
            "(DEPRECATED; use --b2-args=boost-link=...) "
        ),
        ("toolset=", None, "(DEPRECATED; use --b2-args=toolset=...) b2 toolset"),
        (
            "pic",
            None,
            "(DEPRECATED; use --b2-args=libtorrent-python-pic=on) "
            "whether to compile with -fPIC",
        ),
        (
            "optimization=",
            None,
            "(DEPRECATED; use --b2-args=optimization=...) " "b2 optimization mode",
        ),
        (
            "hash",
            None,
            "(DEPRECATED; use --b2-args=--hash) "
            "use a property hash for the build directory, rather than "
            "property subdirectories",
        ),
        (
            "cxxstd=",
            None,
            "(DEPRECATED; use --b2-args=cxxstd=...) "
            "boost cxxstd value (14, 17, 20, etc.)",
        ),
    ]

    boolean_options = BuildExtBase.boolean_options + ["pic", "hash"]

    def initialize_options(self):

        self.config_mode = self.CONFIG_MODE_DISTUTILS
        self.b2_args = ""
        self.no_autoconf = ""

        self.cxxflags = None
        self.linkflags = None

        # TODO: this is for backwards compatibility
        # loading these files will be removed in libtorrent-2.0
        try:
            with open('compile_flags') as f:
                opts = f.read()
                if '-std=c++' in opts:
                    self.cxxflags = ['-std=c++' + opts.split('-std=c++')[-1].split()[0]]
        except OSError:
            pass

        # TODO: this is for backwards compatibility
        # loading these files will be removed in libtorrent-2.0
        try:
            with open('link_flags') as f:
                opts = f.read().split(' ')
                opts = [x for x in opts if x.startswith('-L')]
                if len(opts):
                    self.linkflags = opts
        except OSError:
            pass

        if os.name == "nt":
            self.toolset = get_msvc_toolset()
        else:
            self.toolset = None
        self.libtorrent_link = None
        self.boost_link = None
        self.pic = None
        self.optimization = None
        self.hash = None
        self.cxxstd = None

        self._b2_args_split = []
        self._b2_args_configured = set()

        return super().initialize_options()

    def finalize_options(self):
        super().finalize_options()

        if self.config_mode not in self.CONFIG_MODES:
            raise distutils.errors.DistutilsOptionError(
                f"--config-mode must be one of {self.CONFIG_MODES}"
            )

        # shlex the args here to warn early on bad config
        self._b2_args_split = shlex.split(self.b2_args or "")
        self._b2_args_configured.update(shlex.split(self.no_autoconf or ""))

        # In b2's arg system only single-character args can consume the next
        # arg, but it may also be concatenated. So we may have "-x",
        # "-x value", or "-xvalue". All --long args which take a value must
        # appear as "--long=value"
        i = 0
        while i < len(self._b2_args_split):
            arg = self._b2_args_split[i]
            m = re.match(r"(-[dfjlmopst])(.*)", arg)
            if m:
                name = m.group(1)
                # An arg that takes a value but wasn't concatenated. Treat the
                # next option as the value
                if not m.group(2):
                    i += 1
            else:
                name = arg.split("=", 1)[0]
            self._b2_args_configured.add(name)
            i += 1

        # Add deprecated args
        if self.libtorrent_link:
            warnings.warn(
                "--libtorrent-link is deprecated; use --b2-args=libtorrent-link=..."
            )
            self._maybe_add_arg(f"libtorrent-link={self.libtorrent_link}")
            self._b2_args_configured.add("libtorrent-link")
        if self.boost_link:
            warnings.warn("--boost-link is deprecated; use --b2-args=boost-link=...")
            self._maybe_add_arg(f"boost-link={self.boost_link}")
            self._b2_args_configured.add("boost-link")
        if self.toolset:
            warnings.warn("--toolset is deprecated; use --b2-args=toolset=...")
            self._maybe_add_arg(f"toolset={self.toolset}")
            self._b2_args_configured.add("toolset")
        if self.pic:
            warnings.warn("--pic is deprecated; use --b2-args=libtorrent-python-pic=on")
            self._maybe_add_arg("libtorrent-python-pic=on")
            self._b2_args_configured.add("libtorrent-python-pic")
        if self.optimization:
            warnings.warn(
                "--optimization is deprecated; use --b2-args=optimization=..."
            )
            self._maybe_add_arg(f"optimization={self.optimization}")
            self._b2_args_configured.add("optimization")
        if self.hash:
            warnings.warn("--hash is deprecated; use --b2-args=--hash")
            self._maybe_add_arg("--hash")
        if self.cxxstd:
            warnings.warn("--cxxstd is deprecated; use --b2-args=cxxstd=...")
            self._maybe_add_arg(f"cxxstd={self.cxxstd}")
            self._b2_args_configured.add("cxxstd")

    def _should_add_arg(self, arg):
        m = re.match(r"(-\w).*", arg)
        if m:
            name = m.group(1)
        else:
            name = arg.split("=", 1)[0]
        return name not in self._b2_args_configured

    def _maybe_add_arg(self, arg):
        if self._should_add_arg(arg):
            self._b2_args_split.append(arg)
            return True
        return False

    def run(self):
        # The current jamfile layout just supports one extension
        self._build_extension_with_b2()
        return super().run()

    def _build_extension_with_b2(self):
        python_binding_dir = pathlib.Path(__file__).parent.absolute()
        with self._configure_b2():
            if self.linkflags:
                for lf in self.linkflags:
                    # since b2 may be running with a different directory as cwd,
                    # relative
                    # paths need to be converted to absolute
                    if lf[2] != '/':
                        lf = '-L' + str(pathlib.Path(lf[2:]).absolute())
                    self._b2_args_split.append("linkflags=" + lf)
            if self.cxxflags:
                for f in self.cxxflags:
                    self._b2_args_split.append("cxxflags=" + f)
            command = ["b2"] + self._b2_args_split
            log.info(" ".join(command))
            subprocess.run(command, cwd=python_binding_dir, check=True)

    @contextlib.contextmanager
    def _configure_b2(self):
        if self.config_mode == self.CONFIG_MODE_DISTUTILS:
            # If we're using distutils mode, we'll auto-configure a lot of args
            # and write temporary config.
            yield from self._configure_b2_with_distutils()
        else:
            # If we're using b2 mode, no configuration needed
            yield

    def _configure_b2_with_distutils(self):
        if os.name == "nt":
            self._maybe_add_arg("--abbreviate-paths")
            self._maybe_add_arg("boost-link=static")
        else:
            self._maybe_add_arg("boost-link=shared")

        self._maybe_add_arg("libtorrent-link=static")

        if distutils.debug.DEBUG:
            self._maybe_add_arg("--debug-configuration")
            self._maybe_add_arg("--debug-building")
            self._maybe_add_arg("--debug-generators")

        # Default feature configuration
        self._maybe_add_arg("deprecated-functions=on")

        variant = "debug" if self.debug else "release"
        self._maybe_add_arg(f"variant={variant}")
        bits = 64 if sys.maxsize > 2 ** 32 else 32
        self._maybe_add_arg(f"address-model={bits}")

        if self.parallel:
            self._maybe_add_arg(f"-j{self.parallel}")

        # We use a "project-config.jam" to instantiate a python environment
        # to exactly match the running one.
        override_project_config = False
        if self._should_add_arg("--project-config"):
            if self._maybe_add_arg(f"python={sysconfig.get_python_version()}"):

                override_project_config = True

                # Jamfile hacks to ensure we select the python environment defined in
                # our project-config.jam
                self._maybe_add_arg("libtorrent-python=on")

        # Our goal is to produce an artifact at this path. If we do this, the
        # distutils build system will skip trying to build it.
        target = pathlib.Path(self.get_ext_fullpath("libtorrent")).absolute()
        self.announce(f"target: {target}")

        # b2 doesn't provide a way to signal the name or paths of its outputs.
        # We try to convince python.jam to name its output file like our target
        # and copy it to our target directory. See comments in
        # write_b2_python_config for limitations on controlling the filename.

        # Jamfile hack to copy the module to our target directory
        self._maybe_add_arg(f"python-install-path={target.parent}")
        self._maybe_add_arg("install_module")

        # We use a "project-config.jam" to instantiate a python environment
        # to exactly match the running one.
        if override_project_config:
            config = tempfile.NamedTemporaryFile(mode="w+", delete=False)
            try:
                write_b2_python_config(config)
                config.seek(0)
                log.info("project-config.jam contents:")
                log.info(config.read())
                config.close()
                self._b2_args_split.append(f"--project-config={config.name}")
                yield
            finally:
                # If we errored while writing config, windows may complain about
                # unlinking a file "in use"
                config.close()
                os.unlink(config.name)
        else:
            yield


setuptools.setup(
    name="python-libtorrent",
    version="2.0.3",
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
