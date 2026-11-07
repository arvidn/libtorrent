#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

"""Build and run read_ahead_trace, then render its traces as SVG diagrams
and an index page.

read_ahead_trace (tools/read_ahead_trace.cpp) replays synthetic block-request
patterns through aux::read_ahead_predictor and records the window it returned
plus its internal state after every request. This script stages it with b2,
captures its output and writes, into --results-path, the raw trace plus one
SVG per scenario: two panels sharing the request axis, the block index of each
request with the read-ahead spans issued and the advised_until high-water
mark, and below it the predictor's confidence next to the window it actually
handed to the disk backend. index.html collects the diagrams with the scenario
descriptions and totals.
"""

from argparse import ArgumentParser
from dataclasses import dataclass
from html import escape
from pathlib import Path
import platform
import subprocess

from matplotlib.figure import Figure

exe = ""

ROOT_DIR = Path(__file__).parent.parent.resolve()
TOOLS_DIR = ROOT_DIR / "tools"

if platform.system() == "Windows":
    exe = ".exe"

HOT = "#2a78d6"
COLD = "#eb6834"
CONF = "#4a3aa7"
WASTE = "#e34948"
BAND = "#eceff4"
GRID = "#d6dae1"
MUTED = "#5d6673"

# the window at or above which peer_connection sets disk_interface::new_piece_hint
NEW_PIECE_HINT_WINDOW = 8


@dataclass
class Event:
    piece: int
    block: int
    t_ms: int
    first_block: int
    window: int
    new_piece: bool
    confidence: int
    advised_until: int
    issued: int
    wasted: int
    hot: bool

    @staticmethod
    def parse(line: str) -> "Event":
        f = [int(v) for v in line.split()]
        return Event(
            piece=f[0],
            block=f[1],
            t_ms=f[2],
            first_block=f[3],
            window=f[4],
            new_piece=bool(f[5]),
            confidence=f[6],
            advised_until=f[7],
            issued=f[8],
            wasted=f[9],
            hot=bool(f[10]),
        )


@dataclass
class Scenario:
    id: str
    title: str
    summary: str
    blocks_in_piece: int
    events: list[Event]

    @property
    def peak_confidence(self) -> int:
        return max(e.confidence for e in self.events)

    @property
    def issued(self) -> int:
        return sum(e.issued for e in self.events)

    @property
    def wasted(self) -> int:
        return sum(e.wasted for e in self.events)

    @property
    def hot(self) -> int:
        return sum(e.hot for e in self.events)


def parse_trace(text: str) -> list[Scenario]:
    """One scenario per block of lines: a scenario line, a title line, a
    summary line, then one request per line, terminated by a blank line."""
    scenarios: list[Scenario] = []
    current: Scenario | None = None
    for line in text.splitlines():
        if not line.strip():
            current = None
        elif line.startswith("scenario "):
            _, sid, bip = line.split()
            current = Scenario(sid, "", "", int(bip), [])
            scenarios.append(current)
        elif current is None:
            raise ValueError(f"request line outside a scenario: {line!r}")
        elif line.startswith("title "):
            current.title = line[len("title ") :]
        elif line.startswith("summary "):
            current.summary = line[len("summary ") :]
        else:
            current.events.append(Event.parse(line))
    return scenarios


def piece_runs(events: list[Event]) -> list[tuple[int, int]]:
    """[start, end) request ranges during which the same piece was tracked."""
    runs: list[tuple[int, int]] = []
    start = 0
    for i in range(1, len(events) + 1):
        if i == len(events) or events[i].piece != events[start].piece:
            runs.append((start, i))
            start = i
    return runs


def plot_scenario(s: Scenario, path: Path) -> None:
    ev = s.events
    n = len(ev)
    last = s.blocks_in_piece - 1
    xs = list(range(n))

    fig = Figure(figsize=(max(9.0, n * 0.075), 6.5))
    top, bottom = fig.subplots(
        2, 1, sharex=True, gridspec_kw={"height_ratios": [2.2, 1], "hspace": 0.12}
    )
    fig.subplots_adjust(left=0.07, right=0.99, top=0.9, bottom=0.1)

    for ax in (top, bottom):
        ax.set_axisbelow(True)
        ax.grid(axis="y", color=GRID, linewidth=0.6)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        for side in ("left", "bottom"):
            ax.spines[side].set_color(GRID)
        ax.tick_params(colors=MUTED, labelsize=8)

    for k, (a, b) in enumerate(piece_runs(ev)):
        if k % 2:
            top.axvspan(a - 0.5, b - 0.5, color=BAND, zorder=0, linewidth=0)
        for ax in (top, bottom):
            ax.axvline(a - 0.5, color=GRID, linestyle=(0, (2, 3)), linewidth=0.8)
        if b - a >= 3:
            top.text(
                a - 0.3,
                last * 1.07,
                f"piece {ev[a].piece}",
                fontsize=7,
                color=MUTED,
                family="monospace",
                va="bottom",
            )

    top.bar(
        [i for i in xs if ev[i].window > 0],
        [ev[i].window for i in xs if ev[i].window > 0],
        bottom=[ev[i].first_block for i in xs if ev[i].window > 0],
        width=0.8,
        color=HOT,
        alpha=0.28,
        linewidth=0,
        label="read-ahead span issued",
    )

    # advised_until as a step line, broken wherever the piece changes
    au_x: list[float] = []
    au_y: list[float] = []
    for i in xs:
        if ev[i].advised_until < 0:
            continue
        if i > 0 and (ev[i - 1].piece != ev[i].piece or ev[i - 1].advised_until < 0):
            au_x.append(float("nan"))
            au_y.append(float("nan"))
        au_x.extend([i - 0.5, i + 0.5])
        au_y.extend([min(ev[i].advised_until, last)] * 2)
    top.plot(au_x, au_y, color=HOT, linewidth=1.2, label="advised_until")

    for hot, color, label in (
        (True, HOT, "request (hot)"),
        (False, COLD, "request (cold)"),
    ):
        pts = [i for i in xs if ev[i].hot == hot]
        top.scatter(
            pts,
            [ev[i].block for i in pts],
            s=12 if n < 150 else 6,
            color=color,
            edgecolors="white",
            linewidths=0.5,
            zorder=3,
            label=label,
        )

    for i in xs:
        if ev[i].wasted:
            top.annotate(
                f"-{ev[i].wasted}",
                (i, -last * 0.02),
                xytext=(0, -9),
                textcoords="offset points",
                ha="center",
                va="top",
                rotation=90,
                fontsize=7,
                color=WASTE,
                family="monospace",
                annotation_clip=False,
            )
            top.plot(
                [i],
                [-last * 0.02],
                marker="s",
                color=WASTE,
                markersize=4,
                clip_on=False,
            )
        if ev[i].new_piece and ev[i].window >= NEW_PIECE_HINT_WINDOW:
            top.plot(
                [i],
                [last * 1.04],
                marker="v",
                color="black",
                markersize=4,
                clip_on=False,
            )

    top.set_ylim(-last * 0.06, last * 1.1)
    top.set_ylabel("block in piece", fontsize=8, color=MUTED)
    top.set_title(s.title, loc="left", fontsize=11, fontweight="semibold", pad=18)
    top.legend(
        loc="upper right", fontsize=7, frameon=False, ncol=4, bbox_to_anchor=(1.0, 1.12)
    )

    # confidence and the window actually issued share one unit, blocks
    bottom.bar(
        [i for i in xs if ev[i].window > 0],
        [ev[i].window for i in xs if ev[i].window > 0],
        width=0.8,
        color=HOT,
        alpha=0.55,
        linewidth=0,
        label="window issued",
    )
    bottom.plot(
        xs, [e.confidence for e in ev], color=CONF, linewidth=1.6, label="confidence"
    )
    bottom.set_ylim(0, max(8, s.peak_confidence, max(e.window for e in ev)) * 1.1)
    bottom.set_ylabel("blocks", fontsize=8, color=MUTED)
    bottom.set_xlabel("request #", fontsize=8, color=MUTED)
    bottom.legend(loc="upper left", fontsize=7, frameon=False, ncol=2)
    bottom.set_xlim(-0.5, n - 0.5)

    fig.savefig(path)


INDEX_CSS = """
body { font: 15px/1.5 system-ui, sans-serif; color: #15181d; background: #f7f8fa;
  margin: 0; }
.wrap { max-width: 76rem; margin: 0 auto; padding: 2rem 16px 4rem; }
h1 { font-size: 1.8rem; margin: 0 0 .25rem; }
h2 { font-size: 1.2rem; margin: 0; }
p { max-width: 70ch; margin: 0; }
.muted { color: #5d6673; }
code { font-family: ui-monospace, Menlo, monospace; font-size: .86em; }
table { border-collapse: collapse; font-variant-numeric: tabular-nums;
  font-size: .85rem; }
th, td { text-align: right; padding: .3rem .7rem; border-bottom: 1px solid #d6dae1; }
th { color: #5d6673; font-weight: 500; font-size: .75rem; text-transform: uppercase; }
th:first-child, td:first-child { text-align: left; }
td a { color: inherit; }
section { margin-top: 2.5rem; padding-top: 1.5rem; border-top: 1px solid #d6dae1;
  display: grid; gap: .8rem; }
.stats { display: flex; flex-wrap: wrap; gap: .4rem 1.6rem; font-size: .85rem;
  color: #5d6673; }
.stats b { color: #15181d; font-weight: 500;
  font-family: ui-monospace, Menlo, monospace; }
.stats .bad b { color: #e34948; }
figure { margin: 0; background: #fff; border: 1px solid #d6dae1; overflow-x: auto; }
figure img { display: block; max-width: none; }
.legend { font-size: .85rem; color: #5d6673; max-width: 70ch; }
"""


def write_index(scenarios: list[Scenario], out: Path) -> None:
    rows = []
    sections = []
    for s in scenarios:
        n = len(s.events)
        amp = s.issued / n
        hot_pct = round(100 * s.hot / n)
        rows.append(
            f'<tr><td><a href="#{s.id}">{escape(s.title)}</a></td><td>{n}</td>'
            f"<td>{s.issued}</td><td>{amp:.1f}&times;</td>"
            f"<td>{s.wasted}</td><td>{hot_pct}%</td>"
            f"<td>{s.peak_confidence}</td></tr>"
        )
        bad = ' class="bad"' if s.wasted else ""
        sections.append(
            f'<section id="{s.id}"><h2>{escape(s.title)}</h2>'
            f'<span class="muted"><code>{s.id}</code> &middot; '
            f"{s.blocks_in_piece} blocks per piece</span>"
            f"<p>{escape(s.summary)}</p>"
            f'<div class="stats"><span>requests <b>{n}</b></span>'
            f"<span>blocks advised <b>{s.issued}</b></span>"
            f"<span>advised per request <b>{amp:.1f}</b></span>"
            f"<span{bad}>wasted <b>{s.wasted}</b></span>"
            f"<span>hot <b>{s.hot}</b> / cold <b>{n - s.hot}</b></span>"
            f"<span>peak confidence <b>{s.peak_confidence}</b></span></div>"
            f'<figure><img src="{s.id}.svg" alt="{escape(s.title)} trace"></figure>'
            "</section>"
        )
    legend = (
        "Each diagram replays one request pattern through the real "
        "<code>aux::read_ahead_predictor</code>. The x axis is the request "
        "sequence; alternating bands are the piece being tracked. Top: the block "
        "each request named (blue if the backend had already been told to "
        "prefetch it, orange otherwise), the read-ahead span the request issued, "
        "and the advised_until high-water mark. Red squares mark blocks wasted "
        "when a piece was left with unconsumed advice; the black triangle marks "
        "a first request whose window was large enough for the new_piece hint "
        f"(&ge; {NEW_PIECE_HINT_WINDOW} blocks). Bottom: confidence against the "
        "window actually handed to the disk backend, both in blocks."
    )
    html = (
        '<!doctype html><html><head><meta charset="utf-8">'
        "<title>Read-Ahead Predictor Traces</title>"
        f'<style>{INDEX_CSS}</style></head><body><div class="wrap">'
        "<h1>Read-Ahead Predictor Traces</h1>"
        '<p class="muted">Produced by tools/read_ahead_trace.</p>'
        f'<p class="legend" style="margin-top:1rem">{legend}</p>'
        '<table style="margin-top:1.5rem"><thead><tr><th>scenario</th>'
        "<th>requests</th><th>blocks advised</th><th>advised / requested</th>"
        "<th>wasted</th><th>hot</th><th>peak confidence</th></tr></thead>"
        f"<tbody>{''.join(rows)}</tbody></table>"
        f"{''.join(sections)}</div></body></html>"
    )
    (out / "index.html").write_text(html)


def main() -> None:
    p = ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--toolset", default="")
    p.add_argument(
        "--results-path",
        default="read-ahead-traces",
        type=Path,
        help="Directory to write the raw trace, the diagrams and index.html to."
        " Defaults to 'read-ahead-traces' in the current working directory.",
    )
    args = p.parse_args()

    subprocess.check_call(
        ["b2", "release", args.toolset, "stage_read_ahead_trace"], cwd=TOOLS_DIR
    )
    trace = subprocess.run(
        [str(TOOLS_DIR / f"read_ahead_trace{exe}")],
        stdout=subprocess.PIPE,
        text=True,
        check=True,
    ).stdout

    results_path: Path = args.results_path
    results_path.mkdir(parents=True, exist_ok=True)
    (results_path / "trace.txt").write_text(trace)
    scenarios = parse_trace(trace)
    for s in scenarios:
        plot_scenario(s, results_path / f"{s.id}.svg")
    write_index(scenarios, results_path)
    print(f"wrote {len(scenarios)} diagrams and index.html to {results_path}")


if __name__ == "__main__":
    main()
