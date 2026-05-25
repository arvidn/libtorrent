#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import re
import time
import shutil
import signal
import subprocess
import parse_session_stats
import perf_call_tree
import disk_latency
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

# transfer modes to exercise. "download" measures the client downloading from
# peers, "upload" measures it seeding to peers, and "dual" runs both directions
# at once. Each selected mode is run through the full variant/backend matrix.
MODES = ["download", "upload", "dual"]

# fixed properties of the generated test torrents. connection_tester's
# gen-torrent uses a 1 MiB piece size (the "-s" flag is the piece count, not
# the piece size), and we always generate this many files per torrent.
PIECE_SIZE_MIB = 1
NUM_FILES = 15

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


def gen_data(torrent: Path, save_path: Path, *, with_resume: bool = False) -> None:
    """Write the canonical payload for `torrent` under `save_path` (a full
    sweep of generate_block() per file). When `with_resume` is true,
    also write a resume file under `save_path/.resume/` that claims
    every piece is present, so a seed-only client (e.g. client_test for
    the upload benchmark) can start in seeding state without the
    startup check pass that would otherwise warm the page cache.
    """
    cmd = [
        str(EXAMPLES_DIR / f"connection_tester{exe}"),
        "gen-data",
        "-t", str(torrent),
        "-P", str(save_path),
    ]
    if with_resume:
        cmd.append("-R")
    subprocess.check_call(cmd)


def payload_dir(save_path: Path, variant: str, num_pieces: int) -> Path:
    return save_path / f"cpu_benchmark-{variant}-{num_pieces}p"


def drop_file_cache(path: Path) -> None:
    """Evict path (recursively) from the OS page cache so the next access
    actually hits disk. Used between benchmark runs to get cold-cache numbers.

    Linux: posix_fadvise(POSIX_FADV_DONTNEED) per file, after fdatasync to
        flush any dirty pages first (only clean pages can be dropped). No
        root required; targeted to just these files. NOTE: this only evicts
        the Linux page cache, which is not enough on every filesystem: ZFS
        serves normal reads from its own ARC (fadvise cannot touch it) and
        tmpfs is RAM-backed (nothing to evict to). A warning is printed when
        the target is on one of those; see _warn_if_ineffective_drop().
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
        _warn_if_ineffective_drop(path)
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


def _filesystem_type(path: Path) -> str | None:
    """Linux only. Return the filesystem type (e.g. 'zfs', 'ext4') backing
    `path` by matching it against the longest mount point in /proc/mounts.
    Returns None if it can't be determined.
    """
    try:
        target = path.resolve()
    except OSError:
        return None
    best_len = -1
    best_fstype = None
    try:
        with open("/proc/mounts", encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.split()
                if len(parts) < 3:
                    continue
                # mount points with spaces etc. are octal-escaped (\040)
                mount_point = parts[1].encode("latin-1").decode(
                    "unicode_escape"
                )
                fstype = parts[2]
                try:
                    target.relative_to(mount_point)
                except ValueError:
                    continue
                if len(mount_point) > best_len:
                    best_len = len(mount_point)
                    best_fstype = fstype
    except OSError:
        return None
    return best_fstype


def _warn_if_ineffective_drop(path: Path) -> None:
    """Linux only. POSIX_FADV_DONTNEED only evicts the Linux page cache.
    Warn when the drop target lives on a filesystem where that is not enough
    to give a cold cache for the next benchmark run:

    - ZFS: normal reads go through its own ARC, which fadvise cannot touch,
      so the data stays warm.
    - tmpfs: RAM-backed, so there is no backing store to evict to and
      fadvise is a no-op; the data is always warm.

    Native page-cache filesystems (ext4, xfs, btrfs, ...) drop correctly and
    get no warning.
    """
    fstype = _filesystem_type(path)
    if fstype == "zfs":
        print(
            f"warning: {path} is on ZFS. POSIX_FADV_DONTNEED does not evict"
            " the ZFS ARC, so the data stays cached and 'cold-cache' runs"
            " are effectively warm-cache. For cold reads, set 'zfs set"
            " primarycache=metadata <dataset>' (needs root), shrink"
            " zfs_arc_max, or point --save-path at a non-ZFS filesystem."
            " Run 'df -hT' to find alternative volumes to test on."
        )
    elif fstype == "tmpfs":
        print(
            f"warning: {path} is on tmpfs (RAM-backed). POSIX_FADV_DONTNEED"
            " is a no-op there -- there is no backing store to evict to, so"
            " the data is always warm and 'cold-cache' runs are effectively"
            " warm-cache. Point --save-path at a real on-disk, non-ZFS"
            " filesystem (ext4/xfs/btrfs) for cold reads. Run 'df -hT' to"
            " find alternative volumes to test on."
        )


RATE_LINE_RE = re.compile(
    r"rate sent:\s*([\d.]+)\s*MB/s\s+received:\s*([\d.]+)\s*MB/s"
)


def parse_test_out(path: Path) -> tuple[float, float]:
    """Return (sent_rate, received_rate) in MB/s parsed from connection_tester's
    'rate sent: X MB/s received: Y MB/s' summary line. connection_tester
    computes this against its own transfer window, which excludes the startup
    spent building v2 merkle trees, so it is a real throughput rate usable
    as-is (no further division by runtime).
    """
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return 0.0, 0.0
    m = RATE_LINE_RE.search(text)
    if not m:
        return 0.0, 0.0
    return float(m.group(1)), float(m.group(2))


def parse_max_rss(path: Path) -> float:
    """Scan a memory_stats.log (written by vmstat.print_output_to_file) and
    return the peak RSS in bytes reached during the run. The file is a
    whitespace-separated table whose first line names the columns; the 'rss'
    column holds resident set size in bytes. Returns 0.0 if the file or the
    column is missing.
    """
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            header = f.readline().split()
            if "rss" not in header:
                return 0.0
            col = header.index("rss")
            peak = 0.0
            for line in f:
                fields = line.split()
                if len(fields) <= col:
                    continue
                try:
                    val = float(fields[col])
                except ValueError:
                    continue
                peak = max(peak, val)
            return peak
    except OSError:
        return 0.0


# stable display order: backends are table rows, variants are columns
_VARIANT_ORDER = {"v1": 0, "v2": 1, "hybrid": 2}
_BACKEND_ORDER = {"mmap": 0, "pread": 1, "posix": 2}


def render_pivot(
    mode_results: list[dict], rate_field: str, transfer_mode: str
) -> str:
    """Render a `disk_io_backend X torrent_variant` matrix of `rate_field`
    as an RST simple table. Backends not yet tested in this mode are
    omitted; cells for untested (backend, variant) pairs show '-'.

    Each rate cell is an anonymous hyperlink reference (`120.00`__) to that
    test's summary page (`<transfer_mode>-<variant>-<backend>/summary.html`,
    written by write_test_summary). The matching anonymous targets are
    emitted after the table to keep the cells (and thus the column widths)
    narrow. Anonymous references resolve against anonymous targets in
    document order, so the targets are listed in the same row-major order
    the references appear in; duplicate URLs (the two dual-mode tables both
    point at the same test dir) are fine.
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
    targets = []  # anonymous hyperlink targets, in reference (row-major) order
    for b in backends:
        row = [b]
        for v in variants:
            rate = by_key.get((b, v))
            if rate is None:
                row.append("-")
            else:
                row.append(f"`{rate:.2f}`__")
                targets.append(f"__ {transfer_mode}-{v}-{b}/summary.html")
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
    table = "\n".join(lines) + "\n"
    if targets:
        table += "\n" + "\n".join(targets) + "\n"
    return table


def render_chart(
    mode_results: list[dict],
    rate_field: str,
    title: str,
    out_path: Path,
    ylabel: str = "MB/s",
    *,
    palette: str | None = None,
    baseline: float | None = None,
) -> bool:
    """Render a grouped bar chart of `rate_field` (backends along the x
    axis, one bar per torrent variant within each group) to `out_path` as
    a PNG. Mirrors render_pivot's stable ordering. Returns True on success,
    False if matplotlib is not installed -- in which case the caller skips
    emitting the image so the report stays valid without the dependency.

    `palette` names a matplotlib colormap to draw the per-variant bar colors
    from (defaults to matplotlib's property-cycle colors).
    `baseline`, when set, pins the y-axis lower limit (the bars are drawn
    from 0 but clipped to this limit, so they appear anchored to it instead
    of to matplotlib's autoscaled floor).
    """
    try:
        import matplotlib
        matplotlib.use("Agg")  # headless backend; no display required
        import matplotlib.pyplot as plt
    except ImportError:
        return False

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

    n_variants = max(len(variants), 1)
    group_width = 0.8
    bar_width = group_width / n_variants
    x_base = list(range(len(backends)))

    # draw the per-variant colors from `palette` when given, sampling it
    # evenly across the variants; otherwise fall back to matplotlib's
    # default property-cycle colors.
    cmap = plt.get_cmap(palette) if palette else None

    fig, ax = plt.subplots(figsize=(max(6.0, len(backends) * 1.6), 4.0))
    for i, variant in enumerate(variants):
        offsets = [
            x + (i - (n_variants - 1) / 2) * bar_width for x in x_base
        ]
        heights = [by_key.get((b, variant)) or 0.0 for b in backends]
        color = cmap((i + 0.5) / n_variants) if cmap else None
        bars = ax.bar(offsets, heights, bar_width, label=variant, color=color)
        # blank labels on absent (or zero) cells, matching the table's '-'
        if hasattr(ax, "bar_label"):
            labels = [f"{h:.1f}" if h else "" for h in heights]
            ax.bar_label(bars, labels=labels, padding=2, fontsize=7)

    if baseline is not None:
        ax.set_ylim(bottom=baseline)

    ax.set_xticks(x_base)
    ax.set_xticklabels(backends)
    ax.set_xlabel("disk I/O backend")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend(title="variant")
    fig.tight_layout()
    fig.savefig(out_path, dpi=100)
    plt.close(fig)
    return True


def build_config_header(
    save_path: Path,
    download_peers: int,
    upload_peers: int,
    num_pieces: int,
    disk_cache_mib: int,
    aio_threads: int,
    hasher_threads: int,
) -> str:
    """Render an RST field list describing the benchmark configuration, so
    results.rst is self-describing. The filesystem reported is the one
    backing the payload (save_path), since that is what the disk I/O hits.
    """
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
        f":Download peers: {download_peers}",
        f":Upload peers: {upload_peers}",
        f":Torrent size: {total_mib} MiB ({num_pieces} pieces x"
        f" {PIECE_SIZE_MIB} MiB, {NUM_FILES} files)",
        f":Disk cache: {disk_cache_mib} MiB (max_queued_disk_bytes)",
        f":AIO threads: {aio_threads} (aio_threads)",
        f":Hasher threads: {hasher_threads} (hashing_threads)",
        "",
        "",
    ]
    return "\n".join(lines)


def update_results(
    results: list[dict],
    benchmarks_dir: Path,
    transfer_mode: str,
    variant: str,
    io_backend: str,
    upload_rate: float,
    download_rate: float,
    max_rss_bytes: float,
    read_latency_p95_ms: float,
    config_header: str,
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
        "max_rss_mib": max_rss_bytes / (1024 * 1024),
        "read_latency_p95_ms": read_latency_p95_ms,
    })

    by_mode: dict[str, list[dict]] = {}
    for r in results:
        by_mode.setdefault(r["transfer_mode"], []).append(r)

    # one mode-section per transfer mode that has results. download- and
    # upload-only modes get a single matrix for the rate that mode
    # actually exercises; dual mode gets one matrix per direction.
    sections = [
        ("download", "Download only",
         [("Download rate (MB/s)", "download_rate_mbps")]),
        ("upload", "Upload only",
         [("Upload rate (MB/s)", "upload_rate_mbps")]),
        ("dual", "Upload & Download", [
            ("Download rate (MB/s)", "download_rate_mbps"),
            ("Upload rate (MB/s)", "upload_rate_mbps"),
        ]),
    ]

    doc_parts: list[str] = [config_header]
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
            doc_parts.append(render_pivot(mode_results, rate_field, mode_key))
            doc_parts.append("\n")
            # a single-direction mode (download/upload) needs no direction
            # suffix; dual mode has two charts, so disambiguate by direction.
            direction = rate_field[: -len("_rate_mbps")]
            slug = mode_key if len(subs) == 1 else f"{mode_key}_{direction}"
            chart_name = f"chart_{slug}.png"
            if render_chart(
                mode_results, rate_field, sub_heading,
                benchmarks_dir / chart_name,
            ):
                doc_parts.append(
                    f".. image:: {chart_name}\n"
                    f"   :alt: {sub_heading}\n\n"
                )
        # peak RSS matrix for this mode -- memory rather than throughput.
        mem_heading = "Max memory usage (MiB RSS)"
        doc_parts.append(f"{mem_heading}\n{'-' * len(mem_heading)}\n\n")
        doc_parts.append(render_pivot(mode_results, "max_rss_mib", mode_key))
        doc_parts.append("\n")
        mem_title = f"{heading}: {mem_heading}"
        mem_chart = f"chart_{mode_key}_rss.png"
        if render_chart(
            mode_results, "max_rss_mib", mem_title,
            benchmarks_dir / mem_chart, ylabel="MiB",
            palette="viridis", baseline=1.0,
        ):
            doc_parts.append(
                f".. image:: {mem_chart}\n"
                f"   :alt: {mem_title}\n\n"
            )
        # disk read-latency matrix for this mode. The cell value is the peak
        # interval p95 read latency (from disk_latency.collect_latency); it is
        # only nonzero for disk-latency-stats builds and backends that have a
        # disk job queue (mmap, pread -- not posix).
        lat_heading = "Disk read latency (peak p95, ms)"
        doc_parts.append(f"{lat_heading}\n{'-' * len(lat_heading)}\n\n")
        doc_parts.append(
            render_pivot(mode_results, "read_latency_p95_ms", mode_key)
        )
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
    """Warn early about sysctls that prevent perf record / perf script from
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
            " resolve in perf script.\n"
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


def check_matplotlib() -> None:
    """Warn once, up front, if matplotlib is missing. Without it render_chart
    silently skips the per-mode bar charts; the RST/HTML tables are still
    produced. find_spec avoids importing the (heavy) module just to probe.
    """
    import importlib.util
    if importlib.util.find_spec("matplotlib") is None:
        print(
            "warning: matplotlib is not installed; result charts will not be"
            " generated (the tables in results.rst/results.html are still"
            " produced).\n"
            "    install it with: pip install matplotlib\n"
        )


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
        "--results-path",
        default=".",
        type=Path,
        help="Directory to write benchmark results and per-test logs to"
        " (in a 'benchmarks' subdirectory). Defaults to the current working"
        " directory, independent of --save-path -- so the payload data can"
        " live on a fast/non-ZFS volume while results stay in the repo.",
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
        "--modes",
        nargs="+",
        choices=MODES,
        default=MODES,
        help="Transfer modes to test. 'download' measures the client"
        " downloading from peers, 'upload' measures it seeding to peers, and"
        " 'dual' runs both directions at once. Each selected mode is run"
        " through the full variant/backend matrix.",
    )
    p.add_argument(
        "--num-pieces",
        type=int,
        default=10000,
        help="Number of pieces in the generated test torrent."
        " Piece size is 1 MiB, so total size is num_pieces MiB.",
    )
    p.add_argument(
        "--disk-cache-size",
        type=int,
        default=100,
        help="Disk write cache size in MiB -- libtorrent's"
        " max_queued_disk_bytes setting (the max bytes queued for write"
        " before the download rate is throttled).",
    )
    p.add_argument(
        "--aio-threads",
        type=int,
        default=5,
        help="Number of disk I/O threads -- libtorrent's aio_threads"
        " setting.",
    )
    p.add_argument(
        "--hasher-threads",
        type=int,
        default=2,
        help="Number of disk threads used for piece hashing -- libtorrent's"
        " hashing_threads setting.",
    )

    args = p.parse_args()

    check_perf_environment()
    check_matplotlib()

    subprocess.check_call(
        [
            "b2",
            "release",
            "debug-symbols=on",
            "cxxflags=-fno-omit-frame-pointer",
            "disk-latency-stats=on",
            args.toolset,
            "stage_client_test",
        ],
        cwd=EXAMPLES_DIR,
    )
    subprocess.check_call(
        ["b2", "release", args.toolset, "stage_connection_tester"], cwd=EXAMPLES_DIR
    )

    for variant in args.variants:
        if not torrent_path(variant, args.num_pieces).exists():
            subprocess.check_call(
                [
                    str(EXAMPLES_DIR / f"connection_tester{exe}"),
                    "gen-torrent",
                    "-s",  # num pieces
                    str(args.num_pieces),
                    "-n",  # num files
                    str(NUM_FILES),
                    "-V",  # metadata version
                    VARIANT_FLAGS[variant],
                    "-t",  # output torrent file
                    str(torrent_path(variant, args.num_pieces)),
                ],
            )

    rm_file_or_dir(Path("t"))

    results: list[dict] = []

    config_header = build_config_header(
        args.save_path, args.download_peers, args.upload_peers,
        args.num_pieces, args.disk_cache_size, args.aio_threads,
        args.hasher_threads,
    )
    max_queued_disk_bytes = args.disk_cache_size * 1024 * 1024

    for variant in args.variants:
        torrent = torrent_path(variant, args.num_pieces)
        data_dir = payload_dir(args.save_path, variant, args.num_pieces)
        # whether the canonical upload payload + resume data (from gen_data)
        # for this variant is currently intact on disk. The payload depends
        # only on the variant, not the io_backend, so once generated it is
        # reused across backends for the upload test. We iterate modes outer
        # and backends inner (below), so all of this variant's upload runs are
        # consecutive: gen_data runs exactly once per variant, never once per
        # backend. We only ever reuse data gen_data produced -- never a
        # download/dual test's output, which could be an incomplete download
        # whose resume data would trigger a cache-warming recheck.
        upload_payload_ready = False
        # modes outer, backends inner. Run the selected modes in MODES order so
        # the matrix is filled in a stable, predictable sequence regardless of
        # the order they were passed on the command line.
        for mode in MODES:
            if mode not in args.modes:
                continue
            for io_backend in args.io_backends:
                if mode == "download":
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
                        results_path=args.results_path,
                        config_header=config_header,
                        max_queued_disk_bytes=max_queued_disk_bytes,
                        aio_threads=args.aio_threads,
                        hasher_threads=args.hasher_threads,
                        transfer_mode="download",
                        variant=variant,
                        io_backend=io_backend,
                    )
                elif mode == "dual":
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
                        results_path=args.results_path,
                        config_header=config_header,
                        max_queued_disk_bytes=max_queued_disk_bytes,
                        aio_threads=args.aio_threads,
                        hasher_threads=args.hasher_threads,
                        transfer_mode="dual",
                        variant=variant,
                        io_backend=io_backend,
                    )
                else:  # upload
                    # the upload test needs a complete, canonical payload to
                    # serve. gen-data -R writes the payload and a resume file
                    # claiming every piece is present, so client_test comes up
                    # in the seeding state without a startup check pass (which
                    # would warm the page cache we want cold).
                    if upload_payload_ready:
                        # the first upload run for this variant already
                        # generated the canonical payload + resume; reuse it
                        # and just start from a fresh session. run_test still
                        # drops the page cache, so this run is cold despite
                        # reusing the on-disk data.
                        rm_file_or_dir(Path(".ses_state"))
                    else:
                        reset_download(
                            args.save_path, variant, args.num_pieces
                        )
                        gen_data(torrent, args.save_path, with_resume=True)
                        upload_payload_ready = True
                    run_test(
                        f"upload-{variant}-{io_backend}",
                        "download",
                        ["-e", "240", "-i", io_backend],
                        args.upload_peers,
                        args.save_path,
                        torrent,
                        data_dir,
                        results=results,
                        results_path=args.results_path,
                        config_header=config_header,
                        max_queued_disk_bytes=max_queued_disk_bytes,
                        aio_threads=args.aio_threads,
                        hasher_threads=args.hasher_threads,
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
    results_path: Path,
    config_header: str,
    max_queued_disk_bytes: int,
    aio_threads: int,
    hasher_threads: int,
    transfer_mode: str,
    variant: str,
    io_backend: str,
) -> None:
    benchmarks_dir = (results_path / "benchmarks").resolve()
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
        # these come after "-k" on the command line, so they override the
        # high-performance-seed preset's values.
        f"--max_queued_disk_bytes={max_queued_disk_bytes}",
        f"--aio_threads={aio_threads}",
        f"--hashing_threads={hasher_threads}",
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

        # stop the client cleanly before reading counters.log -- otherwise
        # parse_session_stats may catch a half-written `session stats (`
        # line and crash on the split. SIGINT triggers client_test's
        # normal shutdown, which flushes and closes the stats file.
        c.send_signal(signal.SIGINT)
        c.wait()

    print(f"runtime {end-start:0.2f} seconds")

    sent_rate, recv_rate = parse_test_out(output_dir / "test.out")
    # connection_tester reports rates from its own perspective: bytes it sent
    # are what the client downloaded, bytes it received are what the client
    # uploaded. The rate already excludes connection_tester's startup (the v2
    # merkle-tree build), so use it directly.
    download_rate = sent_rate
    upload_rate = recv_rate
    max_rss_bytes = parse_max_rss(output_dir / "memory_stats.log")
    # summarize disk read-job latency over the run and render the per-test
    # median/avg/p95 plot (embedded in summary.html). The peak interval p95 is
    # the single number that goes into the results latency table.
    latency = disk_latency.collect_latency(output_dir / "counters.log")
    disk_latency.plot_latency(
        latency, output_dir / "disk_read_latency.png",
        title=f"{name}: disk read latency",
    )
    update_results(
        results, benchmarks_dir, transfer_mode, variant, io_backend,
        upload_rate, download_rate, max_rss_bytes, latency.peak_p95_ms,
        config_header,
    )

    # perf-based profiling is Linux only
    if platform.system() == "Linux":
        print("generating call tree report...")
        perf_call_tree.main(
            output_dir / "perf.data", output_dir / "profile.html", 0.1
        )

    parse_session_stats.main(output_dir / "counters.log", 8, output_dir)

    # write the per-test summary page that results.rst links to. Done last so
    # the artifacts it references (index.html, vmstat plots, profile.html)
    # already exist.
    write_test_summary(output_dir, name)


def write_test_summary(output_dir: Path, name: str) -> None:
    """Write summary.html into a test run's output_dir. It links the session
    stats page (index.html, from parse_session_stats), embeds the vmstat
    plots, and links the raw logs. results.rst points each rate cell here.
    Artifacts that are absent (e.g. plots when gnuplot is missing, or
    profile.html off Linux) are simply skipped.
    """
    parts = [
        "<!doctype html>",
        "<html><head><meta charset='utf-8'>",
        f"<title>{name}</title></head><body>",
        f"<h1>{name}</h1>",
    ]

    links = []
    if (output_dir / "index.html").exists():
        links.append(("index.html", "session stats"))
    for fname, label in (
        ("events.log", "events.log (libtorrent alerts)"),
        ("test.out", "test.out (connection_tester output)"),
        ("profile.html", "profile.html (call tree)"),
    ):
        if (output_dir / fname).exists():
            links.append((fname, label))
    if links:
        parts.append("<ul>")
        parts += [f'<li><a href="{href}">{label}</a></li>' for href, label in links]
        parts.append("</ul>")

    # vmstat plots, embedded (see plot_output() in vmstat.py for the names),
    # plus the disk read-latency plot (from disk_latency.plot_latency)
    plots = [
        ("disk_read_latency.png", "disk read latency (median/avg/p95)"),
        ("memory_stats.log-memory.png", "memory usage"),
        ("memory_stats.log-vm.png", "vm stats"),
        ("memory_stats.log-read.png", "disk read"),
        ("memory_stats.log-write.png", "disk write"),
    ]
    imgs = [(img, alt) for img, alt in plots if (output_dir / img).exists()]
    if imgs:
        parts.append("<h2>resource usage</h2>")
        parts += [
            f'<p><img src="{img}" alt="{alt}" style="max-width:100%"></p>'
            for img, alt in imgs
        ]

    parts.append("</body></html>")
    (output_dir / "summary.html").write_text("\n".join(parts))


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
