#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import re
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


def torrent_path(variant: str, num_pieces: int) -> Path:
    return EXAMPLES_DIR / f"cpu_benchmark-{variant}-{num_pieces}p.torrent"


def reset_download(save_path: Path, variant: str, num_pieces: int) -> None:
    rm_file_or_dir(Path(".ses_state"))
    rm_file_or_dir(save_path / ".resume")
    # the payload directory is named after the torrent file stem
    # (see generate_torrent() in connection_tester.cpp).
    rm_file_or_dir(payload_dir(save_path, variant, num_pieces))


def payload_dir(save_path: Path, variant: str, num_pieces: int) -> Path:
    return save_path / f"cpu_benchmark-{variant}-{num_pieces}p"


def drop_file_cache(path: Path) -> None:
    """Evict path (recursively) from the OS page cache so the next access
    actually hits disk. Used between benchmark runs to get cold-cache numbers.

    Linux: posix_fadvise(POSIX_FADV_DONTNEED) per file, after fdatasync to
        flush any dirty pages first (only clean pages can be dropped). No
        root required; targeted to just these files.
    macOS: posix_fadvise is not implemented in Darwin's libc, so
        os.posix_fadvise is not defined on Python/macOS. Falls back to the
        Apple-supplied 'purge' command, which flushes the entire system
        cache. No sudo on modern macOS. Note: not targeted.
    Windows: there is no per-file cache eviction without admin (Sysinternals
        RAMMap /Et, or NtSetSystemInformation). Prints a warning and is a
        no-op; for cold-cache benchmarks on Windows, reboot between runs
        or run RAMMap manually.
    """
    if not path.exists():
        return

    system = platform.system()
    if system == "Linux" and hasattr(os, "posix_fadvise") \
            and hasattr(os, "POSIX_FADV_DONTNEED"):
        for entry in _walk_files(path):
            try:
                fd = os.open(str(entry), os.O_RDONLY)
            except OSError:
                continue
            try:
                # only clean pages get dropped; flush dirty ones first
                try:
                    os.fdatasync(fd)
                except OSError:
                    pass
                os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
            finally:
                os.close(fd)
        return

    if system == "Darwin":
        try:
            r = subprocess.run(
                ["purge"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
        except FileNotFoundError:
            print("warning: 'purge' not found on PATH; "
                  "cannot drop file caches on macOS")
            return
        if r.returncode != 0:
            # don't let a silent purge failure turn a cold-cache run
            # into a warm-cache one without telling anyone.
            msg = r.stdout.decode(errors="replace").strip()
            print(f"warning: 'purge' exited with code {r.returncode};"
                  " file caches may NOT have been dropped, so this"
                  " run may be warm-cache. purge output:")
            for line in msg.splitlines() or ["(no output)"]:
                print(f"  {line}")
        return

    if system == "Windows":
        print("warning: automatic file cache drop is not available on "
              "Windows without admin. For cold-cache benchmarks, reboot "
              "between runs or use Sysinternals RAMMap (/Et) manually.")
        return


def _walk_files(root: Path):
    if root.is_file():
        yield root
        return
    if not root.is_dir():
        return
    for entry in root.rglob("*"):
        if entry.is_file():
            yield entry


def compact_indent(line: str) -> str:
    """Compact a perf '--call-graph graph' tree line: each 11-column tree
    level shrinks to 2 columns so deep trees fit on screen.

    Approach: split the indent prefix at each '|' and shrink each interior
    space run proportionally. perf renders each tree level as 11 columns,
    so 10 spaces between pipes (the body of one `|          ` unit) become
    1 space. The 15-space baseline (the Children% column) becomes 5 spaces,
    with each additional 11-col last-sibling unit (11 spaces, no pipe)
    contributing 2 more. Content past the indent (`|--12.34%-- foo`,
    ` --12.34%-- foo`, a function name, or nothing for a pure
    sibling-separator line) is preserved unchanged.

    A `|` belongs to the indent only when followed by whitespace or
    end-of-line. A `|` immediately followed by `-` is the content marker
    `|--XX.XX%--`, so the scan stops before it.

    Pure sibling-separator lines (e.g. `               |          |`) get
    the same treatment so the tree's branching structure is preserved.

    The first child of a top-level entry is rendered as
    `<12 spaces>---name` (no percentage) and is handled separately.
    """
    if line.startswith("            ---"):
        return "  " + line[12:]
    if not line.startswith("               "):  # 15-space Children% baseline
        return line

    stripped = line.rstrip("\n")
    nl = line[len(stripped):]

    # find the end of the indent prefix: a leading run of ' ' and 'indent'
    # pipes. anything past it is content (or nothing for a sibling
    # separator).
    i = 0
    while i < len(stripped):
        c = stripped[i]
        if c == " ":
            i += 1
        elif c == "|":
            nxt = stripped[i + 1] if i + 1 < len(stripped) else ""
            if nxt == "" or nxt == " ":
                i += 1
            else:
                break
        else:
            break
    indent, rest = stripped[:i], stripped[i:]

    pieces = indent.split("|")
    out = []
    for idx, p in enumerate(pieces):
        if idx == 0:
            # baseline (15) -> 5; each extra 11-col last-sibling unit -> 2.
            # anything left over (off-by-one whitespace) is kept verbatim.
            extra = len(p) - 15
            out.append(" " * (5 + 2 * (extra // 11) + extra % 11))
        else:
            # 10 spaces inside one `|          ` unit -> 1 space; round
            # other widths proportionally.
            out.append(" " * round(len(p) / 10))

    return "|".join(out) + rest + nl


RATE_LINE_RE = re.compile(
    r"rate sent:\s*([\d.]+)\s*MB/s\s+received:\s*([\d.]+)\s*MB/s"
)


def parse_test_out(path: Path) -> tuple[float, float]:
    """Return (sent_MB, received_MB) parsed from connection_tester's summary.
    The 'MB/s' label in connection_tester's output is misleading -- the
    printed value is total bytes / 1e6, not divided by runtime. Caller
    converts to a real rate using its own runtime measurement.
    """
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return 0.0, 0.0
    m = RATE_LINE_RE.search(text)
    if not m:
        return 0.0, 0.0
    return float(m.group(1)), float(m.group(2))


# stable display order: backends are table rows, variants are columns
_VARIANT_ORDER = {"v1": 0, "v2": 1, "hybrid": 2}
_BACKEND_ORDER = {"mmap": 0, "pread": 1, "posix": 2}


def render_pivot(mode_results: list[dict], rate_field: str) -> str:
    """Render a `disk_io_backend X torrent_variant` matrix of `rate_field`
    as an RST simple table. Backends not yet tested in this mode are
    omitted; cells for untested (backend, variant) pairs show '-'.
    """
    backends = sorted(
        {r["io_backend"] for r in mode_results},
        key=lambda b: _BACKEND_ORDER.get(b, 99),
    )
    variants = sorted(
        {r["variant"] for r in mode_results},
        key=lambda v: _VARIANT_ORDER.get(v, 99),
    )
    by_key = {
        (r["io_backend"], r["variant"]): r[rate_field]
        for r in mode_results
    }

    headers = ["Backend"] + variants
    body = []
    for b in backends:
        row = [b]
        for v in variants:
            rate = by_key.get((b, v))
            row.append(f"{rate:.2f}" if rate is not None else "-")
        body.append(row)

    widths = [len(h) for h in headers]
    for row in body:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    sep = "  ".join("=" * w for w in widths)

    def fmt(cells: list[str]) -> str:
        return "  ".join(c.ljust(w) for c, w in zip(cells, widths))

    lines = [sep, fmt(headers), sep]
    for row in body:
        lines.append(fmt(row))
    lines.append(sep)
    return "\n".join(lines) + "\n"


def update_results(
    results: list[dict],
    benchmarks_dir: Path,
    transfer_mode: str,
    variant: str,
    io_backend: str,
    upload_rate: float,
    download_rate: float,
) -> None:
    """Record one test's result into `results` (mutated in place) and
    re-render the RST summary table -- plus its HTML rendering, when
    docutils is importable. `results` is the in-memory source of truth;
    the RST/HTML are rewritten from it after every test so a Ctrl-C run
    leaves a coherent partial table behind, but nothing is persisted
    across script restarts.
    """
    benchmarks_dir.mkdir(parents=True, exist_ok=True)

    key = (transfer_mode, variant, io_backend)
    results[:] = [
        r for r in results
        if (r["transfer_mode"], r["variant"], r["io_backend"]) != key
    ]
    results.append({
        "transfer_mode": transfer_mode,
        "variant": variant,
        "io_backend": io_backend,
        "upload_rate_mbps": upload_rate,
        "download_rate_mbps": download_rate,
    })

    by_mode: dict[str, list[dict]] = {}
    for r in results:
        by_mode.setdefault(r["transfer_mode"], []).append(r)

    # one mode-section per transfer mode that has results. download- and
    # upload-only modes get a single matrix for the rate that mode
    # actually exercises; dual mode gets one matrix per direction.
    sections = [
        ("download", "Download mode",
         [(None, "download_rate_mbps")]),
        ("upload", "Upload mode",
         [(None, "upload_rate_mbps")]),
        ("dual", "Dual mode", [
            ("Download rate (MB/s)", "download_rate_mbps"),
            ("Upload rate (MB/s)", "upload_rate_mbps"),
        ]),
    ]

    doc_parts: list[str] = []
    for mode_key, heading, subs in sections:
        mode_results = by_mode.get(mode_key, [])
        if not mode_results:
            continue
        doc_parts.append(f"{heading}\n{'=' * len(heading)}\n\n")
        for sub_heading, rate_field in subs:
            if sub_heading:
                doc_parts.append(
                    f"{sub_heading}\n{'-' * len(sub_heading)}\n\n"
                )
            doc_parts.append(render_pivot(mode_results, rate_field))
            doc_parts.append("\n")
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


def check_perf_environment() -> None:
    """Warn early about sysctls that prevent perf record / perf report from
    producing useful output. Linux only; no-op elsewhere.
    """
    if platform.system() != "Linux":
        return

    def read_sysctl(name):
        try:
            return Path(f"/proc/sys/{name.replace('.', '/')}").read_text().strip()
        except OSError:
            return None

    warnings = []
    kptr = read_sysctl("kernel.kptr_restrict")
    if kptr is not None and kptr != "0":
        warnings.append(
            f"  kernel.kptr_restrict = {kptr} -- kernel symbols will not"
            " resolve in perf report.\n"
            "    fix: sudo sysctl kernel.kptr_restrict=0"
        )

    paranoid = read_sysctl("kernel.perf_event_paranoid")
    # 2 is the default on many distros; perf record on a PID we own usually
    # needs <= 1 to capture kernel-side samples.
    if paranoid is not None and paranoid not in ("-1", "0", "1"):
        warnings.append(
            f"  kernel.perf_event_paranoid = {paranoid} -- 'perf record'"
            " may fail to attach or miss kernel samples.\n"
            "    fix: sudo sysctl kernel.perf_event_paranoid=1"
        )

    ptrace = read_sysctl("kernel.yama.ptrace_scope")
    if ptrace is not None and ptrace != "0":
        warnings.append(
            f"  kernel.yama.ptrace_scope = {ptrace} -- 'perf record -p'"
            " may not be able to attach.\n"
            "    fix: sudo sysctl kernel.yama.ptrace_scope=0"
        )

    if warnings:
        print("warning: perf environment is restrictive:")
        for w in warnings:
            print(w)
        print()


def main() -> None:
    p = ArgumentParser()
    p.add_argument("--toolset", default="")
    p.add_argument(
        "--download-peers", type=int, default=50, help="Number of peers to use for download test"
    )
    p.add_argument(
        "--upload-peers", type=int, default=20, help="Number of peers to use for upload test"
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
    p.add_argument(
        "--num-pieces",
        type=int,
        default=50000,
        help="Number of pieces in the generated test torrent."
        " Piece size is 1 MiB, so total size is num_pieces MiB.",
    )

    args = p.parse_args()

    check_perf_environment()

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
        reset_download(args.save_path, variant, args.num_pieces)
        if not torrent_path(variant, args.num_pieces).exists():
            subprocess.check_call(
                [
                    str(EXAMPLES_DIR / f"connection_tester{exe}"),
                    "gen-torrent",
                    "-s",  # num pieces
                    str(args.num_pieces),
                    "-n",  # num files
                    "15",
                    "-V",  # metadata version
                    VARIANT_FLAGS[variant],
                    "-t",  # output torrent file
                    str(torrent_path(variant, args.num_pieces)),
                ],
            )

    rm_file_or_dir(Path("t"))

    results: list[dict] = []

    for variant in args.variants:
        torrent = torrent_path(variant, args.num_pieces)
        data_dir = payload_dir(args.save_path, variant, args.num_pieces)
        for io_backend in args.io_backends:
            reset_download(args.save_path, variant, args.num_pieces)
            run_test(
                f"download-{variant}-{io_backend}",
                "upload",
                ["-1", "-i", io_backend],
                args.download_peers,
                args.save_path,
                torrent,
                data_dir,
                results=results,
                transfer_mode="download",
                variant=variant,
                io_backend=io_backend,
            )
            reset_download(args.save_path, variant, args.num_pieces)
            run_test(
                f"dual-{variant}-{io_backend}",
                "dual",
                ["-1", "-i", io_backend],
                args.download_peers,
                args.save_path,
                torrent,
                data_dir,
                results=results,
                transfer_mode="dual",
                variant=variant,
                io_backend=io_backend,
            )
            run_test(
                f"upload-{variant}-{io_backend}",
                "download",
                ["-G", "-e", "240", "-i", io_backend],
                args.upload_peers,
                args.save_path,
                torrent,
                data_dir,
                results=results,
                transfer_mode="upload",
                variant=variant,
                io_backend=io_backend,
            )


def run_test(
    name: str,
    action: str,
    client_arg: list[str],
    num_peers: int,
    save_path: Path,
    torrent: Path,
    data_dir: Path,
    *,
    results: list[dict],
    transfer_mode: str,
    variant: str,
    io_backend: str,
) -> None:
    benchmarks_dir = (save_path / "benchmarks").resolve()
    output_dir = benchmarks_dir / name

    rm_file_or_dir(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    port = (int(time.time()) % 50000) + 2000

    # evict the test's payload files from the OS page cache so this run
    # starts cold (matters most for the upload test, which reads the data
    # written by a previous run)
    drop_file_cache(data_dir)

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
                    "  3. kernel.kptr_restrict hides kernel addresses (so"
                    " kernel symbols won't resolve):\n"
                    "       sudo sysctl kernel.kptr_restrict=0\n"
                    "  4. Or grant capabilities to the perf binary:\n"
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
        test_start = time.monotonic()
        t = subprocess.Popen(test_cmd, stdout=test_out, stderr=test_out)

        out: dict[str, list[float]] = {}
        while t.returncode is None:
            capture_sample(c.pid, start, out)
            time.sleep(0.1)
            t.poll()
        end = time.monotonic()
        test_runtime = end - test_start

        if profiler is not None:
            profiler.send_signal(signal.SIGINT)
            profiler.wait()

        stats_filename = output_dir / "memory_stats.log"
        keys = print_output_to_file(out, stats_filename)
        plot_output(stats_filename, keys)

        t.wait()

        # stop the client cleanly before reading counters.log -- otherwise
        # parse_session_stats may catch a half-written `session stats (`
        # line and crash on the split. SIGINT triggers client_test's
        # normal shutdown, which flushes and closes the stats file.
        c.send_signal(signal.SIGINT)
        c.wait()

    print(f"runtime {end-start:0.2f} seconds")

    sent_mb, recv_mb = parse_test_out(output_dir / "test.out")
    # connection_tester reports totals from its own perspective: it `sent` to
    # the client (so client downloaded) and `received` from it (so client
    # uploaded). Convert to rates against the test-only runtime.
    download_rate = sent_mb / test_runtime if test_runtime > 0 else 0.0
    upload_rate = recv_mb / test_runtime if test_runtime > 0 else 0.0
    update_results(
        results, benchmarks_dir, transfer_mode, variant, io_backend,
        upload_rate, download_rate,
    )

    # perf-based profiling is Linux only
    if platform.system() == "Linux":
        print("generating call tree report...")
        raw_report = subprocess.check_output(
            [
                "perf", "report",
                "-i", str(output_dir / "perf.data"),
                "--stdio",
                "--no-header",
                "--children",
                "--sort", "symbol",
                "-g", "graph,0.5,caller",
                "--percent-limit", "0.5",
            ],
            text=True,
        )
        # collapse each 11-column tree level to 2 columns so deep trees
        # fit on screen. sibling-separator lines (whitespace plus `|`)
        # are kept because they show where the tree's branching happens.
        with open(output_dir / "perf.report", "w") as perf_report:
            for line in raw_report.splitlines(keepends=True):
                perf_report.write(compact_indent(line))

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
