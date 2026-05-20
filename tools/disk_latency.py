#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

"""Summarize disk read-job latency over time from a libtorrent session-stats
log (the file written by ``client_test -O``, the same format
parse_session_stats.py reads).

libtorrent records read-job latency (queue wait + execution + completion-queue
wait, measured on the network thread) as a histogram of monotonic counters
named ``disk.disk_read_latency1`` .. ``disk.disk_read_latency20``, linear in
30 ms steps: ``disk_read_latencyN`` counts read jobs whose latency fell in
[30*(N-1), 30*N) ms, and the last counter (latency20) is the catch-all for
>= 570 ms. These are only populated in disk-latency-stats builds; without that
build flag the buckets stay zero and there is nothing to plot.

This tool diffs the cumulative buckets between samples, groups the resulting
per-job counts into fixed wall-clock intervals (5 s by default), and from each
interval's histogram estimates the median, mean, and 95th-percentile latency,
reported in milliseconds. collect_latency() returns those series plus a single
representative number for the whole run -- the peak interval p95 -- which
run_benchmark.py drops into its results table.
"""

from argparse import ArgumentParser
from collections.abc import Iterator
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
import re

# number of disk_read_latency buckets exposed by libtorrent (see
# performance_counters.hpp: disk_read_latency1 .. disk_read_latency20), linear
# in 30 ms steps.
NUM_BUCKETS = 20
# width of each linear bucket, in milliseconds
BUCKET_STEP_MS = 30
# counter-name prefix in the session-stats header (category "disk"); the
# counters are 1-indexed (latency1 .. latency20), so the first index is 1.
BUCKET_PREFIX = "disk.disk_read_latency"
BUCKET_FIRST = 1

# leading "[<ms>]" timestamp client_test writes on every stats line
_TS_RE = re.compile(r"^\[(\d+)\]")


def bucket_rep_ms(i: int) -> float:
    """Representative latency, in milliseconds, for histogram bucket `i`
    (0-based; bucket i is the counter disk_read_latency{i+1}).

    Bucket i covers [30*i, 30*(i+1)) ms, so we use its midpoint, 30*i + 15 ms.
    For the open-ended top bucket this is just the midpoint of its first 30 ms
    step, an approximate lower bound.
    """
    return BUCKET_STEP_MS * i + BUCKET_STEP_MS / 2.0


@dataclass
class LatencySeries:
    """Per-interval disk read-latency summary plus a single run-level number."""

    # interval midpoint, in seconds since the start of the run
    times: list[float] = field(default_factory=list)
    # per-interval latency statistics, in milliseconds
    median_ms: list[float] = field(default_factory=list)
    mean_ms: list[float] = field(default_factory=list)
    p95_ms: list[float] = field(default_factory=list)
    # number of read jobs observed in each interval
    counts: list[int] = field(default_factory=list)
    # representative latency for the whole run (ms): the worst (peak) interval
    # p95. 0.0 when no read-latency samples were recorded -- e.g. a build
    # without disk-latency-stats, or the posix backend (no disk job queue).
    peak_p95_ms: float = 0.0


def _parse_samples(path: Path) -> Iterator[tuple[float, list[int]]]:
    """Yield (time_s, counts) for each session-stats sample in `path`, where
    counts is a list of the NUM_BUCKETS cumulative latency-bucket values. The
    leading "[<ms>]" timestamp gives wall-clock time; without it we fall back
    to the sample index (assuming 1 s spacing). Yields nothing if the file is
    unreadable or its header has no latency columns.
    """
    try:
        f = open(path, encoding="utf-8", errors="replace")
    except OSError:
        return
    with f:
        # find the header and locate the latency columns
        cols = None
        for line in f:
            if "session stats header:" in line:
                keys = line.split("session stats header:")[1].strip().split(", ")
                cols = []
                for n in range(NUM_BUCKETS):
                    try:
                        cols.append(keys.index(f"{BUCKET_PREFIX}{BUCKET_FIRST + n}"))
                    except ValueError:
                        return  # this log has no latency columns
                break
        if not cols:
            return

        idx = 0
        for line in f:
            if "session stats (" not in line:
                continue
            try:
                values = line.split(" values): ")[1].strip().split(", ")
                counts = [int(values[c]) for c in cols]
            except (IndexError, ValueError):
                continue
            m = _TS_RE.match(line)
            t_s = (int(m.group(1)) / 1000.0) if m else float(idx)
            idx += 1
            yield t_s, counts


def _percentile(hist: list[int], frac: float, total: int) -> float:
    """The `frac` quantile (e.g. 0.95) of a bucketed histogram, returned as the
    representative latency (ms) of the bucket the quantile falls in.
    """
    threshold = frac * total
    cum = 0
    for i, c in enumerate(hist):
        cum += c
        if cum >= threshold:
            return bucket_rep_ms(i)
    return bucket_rep_ms(len(hist) - 1)


def collect_latency(path: str | Path, interval_s: float = 5.0) -> LatencySeries:
    """Parse `path` and return a LatencySeries: per-interval median/mean/p95
    disk read latency (ms) over `interval_s`-second wall-clock windows, plus
    the peak interval p95 as a single representative number for the run.

    The buckets are cumulative counters, so each sample contributes its delta
    from the previous sample; those deltas are accumulated into the interval
    the sample falls in. The first sample is taken as a baseline (counters may
    already be nonzero when logging starts, and attributing that backlog to
    interval 0 would distort it).
    """
    by_interval: dict[int, list[int]] = {}
    prev: list[int] | None = None
    for t_s, counts in _parse_samples(Path(path)):
        if prev is None:
            prev = counts
            continue
        # guard against counter resets / out-of-order lines with max(0, .)
        delta = [max(0, counts[i] - prev[i]) for i in range(NUM_BUCKETS)]
        prev = counts
        acc = by_interval.setdefault(int(t_s // interval_s), [0] * NUM_BUCKETS)
        for i in range(NUM_BUCKETS):
            acc[i] += delta[i]

    series = LatencySeries()
    for k in sorted(by_interval):
        hist = by_interval[k]
        n = sum(hist)
        if n == 0:
            continue
        p95 = _percentile(hist, 0.95, n)
        series.times.append((k + 0.5) * interval_s)
        series.median_ms.append(_percentile(hist, 0.5, n))
        series.mean_ms.append(sum(c * bucket_rep_ms(i) for i, c in enumerate(hist)) / n)
        series.p95_ms.append(p95)
        series.counts.append(n)
        series.peak_p95_ms = max(series.peak_p95_ms, p95)
    return series


def plot_latency(
    series: LatencySeries, out_path: Path, title: str = "disk read latency"
) -> bool:
    """Plot median / average / p95 read latency over time to `out_path` (PNG).
    Returns True on success, False if matplotlib is missing or there is no
    data to plot (so callers can skip referencing the image).
    """
    if not series.times:
        return False
    try:
        import matplotlib

        matplotlib.use("Agg")  # headless backend; no display required
        import matplotlib.pyplot as plt
    except ImportError:
        return False

    fig, ax = plt.subplots(figsize=(10.0, 4.0))
    ax.plot(series.times, series.median_ms, label="median")
    ax.plot(series.times, series.mean_ms, label="average")
    ax.plot(series.times, series.p95_ms, label="95th percentile")
    # linear y-axis in milliseconds, for readable tick marks
    ax.set_ylim(bottom=0)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("read latency (ms)")
    ax.set_title(title)
    ax.grid(True, which="both", linewidth=0.3, alpha=0.5)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=100)
    plt.close(fig)
    return True


def main(
    input_file: str | Path, output_dir: str | Path, interval_s: float = 5.0
) -> float:
    """Parse `input_file`, write disk_read_latency.png into `output_dir`, and
    return the run's representative latency (peak interval p95, ms).
    """
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    series = collect_latency(input_file, interval_s)
    out_png = output_dir / "disk_read_latency.png"
    if plot_latency(series, out_png):
        print(f"wrote {out_png}")
    elif not series.times:
        print(
            "no disk read-latency samples found" " (needs a disk-latency-stats build)"
        )
    else:
        print("matplotlib not installed; skipped latency plot")
    print(f"peak interval p95 read latency: {series.peak_p95_ms:.3f} ms")
    return series.peak_p95_ms


if __name__ == "__main__":
    p = ArgumentParser(description=__doc__)
    p.add_argument(
        "input",
        type=Path,
        help="session-stats log to parse (client_test -O output)",
    )
    p.add_argument(
        "--output-dir",
        type=Path,
        default=Path("."),
        help="directory to write disk_read_latency.png to",
    )
    p.add_argument(
        "--interval",
        type=float,
        default=5.0,
        help="bucket interval in seconds (default: 5)",
    )
    args = p.parse_args()
    main(args.input, args.output_dir, args.interval)
