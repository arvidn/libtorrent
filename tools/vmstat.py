# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

from dataclasses import dataclass
import os
import platform
import subprocess
from time import monotonic
from typing import Dict
from typing import List
from typing import Set


@dataclass(frozen=True)
class Metric:
    axis: str
    cumulative: bool


metrics = {
    "peak_nonpaged_pool": Metric("x1y1", False),
    "nonpaged_pool": Metric("x1y1", False),
    "num_page_faults": Metric("x1y2", True),
    "paged_pool": Metric("x1y1", False),
    "peak_paged_pool": Metric("x1y1", False),
    "peak_pagefile": Metric("x1y1", False),
    "peak_wset": Metric("x1y1", False),
    "private": Metric("x1y1", False),
    "rss": Metric("x1y1", False),
    "uss": Metric("x1y1", False),
    "data": Metric("x1y1", False),
    "shared": Metric("x1y1", False),
    "text": Metric("x1y1", False),
    "dirty": Metric("x1y1", False),
    "lib": Metric("x1y1", False),
    "vms": Metric("x1y1", False),
    "other_bytes": Metric("x1y1", True),
    "other_count": Metric("x1y2", True),
    "read_bytes": Metric("x1y1", True),
    "read_chars": Metric("x1y1", True),
    "read_count": Metric("x1y2", True),
    "write_bytes": Metric("x1y1", True),
    "write_chars": Metric("x1y1", True),
    "write_count": Metric("x1y2", True),
    "pfaults": Metric("x1y2", True),
    "pageins": Metric("x1y2", True),
    "minor_faults": Metric("x1y2", True),
    "major_faults": Metric("x1y2", True),
    "pss": Metric("x1y1", False),
    "pss_anon": Metric("x1y1", False),
    "pss_file": Metric("x1y1", False),
    "pss_shmem": Metric("x1y1", False),
    "shared_clean": Metric("x1y1", False),
    "shared_dirty": Metric("x1y1", False),
    "private_clean": Metric("x1y1", False),
    "private_dirty": Metric("x1y1", False),
    "referenced": Metric("x1y1", False),
    "anonymous": Metric("x1y1", False),
    "lazyfree": Metric("x1y1", False),
    "anonhugepages": Metric("x1y1", False),
    "shmempmdmapped": Metric("x1y1", False),
    "filepmdmapped": Metric("x1y1", False),
    "shared_hugetlb": Metric("x1y1", False),
    "private_hugetlb": Metric("x1y1", False),
    "swap": Metric("x1y1", False),
    "swappss": Metric("x1y1", False),
    "locked": Metric("x1y1", False),
}


@dataclass(frozen=True)
class Plot:
    name: str
    title: str
    ylabel: str
    y2label: str
    lines: List[str]


plots = [
    Plot(
        "memory",
        "libtorrent memory usage",
        "Memory Size",
        "",
        [
            "pss",
            "pss_file",
            "pss_anon",
            "rss",
            "dirty",
            "private_dirty",
            "private_clean",
            "lazyfree",
            "anonymous",
            "vms",
            "private",
            "paged_pool",
        ],
    ),
    Plot(
        "vm",
        "libtorrent vm stats",
        "",
        "count",
        [
            "pfaults",
            "pageins",
            "num_page_faults",
            "major_faults",
            "minor_faults",
        ],
    ),
    Plot(
        "io",
        "libtorrent disk I/O",
        "Size",
        "count",
        [
            "other_bytes",
            "other_count",
            "read_bytes",
            "read_chars",
            "read_count",
            "write_bytes",
            "write_chars",
            "write_count",
        ],
    ),
]

if platform.system() == "Linux":

    def capture_sample(
        pid: int, start_time: int, output: Dict[str, List[float]]
    ) -> None:
        try:
            with open(f"/proc/{pid}/smaps_rollup") as f:
                sample = f.read()
            with open(f"/proc/{pid}/stat") as f:
                sample2 = f.read()
            timestamp = monotonic() - start_time
        except Exception:
            return

        if "time" not in output:
            time_delta = timestamp - start_time
            output["time"] = [timestamp]
        else:
            time_delta = timestamp - output["time"][-1]
            output["time"].append(timestamp)

        for line in sample.split("\n"):
            if "[rollup]" in line:
                continue
            if line.strip() == "":
                continue

            key, value = line.split(":")
            val = int(value.split()[0].strip())
            key = key.strip().lower()

            if key not in output:
                output[key] = [val * 1024]
            else:
                output[key].append(val * 1024)

        stats = sample2.split()

        def add_counter(key: str, val: float) -> None:
            m = metrics[key]
            if key not in output:
                if m.cumulative:
                    output[key + "-raw"] = [val]
                    val = val / time_delta
                output[key] = [val]
            else:

                if m.cumulative:
                    raw_val = val
                    val = (val - output[key + "-raw"][-1]) / time_delta
                    output[key + "-raw"].append(raw_val)
                output[key].append(val)

        add_counter("minor_faults", float(stats[9]))
        add_counter("major_faults", float(stats[11]))


# example output:
# 8affffff000-7fffba926000 ---p 00000000 00:00 0  [rollup]
#    Rss:               76932 kB
#    Pss:               17508 kB
#    Pss_Anon:          11376 kB
#    Pss_File:           6101 kB
#    Pss_Shmem:            30 kB
#    Shared_Clean:      65380 kB
#    Shared_Dirty:         88 kB
#    Private_Clean:        80 kB
#    Private_Dirty:     11384 kB
#    Referenced:        76932 kB
#    Anonymous:         11376 kB
#    LazyFree:              0 kB
#    AnonHugePages:         0 kB
#    ShmemPmdMapped:        0 kB
#    FilePmdMapped:         0 kB
#    Shared_Hugetlb:        0 kB
#    Private_Hugetlb:       0 kB
#    Swap:                  0 kB
#    SwapPss:               0 kB
#    Locked:                0 kB

else:

    import psutil

    def capture_sample(
        pid: int, start_time: int, output: Dict[str, List[float]]
    ) -> None:
        try:
            p = psutil.Process(pid)
            mem = p.memory_full_info()
            io_cnt = p.io_counters()
            timestamp = monotonic() - start_time
        except Exception:
            return

        if "time" not in output:
            time_delta = timestamp - start_time
            output["time"] = [timestamp]
        else:
            time_delta = timestamp - output["time"][-1]
            output["time"].append(timestamp)

        for key in dir(mem):

            if key not in metrics:
                if not key.startswith("_") and key not in [
                    "pagefile",
                    "wset",
                    "count",
                    "index",
                ]:
                    print(f"missing key: {key}")
                continue

            val = getattr(mem, key)

            m = metrics[key]
            if key not in output:
                if m.cumulative:
                    output[key + "-raw"] = [val]
                    val = val / time_delta
                output[key] = [val]
            else:
                if m.cumulative:
                    raw_val = val
                    val = (val - output[key + "-raw"][-1]) / time_delta
                    output[key + "-raw"].append(raw_val)

                output[key].append(val)

        for key in dir(io_cnt):

            if key not in metrics:
                if not key.startswith("_") and key not in [
                    "pagefile",
                    "wset",
                    "count",
                    "index",
                ]:
                    print(f"missing key: {key}")
                continue

            m = metrics[key]
            if key not in output:
                if m.cumulative:
                    output[key + "-raw"] = [val]
                    val = val / time_delta
                output[key] = [val]
            else:
                if m.cumulative:
                    raw_val = val
                    val = (val - output[key + "-raw"][-1]) / time_delta
                    output[key + "-raw"].append(raw_val)

                output[key].append(val)


def print_output_to_file(out: Dict[str, List[int]], filename: str) -> List[str]:

    if out == {}:
        return []

    with open(filename, "w+") as stats_output:
        non_zero_keys: Set[str] = set()
        non_zero_keys.add("time")
        keys = out.keys()
        for key in keys:
            stats_output.write(f"{key} ")
        stats_output.write("\n")
        idx = 0
        while len(out["time"]) > idx:
            for key in keys:
                stats_output.write(f"{out[key][idx]:f} ")
                if out[key][idx] != 0:
                    non_zero_keys.add(key)
            stats_output.write("\n")
            idx += 1
    return [k if k in non_zero_keys else "" for k in keys]


def plot_output(filename: str, keys: List[str]) -> None:
    if "time" not in keys:
        return

    output_dir, in_file = os.path.split(filename)
    gnuplot_file = f"{output_dir}/plot_{in_file}.gnuplot"
    with open(gnuplot_file, "w+") as f:
        f.write(
            """set term png size 1200,700
set format y '%.0f'
set xlabel "time (s)"
set xrange [0:*]
set yrange [2:*]
set y2range [0:*]
set logscale y 2
set logscale y2 2
set grid
"""
        )

        for plot in plots:
            f.write(
                f"""set output "{in_file}-{plot.name}.png"
set title "{plot.title}"
set ylabel "{plot.ylabel} (MB)"
set y2label "{plot.y2label}"
{"set y2tics" if plot.y2label != "" else ""}
"""
            )

            plot_string = "plot "
            tidx = keys.index("time") + 1
            idx = 0
            for p in keys:
                idx += 1
                if p == "time" or p == "":
                    continue

                if p not in plot.lines:
                    continue

                m = metrics[p]

                title = p.replace("_", "\\\\_")
                if m.cumulative:
                    title += "/s"

                divider = 1
                if m.axis == "x1y1":
                    divider = 1024 * 1024

                # escape underscores, since gnuplot interprets those as markup
                plot_string += (
                    f'"{in_file}" using {tidx}:(${idx}/{divider}) '
                    + f'title "{title}" axis {m.axis} with steps, \\\n'
                )
            if len(plot_string) > 5:
                plot_string = plot_string[0:-4] + "\n\n"
                f.write(plot_string)

    subprocess.check_output(["gnuplot", os.path.split(gnuplot_file)[1]], cwd=output_dir)
