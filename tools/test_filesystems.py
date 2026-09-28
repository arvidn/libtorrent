#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

"""
Test libtorrent disk I/O on various Linux filesystems.

Creates a disk image per filesystem, formats it once, then mounts it and runs
test_filesystem with cwd set to the mount point. Images are preserved across
runs to avoid repeated mkfs overhead. tools/disk_io_stress_test is also run
on each mounted filesystem by default (disable with --no-stress-test); by
default this runs disk_io_stress_test's bounded "smoke" suite, since its full
default suite can write several GiB per test case (use --stress-test-args and
--image-size-mb to run something bigger).

Requires sudo access for losetup, mount, umount, chmod, and zpool.
"""

from argparse import ArgumentParser
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Optional
from typing import Protocol

SCRIPT_DIR = Path(__file__).parent.resolve()
REPO_ROOT = SCRIPT_DIR.parent
TEST_DIR = REPO_ROOT / "test"
TOOLS_DIR = REPO_ROOT / "tools"
DEFAULT_IMAGE_DIR = SCRIPT_DIR / "fs_images"
MOUNT_BASE = Path("/tmp/lt_fs_test")
# disk_io_stress_test's "smoke" suite only writes on the order of 10s of MiB,
# but leave some headroom over test_filesystem's own usage.
IMAGE_SIZE_MB = 512


def header(msg: str) -> None:
    print(f"\n{'='*5} {msg}\n")


def run(
    cmd: list[str], cwd: Optional[Path] = None
) -> subprocess.CompletedProcess[bytes]:
    print(f"  $ {' '.join(cmd)}")
    return subprocess.run(cmd, check=True, cwd=cwd)


def run_sudo(cmd: list[str]) -> subprocess.CompletedProcess[bytes]:
    return run(["sudo"] + cmd)


def attach_loop(image: Path) -> str:
    cmd = ["sudo", "losetup", "--find", "--show", str(image)]
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(
        cmd,
        check=True,
        capture_output=True,
        text=True,
    )
    loop_dev = result.stdout.strip()
    # Wait for udev to finish processing the new loop device before we mount
    # it. Without this, udisks2 may still be probing the device when mount(8)
    # tries to open it, causing mount to block indefinitely in kernel.
    run_sudo(["udevadm", "settle"])
    return loop_dev


def detach_loop(loop_dev: str) -> None:
    run_sudo(["losetup", "-d", loop_dev])


class Filesystem(Protocol):
    name: str

    def available(self) -> bool: ...
    def format(self, image: Path) -> None: ...
    def mount(self, image: Path, mount_point: Path) -> None: ...
    def umount(self, mount_point: Path) -> None: ...


class LoopFilesystem:
    """Filesystem that uses a loop device with mkfs + mount."""

    def __init__(
        self, name: str, mkfs_cmd: list[str], mount_opts: Optional[list[str]] = None
    ) -> None:
        self.name = name
        self.mkfs_cmd = mkfs_cmd
        self.mount_opts = mount_opts or []
        self._loop_dev: Optional[str] = None

    def available(self) -> bool:
        return shutil.which(self.mkfs_cmd[0]) is not None

    def format(self, image: Path) -> None:
        loop_dev = attach_loop(image)
        try:
            run_sudo(self.mkfs_cmd + [loop_dev])
        finally:
            detach_loop(loop_dev)

    def mount(self, image: Path, mount_point: Path) -> None:
        loop_dev = attach_loop(image)
        self._loop_dev = loop_dev
        opts = ["-o", ",".join(self.mount_opts)] if self.mount_opts else []
        try:
            run_sudo(["mount"] + opts + [loop_dev, str(mount_point)])
        except Exception:
            detach_loop(loop_dev)
            self._loop_dev = None
            raise

    def umount(self, mount_point: Path) -> None:
        try:
            run_sudo(["umount", str(mount_point)])
        finally:
            if self._loop_dev is not None:
                detach_loop(self._loop_dev)
                self._loop_dev = None


class ZfsFilesystem:
    """ZFS: zpool create both formats and mounts; zpool destroy unmounts."""

    POOL_NAME = "lt_test_zfs"
    name = "zfs"

    def available(self) -> bool:
        return shutil.which("zpool") is not None

    def format(self, image: Path) -> None:
        pass  # zpool create handles formatting and mounting together

    def mount(self, image: Path, mount_point: Path) -> None:
        # Destroy any stale pool left by a previous crashed run
        cmd = ["sudo", "zpool", "destroy", self.POOL_NAME]
        print(f"  $ {' '.join(cmd)}")
        subprocess.run(cmd, capture_output=True)
        run_sudo(
            [
                "zpool",
                "create",
                "-f",
                "-m",
                str(mount_point),
                self.POOL_NAME,
                str(image),
            ]
        )

    def umount(self, mount_point: Path) -> None:
        run_sudo(["zpool", "destroy", self.POOL_NAME])


FILESYSTEMS: list[Filesystem] = [
    LoopFilesystem("ext4", ["mkfs.ext4", "-F"]),  # apt install e2fsprogs
    LoopFilesystem("btrfs", ["mkfs.btrfs", "-f"]),  # apt install btrfs-progs
    # TODO: xfs keeps hanging in the kernel
    # LoopFilesystem("xfs", ["mkfs.xfs", "-f"]),  # apt install xfsprogs
    LoopFilesystem("f2fs", ["mkfs.f2fs", "-f"]),  # apt install f2fs-tools
    LoopFilesystem("jfs", ["mkfs.jfs", "-q"]),  # apt install jfsutils
    # nilfs2 is log-structured: writes never happen in place, so obsoleted
    # blocks are only reclaimed by nilfs_cleanerd, which by default won't
    # touch anything younger than a 1 hour protection period. pp=0 lets it
    # reclaim garbage immediately, which the stress test needs to avoid
    # running out of space well before the image is actually full.
    LoopFilesystem(
        "nilfs2", ["mkfs.nilfs2", "-f"], ["pp=0"]
    ),  # apt install nilfs-tools
    # LoopFilesystem("bcachefs", ["mkfs.bcachefs", "-f"]),  # apt install bcachefs-tools
    ZfsFilesystem(),  # apt install zfsutils-linux
]


def build_binary(target: str, source_dir: Path) -> Path:
    header(f"Building {target} (link=static)")
    run(["b2", "link=static", f"stage_{target}"], cwd=source_dir)
    binary = source_dir / target
    if not binary.exists():
        sys.exit(f"ERROR: build succeeded but {binary} not found")
    print(f"  Binary: {binary}")
    return binary


def test_filesystem(
    fs: Filesystem,
    binary: Path,
    image_dir: Path,
    reformat: bool,
    image_size_mb: int = IMAGE_SIZE_MB,
    stress_binary: Optional[Path] = None,
    stress_args: Optional[list[str]] = None,
) -> Optional[bool]:
    """Returns True=pass, False=fail, None=skipped."""
    if not fs.available():
        print(f"[{fs.name}] SKIP  (required tool not found)")
        return None

    header(f"Filesystem: {fs.name}")
    image = image_dir / f"{fs.name}.img"
    mount_point = MOUNT_BASE / fs.name

    if reformat and image.exists():
        print(f"  Removing existing image for reformat: {image}")
        image.unlink()

    if not image.exists():
        print(f"  Creating {image_size_mb} MiB image: {image}")
        image.parent.mkdir(parents=True, exist_ok=True)
        if shutil.which("fallocate"):
            run(["fallocate", "-l", f"{image_size_mb}M", str(image)])
        else:
            run(
                [
                    "dd",
                    "if=/dev/zero",
                    f"of={image}",
                    "bs=1M",
                    f"count={image_size_mb}",
                    "status=none",
                ]
            )
        print(f"  Formatting as {fs.name}...")
        fs.format(image)
    else:
        print(f"  Reusing existing image: {image}")

    mount_point.mkdir(parents=True, exist_ok=True)
    fs.mount(image, mount_point)
    try:
        run_sudo(["chmod", "777", str(mount_point)])
        result = subprocess.run([str(binary)], cwd=mount_point)
        if result.returncode != 0:
            return False
        if stress_binary is not None:
            header(f"{fs.name}: disk_io_stress_test")
            result = subprocess.run(
                [str(stress_binary)] + (stress_args or []), cwd=mount_point
            )
            if result.returncode != 0:
                return False
        return True
    finally:
        fs.umount(mount_point)


def main() -> None:
    p = ArgumentParser(
        description="Test libtorrent disk I/O on various Linux filesystems."
    )
    p.add_argument(
        "--reformat", action="store_true", help="Delete and recreate all disk images"
    )
    p.add_argument(
        "--filesystems",
        nargs="+",
        metavar="FS",
        help="Which filesystems to test (default: all supported)",
    )
    p.add_argument(
        "--no-stress-test",
        dest="stress_test",
        action="store_false",
        help="Don't run tools/disk_io_stress_test on each mounted filesystem",
    )
    p.add_argument(
        "--stress-test-args",
        nargs="*",
        default=["smoke"],
        metavar="ARG",
        help='Arguments passed through to disk_io_stress_test (default: "smoke", '
        "which keeps total file sizes small; pass no arguments to run its full "
        "default suite, which needs a much larger --image-size-mb)",
    )
    p.add_argument(
        "--image-size-mb",
        type=int,
        default=IMAGE_SIZE_MB,
        metavar="MB",
        help=f"Size of each filesystem's disk image in MiB (default: {IMAGE_SIZE_MB}). "
        "Only takes effect when creating a new image. disk_io_stress_test's full "
        "default suite (i.e. --stress-test-args with no arguments) can "
        "write several GiB per test case and needs a correspondingly larger image.",
    )
    args = p.parse_args()

    print("NOTE: sudo is required for losetup / mount / umount / zpool.")

    if os.geteuid() == 0:
        print("WARNING: running as root; images will be owned by root.")

    all_fs_names = [fs.name for fs in FILESYSTEMS]
    if args.filesystems:
        unknown = set(args.filesystems) - set(all_fs_names)
        if unknown:
            sys.exit(
                f"ERROR: unknown filesystem(s): {', '.join(sorted(unknown))}\n"
                f"  Available: {', '.join(all_fs_names)}"
            )
        requested = args.filesystems
    else:
        requested = all_fs_names

    binary = build_binary("test_filesystem", TEST_DIR)

    stress_binary = None
    if args.stress_test:
        stress_binary = build_binary("disk_io_stress_test", TOOLS_DIR)

    results: dict[str, Optional[bool]] = {}
    for name in requested:
        fs = next(f for f in FILESYSTEMS if f.name == name)
        results[name] = test_filesystem(
            fs,
            binary,
            DEFAULT_IMAGE_DIR,
            args.reformat,
            args.image_size_mb,
            stress_binary,
            args.stress_test_args,
        )

    header("Results")
    width = max(len(n) for n in results) + 2
    any_failed = False
    for name, outcome in results.items():
        if outcome is None:
            label = "SKIP"
        elif outcome:
            label = "PASS"
        else:
            label = "FAIL"
            any_failed = True
        print(f"  {name:<{width}} {label}")

    sys.exit(1 if any_failed else 0)


if __name__ == "__main__":
    main()
