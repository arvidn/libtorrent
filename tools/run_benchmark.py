#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import time
import shutil
import signal
import subprocess
import parse_session_stats
from pathlib import Path
from argparse import ArgumentParser

from vmstat import capture_sample, plot_output, print_output_to_file

import platform

exe = ""

ROOT_DIR = Path(__file__).parent.parent.resolve()
EXAMPLES_DIR = ROOT_DIR / "examples"

# torrent metadata variants to exercise. Maps the variant name to the
# connection_tester gen-torrent "-V" flag value (1 = v1-only, 2 = v2-only,
# h = hybrid). The variant name is also used to suffix the torrent filename
# and the payload directory.
VARIANT_FLAGS = {"v1": "1", "v2": "2", "hybrid": "h"}

# disk I/O backends to exercise. Passed to client_test via "-i".
IO_BACKENDS = ["mmap", "pread", "posix"]

if platform.system() == "Windows":
    exe = ".exe"


def torrent_path(variant: str) -> Path:
    return EXAMPLES_DIR / f"cpu_benchmark-{variant}.torrent"


def reset_download(save_path: Path, variant: str) -> None:
    rm_file_or_dir(Path(".ses_state"))
    rm_file_or_dir(save_path / ".resume")
    # the payload directory is named after the torrent file stem
    # (see generate_torrent() in connection_tester.cpp).
    rm_file_or_dir(save_path / f"cpu_benchmark-{variant}")


def main() -> None:
    p = ArgumentParser()
    p.add_argument("--toolset", default="")
    p.add_argument(
        "-y",
        action="store_true",
        dest="always_yes",
        help="don't wait for interactive input, keep running",
    )
    p.add_argument(
        "--download-peers", default=50, help="Number of peers to use for download test"
    )
    p.add_argument(
        "--upload-peers", default=20, help="Number of peers to use for upload test"
    )
    p.add_argument(
        "--save-path",
        default=".",
        type=Path,
        help="The directory to download to or upload from",
    )
    p.add_argument(
        "--variants",
        nargs="+",
        choices=list(VARIANT_FLAGS.keys()),
        default=list(VARIANT_FLAGS.keys()),
        help="Torrent metadata variants to test. Each selected variant is"
        " run through the full backend/test matrix.",
    )
    p.add_argument(
        "--io-backends",
        nargs="+",
        choices=IO_BACKENDS,
        default=IO_BACKENDS,
        help="Disk I/O backends to test. Each selected backend is run"
        " through the full variant/test matrix.",
    )

    args = p.parse_args()

    subprocess.check_call(
        [
            "b2",
            "release",
            "debug-symbols=on",
            "cxxflags=-fno-omit-frame-pointer",
            args.toolset,
            "stage_client_test",
        ],
        cwd=EXAMPLES_DIR,
    )
    subprocess.check_call(
        ["b2", "release", args.toolset, "stage_connection_tester"], cwd=EXAMPLES_DIR
    )

    for variant in args.variants:
        reset_download(args.save_path, variant)
        if not torrent_path(variant).exists():
            subprocess.check_call(
                [
                    str(EXAMPLES_DIR / f"connection_tester{exe}"),
                    "gen-torrent",
                    "-s",  # num pieces
                    "50000",
                    "-n",  # num files
                    "15",
                    "-V",  # metadata version
                    VARIANT_FLAGS[variant],
                    "-t",  # output torrent file
                    str(torrent_path(variant)),
                ],
            )

    rm_file_or_dir(Path("t"))

    for variant in args.variants:
        torrent = torrent_path(variant)
        for io_backend in args.io_backends:
            reset_download(args.save_path, variant)
            run_test(
                f"download-{variant}-{io_backend}",
                "upload",
                ["-1", "-i", io_backend],
                args.download_peers,
                args.save_path,
                torrent,
                args.always_yes,
            )
            reset_download(args.save_path, variant)
            run_test(
                f"dual-{variant}-{io_backend}",
                "dual",
                ["-1", "-i", io_backend],
                args.download_peers,
                args.save_path,
                torrent,
                args.always_yes,
            )
            run_test(
                f"upload-{variant}-{io_backend}",
                "download",
                ["-G", "-e", "240", "-i", io_backend],
                args.upload_peers,
                args.save_path,
                torrent,
                args.always_yes,
            )


def run_test(
    name: str,
    action: str,
    client_arg: list[str],
    num_peers: int,
    save_path: Path,
    torrent: Path,
    always_yes: bool,
) -> None:
    output_dir = (save_path / f"logs_{name}").resolve()

    rm_file_or_dir(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    port = (int(time.time()) % 50000) + 2000

    if not always_yes:
        print('drop caches now. e.g. "echo 1 | sudo tee /proc/sys/vm/drop_caches"')
        input("Press Enter to continue...")

    start = time.monotonic()
    client_cmd = [
        str(EXAMPLES_DIR / f"client_test{exe}"),
        str(torrent),
        "-k",  # high performance seed settings
        "-O",  # print stats counters to specified file
        str(output_dir / "counters.log"),
        "-T",  # max connections per torrent
        f"{num_peers*2}",
        "-f",  # print log to file
        str(output_dir / "events.log"),
        "-s",  # directory to download torrents to
        str(save_path),
        f"--listen_interfaces=127.0.0.1:{port}",
        "--enable_dht=0",
        "--enable_lsd=0",
        "--enable_upnp=0",
        "--enable_natpmp=0",
        "--allow_multiple_connections_per_ip=1",
        f"--connections_limit={num_peers*2}",
        "--alert_mask=error,status,connect,performance_warning,storage,peer",
    ] + client_arg

    test_cmd = [
        str(EXAMPLES_DIR / f"connection_tester{exe}"),
        action,
        "-c",
        f"{num_peers}",
        "-d",
        "127.0.0.1",
        "-p",
        f"{port}",
        "-t",
        str(torrent),
        "-s",
        str(save_path),
    ]

    with (
        open(output_dir / "client.out", "w+") as client_out,
        open(output_dir / "test.out", "w+") as test_out,
    ):
        print(f"client_cmd: {' '.join(client_cmd)}")
        c = subprocess.Popen(
            client_cmd,
            stdout=client_out,
            stderr=client_out,
            stdin=subprocess.PIPE,
        )
        time.sleep(2)

        profiler = None
        if platform.system() == "Linux":
            perf_log_path = output_dir / "perf.log"
            perf_cmd = [
                "perf", "record",
                "-F", "999",
                "--call-graph", "dwarf,65528",
                "-o", str(output_dir / "perf.data"),
                "-p", str(c.pid),
            ]
            print(f"perf_cmd: {' '.join(perf_cmd)}")
            try:
                with open(perf_log_path, "wb") as perf_log:
                    profiler = subprocess.Popen(
                        perf_cmd, stdout=perf_log, stderr=perf_log
                    )
            except FileNotFoundError:
                c.send_signal(signal.SIGINT)
                c.wait()
                raise SystemExit(
                    "ERROR: 'perf' is not installed.\n"
                    "  Debian/Ubuntu: sudo apt install"
                    " linux-tools-$(uname -r) linux-tools-generic\n"
                    "  Fedora/RHEL:   sudo dnf install perf"
                )
            # if perf can't open the events (typically due to
            # kernel.perf_event_paranoid or ptrace_scope) it exits within a
            # few milliseconds. Give it a moment then check.
            time.sleep(0.5)
            if profiler.poll() is not None:
                msg = perf_log_path.read_text(errors="replace").strip()
                c.send_signal(signal.SIGINT)
                c.wait()
                raise SystemExit(
                    f"ERROR: 'perf record' exited with code"
                    f" {profiler.returncode}.\n\n"
                    f"perf output:\n{msg}\n\n"
                    "Common causes and fixes:\n"
                    "  1. kernel.perf_event_paranoid is too restrictive:\n"
                    "       sudo sysctl kernel.perf_event_paranoid=1\n"
                    "  2. kernel.yama.ptrace_scope blocks attaching to"
                    " another process:\n"
                    "       sudo sysctl kernel.yama.ptrace_scope=0\n"
                    "  3. Or grant capabilities to the perf binary:\n"
                    "       sudo setcap cap_perfmon,cap_sys_ptrace+ep"
                    " $(which perf)"
                )
        elif platform.system() == "Darwin":
            xctrace_log_path = output_dir / "xctrace.log"
            xctrace_cmd = [
                "xcrun", "xctrace", "record",
                "--template", "Time Profiler",
                "--attach", str(c.pid),
                "--output", str(output_dir / "run.trace"),
            ]
            print(f"xctrace_cmd: {' '.join(xctrace_cmd)}")
            try:
                with open(xctrace_log_path, "wb") as xctrace_log:
                    profiler = subprocess.Popen(
                        xctrace_cmd,
                        stdout=xctrace_log,
                        stderr=xctrace_log,
                    )
            except FileNotFoundError:
                c.send_signal(signal.SIGINT)
                c.wait()
                raise SystemExit(
                    "ERROR: 'xcrun' is not installed.\n"
                    "  Install Xcode Command Line Tools:"
                    " xcode-select --install"
                )
            time.sleep(0.5)
            if profiler.poll() is not None:
                msg = xctrace_log_path.read_text(errors="replace").strip()
                c.send_signal(signal.SIGINT)
                c.wait()
                raise SystemExit(
                    f"ERROR: 'xctrace record' exited with code"
                    f" {profiler.returncode}.\n\n"
                    f"xctrace output:\n{msg}"
                )

        print(f"test_cmd: \"{' '.join(test_cmd)}\"")
        t = subprocess.Popen(test_cmd, stdout=test_out, stderr=test_out)

        out: dict[str, list[float]] = {}
        while t.returncode is None:
            capture_sample(c.pid, start, out)
            time.sleep(0.1)
            t.poll()
        end = time.monotonic()

        if profiler is not None:
            profiler.send_signal(signal.SIGINT)
            profiler.wait()

        stats_filename = output_dir / "memory_stats.log"
        keys = print_output_to_file(out, stats_filename)
        plot_output(stats_filename, keys)

        t.wait()

    print(f"runtime {end-start:0.2f} seconds")

    # perf-based profiling is Linux only
    if platform.system() == "Linux":
        print("analyzing profile...")
        with open(output_dir / "perf.out", "w+") as perf_script:
            subprocess.check_call(
                ["perf", "script", "-i", str(output_dir / "perf.data")],
                stdout=perf_script,
            )
        print("generating profile graph...")

        with (
            open(output_dir / "perf.out") as perf_script,
            open(output_dir / "perf.dot", "w+") as dot,
        ):
            subprocess.check_call(
                ["gprof2dot", "-f", "perf", "--strip"],
                stdin=perf_script,
                stdout=dot,
            )
            with open(output_dir / "cpu_profile.png", "wb") as profile:
                dot.seek(0)
                subprocess.check_call(["dot", "-Tpng"], stdin=dot, stdout=profile)

        print("generating flame graph...")
        try:
            with (
                open(output_dir / "perf.out") as perf_script,
                open(output_dir / "flame.folded", "w+") as folded,
            ):
                subprocess.check_call(
                    ["stackcollapse-perf.pl"],
                    stdin=perf_script,
                    stdout=folded,
                )
            with (
                open(output_dir / "flame.folded") as folded,
                open(output_dir / "flame.svg", "w+") as svg,
            ):
                subprocess.check_call(
                    ["flamegraph.pl"], stdin=folded, stdout=svg
                )
        except FileNotFoundError:
            print(
                "skipping flame graph: FlameGraph tools not found on PATH.\n"
                "  install: git clone"
                " https://github.com/brendangregg/FlameGraph\n"
                "  then add the cloned directory to PATH"
            )

    parse_session_stats.main(output_dir / "counters.log", 8, output_dir)


def rm_file_or_dir(path: Path) -> None:
    """Attempt to remove file or directory at path"""
    try:
        shutil.rmtree(path)
    except Exception:
        pass

    try:
        os.remove(path)
    except Exception:
        pass


if __name__ == "__main__":
    main()
