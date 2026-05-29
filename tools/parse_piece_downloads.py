#!/usr/bin/env python3
"""Plot the order in which pieces pass the hash check over time.

Parses an ``events.log`` produced with the ``torrent_log`` alert category
enabled and plots a dot per ``PIECE_PASSED`` event, with time on the x-axis and
piece index on the y-axis. This visualises the download/verify order of a
torrent (e.g. sequential vs. scattered across the file).

Log lines look like::

    [2591] 3df970: PIECE_PASSED (986) (num_have: 0)

where the bracketed value is a millisecond timestamp, the hex value is the
torrent id, and the value in parentheses is the piece index.
"""

import argparse
from collections import defaultdict
from pathlib import Path
import re

import plot_layout

LINE_RE = re.compile(
    r"^\[(?P<ts>\d+)\]\s+(?P<tid>[0-9a-f]+):\s+PIECE_PASSED\s+\((?P<piece>\d+)\)"
)


def parse(path: Path) -> dict[str, list[tuple[int, int]]]:
    """Return ``{torrent_id: [(timestamp_ms, piece_index), ...]}``."""
    points: dict[str, list[tuple[int, int]]] = defaultdict(list)
    with path.open() as f:
        for line in f:
            m = LINE_RE.match(line)
            if m is None:
                continue
            points[m.group("tid")].append((int(m.group("ts")), int(m.group("piece"))))
    return points


def plot_piece_downloads(
    events_log: Path, output: Path, title: str | None = None
) -> bool:
    """Plot a dot per PIECE_PASSED event (x: time, y: piece index) and save it
    to `output`. Returns False (writing nothing) if the log has no such events
    or matplotlib is not installed."""
    points = parse(events_log)
    if not points:
        return False

    try:
        import matplotlib

        matplotlib.use("Agg")  # headless backend; no display required
        import matplotlib.pyplot as plt
    except ImportError:
        return False

    # use the earliest timestamp across all torrents as t=0
    t0 = min(ts for pts in points.values() for ts, _ in pts)

    fig, ax = plt.subplots(figsize=(12, 7))
    multi = len(points) > 1
    for tid, pts in sorted(points.items()):
        xs = [(ts - t0) / 1000.0 for ts, _ in pts]
        ys = [piece for _, piece in pts]
        ax.scatter(xs, ys, s=4, label=tid if multi else None)

    ax.set_xlabel("time (s)")
    ax.set_ylabel("piece index")
    ax.set_title(title or f"piece pass order: {events_log.parent.name}")
    if multi:
        ax.legend(title="torrent", markerscale=3)
    ax.grid(True, alpha=0.3)

    # pin the plot box to the shared summary-page margins (see plot_layout) so
    # it lines up with the other plots, instead of letting tight_layout pick a
    # near-zero right margin (which makes this box wider than the rest).
    fig.subplots_adjust(
        left=plot_layout.BOX_LEFT,
        right=plot_layout.BOX_RIGHT,
        bottom=0.09,
        top=0.93,
    )
    fig.savefig(output, dpi=100)
    plt.close(fig)
    return True


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("events_log", type=Path, help="path to events.log")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="output PNG (default: <events_log dir>/piece_downloads.png)",
    )
    parser.add_argument("--title", default=None, help="plot title")
    args = parser.parse_args()

    out = args.output or args.events_log.parent / "piece_downloads.png"
    if not plot_piece_downloads(args.events_log, out, args.title):
        raise SystemExit(
            f"no PIECE_PASSED events in {args.events_log}, or matplotlib"
            " is not installed"
        )
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
