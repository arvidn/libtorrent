#!/usr/bin/env python3

# Runs ./bencher under callgrind and writes its results as Bencher Metric
# Format (BMF) JSON, for `bencher run --adapter json --file`. Must be run
# from tools/, like bencher itself.
#
# The results are split in two files. The primary file has only the measure
# with a threshold, and is the one reported back to GitHub. Reporting every
# measure there exceeds the size limit of a GitHub check. The details file
# has the remaining measures, and is only uploaded to the Bencher dashboard.

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

# The cache geometry is fixed, rather than detected from the host, so the
# counts don't depend on which machine the benchmarks run on
VALGRIND_ARGS = [
    "--tool=callgrind",
    "--collect-atstart=no",
    "--cache-sim=yes",
    "--branch-sim=yes",
    "--simulate-hwpref=yes",
    "--I1=32768,8,64",
    "--D1=32768,8,64",
    "--LL=8388608,16,64",
]

# rough per-event costs in cycles. The simulation has no notion of
# overlapping misses, so this is a weighting to rank regressions by, not a
# prediction of real cycles
L1_MISS_COST = 10
LL_MISS_COST = 100
BRANCH_MISS_COST = 10


def parse_dump(path: str) -> dict[str, int]:
    events: list[str] = []
    totals: list[int] = []
    with open(path) as f:
        for line in f:
            if line.startswith("events:"):
                events = line.split()[1:]
            elif line.startswith("totals:"):
                totals = [int(v) for v in line.split()[1:]]
    # trailing events with a count of zero are omitted
    totals += [0] * (len(events) - len(totals))
    return dict(zip(events, totals))


PRIMARY_MEASURES = {"cycles"}

# a benchmark this cheap per iteration is dominated by a handful of simulated
# cache and branch misses, which depend on where code and data happen to
# land, rather than on the code being measured. Such benchmarks should do
# more work per iteration
MIN_CYCLES = 1000


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--primary", required=True, help="BMF file for GitHub")
    parser.add_argument("--details", required=True, help="BMF file for the rest")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as out_dir:
        out_file = os.path.join(out_dir, "callgrind.out")
        valgrind = shutil.which("valgrind")
        if valgrind is None:
            print("valgrind not found", file=sys.stderr)
            return 1
        # the simulated cache and branch predictor are indexed by virtual
        # address, and the size of the environment shifts the initial stack.
        # valgrind places the program itself, so ASLR doesn't apply
        proc = subprocess.run(
            [
                "env",
                "-i",
                valgrind,
                *VALGRIND_ARGS,
                f"--callgrind-out-file={out_file}",
                "./bencher",
            ],
            stdout=subprocess.PIPE,
            text=True,
        )
        if proc.returncode != 0:
            print(f"bencher failed with exit code {proc.returncode}", file=sys.stderr)
            return 1
        manifest = json.loads(proc.stdout)

        # callgrind writes the Nth dump to <out_file>.N
        dumps = {e["dump"]: parse_dump(f"{out_file}.{e['dump']}") for e in manifest}

    bmf = {}
    for entry in manifest:
        c = dumps[entry["dump"]]
        n = entry["iterations"]
        instructions = c["Ir"]
        l1_misses = c["I1mr"] + c["D1mr"] + c["D1mw"]
        ll_misses = c["ILmr"] + c["DLmr"] + c["DLmw"]
        branch_misses = c["Bcm"] + c["Bim"]
        cycles = (
            instructions
            + L1_MISS_COST * l1_misses
            + LL_MISS_COST * ll_misses
            + BRANCH_MISS_COST * branch_misses
        )
        bmf[entry["name"]] = {
            "cycles": {"value": cycles / n},
            "instructions": {"value": instructions / n},
            "l1-misses": {"value": l1_misses / n},
            "ll-misses": {"value": ll_misses / n},
            "branch-misses": {"value": branch_misses / n},
        }

    too_small = [
        (name, ms["cycles"]["value"])
        for name, ms in bmf.items()
        if ms["cycles"]["value"] < MIN_CYCLES
    ]
    for name, cycles in too_small:
        print(
            f"benchmark too small: {name}: {cycles:.0f} cycles per"
            f" iteration, the minimum is {MIN_CYCLES}",
            file=sys.stderr,
        )
    if too_small:
        return 1

    for path, primary in ((args.primary, True), (args.details, False)):
        subset = {
            name: {
                m: v for m, v in measures.items() if (m in PRIMARY_MEASURES) == primary
            }
            for name, measures in bmf.items()
        }
        with open(path, "w") as f:
            json.dump(subset, f, indent=2)
            f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
