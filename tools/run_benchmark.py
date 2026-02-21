#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import time
import shutil
import subprocess
import parse_session_stats
from pathlib import Path
from argparse import ArgumentParser

from vmstat import capture_sample, plot_output, print_output_to_file

import platform

exe = ""

ROOT_DIR = Path(__file__).parent.parent.resolve()
EXAMPLES_DIR = ROOT_DIR / "examples"

if platform.system() == "Windows":
    exe = ".exe"


def reset_download(save_path: Path) -> None:
    rm_file_or_dir(Path(".ses_state"))
    rm_file_or_dir(save_path / ".resume")
    rm_file_or_dir(save_path / "cpu_benchmark")


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

    args = p.parse_args()

    subprocess.check_call(
        ["b2", "profile", args.toolset, "stage_client_test"], cwd=EXAMPLES_DIR
    )
    subprocess.check_call(
        ["b2", "release", args.toolset, "stage_connection_tester"], cwd=EXAMPLES_DIR
    )

    reset_download(args.save_path)

    if not (EXAMPLES_DIR / "cpu_benchmark.torrent").exists():
        subprocess.check_call(
            [
                str(EXAMPLES_DIR / f"connection_tester{exe}"),
                "gen-torrent",
                "-s",  # num pieces
                "50000",
                "-n",  # num files
                "15",
                "-t",  # output torrent file
                str(EXAMPLES_DIR / "cpu_benchmark.torrent"),
            ],
        )

    rm_file_or_dir(Path("t"))

    run_test(
        "download-write-through",
        "upload",
        ["-1", "--disk_io_write_mode=write_through"],
        args.download_peers,
        args.save_path,
        args.always_yes,
    )
    reset_download(args.save_path)
    run_test(
        "download-full-cache",
        "upload",
        ["-1", "--disk_io_write_mode=enable_os_cache"],
        args.download_peers,
        args.save_path,
        args.always_yes,
    )
    run_test(
        "upload",
        "download",
        ["-G", "-e", "240"],
        args.upload_peers,
        args.save_path,
        args.always_yes,
    )


def run_test(
    name: str,
    action: str,
    client_arg: list[str],
    num_peers: int,
    save_path: Path,
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
        str(EXAMPLES_DIR / "cpu_benchmark.torrent"),
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
        str(EXAMPLES_DIR / "cpu_benchmark.torrent"),
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
        print(f"test_cmd: \"{' '.join(test_cmd)}\"")
        t = subprocess.Popen(test_cmd, stdout=test_out, stderr=test_out)

        out: dict[str, list[float]] = {}
        while c.returncode is None:
            capture_sample(c.pid, start, out)
            time.sleep(0.1)
            c.poll()
        end = time.monotonic()

        stats_filename = output_dir / "memory_stats.log"
        keys = print_output_to_file(out, stats_filename)
        plot_output(stats_filename, keys)

        t.wait()

    print(f"runtime {end-start:0.2f} seconds")

    # MacOS no longer supports gprof
    if platform.system() == "Linux":
        print("analyzing profile...")
        with open(output_dir / "gprof.out", "w+") as gprof:
            print(f"gprof " + str(EXAMPLES_DIR / f"client_test{exe}"))
            subprocess.check_call(
                ["gprof", str(EXAMPLES_DIR / f"client_test{exe}")], stdout=gprof
            )
        print("generating profile graph...")

        with (
            open(output_dir / "gprof.out") as gprof,
            open(output_dir / "gprof.dot", "w+") as dot,
        ):
            subprocess.check_call(["gprof2dot", "--strip"], stdin=gprof, stdout=dot)
            with open(output_dir / "cpu_profile.png", "w+") as profile:
                dot.seek(0)
                subprocess.check_call(["dot", "-Tpng"], stdin=dot, stdout=profile)

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
