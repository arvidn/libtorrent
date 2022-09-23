#!/usr/bin/env python3

import contextlib
from distutils import log
import distutils.cmd
import distutils.command.install_data as install_data_lib
import distutils.debug
import distutils.errors
import distutils.sysconfig
import functools
import itertools
import os
import pathlib
import re
import shlex
import subprocess
import sys
import sysconfig
import tempfile
from typing import Callable
from typing import cast
from typing import IO
from typing import Iterator
from typing import List
from typing import Optional
from typing import Sequence
from typing import Set
from typing import Tuple
import warnings

import setuptools
import setuptools.command.build_ext as build_ext_lib


def b2_bool(value: bool) -> str:
    if value:
        return "on"
    return "off"


def b2_version() -> Tuple[int, ...]:
    # NB: b2 --version returns exit status 1
    proc = subprocess.run(
        ["b2", "--version"], stdout=subprocess.PIPE, universal_newlines=True
    )
    # Expected output examples:
    #   Boost.Build 2015.07-git
    #   B2 4.3-git
    m = re.match(r".*\s([\d\.]+).*", proc.stdout)
    assert m is not None, f"{proc.stdout} doesn't match expected output"
    result = tuple(int(part) for part in re.split(r"\.", m.group(1)))
    # Boost 1.71 changed from YYYY.MM to version 4.0. Return an "epoch" as the first
    # part of the tuple to distinguish these version patterns.
    if result[0] > 1999:
        return (0, *result)
    else:
        return (1, *result)


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
    def reinitialize_command(
        self, command: str, reinit_subcommands: int = 0
    ) -> distutils.cmd.Command:
        if command == "build_ext":
            return cast(distutils.cmd.Command, self.get_command_obj("build_ext"))
        return cast(
            distutils.cmd.Command,
            super().reinitialize_command(
                command, reinit_subcommands=reinit_subcommands
            ),
        )


# Various setuptools logic expects us to provide Extension instances for each
# extension in the distro.
class StubExtension(setuptools.Extension):
    def __init__(self, name: str):
        # An empty sources list ensures the base build_ext command won't build
        # anything
        super().__init__(name, sources=[])


def b2_escape(value: str) -> str:
    value = value.replace("\\", "\\\\")
    value = value.replace('"', '\\"')
    return f'"{value}"'


def write_b2_python_config(
    include_dirs: Sequence[str], library_dirs: Sequence[str], config: IO[str]
) -> None:
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

    # python.jam's autodetection of library paths and include paths has various
    # bugs, and has very poor support of non-system python environments,
    # such as pyenv or virtualenvs. distutils' autodetection is much more
    # robust, and we trust it more. In case distutils gives empty results,
    # feed garbage values to boost to block its autodetection.

    write("using python")
    write(f" : {sysconfig.get_python_version()}")
    write(f" : {b2_escape(sys.executable)}")
    write(" : ")
    if include_dirs:
        write(" ".join(b2_escape(path) for path in include_dirs))
    else:
        write("__BLOCK_AUTODETECTION__")
    write(" : ")
    if library_dirs:
        # Note that python.jam only accepts one library dir! We depend on
        # passing other library dirs by other means. Not sure if we should
        # do something smarter here, like pass the first directory that exists.
        write(b2_escape(library_dirs[0]))
    else:
        write("__BLOCK_AUTODETECTION__")
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
    ext_suffix = str(ext_suffix or "")

    # python.jam appends the platform-specific final suffix on its own. I can't
    # find a consistent value from sysconfig or distutils.sysconfig for this.
    for plat_suffix in (".pyd", ".dll", ".so", ".sl"):
        if ext_suffix.endswith(plat_suffix):
            ext_suffix = ext_suffix[: -len(plat_suffix)]
            break
    write(f" : {b2_escape(ext_suffix)}")
    write(" ;\n")


PYTHON_BINDING_DIR = pathlib.Path(__file__).parent.absolute()


class LibtorrentBuildExt(build_ext_lib.build_ext):

    CONFIG_MODE_DISTUTILS = "distutils"
    CONFIG_MODE_B2 = "b2"
    CONFIG_MODES = (CONFIG_MODE_DISTUTILS, CONFIG_MODE_B2)

    user_options = build_ext_lib.build_ext.user_options + [
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
        ("boost-link=", None, "(DEPRECATED; use --b2-args=boost-link=...) "),
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
            "boost cxxstd value (14, 17, 20, etc.)",
        ),
        (
            "configure-from-autotools",
            None,
            "(DEPRECATED) "
            "when in --config-mode=distutils, also apply cxxflags= and linkflags= "
            "based on files generated from autotools",
        ),
    ]

    boolean_options = build_ext_lib.build_ext.boolean_options + ["pic", "hash"]

    def initialize_options(self) -> None:
        self.libtorrent_link: Optional[str] = None
        self.boost_link: Optional[str] = None
        self.toolset: Optional[str] = None
        self.pic: Optional[bool] = None
        self.optimization: Optional[str] = None
        self.hash: Optional[bool] = None
        self.cxxstd: Optional[str] = None
        self.configure_from_autotools: Optional[bool] = None

        self.config_mode = self.CONFIG_MODE_DISTUTILS
        self.b2_args = ""
        self.no_autoconf = ""

        self._b2_args_split: List[str] = []
        self._b2_args_configured: Set[str] = set()

        self._b2_version = b2_version()

        log.info("b2 version: %s", self._b2_version)

        super().initialize_options()

    def finalize_options(self) -> None:
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
            # the cxxstd feature was introduced in boost 1.66. However the output of
            # b2 --version is 2015.07 for both 1.65 and 1.66.
            if self._b2_version > (0, 2015, 7):
                self._maybe_add_arg(f"cxxstd={self.cxxstd}")
            else:
                warnings.warn(
                    f"--cxxstd supplied, but b2 is too old ({self._b2_version}). "
                )

    def _should_add_arg(self, arg: str) -> bool:
        m = re.match(r"(-\w).*", arg)
        if m:
            name = m.group(1)
        else:
            name = arg.split("=", 1)[0]
        return name not in self._b2_args_configured

    def _maybe_add_arg(self, arg: str) -> bool:
        if self._should_add_arg(arg):
            self._b2_args_split.append(arg)
            return True
        return False

    def run(self) -> None:
        # The current jamfile layout just supports one extension
        self._build_extension_with_b2()
        super().run()

    def _build_extension_with_b2(self) -> None:
        with self._configure_b2():
            command = ["b2"] + self._b2_args_split
            log.info(" ".join(command))
            subprocess.run(command, cwd=PYTHON_BINDING_DIR, check=True)
        # The jamfile only builds "libtorrent.so", but we want
        # "libtorrent/__init__.so"
        src = self.get_ext_fullpath("libtorrent")
        dst = self.get_ext_fullpath(self.extensions[0].name)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        log.info("rename %s -> %s", src, dst)
        os.rename(src, dst)

    @contextlib.contextmanager
    def _configure_b2(self) -> Iterator[None]:
        if self.config_mode == self.CONFIG_MODE_DISTUTILS:
            # If we're using distutils mode, we'll auto-configure a lot of args
            # and write temporary config.
            yield from self._configure_b2_with_distutils()
        else:
            # If we're using b2 mode, no configuration needed
            yield

    def _configure_b2_with_distutils(self) -> Iterator[None]:
        if os.name == "nt":
            self._maybe_add_arg("--abbreviate-paths")

        if distutils.debug.DEBUG:
            self._maybe_add_arg("--debug-configuration")
            self._maybe_add_arg("--debug-building")
            self._maybe_add_arg("--debug-generators")

        if sys.platform == "darwin":
            # boost.build defaults to toolset=clang on mac. However python.jam
            # on boost 1.77+ breaks with toolset=clang if using a framework-type
            # python installation, such as installed by homebrew.
            self._maybe_add_arg("toolset=darwin")

        if self.configure_from_autotools:
            self._configure_from_autotools()

        # Default feature configuration
        self._maybe_add_arg("deprecated-functions=on")
        self._maybe_add_arg("boost-link=static")
        self._maybe_add_arg("libtorrent-link=static")

        self._maybe_add_arg("crypto=openssl")

        variant = "debug" if self.debug else "release"
        self._maybe_add_arg(f"variant={variant}")
        bits = 64 if sys.maxsize > 2**32 else 32
        self._maybe_add_arg(f"address-model={bits}")

        # Cross-compiling logic: tricky, because autodetection is usually
        # better than our matching
        if sys.platform == "darwin":
            # macOS uses multi-arch binaries. Attempt to match the
            # configuration of the running python by translating distutils
            # platform modes to b2 architecture modes
            machine = distutils.util.get_platform().split("-")[-1]
            if machine == "arm64":
                self._maybe_add_arg("architecture=arm")
            elif machine in ("ppc", "ppc64"):
                self._maybe_add_arg("architecture=power")
            elif machine in ("i386", "x86_64", "intel"):
                self._maybe_add_arg("architecture=x86")
            elif machine in ("universal", "fat", "fat3", "fat64"):
                self._maybe_add_arg("architecture=combined")
            # NB: as of boost 1.75.0, b2 doesn't have a straightforward way to
            # build a "universal2" (arm64 + x86_64) binary

        if self.parallel:
            self._maybe_add_arg(f"-j{self.parallel}")

        # We use a "project-config.jam" to instantiate a python environment
        # to exactly match the running one.
        # Don't create project-config.jam if the user specified
        # --b2-args=--project-config=..., or has an existing project-config.jam.
        config_writers: List[Callable[[IO[str]], None]] = []
        if self._should_add_arg("--project-config") or self._find_project_config():
            if self._maybe_add_arg(f"python={sysconfig.get_python_version()}"):
                config_writers.append(
                    functools.partial(
                        write_b2_python_config, self.include_dirs, self.library_dirs
                    )
                )

                # Jamfile hacks to ensure we select the python environment defined in
                # our project-config.jam
                self._maybe_add_arg("libtorrent-python=on")

                # python.jam only allows ONE library dir! distutils may autodetect
                # multiple, and finding the "right" one isn't straightforward. We just
                # pass them all here and hopefully the right thing happens.
                for path in self.library_dirs:
                    self._b2_args_split.append(f"library-path={b2_escape(path)}")

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

        # Two paths depending on whether or not we use a generated
        # project-config.jam or not.
        if config_writers:
            # We prefer to use a temporary file, and pass it with --project-config=...
            # This option was introduced in boost 1.68. Otherwise, we just write to
            # project-config.jam in the bindings directory.
            if self._b2_version >= (0, 2018, 2):
                temp_config = tempfile.NamedTemporaryFile(mode="w+", delete=True)
                temp_config.close()
                config_path = pathlib.Path(temp_config.name)
                self._b2_args_split.append(f"--project-config={temp_config.name}")
            else:
                config_path = PYTHON_BINDING_DIR / "project-config.jam"
            try:
                with config_path.open(mode="w+") as config:
                    for writer in config_writers:
                        writer(config)
                    config.seek(0)
                    log.info("project-config.jam contents:")
                    log.info(config.read())
                yield
            finally:
                with contextlib.suppress(FileNotFoundError):
                    config_path.unlink()
        else:
            yield

    def _find_project_config(self) -> Optional[pathlib.Path]:
        for directory in itertools.chain(
            (PYTHON_BINDING_DIR,), PYTHON_BINDING_DIR.parents
        ):
            path = directory / "project-config.jam"
            if path.exists():
                return path
        return None

    def _configure_from_autotools(self) -> None:
        # This is a hack to allow building the python bindings from autotools

        compile_flags_path = PYTHON_BINDING_DIR / "compile_flags"
        log.info("configure_from_autotools: checking %s", compile_flags_path)
        with contextlib.suppress(FileNotFoundError):
            for arg in shlex.split(compile_flags_path.read_text()):
                if arg.startswith("-std=c++"):
                    self._maybe_add_arg(f"cxxflags={arg}")
                    log.info("configure_from_autotools: adding cxxflags=%s", arg)

        link_flags_path = PYTHON_BINDING_DIR / "link_flags"
        log.info("configure_from_autotools: checking %s", link_flags_path)
        with contextlib.suppress(FileNotFoundError):
            for arg in shlex.split(link_flags_path.read_text()):
                if arg.startswith("-L"):
                    linkpath = pathlib.Path(arg[2:])
                    linkpath = linkpath.resolve()
                    self._maybe_add_arg(f"linkflags=-L{linkpath}")
                    log.info(
                        "configure_from_autotools: adding linkflags=-L%s", linkpath
                    )


class InstallDataToLibDir(install_data_lib.install_data):
    def finalize_options(self) -> None:
        # install_data installs to the *base* directory, which is useless.
        # Nothing ever gets installed there, no tools search there. You could
        # only make use of it by manually picking the right install paths.
        # This instead defaults the "install_dir" option to be "install_lib",
        # which is "where packages are normally installed".
        self.set_undefined_options(
            "install",
            ("install_lib", "install_dir"),  # note "install_lib"
            ("root", "root"),
            ("force", "force"),
        )


def find_all_files(path: str) -> Iterator[str]:
    for dirpath, _, filenames in os.walk(path):
        for filename in filenames:
            yield os.path.join(dirpath, filename)


setuptools.setup(
    name="libtorrent",
    author="Arvid Norberg",
    author_email="arvid@libtorrent.org",
    description="Python bindings for libtorrent-rasterbar",
    long_description="Python bindings for libtorrent-rasterbar",
    url="http://libtorrent.org",
    license="BSD",
    ext_modules=[StubExtension("libtorrent.__init__")],
    cmdclass={
        "build_ext": LibtorrentBuildExt,
        "install_data": InstallDataToLibDir,
    },
    distclass=B2Distribution,
    data_files=[
        ("libtorrent", list(find_all_files("install_data"))),
    ],
)
