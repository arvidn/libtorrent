# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
from time import time
from typing import Dict
from typing import List
from typing import Set


def capture_sample(pid: int, start_time: int, output: Dict[str, List[int]]) -> None:
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            sample = f.read()
            timestamp = int((time() - start_time) * 1000)
    except Exception:
        return

    if "time" not in output:
        output["time"] = [timestamp]
    else:
        output["time"].append(timestamp)

    for line in sample.split("\n"):
        if "[rollup]" in line:
            continue
        if line.strip() == "":
            continue

        key, value = line.split(":")
        val = int(value.split()[0].strip())
        key = key.strip()

        if key not in output:
            output[key] = [val]
        else:
            output[key].append(val)


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
                stats_output.write(f"{out[key][idx]} ")
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
            f"""set term png size 1200,700
set output "{in_file}.png"
set format y '%.0f MB'
set title "libtorrent memory usage"
set ylabel "Memory Size"
set xlabel "time (s)"
set xrange [0:*]
set yrange [2:*]
set logscale y 2
set grid
plot """
        )

        plot_string = ""
        tidx = keys.index("time") + 1
        idx = 0
        for p in keys:
            idx += 1
            if p == "time" or p == "":
                continue
            # escape underscores, since gnuplot interprets those as markup
            p = p.replace("_", "\\\\_")
            plot_string += (
                f'"{in_file}" using (${tidx}/1000):(${idx}/1024) '
                + f'title "{p}" with steps, \\\n'
            )
        plot_string = plot_string[0:-4]
        f.write(plot_string)

    os.system(f"(cd {output_dir}; gnuplot {os.path.split(gnuplot_file)[1]})")
