#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

"""Benchmarks create_torrent's hashing (examples/make_torrent, which drives
lt.set_piece_hashes() to actually create the torrent) across disk I/O
backends and torrent metadata variants.

This is deliberately a separate script from run_benchmark.py rather than a
new mode in its variant/io_backend/mode matrix: creating a torrent has no
session, no peers and no counters.log-producing alert loop, so the driver
logic (a single foreground process) doesn't fit run_test()'s shape. It
reuses run_benchmark.py's cache-dropping, profiling, results table/chart
rendering and summary-page helpers directly via import rather than factoring
them into a shared module, since the two scripts' matrices and driver loops
are different enough that a shared module isn't obviously worth it yet, but
the helpers themselves are identical.
"""

from argparse import ArgumentParser
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import time

import perf_call_tree
from run_benchmark import _filesystem_type
from run_benchmark import check_docutils
from run_benchmark import check_matplotlib
from run_benchmark import check_perf_environment
from run_benchmark import CHECK_START_DELAY
from run_benchmark import drop_file_cache
from run_benchmark import EXAMPLES_DIR
from run_benchmark import IO_BACKENDS
from run_benchmark import parse_max_rss
from run_benchmark import PIECE_SIZE_MIB
from run_benchmark import render_chart
from run_benchmark import render_pivot
from run_benchmark import rm_file_or_dir
from run_benchmark import suggest_dirty_limit_if_high_ram
from run_benchmark import write_test_summary
from vmstat import capture_sample
from vmstat import plot_output
from vmstat import print_output_to_file

exe = ".exe" if platform.system() == "Windows" else ""

# metadata variants to test. hybrid is the expensive one in practice (it
# hashes both a v1 and a v2 tree per file) so it's included by default here,
# unlike run_benchmark.py's DEFAULT_VARIANTS.
VARIANTS = ["v1", "v2", "hybrid"]

# make_torrent's own command line flags for selecting the metadata version
# (no flag at all means hybrid).
VERSION_FLAGS = {"v1": ["-1"], "v2": ["-2"], "hybrid": []}

# the payload content is never read back for correctness, only hashed, so
# its bytes don't matter, only that they're real (not a sparse hole), so a
# cold-cache run actually measures disk I/O; one buffer is reused across
# every file to avoid the random source becoming the bottleneck on large
# payloads.
PAYLOAD_FILL_BYTES = 1024 * 1024

RATE_RE = re.compile(r"hash rate:\s*([\d.]+)\s*MB/s")


def payload_dir(save_path: Path, num_pieces: int, num_files: int) -> Path:
    # the file count is part of the name so a payload generated with a
    # different --num-files isn't reused
    return save_path / f"create_torrent_bench-{num_pieces}p-{num_files}f"


def generate_payload(path: Path, num_pieces: int, num_files: int) -> None:
    """Write num_files files under `path` summing to num_pieces MiB. make_torrent's
    hash rate doesn't depend on the data's content, only that the files are
    real (not sparse), so the same fill buffer is just written repeatedly
    across every file; skips regenerating a payload that already has the
    right file count and total size, so reruns don't repay the write cost.
    """
    total_size = num_pieces * PIECE_SIZE_MIB * 1024 * 1024

    if path.is_dir():
        existing = sorted(path.iterdir())
        if (
            len(existing) == num_files
            and sum(f.stat().st_size for f in existing) == total_size
        ):
            return
        rm_file_or_dir(path)

    path.mkdir(parents=True)
    fill = os.urandom(PAYLOAD_FILL_BYTES)
    base_size, remainder = divmod(total_size, num_files)
    for i in range(num_files):
        size = base_size + (remainder if i == num_files - 1 else 0)
        with open(path / f"file-{i}", "wb") as f:
            written = 0
            while written < size:
                n = min(len(fill), size - written)
                f.write(fill[:n])
                written += n


def parse_hash_rate(path: Path) -> float:
    """Return the hash rate in MB/s from make_torrent's
    'hash rate: X MB/s (...)' summary line. Returns 0.0 if absent (e.g. the
    run failed before finishing).
    """
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return 0.0
    m = RATE_RE.search(text)
    return float(m.group(1)) if m else 0.0


def build_config_header(
    save_path: Path,
    num_pieces: int,
    num_files: int,
    aio_threads: int,
    hasher_threads: int,
) -> str:
    fstype = _filesystem_type(save_path) or "unknown"
    try:
        resolved = save_path.resolve()
    except OSError:
        resolved = save_path
    total_mib = num_pieces * PIECE_SIZE_MIB
    lines = [
        "Benchmark configuration",
        "=======================",
        "",
        f":Filesystem: {fstype} ({resolved})",
        f":Payload size: {total_mib} MiB ({num_pieces} pieces x"
        f" {PIECE_SIZE_MIB} MiB, {num_files} files)",
        f":Disk I/O threads: {aio_threads} (aio_threads)",
        f":Hashing threads: {hasher_threads} (hashing_threads)",
        "",
        "",
    ]
    return "\n".join(lines)


def update_results(
    results: list[dict],
    benchmarks_dir: Path,
    variant: str,
    io_backend: str,
    hash_rate: float,
    max_rss_bytes: float,
    config_header: str,
) -> None:
    """Record one test's result and re-render results.rst/html from
    `results` (the in-memory source of truth), mirroring
    run_benchmark.update_results.
    """
    benchmarks_dir.mkdir(parents=True, exist_ok=True)

    key = (variant, io_backend)
    results[:] = [r for r in results if (r["variant"], r["io_backend"]) != key]
    results.append(
        {
            "variant": variant,
            "io_backend": io_backend,
            "hash_rate_mbps": hash_rate,
            "max_rss_mib": max_rss_bytes / (1024 * 1024),
        }
    )

    doc_parts: list[str] = [config_header]

    rate_heading = "Hashing rate (MB/s)"
    doc_parts.append(f"{rate_heading}\n{'=' * len(rate_heading)}\n\n")
    doc_parts.append(render_pivot(results, "hash_rate_mbps", "create_torrent"))
    doc_parts.append("\n")
    if width := render_chart(
        results,
        "hash_rate_mbps",
        rate_heading,
        benchmarks_dir / "chart_hash_rate.png",
    ):
        doc_parts.append(
            f".. image:: chart_hash_rate.png\n"
            f"   :alt: {rate_heading}\n"
            f"   :width: {width}px\n\n"
        )

    mem_heading = "Max memory usage (MiB RSS)"
    doc_parts.append(f"{mem_heading}\n{'-' * len(mem_heading)}\n\n")
    doc_parts.append(render_pivot(results, "max_rss_mib", "create_torrent"))
    doc_parts.append("\n")
    mem_chart = "chart_rss.png"
    if width := render_chart(
        results,
        "max_rss_mib",
        mem_heading,
        benchmarks_dir / mem_chart,
        ylabel="MiB",
        palette="viridis",
        baseline=1.0,
        higher_is_better=False,
    ):
        doc_parts.append(
            f".. image:: {mem_chart}\n"
            f"   :alt: {mem_heading}\n"
            f"   :width: {width}px\n\n"
        )

    rst_text = "".join(doc_parts)

    rst_path = benchmarks_dir / "results.rst"
    tmp_rst = rst_path.with_suffix(".rst.tmp")
    tmp_rst.write_text(rst_text)
    tmp_rst.replace(rst_path)

    try:
        from docutils.core import publish_string
    except ImportError:
        return
    html_bytes = publish_string(source=rst_text, writer_name="html5")
    html_path = benchmarks_dir / "results.html"
    tmp_html = html_path.with_suffix(".html.tmp")
    tmp_html.write_bytes(html_bytes)
    tmp_html.replace(html_path)


def run_test(
    name: str,
    variant: str,
    io_backend: str,
    data_dir: Path,
    *,
    results: list[dict],
    results_path: Path,
    config_header: str,
    piece_size: int,
    aio_threads: int,
    hasher_threads: int,
    keep_perf_data: bool,
) -> None:
    """Run make_torrent once against `data_dir` and record the result.
    Unlike run_benchmark.run_test there is only one process to drive, so
    profiling attaches directly to it.
    """
    benchmarks_dir = results_path.resolve()
    output_dir = benchmarks_dir / name

    rm_file_or_dir(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # evict data_dir from the OS page cache so this run starts cold
    drop_file_cache(data_dir)

    cmd = [
        str(EXAMPLES_DIR / f"make_torrent{exe}"),
        str(data_dir),
        "-s",
        str(piece_size),
        "-i",
        io_backend,
        "-a",
        str(aio_threads),
        "-H",
        str(hasher_threads),
        # keeps hashing paused until the profiler below has had time to
        # attach; the pause itself isn't counted in the reported hash rate
        "-p",
        str(CHECK_START_DELAY),
        # written as a file rather than left to go to stdout, so the
        # generated .torrent's bytes don't end up mixed into run.out
        "-o",
        str(output_dir / "out.torrent"),
    ] + VERSION_FLAGS[variant]

    print(f"cmd: {' '.join(cmd)}")

    start = time.monotonic()
    with open(output_dir / "run.out", "w+") as run_out:
        c = subprocess.Popen(cmd, stdout=run_out, stderr=run_out)

        # perf-based profiling is Linux only, same as run_benchmark.py
        profiler = None
        if platform.system() == "Linux":
            perf_log_path = output_dir / "perf.log"
            perf_cmd = [
                "perf",
                "record",
                "-F",
                "999",
                "--call-graph",
                "dwarf,65528",
                "-o",
                str(output_dir / "perf.data"),
                "-p",
                str(c.pid),
            ]
            with open(perf_log_path, "wb") as perf_log:
                profiler = subprocess.Popen(perf_cmd, stdout=perf_log, stderr=perf_log)
            time.sleep(0.5)
            if profiler.poll() is not None:
                msg = perf_log_path.read_text(errors="replace").strip()
                c.wait()
                raise SystemExit(
                    f"ERROR: 'perf record' exited with code"
                    f" {profiler.returncode}.\n\nperf output:\n{msg}\n"
                )

        samples: dict[str, list[float]] = {}
        while c.poll() is None:
            capture_sample(c.pid, start, samples)
            time.sleep(0.1)
        end = time.monotonic()

        if profiler is not None:
            profiler.send_signal(signal.SIGINT)
            profiler.wait()

    print(f"runtime {end-start:0.2f} seconds")

    if c.returncode != 0:
        msg = (output_dir / "run.out").read_text(errors="replace").strip()
        raise SystemExit(
            f"ERROR: '{cmd[0]}' exited with code {c.returncode}.\n\n"
            f"output:\n{msg}\n"
        )

    stats_filename = output_dir / "memory_stats.log"
    keys = print_output_to_file(samples, str(stats_filename))
    plot_output(str(stats_filename), keys)

    hash_rate = parse_hash_rate(output_dir / "run.out")
    max_rss_bytes = parse_max_rss(stats_filename)

    update_results(
        results,
        benchmarks_dir,
        variant,
        io_backend,
        hash_rate,
        max_rss_bytes,
        config_header,
    )

    if platform.system() == "Linux":
        print("generating call tree report...")
        perf_data = output_dir / "perf.data"
        perf_call_tree.main(perf_data, output_dir / "profile.html", 0.1)
        if not keep_perf_data:
            perf_data.unlink(missing_ok=True)

    write_test_summary(output_dir, name)


def main() -> None:
    p = ArgumentParser()
    p.add_argument("--toolset", default="")
    p.add_argument(
        "--save-path",
        default=".",
        type=Path,
        help="The directory to generate the payload to hash in",
    )
    p.add_argument(
        "--results-path",
        default="benchmarks_create_torrent",
        type=Path,
        help="Directory to write benchmark results and per-test logs to",
    )
    p.add_argument(
        "--variants",
        nargs="+",
        choices=VARIANTS,
        default=VARIANTS,
        help="Torrent metadata variants to test",
    )
    p.add_argument(
        "--io-backends",
        nargs="+",
        choices=IO_BACKENDS,
        default=IO_BACKENDS,
        help="Disk I/O backends to test",
    )
    p.add_argument(
        "--num-pieces",
        type=int,
        default=10000,
        help="Number of pieces in the generated payload."
        " Piece size is 1 MiB, so total size is num_pieces MiB.",
    )
    p.add_argument(
        "--num-files",
        type=int,
        default=15,
        help="Number of files in the generated payload.",
    )
    p.add_argument(
        "--aio-threads",
        type=int,
        default=4,
        help="Number of disk I/O threads, libtorrent's aio_threads setting.",
    )
    p.add_argument(
        "--hasher-threads",
        type=int,
        default=2,
        help="Number of hashing threads, libtorrent's hashing_threads setting.",
    )
    p.add_argument(
        "--cleanup",
        action="store_true",
        help="Delete the generated payload once it is no longer needed,"
        " rather than leaving it for the next run to remove.",
    )
    p.add_argument(
        "--keep-perf-data",
        action="store_true",
        help="Keep each test's perf.data capture.",
    )

    args = p.parse_args()

    if args.num_files < 1:
        p.error("--num-files must be at least 1")

    if not args.save_path.is_dir():
        raise SystemExit(
            f"ERROR: --save-path {args.save_path} does not exist or is not a"
            " directory. Is the volume mounted?"
        )

    check_perf_environment()
    missing_matplotlib = check_matplotlib()
    missing_docutils = check_docutils()
    suggest_dirty_limit_if_high_ram()
    if missing_matplotlib or missing_docutils:
        raise SystemExit(1)

    subprocess.check_call(
        [
            "b2",
            "release",
            "debug-symbols=on",
            "cxxflags=-fno-omit-frame-pointer",
            args.toolset,
            "stage_make_torrent",
        ],
        cwd=EXAMPLES_DIR,
    )

    piece_size = PIECE_SIZE_MIB * 1024 * 1024

    # the payload is the same regardless of metadata variant, since that
    # only changes how make_torrent hashes it, not the data itself, so it's
    # generated once up front and shared across the whole variant/backend
    # matrix rather than once per variant.
    data_dir = payload_dir(args.save_path, args.num_pieces, args.num_files)
    generate_payload(data_dir, args.num_pieces, args.num_files)

    results: list[dict] = []
    config_header = build_config_header(
        args.save_path,
        args.num_pieces,
        args.num_files,
        args.aio_threads,
        args.hasher_threads,
    )

    for variant in args.variants:
        for io_backend in args.io_backends:
            run_test(
                f"create_torrent-{variant}-{io_backend}",
                variant,
                io_backend,
                data_dir,
                results=results,
                results_path=args.results_path,
                config_header=config_header,
                piece_size=piece_size,
                aio_threads=args.aio_threads,
                hasher_threads=args.hasher_threads,
                keep_perf_data=args.keep_perf_data,
            )

    if args.cleanup:
        rm_file_or_dir(data_dir)


if __name__ == "__main__":
    main()
