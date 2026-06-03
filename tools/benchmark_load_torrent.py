#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

"""benchmark_load_torrent - generate "torture" .torrent files and benchmark
the time and memory cost of loading them with tools/benchmark_load_torrent
(the C++ companion to this script).

For each case the script:
  1. generates a torrent with tools/gen_torture_torrent.
  2. runs benchmark_load_torrent <torrent> <iterations> under the
     platform's sampling profiler:
       - Linux: perf record; the resulting perf.data is rendered into
         a per-case CPU call tree (profile.html) via
         tools/perf_call_tree.py.
       - macOS: xctrace; the resulting run.trace bundle is kept as-is
         and linked from the report for hand-off to Instruments
         (no HTML call tree -- perf_call_tree.py only consumes
         perf.data and collapsed-stack output).
  3. on Linux, runs the case a second time under heaptrack and renders
     a peak-cost allocation call tree (heap.html) -- also via
     perf_call_tree.py, but from heaptrack's flamegraph output.

benchmark_load_torrent prints per-iteration timing and peak RSS (via
getrusage), so we don't have to sample /proc ourselves.

A summary report (report.html) is written under ./load-torrent/ in the
current working directory, with one row per (case, variant) linking to
the per-case directory.

All cases stay within the load_torrent_limits defaults.
"""

from argparse import ArgumentParser
from dataclasses import dataclass
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Callable

import matplotlib

matplotlib.use("Agg")  # headless backend; no display required
import matplotlib.pyplot as plt  # noqa: E402

# perf_call_tree lives in this directory; tooling for the call-tree HTML
# is the only thing we still import out of tools/. The path insertion has
# to happen before the import, hence the E402 noqa marker.
SCRIPT_DIR = Path(__file__).parent.resolve()
sys.path.insert(0, str(SCRIPT_DIR))

import perf_call_tree  # noqa: E402

ROOT_DIR = SCRIPT_DIR.parent
TOOLS_DIR = ROOT_DIR / "tools"

EXE_SUFFIX = ".exe" if platform.system() == "Windows" else ""


# ----------------------------------------------------------------------------
# test matrix
# ----------------------------------------------------------------------------

# Default load_torrent_limits (from include/libtorrent/torrent_info.hpp):
#   max_buffer_size       = 10 MB
#   max_pieces            = 0x200000 (~2.1M)
#   max_decode_depth      = 100
#   max_decode_tokens     = 3,000,000
#   max_duplicate_filenames = 500   (per-resolution-pass collision count,
#                                    which grows quadratically with the
#                                    number of duplicates of one name)
#   max_directory_depth   = 100
#
# Each case below loads cleanly under those defaults. A few of the
# heaviest cases would exceed max_decode_tokens or the bdecode depth
# limit in some versions (e.g. flat_huge in hybrid); those skip the
# version that wouldn't fit via the `versions` field on Case.


@dataclass(frozen=True)
class Case:
    name: str
    description: str
    # extra flags to gen_torture_torrent, in addition to --version and --output
    flags: list[str]
    # versions to run this case in. Some cases drop a variant because the
    # combined metadata wouldn't fit under the default load_torrent_limits
    # (e.g. "flat_huge" omits hybrid: v1 + v2 together exceed
    # max_decode_tokens).
    versions: tuple[str, ...] = ("v1", "v2", "hybrid")
    # per-case iteration count override; 0 means "use the global
    # ITERATIONS default" (sentinel, so the default can sit before
    # ITERATIONS is defined later in the file).
    iterations: int = 0


def mk_case(name: str, desc: str, flags: str, **kw: object) -> Case:
    """Constructor sugar: `flags` is a single whitespace-separated string
    instead of a list (so each Case entry fits on a few lines), and
    `versions` / `iterations` overrides come through **kw."""
    return Case(name, desc, flags.split(), **kw)  # type: ignore[arg-type]


CASES: list[Case] = [
    mk_case(
        "baseline",
        "Vanilla 100-file torrent, small everything. Establishes the floor.",
        "--num-files 100 --file-size 64K",
    ),
    mk_case(
        "many_pieces",
        "One large file split into ~65k 16 KiB pieces. Exercises the"
        " per-piece SHA-1 / merkle-root copy and (for v2) the piece-layers"
        " section.",
        "--num-files 1 --file-size 1G --piece-size 16K",
    ),
    mk_case(
        "merkle_padding",
        "500 files each sized one block above a power-of-2 number of"
        " blocks (257 blocks = 4 MiB + 16 KiB per file). v2 builds a"
        " merkle tree per file and pads the leaf count up to the next"
        " power of 2 (512 here), so every tree carries as much padding"
        " as real data -- the worst-case ratio for per-file merkle"
        " allocation. v1 has no per-file merkle tree and is unaffected;"
        " it's included for contrast.",
        "--num-files 500 --file-size 4210688 --piece-size 16K",
    ),
    mk_case(
        "many_files",
        "5000 small files. Exercises file-entry parsing and (for v2) the"
        " v2 file tree dict walk.",
        "--num-files 5000 --file-size 16K",
    ),
    mk_case(
        "many_pad_files",
        "5000 small files whose size (100000 B) is not a piece-size"
        " multiple, so hybrid mode inserts a pad file between every pair"
        " of files. v1 ends up with ~10000 file entries and every path"
        " goes through extract_single_file's '.pad/<size>' synthesis +"
        " bitcomet-pad substring scan. v1-only and v2-only have no pad"
        " files (v2 uses per-file merkle trees instead of a flat piece"
        " stream), so only hybrid is informative here.",
        "--num-files 5000 --file-size 100000 --piece-size 1M",
        versions=("hybrid",),
    ),
    mk_case(
        "long_dirs",
        "1000 files under 4 nested directories whose names are 200 chars"
        " long. Exercises path sanitization and string handling on every"
        " path component.",
        "--num-files 1000 --dir-depth 4 --branching 1 --dir-name-len 200"
        " --file-size 16K",
    ),
    mk_case(
        "wide_tree",
        "1000 files spread across 5^3 = 125 leaves of a branching=5 tree."
        " Exercises directory-tree walking with realistic branching.",
        "--num-files 1000 --dir-depth 3 --branching 5 --file-size 16K",
    ),
    mk_case(
        "deep_v2_tree",
        "90 files in a single leaf at the bottom of a 90-deep path"
        " (close to the default max_directory_depth of 100). Exercises"
        " the explicit parse stack in extract_files2(). v2-only.",
        "--num-files 90 --dir-depth 90 --branching 1 --file-size 16K",
        versions=("v2",),
    ),
    mk_case(
        "extension_truncation",
        "1000 files with 260-char names ending in '.txt'. Every file's"
        " path component crosses the 240-char threshold in"
        " sanitize_append_path_element(), which scans the last 10 bytes"
        " for an extension and replays the loop with the extension"
        " appended. No existing case puts a '.' in the long-name window,"
        " so this is the only one that takes that branch.",
        "--num-files 1000 --file-size 16K --file-name-len 260"
        " --file-name-suffix .txt",
    ),
    mk_case(
        "non_utf8_paths",
        "5000 small files whose name padding is the invalid-utf8 byte"
        " 0xFF. Every byte of every path component takes the 'invalid"
        " utf8 sequence, replace with _' branch of"
        " sanitize_append_path_element(). Same shape as many_files but"
        " on the per-byte slow path; the delta isolates the cost of"
        " utf8 validation.",
        "--num-files 5000 --file-size 16K --file-name-len 64" " --invalid-utf8-names",
    ),
    mk_case(
        "many_trackers",
        "1000 synthetic tracker URLs. Exercises the tracker-list parser"
        " and is_valid_tracker_url() check.",
        "--num-files 10 --file-size 16K --num-trackers 1000",
    ),
    mk_case(
        "many_announce_tiers",
        "1000 tracker URLs distributed one-per-tier across 1000 tiers."
        " many_trackers puts all URLs in tier 0 (a single inner list);"
        " this case maximizes the outer iteration in"
        " load_torrent.cpp's announce-list parser instead. The torrent"
        " body is otherwise tiny, so the variant column is uninformative"
        " and only v1 is run.",
        "--num-files 10 --file-size 16K --num-trackers 1000" " --num-tiers 1000",
        versions=("v1",),
    ),
    mk_case(
        "many_url_seeds",
        "2000 unique url-list (BEP 19) entries. Exercises the"
        " std::set<string_view> dedup loop in load_torrent.cpp plus"
        " maybe_url_encode() per URL. Top-level metadata, v1 only.",
        "--num-files 10 --file-size 16K --num-url-seeds 2000",
        versions=("v1",),
    ),
    mk_case(
        "many_dht_nodes",
        "2000 DHT bootstrap nodes in the top-level 'nodes' list."
        " Exercises the per-entry type-check + (host, port) push-back"
        " loop in load_torrent.cpp. v1 only -- the field is version"
        " independent.",
        "--num-files 10 --file-size 16K --num-dht-nodes 2000",
        versions=("v1",),
    ),
    mk_case(
        "many_similar",
        "2000 'similar' info-hashes (BEP 38). Exercises the"
        " per-entry length / type check loop in parse_torrent_file()"
        " and the m_similar_torrents offset push-back in"
        " torrent_info::parse_info_section_impl. v1 only.",
        "--num-files 10 --file-size 16K --num-similar 2000",
        versions=("v1",),
    ),
    mk_case(
        "many_collections",
        "2000 'collection' strings (BEP 38). Exercises the"
        " m_collections offset+length push-back loop. v1 only.",
        "--num-files 10 --file-size 16K --num-collections 2000",
        versions=("v1",),
    ),
    mk_case(
        "long_comment",
        "1 MiB 'comment' + 1 MiB 'created by' fields. Both go through"
        " aux::verify_encoding() in parse_torrent_file(), which scans"
        " the string byte-by-byte for utf-8 validity. Isolates that"
        " cost from everything else. v1 only.",
        "--num-files 10 --file-size 16K --comment-len 1048576"
        " --created-by-len 1048576",
        versions=("v1",),
    ),
    mk_case(
        "many_symlinks",
        "1100 files of which the last 1000 are symlinks pointing at file"
        " 0. Exercises the symlink-target sanitize/match path.",
        "--num-files 1100 --num-symlinks 1000 --file-size 16K",
    ),
    mk_case(
        "symlink_chain",
        "1100 files of which the last 1000 are symlinks chained back to"
        " the last regular file: symlink i targets file (i-1). All but"
        " the first one resolve through another symlink, which is the"
        " only path that enters the second pass of"
        " file_storage::sanitize_symlinks() with its dir_links walk"
        " and per-entry std::set<std::string> traversed.",
        "--num-files 1100 --num-symlinks 1000 --symlink-mode chain" " --file-size 16K",
    ),
    mk_case(
        "symlink_to_dir",
        "1100 files in a single subdirectory, of which the last 1000 are"
        " symlinks targeting that directory rather than a file."
        " Forces sanitize_symlinks() into the is_directory() branch,"
        " which lazily builds a sorted view of m_paths and does"
        " lower_bound for every symlink. None of the other cases"
        " trigger that branch.",
        "--num-files 1100 --num-symlinks 1000 --symlink-mode dir"
        " --dir-depth 1 --branching 1 --file-size 16K",
    ),
    mk_case(
        "many_duplicates",
        "30 file entries whose filenames differ only in capitalization."
        " They are distinct dict keys in v2's file tree but collide under"
        " libtorrent's case-insensitive resolve_duplicate_filenames(), so"
        " all three variants exercise the rename path. Collisions grow"
        " quadratically with the number of dups, so 30 gives ~406"
        " collisions (under the default 500-collision limit).",
        "--num-files 1000 --file-size 16K --num-duplicates 30",
    ),
    mk_case(
        "huge_files",
        "60000 files in 8 nested directories with 100-char names. The"
        " heaviest case in the matrix: combines a large file count, deep"
        " nesting and long path components. v1 metadata reaches ~50 MiB"
        " (each path is encoded per file); v2 is much smaller (~5 MiB)"
        " because the directory names are shared in the file-tree dict.",
        "--num-files 60000 --dir-depth 8 --branching 1 --dir-name-len 100"
        " --file-size 16K",
        iterations=20,
    ),
    mk_case(
        "flat_huge",
        "200000 small files in a single directory. Stresses raw file-"
        "entry parsing and string allocation at scale. Hybrid is omitted"
        " because the v1+v2 metadata together exceed the default"
        " max_decode_tokens (3M) bdecode limit.",
        "--num-files 200000 --file-name-len 30 --file-size 16K",
        versions=("v1", "v2"),
        iterations=20,
    ),
]


# ----------------------------------------------------------------------------
# binary build + stage
# ----------------------------------------------------------------------------


def build_binaries() -> tuple[Path, Path]:
    """Build and stage gen_torture_torrent and benchmark_load_torrent.
    The tools/Jamfile 'install stage' rule uses `<location>.`, so b2
    places each binary at the well-known path next to the Jamfile rather
    than buried under bin/<toolset>/... -- no glob needed to find it.
    """
    subprocess.check_call(["b2", "release", "debug-symbols=on", "stage"], cwd=TOOLS_DIR)
    gen = TOOLS_DIR / f"gen_torture_torrent{EXE_SUFFIX}"
    bench = TOOLS_DIR / f"benchmark_load_torrent{EXE_SUFFIX}"
    for path in (gen, bench):
        if not path.exists():
            raise SystemExit(
                f"ERROR: b2 stage did not produce {path}." " Check the b2 output above."
            )
    return gen, bench


# ----------------------------------------------------------------------------
# running one test
# ----------------------------------------------------------------------------

# Fixed iteration count per case. benchmark_load_torrent parses the
# in-memory buffer this many times, so the timing averages over the same
# workload across cases and the profiler always sees the same shape of
# run.
ITERATIONS = 200

# matches the lines benchmark_load_torrent prints, e.g.
#   "load time: 134.567 ms total over 50 iterations (2.691 ms/iter)"
#   "max rss: 5242880 bytes"
TIME_LINE_RE = re.compile(
    r"load time:\s*([\d.]+)\s*ms"
    r" total over\s*(\d+)\s*iterations"
    r"\s*\(([\d.]+)\s*ms/iter\)"
)
MAX_RSS_LINE_RE = re.compile(r"max rss:\s*(\d+)\s*bytes")


@dataclass
class Result:
    case: str
    variant: str
    torrent_size: int = 0
    per_iter_ms: float = 0.0
    total_ms: float = 0.0
    max_rss_bytes: float = 0.0
    profile_html: str = ""  # relative path from report dir, or "" if unavailable
    heap_html: str = ""  # relative path to heap.html, or "" if unavailable
    output_dir: str = ""  # relative path from report dir
    error: str = ""


def generate_torrent(gen: Path, case: Case, variant: str, torrent_path: Path) -> None:
    """Run gen_torture_torrent for one (case, variant) into torrent_path."""
    cmd = [
        str(gen),
        "--version",
        variant,
        "--output",
        str(torrent_path),
        "--name",
        f"{case.name}_{variant}",
    ] + case.flags
    print(f"  generating: {' '.join(case.flags)}")
    subprocess.check_call(cmd, stdout=subprocess.DEVNULL)


def wrap_with_profiler(cmd: list[str], output_dir: Path) -> tuple[list[str], str]:
    """Wrap a command with the platform's sampling profiler so the
    profiler launches the target instead of attaching to it after the
    fact -- launching avoids the attach-time race that loses samples (or
    fails outright) on a fast-finishing process.

    Returns (full_cmd, profiler_kind). profiler_kind is "perf", "xctrace"
    or "" (no profiler available / supported). On Linux/macOS the wrapper
    name (perf / xcrun) is checked here so a missing tool is reported
    before we try to run anything.
    """
    system = platform.system()
    if system == "Linux":
        if shutil.which("perf") is None:
            print(
                "  warning: 'perf' is not installed; skipping profile."
                " Install linux-tools-* (Debian/Ubuntu) or 'perf' (Fedora)."
            )
            return cmd, ""
        wrap = [
            "perf",
            "record",
            "-F",
            "999",
            "--call-graph",
            "dwarf,65528",
            "-o",
            str(output_dir / "perf.data"),
            "--",
        ]
        return wrap + cmd, "perf"
    if system == "Darwin":
        if shutil.which("xcrun") is None:
            print(
                "  warning: 'xcrun' is not installed; skipping profile."
                " Install Xcode Command Line Tools: xcode-select --install"
            )
            return cmd, ""
        # xctrace --launch starts the target itself (analogous to perf's
        # '-- cmd' form on Linux), so we don't have to attach to a PID
        # that may already have exited.
        wrap = [
            "xcrun",
            "xctrace",
            "record",
            "--template",
            "Time Profiler",
            "--output",
            str(output_dir / "run.trace"),
            "--launch",
            "--",
        ]
        return wrap + cmd, "xctrace"
    print(
        f"  note: no sampling profiler integration on {system};"
        " timing and memory will still be collected."
    )
    return cmd, ""


def run_heaptrack(
    bench: Path, torrent: Path, iterations: int, output_dir: Path
) -> tuple[Path, str]:
    """Run the benchmark a second time under heaptrack, generate a
    collapsed-stack flamegraph from the trace, and render an allocation
    call tree HTML. Returns (heap_html_path, error_message). On any
    failure, heap_html_path is set to a path that may not exist and the
    error string explains what happened.

    heaptrack hooks malloc via LD_PRELOAD. b2's `link=static` only
    statically links project/boost libraries; the runtime libc.so is
    still dynamic, so heaptrack works on the staged binary as-is.
    """
    heap_html = output_dir / "heap.html"

    # heaptrack -o PATH writes to PATH.zst (it appends the extension). We
    # let it write into the per-case output dir alongside everything else.
    trace_base = output_dir / "heaptrack"
    heap_cmd = [
        "heaptrack",
        "-o",
        str(trace_base),
        "--",
        str(bench),
        str(torrent),
        str(iterations),
    ]
    rc = subprocess.call(heap_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    if rc != 0:
        return heap_html, f"heaptrack exited with code {rc}"

    # heaptrack appends a compression suffix; find whatever it produced.
    traces = list(output_dir.glob("heaptrack*"))
    traces = [p for p in traces if p.suffix in (".zst", ".gz")]
    if not traces:
        return heap_html, "heaptrack produced no trace file"
    trace = traces[0]

    # ask heaptrack_print for the 'peak' cost type so the tree weights
    # each callsite by its contribution to peak heap usage rather than
    # by allocation count (the default). Peak is what aligns with the
    # 'max rss' number the benchmark already prints.
    # heaptrack_print writes the flamegraph data to the path given with
    # -F (not to stdout), so we capture stdout/stderr separately for
    # error reporting -- a wrong flag name or unknown cost-type value
    # otherwise gets silently merged into the flame file.
    flame = output_dir / "heap_flame.txt"
    proc = subprocess.run(
        [
            "heaptrack_print",
            "-F",
            str(flame),
            "--flamegraph-cost-type",
            "peak",
            str(trace),
        ],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0 or not flame.exists() or flame.stat().st_size == 0:
        trace.unlink(missing_ok=True)
        msg = proc.stderr.strip() or proc.stdout.strip() or "(no output)"
        return heap_html, (
            f"heaptrack_print failed (exit {proc.returncode}): {msg[-500:]}"
        )

    # let SystemExit from perf_call_tree propagate -- "no stacks found"
    # in heaptrack output is a real failure (heaptrack ran but produced
    # nothing usable) and shouldn't be hidden as a per-case warning.
    perf_call_tree.main_collapsed(
        flame, heap_html, 0.1, title=f"allocations: {output_dir.name}"
    )

    # heap.html is what we keep; the raw trace and intermediate
    # collapsed-stack file are tens of MB and add no information beyond
    # what the tree already shows.
    flame.unlink(missing_ok=True)
    trace.unlink(missing_ok=True)
    return heap_html, ""


def run_one(
    gen: Path,
    bench: Path,
    case: Case,
    variant: str,
    benchmarks_dir: Path,
    with_heaptrack: bool = False,
) -> Result:
    """Generate one (case, variant) torrent, benchmark it under the
    platform profiler, and return a Result. benchmark_load_torrent
    reports its own peak RSS via getrusage, so there is no separate
    sampling loop. Errors during torrent generation or loading are
    captured in result.error and the benchmark continues with the next
    case.
    """
    name = f"{variant}-{case.name}"
    output_dir = benchmarks_dir / name
    if output_dir.exists():
        shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    result = Result(case=case.name, variant=variant)
    result.output_dir = str(output_dir.relative_to(benchmarks_dir))

    torrent = output_dir / f"{name}.torrent"
    try:
        generate_torrent(gen, case, variant, torrent)
    except subprocess.CalledProcessError as e:
        result.error = f"generation failed (exit {e.returncode})"
        return result
    result.torrent_size = torrent.stat().st_size

    iterations = case.iterations or ITERATIONS
    cmd = [str(bench), str(torrent), str(iterations)]
    full_cmd, profiler_kind = wrap_with_profiler(cmd, output_dir)
    run_out_path = output_dir / "run.out"

    with open(run_out_path, "w+") as run_out:
        rc = subprocess.call(full_cmd, stdout=run_out, stderr=subprocess.STDOUT)

    run_text = run_out_path.read_text(errors="replace")
    if rc != 0:
        result.error = (
            f"benchmark_load_torrent exited with code {rc}.\n" + run_text[-2000:]
        )
        return result

    m = TIME_LINE_RE.search(run_text)
    if m:
        result.total_ms = float(m.group(1))
        result.per_iter_ms = float(m.group(3))
    m = MAX_RSS_LINE_RE.search(run_text)
    if m:
        result.max_rss_bytes = float(m.group(1))

    if profiler_kind == "perf":
        # generate the perf call tree HTML, then drop perf.data -- it's
        # the input to profile.html (already kept and linked from the
        # report) and would otherwise be tens of MB of redundant samples
        # per case. If perf_call_tree raises SystemExit (perf not
        # installed, perf script failed, perf.data has no samples, etc.)
        # we let it propagate -- those all mean something is genuinely
        # wrong and the benchmark should stop, not produce a report with
        # quietly missing profiles.
        perf_data = output_dir / "perf.data"
        if perf_data.exists() and perf_data.stat().st_size > 0:
            perf_call_tree.main(
                perf_data, output_dir / "profile.html", 0.1, interval=0.1
            )
            result.profile_html = str(
                (output_dir / "profile.html").relative_to(benchmarks_dir)
            )
        perf_data.unlink(missing_ok=True)
    elif profiler_kind == "xctrace":
        # xctrace writes a .trace bundle (a directory on macOS); we
        # don't post-process it. Linking the bundle from the report
        # makes it discoverable, and a click on the file:// URL hands
        # it to Instruments via Launch Services.
        trace_path = output_dir / "run.trace"
        if trace_path.exists():
            result.profile_html = str(trace_path.relative_to(benchmarks_dir))

    if with_heaptrack:
        heap_html, err = run_heaptrack(bench, torrent, iterations, output_dir)
        if err:
            print(f"  warning: heaptrack pass: {err}")
        elif heap_html.exists():
            result.heap_html = str(heap_html.relative_to(benchmarks_dir))

    return result


# ----------------------------------------------------------------------------
# report
# ----------------------------------------------------------------------------


def fmt_bytes(n: float) -> str:
    if n <= 0:
        return "-"
    if n < 1024:
        return f"{int(n)} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KiB"
    return f"{n / (1024 * 1024):.2f} MiB"


def fmt_ms(ms: float) -> str:
    if ms <= 0:
        return "-"
    return f"{ms:.3f} ms"


VARIANTS = ("v1", "v2", "hybrid")


def render_chart(
    results: list[Result],
    value_fn: Callable[[Result], float],
    title: str,
    ylabel: str,
    out_path: Path,
) -> None:
    """Render a grouped bar chart of `value_fn(r)` for each (case,
    variant), with one group per case along the x-axis and one bar per
    variant within each group. Bars for missing or zero values are
    omitted, which keeps the log y-axis clean (zero has no place on a
    log scale).
    """
    by_key = {(r.case, r.variant): r for r in results}
    case_names = [c.name for c in CASES]
    n_variants = len(VARIANTS)
    # Narrow bars (group spans 0.45 of its slot rather than the matplotlib
    # default of ~0.8); the inter-group gap is then group_pitch - group_width.
    # Pitch < 1.0 packs groups closer together than the natural integer-slot
    # spacing.
    group_width = 0.45
    bar_width = group_width / n_variants
    group_pitch = 0.615  # group_width + 0.165 gap

    fig, ax = plt.subplots(figsize=(max(8.0, len(case_names) * 1.2 * group_pitch), 4.5))

    for i, variant in enumerate(VARIANTS):
        offset = (i - (n_variants - 1) / 2) * bar_width
        xs: list[float] = []
        ys: list[float] = []
        for j, name in enumerate(case_names):
            r = by_key.get((name, variant))
            if r is None or r.error:
                continue
            v = value_fn(r)
            if v <= 0:
                continue
            xs.append(j * group_pitch + offset)
            ys.append(v)
        if xs:
            ax.bar(xs, ys, bar_width, label=variant)

    ax.set_yscale("log")
    ax.set_xticks([j * group_pitch for j in range(len(case_names))])
    ax.set_xticklabels(case_names, rotation=30, ha="right")
    # tighten the x-axis: groups sit at j*group_pitch with bars offset by at
    # most group_width/2; clamp to half a pitch beyond the first/last group
    # so matplotlib doesn't leave its default ~5% of empty space at the ends.
    ax.set_xlim(
        -group_pitch / 2,
        (len(case_names) - 1) * group_pitch + group_pitch / 2,
    )
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    # upper-left rather than the default best/upper-right -- the rightmost
    # cases tend to be the heaviest ones, and an auto-placed legend often
    # ends up sitting on top of their bars.
    ax.legend(title="variant", loc="upper left")
    # plain decimal tick labels ("1", "10", "0.1") instead of the
    # default "10^0", "10^1", "10^-1" -- easier to read at a glance.
    # '%g' strips trailing zeros and avoids scientific notation for
    # the range we care about here.
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda y, _: f"{y:g}"))
    # solid gridlines at each decade, lighter dotted lines at the
    # decade subdivisions (2,3,...,9) so bars are easier to read
    # between major ticks.
    ax.set_axisbelow(True)
    ax.grid(True, axis="y", which="major", linestyle="-", alpha=0.5)
    ax.grid(True, axis="y", which="minor", linestyle=":", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=100)
    plt.close(fig)


def write_report(benchmarks_dir: Path, results: list[Result], report_name: str) -> Path:
    """Render an HTML matrix (rows: case, cols: variant) of timing /
    memory / profile-link for every benchmarked (case, variant). Cells
    for combinations that weren't run (e.g. flat_huge in hybrid) show
    a dash, so the intentionally-skipped variants are visible in the
    table rather than mysteriously absent.
    """
    by_key = {(r.case, r.variant): r for r in results}

    parts: list[str] = [
        "<!doctype html>",
        "<html><head><meta charset='utf-8'>",
        "<title>libtorrent: load_torrent benchmark</title>",
        "<style>",
        " body { font-family: sans-serif; margin: 1.5em; }",
        " table { border-collapse: collapse; margin: 1em 0; }",
        " th, td { border: 1px solid #ccc; padding: 4px 8px; text-align: right; }",
        " th { background: #eee; }",
        " td.name, th.name { text-align: left; }",
        " td.err { color: #a00; text-align: left; font-style: italic; }",
        " td.case-desc { font-style: italic; color: #444; max-width: 50em; }",
        "</style>",
        "</head><body>",
        "<h1>libtorrent: load_torrent benchmark</h1>",
        f"<p>Host: {platform.platform()}</p>",
        f"<p>Iterations per case: {ITERATIONS} by default"
        " (the heaviest cases override this; the 'ms' cells are the"
        " per-parse average regardless).</p>",
    ]

    # three grouped bar charts (cases along x, one bar per variant per
    # group). All three metrics span enough range across cases (baseline
    # is orders of magnitude smaller than huge_files) that a log y-axis
    # keeps the small cases readable instead of crushing them into
    # invisible slivers next to the heavy ones; render_chart hard-codes
    # that.
    charts = [
        ("chart_time.png", "Time per load (ms)", "ms", lambda r: r.per_iter_ms),
        (
            "chart_rss.png",
            "Peak RSS (MiB)",
            "MiB",
            lambda r: r.max_rss_bytes / (1024 * 1024),
        ),
        (
            "chart_size.png",
            ".torrent file size (MiB)",
            "MiB",
            lambda r: r.torrent_size / (1024 * 1024),
        ),
    ]
    for fname, title, ylabel, fn in charts:
        render_chart(results, fn, title, ylabel, benchmarks_dir / fname)
        parts.append(f"<h2>{title}</h2>")
        parts.append(f"<p><img src='{fname}' alt='{title}' style='max-width:100%'></p>")

    # per-case details with profile links and full per-variant table
    parts.append("<h2>Per-case details</h2>")
    for case in CASES:
        parts.append(f"<h3>{case.name}</h3>")
        parts.append(f"<p class='case-desc'>{case.description}</p>")
        parts.append(
            "<p><code>gen_torture_torrent " + " ".join(case.flags) + "</code></p>"
        )
        parts.append("<table>")
        parts.append(
            "<tr><th>variant</th><th>.torrent size</th>"
            "<th>ms</th><th>peak RSS</th>"
            "<th>links</th></tr>"
        )
        for v in VARIANTS:
            r = by_key.get((case.name, v))
            if r is None:
                # combination was intentionally skipped (case.versions
                # excluded it). Emit a dash row so the omission is visible.
                parts.append(
                    f"<tr><td>{v}</td>" "<td>-</td><td>-</td><td>-</td><td>-</td></tr>"
                )
                continue
            if r.error:
                parts.append(
                    f"<tr><td>{v}</td>"
                    f"<td colspan='4' class='err'>{r.error}</td></tr>"
                )
                continue
            links = []
            if r.profile_html:
                links.append(f"<a href='{r.profile_html}'>profile</a>")
            if r.heap_html:
                links.append(f"<a href='{r.heap_html}'>heap</a>")
            run_log = Path(r.output_dir) / "run.out"
            if (benchmarks_dir / run_log).exists():
                links.append(f"<a href='{run_log.as_posix()}'>log</a>")
            parts.append(
                f"<tr><td>{v}</td>"
                f"<td>{fmt_bytes(r.torrent_size)}</td>"
                f"<td>{fmt_ms(r.per_iter_ms)}</td>"
                f"<td>{fmt_bytes(r.max_rss_bytes)}</td>"
                f"<td>{' '.join(links) if links else '-'}</td>"
                "</tr>"
            )
        parts.append("</table>")

    parts.append("</body></html>")
    report_path = benchmarks_dir / report_name
    report_path.write_text("\n".join(parts))
    return report_path


# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------


def main() -> None:
    # no flags; argparse is here for --help and to reject unknown args.
    ArgumentParser(description=__doc__).parse_args()

    # heaptrack is Linux-only (it hooks malloc via LD_PRELOAD against the
    # GNU dynamic linker). When we're on Linux we always run a second
    # heaptrack pass per case for the allocation tree -- so fail fast if
    # the tools aren't installed, rather than warning per-case.
    with_heaptrack = platform.system() == "Linux"
    if with_heaptrack:
        missing = [
            t for t in ("heaptrack", "heaptrack_print") if shutil.which(t) is None
        ]
        if missing:
            raise SystemExit(
                f"ERROR: {', '.join(missing)} not found on PATH"
                " (Debian/Ubuntu: sudo apt install heaptrack)."
            )

    gen, bench = build_binaries()
    print(f"gen_torture_torrent:    {gen}")
    print(f"benchmark_load_torrent: {bench}")

    benchmarks_dir = (Path.cwd() / "load-torrent").resolve()
    benchmarks_dir.mkdir(parents=True, exist_ok=True)
    print(f"results:             {benchmarks_dir}")

    results: list[Result] = []
    for case in CASES:
        for variant in case.versions:
            print(f"\n== {case.name} / {variant} ==")
            r = run_one(
                gen,
                bench,
                case,
                variant,
                benchmarks_dir,
                with_heaptrack=with_heaptrack,
            )
            iterations = case.iterations or ITERATIONS
            if r.error:
                print(f"  ERROR: {r.error}")
            else:
                print(
                    f" {r.per_iter_ms:.3f} ms"
                    f" (x{iterations} = {r.total_ms:.1f} ms total),"
                    f" peak RSS {fmt_bytes(r.max_rss_bytes)}"
                )
            results.append(r)
            # write the report after every case so a Ctrl-C leaves a
            # coherent partial report behind.
            write_report(benchmarks_dir, results, "report.html")

    report = write_report(benchmarks_dir, results, "report.html")
    print(f"\nwrote {report}")


if __name__ == "__main__":
    main()
