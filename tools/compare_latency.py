#!/usr/bin/env python3
"""Compare benchmark latency values between two JSON result files."""

import argparse
import json
from typing import NamedTuple

import matplotlib.pyplot as plt


class LatencyStats(NamedTuple):
    value: float
    lower_value: float
    upper_value: float


def load_latencies(path: str) -> dict[str, LatencyStats]:
    with open(path) as f:
        data = json.load(f)
    return {
        name: LatencyStats(
            entry["latency"]["value"],
            entry["latency"]["lower_value"],
            entry["latency"]["upper_value"],
        )
        for name, entry in data.items()
    }


def print_table(
    before: dict[str, LatencyStats], after: dict[str, LatencyStats], names: list[str]
) -> None:
    print("| benchmark | before | after | speed-up |")
    print("|---|---|---|---|")
    for name in names:
        before_stats = before.get(name)
        after_stats = after.get(name)
        before_str = f"{before_stats.value:.2f}" if before_stats is not None else "N/A"
        after_str = f"{after_stats.value:.2f}" if after_stats is not None else "N/A"
        if (
            before_stats is not None
            and after_stats is not None
            and after_stats.value != 0
        ):
            ratio_str = f"{before_stats.value / after_stats.value:.3f}"
        else:
            ratio_str = "N/A"
        print(f"| {name} | {before_str} | {after_str} | {ratio_str} |")


def plot_comparison(
    before: dict[str, LatencyStats],
    after: dict[str, LatencyStats],
    names: list[str],
    output_path: str,
) -> None:
    plotted_names = [
        name
        for name in names
        if name in before and name in after and before[name].value != 0
    ]

    before_values = [1.0 for _ in plotted_names]
    after_values = [after[name].value / before[name].value for name in plotted_names]
    before_err = [
        [1.0 - before[name].lower_value / before[name].value for name in plotted_names],
        [before[name].upper_value / before[name].value - 1.0 for name in plotted_names],
    ]
    after_err = [
        [
            (after[name].value - after[name].lower_value) / before[name].value
            for name in plotted_names
        ],
        [
            (after[name].upper_value - after[name].value) / before[name].value
            for name in plotted_names
        ],
    ]

    x = range(len(plotted_names))
    width = 0.35

    fig, ax = plt.subplots(figsize=(max(8, len(plotted_names) * 0.6), 6))
    ax.bar(
        [i - width / 2 for i in x],
        before_values,
        width,
        yerr=before_err,
        capsize=3,
        label="before",
    )
    ax.bar(
        [i + width / 2 for i in x],
        after_values,
        width,
        yerr=after_err,
        capsize=3,
        label="after",
    )

    ax.axhline(1.0, color="gray", linewidth=0.8, linestyle="--")
    ax.set_ylabel("latency relative to before")
    ax.set_title("benchmark latency comparison")
    ax.set_xticks(list(x))
    ax.set_xticklabels(plotted_names, rotation=45, ha="right")
    ax.legend()

    fig.tight_layout()
    fig.savefig(output_path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", help="JSON file with the baseline benchmark results")
    parser.add_argument("after", help="JSON file with the new benchmark results")
    parser.add_argument(
        "--plot",
        default="latency_comparison.png",
        help="path to write the bar chart image to (default: %(default)s)",
    )
    args = parser.parse_args()

    before = load_latencies(args.before)
    after = load_latencies(args.after)

    names = sorted(set(before) | set(after))

    print_table(before, after, names)
    plot_comparison(before, after, names, args.plot)


if __name__ == "__main__":
    main()
