# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

"""Shared plot-box geometry for the benchmark summary-page plots.

The per-test summary page (see run_benchmark.write_test_summary) stacks several
plots produced by a few tools, all via matplotlib: the vmstat resource plots
(vmstat.py), the piece-pass-order scatter (parse_piece_downloads.py), and the
disk-read-latency heat-map (disk_latency.py). They are all 1200 px wide and
embedded at the same width, so to make them line up every plot pins its plot
box to the same left and right edges, given here as a fraction of the figure
width.

Space to the left of BOX_LEFT holds the y-axis title and tick labels; space to
the right of BOX_RIGHT holds the vmstat plots' secondary-axis labels and the
disk-read-latency colorbar. The values are deliberately generous so those
labels do not clip once the margins are pinned (which disables the auto-fitting
matplotlib would otherwise do).
"""

# left and right edges of the plot box, as a fraction of the figure width
BOX_LEFT = 0.10
BOX_RIGHT = 0.87
