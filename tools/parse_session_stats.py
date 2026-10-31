#!/usr/bin/env python3
# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4

# Copyright (c) 2016, Arvid Norberg
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the distribution.
#     * Neither the name of the author nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# this script can parse and generate reports from the alert log from a
# libtorrent session

import os
import re
import sys
import math
import functools
from multiprocessing import Pool
from pathlib import Path
from argparse import ArgumentParser
from typing import Any

import numpy as np
from matplotlib import colormaps
from matplotlib.colors import to_hex
from matplotlib.figure import Figure
from matplotlib.ticker import EngFormatter

import plot_png

THUMB_WIDTH = 150
THUMB_HEIGHT = 100
GRAPH_WIDTH = 1200

line_graph = 0
histogram = 1
stacked = 2
diff = 3


def graph_colors() -> list[str]:
    # tab20 alternates dark and light variants of 10 hues. Taking the dark ones
    # first makes graphs with up to 10 series use plain tab10, and the light
    # variants only kick in beyond that
    tab20 = [to_hex(c) for c in colormaps["tab20"].colors]
    return tab20[0::2] + tab20[1::2]


def gradient_colors(num_colors: int) -> list[str]:
    colors = []
    for i in range(0, num_colors):
        f = i / float(num_colors)
        pi = 3.1415927
        r = max(int(255 * (math.sin(f * pi) + 0.2)), 0)
        g = max(int(255 * (math.sin((f - 0.5) * pi) + 0.2)), 0)
        b = max(int(255 * (math.sin((f + 0.5) * pi) + 0.2)), 0)
        c = "#%02x%02x%02x" % (min(r, 255), min(g, 255), min(b, 255))
        colors.append(c)
    return colors


def to_title(key: str) -> str:
    return key.replace("_", " ").replace(".", " - ")


def format_bytes(n: int) -> str:
    for suffix in ("B", "KiB", "MiB"):
        if n < 1024 or suffix == "MiB":
            return f"{n} {suffix}"
        n //= 1024
    raise AssertionError


def bucket_range(key: str) -> str:
    # socket_{send,recv}_sizeN counts buffers of size [2^N, 2^(N+1)). The
    # first bucket also holds everything smaller, the last one is unbounded
    # (see session_impl::sent_buffer())
    m = re.fullmatch(r"sock_bufs\.socket_(?:send|recv)_size(\d+)", key)
    if m:
        n = int(m.group(1))
        if n == 20:
            return f">= {format_bytes(1 << n)}"
        lo = 0 if n == 3 else 1 << n
        return f"[{format_bytes(lo)}, {format_bytes(1 << (n + 1))})"

    # disk_read_latencyN counts jobs that took [30 * (N-1), 30 * N) ms, the
    # last bucket is unbounded (see disk_completed_queue.cpp)
    m = re.fullmatch(r"disk\.disk_read_latency(\d+)", key)
    if m:
        n = int(m.group(1))
        if n == 20:
            return f">= {30 * (n - 1)} ms"
        return f"[{30 * (n - 1)}, {30 * n}) ms"
    return ""


def series_label(key: str) -> str:
    rng = bucket_range(key)
    if not rng:
        return to_title(key)
    # the category prefix is just noise for these long series
    return f"{to_title(key.split('.', 1)[1])} {rng}"


@functools.lru_cache(maxsize=1)
def load_data(path: Path) -> np.ndarray:
    return np.loadtxt(path, delimiter="\t", ndmin=2)


def gen_report(
    output_dir: Path,
    keys: list[str],
    name: str,
    unit: str,
    lines: list[str],
    short_unit: str,
    description: str,
    generation: int,
    log_file: Path,
    options: dict[str, Any],
) -> bool:
    filename = output_dir / f"{name}_{generation:04d}.png"
    thumb = output_dir / f"{name}_{generation:04d}_thumb.png"

    # don't re-render a graph unless the logfile has changed
    try:
        dst1 = filename.stat()
        dst2 = thumb.stat()
        src = log_file.stat()

        if dst1.st_mtime > src.st_mtime and dst2.st_mtime > src.st_mtime:
            sys.stdout.write(".")
            sys.stdout.flush()
            return False

    except Exception:
        pass

    data = load_data(log_file)
    if data.shape[0] == 0:
        return False

    series: list[tuple[str, np.ndarray]] = []
    for k in lines:
        if k not in keys:
            print('"%s" not found' % k)
            continue
        series.append((k, data[:, keys.index(k) + 1]))

    if not series:
        return False

    if options.get("cumulative"):
        # rate per sample, the first sample has no predecessor
        series = [(k, np.diff(y, prepend=y[0])) for k, y in series]

    if "normalize" in options:
        norm_key = options["normalize"]
        if norm_key not in keys:
            print('"%s" not found' % norm_key)
            return False
        den = data[:, keys.index(norm_key) + 1]
        if options.get("cumulative"):
            den = np.diff(den, prepend=den[0])
        # no denominator events in a sample means no data, not zero
        series = [
            (k, np.divide(y, den, out=np.full_like(y, np.nan), where=den != 0))
            for k, y in series
        ]

    # converts the counter's native unit into the base unit in short_unit, so
    # that matplotlib can pick the SI prefix
    if "scale" in options:
        series = [(k, y * options["scale"]) for k, y in series]

    colors = graph_colors()
    if "gradient" in options:
        colors = gradient_colors(options["gradient"])
    series_colors = [colors[i % len(colors)] for i in range(len(series))]

    x = data[:, 0]
    fig = Figure(figsize=(12, 7))
    ax = fig.subplots()

    if options["type"] == histogram:
        binwidth = options["binwidth"]
        numbins = int(options["numbins"])
        ax.hist(series[0][1], bins=np.arange(numbins + 1) * binwidth)
        ax.set_xlim(0, binwidth * numbins)
        ax.set_xlabel(unit)
        ax.set_ylabel("number")
    else:
        ax.set_xlabel("time (s)")
        rate = options.get("cumulative") and "normalize" not in options
        ax.set_ylabel(f"{unit} / s" if rate else unit)
        # ratios read better as plain numbers than with an SI prefix
        if "normalize" not in options:
            ax.yaxis.set_major_formatter(EngFormatter(unit=short_unit))

        if options["type"] == stacked:
            ax.stackplot(
                x,
                [y for _, y in series],
                labels=[series_label(k) for k, _ in series],
                colors=series_colors,
                step="post",
            )
        elif options["type"] == diff:
            y = series[0][1] - sum(v for _, v in series[1:])
            ax.step(x, y, where="post", label=" - ".join(series_label(k) for k, _ in series))
        else:
            for (k, y), c in zip(series, series_colors):
                ax.step(x, y, where="post", label=series_label(k), color=c)

    if options["type"] != histogram:
        ax.set_xlim(left=0)
    if "allow-negative" not in options:
        ax.set_ylim(bottom=0)
    ax.spines[["top", "right"]].set_visible(False)
    if options["type"] != histogram:
        ax.legend(loc="best", frameon=True, fontsize="small")

    ax.set_title(f"{name}: {description}" if description else name)
    fig.tight_layout()
    plot_png.save_png(fig, filename)

    legend = ax.get_legend()
    if legend is not None:
        legend.remove()
    ax.set_title("")
    ax.set_xlabel("")
    ax.set_ylabel("")
    ax.set_xticks([])
    ax.set_yticks([])
    # the default line width is heavy at thumbnail size
    for line in ax.lines:
        line.set_linewidth(72.0 / plot_png.DISPLAY_DPI)
    fig.set_size_inches(1.5, 1.0)
    fig.subplots_adjust(left=0, right=1, top=1, bottom=0)
    plot_png.save_png(fig, thumb)

    sys.stdout.write(".")
    sys.stdout.flush()
    return True


categories = ["network", "disk", "bittorrent"]


def gen_html(
    reports: list[tuple[str, str, str, str, list[str], dict[str, Any]]],
    num_generations: int,
    output_dir: Path,
) -> None:
    with open(output_dir / "index.html", "w+") as file:

        css = """img { margin: 0}
.tabs { position: sticky; top: 0; background: white; border-bottom: 1px solid #888; padding-top: 0.5em }
.tabs button { font-size: 1.5em; font-weight: bold; padding: 0.2em 1em; margin-right: 0.2em;
  border: 1px solid #888; border-bottom: none; border-radius: 0.4em 0.4em 0 0;
  background: #eee; cursor: pointer }
.tabs button.active { background: white; margin-bottom: -1px; padding-bottom: calc(0.2em + 1px) }
.pane { display: none }
.pane.active { display: block }
.pane > div { margin: 0.5em 0 0.5em 1.5em }
.graphs { white-space:nowrap; }
dialog { padding: 0; border: none; max-width: 100vw; max-height: 100vh; overflow: auto; cursor: pointer }
dialog img { display: block; max-width: none; width: %dpx }
h2 { line-height: 1; display: inline; font-size: 1.2em }
.desc { line-height: 1; display: inline; font-size: 1em; font-weight: normal }"""

        script = """function show(name) {
  for (const e of document.querySelectorAll('.tabs button, .pane')) {
    e.classList.toggle('active', e.dataset.tab === name);
  }
  history.replaceState(null, '', '#' + name);
}
for (const b of document.querySelectorAll('.tabs button')) {
  b.addEventListener('click', () => show(b.dataset.tab));
}
const viewer = document.getElementById('viewer');
document.addEventListener('click', (e) => {
  const a = e.target.closest('.graphs a');
  if (!a || e.button !== 0 || e.ctrlKey || e.metaKey || e.shiftKey) return;
  e.preventDefault();
  document.getElementById('full').src = a.getAttribute('href');
  viewer.showModal();
});
viewer.addEventListener('click', () => viewer.close());
const initial = location.hash.slice(1);
show(document.querySelector('.tabs button[data-tab="' + initial + '"]')
  ? initial : document.querySelector('.tabs button').dataset.tab);"""

        print(
            '<html><head><style type="text/css">%s</style></head><body>' % (css % GRAPH_WIDTH),
            file=file,
        )

        print('<div class="tabs">', file=file)
        for category in categories:
            print('<button data-tab="%s">%s</button>' % (category, category), file=file)
        print("</div>", file=file)

        for category in categories:
            print('<div class="pane" data-tab="%s">' % category, file=file)
            for i in reports:
                if i[5]["category"] != category:
                    continue
                print(
                    '<div><h2>%s </h2><span class="desc">%s</span><div class="graphs">'
                    % (i[0], i[3]),
                    file=file,
                )
                for g in range(num_generations):
                    print(
                        '<a href="%s_%04d.png"><img loading="lazy" width="%d" height="%d" src="%s_%04d_thumb.png"></a>'
                        % (i[0], g, THUMB_WIDTH, THUMB_HEIGHT, i[0], g),
                        file=file,
                    )
                print("</div></div>", file=file)
            print("</div>", file=file)

        print('<dialog id="viewer"><img id="full"></dialog>', file=file)
        print("<script>%s</script>" % script, file=file)
        print("</body></html>", file=file)


def write_generation(path: Path, rows: list[str]) -> None:
    with open(path, "w") as f:
        f.writelines(rows)


def main(input_file: Path, num_threads: int, output_dir: Path) -> None:
    pool = Pool(num_threads)
    output_dir.mkdir(parents=True, exist_ok=True)
    g = 0
    with open(input_file) as stat:
        print("looking for stats header")
        while True:
            line = stat.readline()
            if not line:
                raise RuntimeError(
                    f"no 'session stats header:' line found in {input_file}"
                )
            if "session stats header:" in line:
                break

        print("found")
        keys = line.split("session stats header:")[1].strip().split(", ")

        rows: list[str] = []
        for line in stat:
            if "session stats (" not in line:
                continue
            rows.append(
                f"{len(rows)}\t" + line.split(" values): ")[1].strip().replace(", ", "\t")
                + "\n"
            )
            if len(rows) >= 3600:
                write_generation(output_dir / f"counters.{g:04}.dat", rows)
                g += 1
                rows = []

    # an input without samples still gets one (empty) generation
    if rows or g == 0:
        write_generation(output_dir / f"counters.{g:04}.dat", rows)
        g += 1

    num_generations = g

    reports: list[tuple[str, str, str, str, list[str], dict[str, Any]]] = [
        (
            "torrents",
            "num",
            "",
            "number of torrents in different torrent states",
            [
                "ses.num_downloading_torrents",
                "ses.num_seeding_torrents",
                "ses.num_checking_torrents",
                "ses.num_stopped_torrents",
                "ses.num_upload_only_torrents",
                "ses.num_error_torrents",
                "ses.num_queued_seeding_torrents",
                "ses.num_queued_download_torrents",
            ],
            {"category": "bittorrent", "type": stacked},
        ),
        (
            "peers",
            "num",
            "",
            "num connected peers",
            ["peer.num_peers_connected", "peer.num_peers_half_open"],
            {"category": "network", "type": stacked},
        ),
        (
            "peers_max",
            "num",
            "",
            "num connected peers",
            ["peer.num_peers_connected", "peer.num_peers_half_open"],
            {"category": "network"},
        ),
        (
            "peer_churn",
            "num",
            "",
            "connecting and disconnecting peers",
            [
                "peer.num_peers_half_open",
                "peer.connection_attempts",
                "peer.boost_connection_attempts",
                "peer.missed_connection_attempts",
                "peer.no_peer_connection_attempts",
            ],
            {"category": "network"},
        ),
        (
            "new_peers",
            "num",
            "",
            "",
            ["peer.incoming_connections", "peer.connection_attempts"],
            {"category": "network", "cumulative": 1},
        ),
        (
            "connection_attempts",
            "num",
            "",
            "",
            ["peer.connection_attempt_loops", "peer.connection_attempts"],
            {"category": "network", "cumulative": 1},
        ),
        (
            "pieces",
            "num",
            "",
            "number completed pieces",
            [
                "ses.num_total_pieces_added",
                "ses.num_piece_passed",
                "ses.num_piece_failed",
            ],
            {"category": "bittorrent", "cumulative": 1},
        ),
        (
            "disk_write_queue",
            "Bytes",
            "B",
            "bytes queued up by peers, to be written to disk",
            ["disk.queued_write_bytes"],
            {"category": "disk"},
        ),
        (
            "peers_requests",
            "num",
            "",
            "incoming piece request rate",
            [
                "peer.piece_requests",
                "peer.max_piece_requests",
                "peer.invalid_piece_requests",
                "peer.choked_piece_requests",
                "peer.cancelled_piece_requests",
            ],
            {"category": "bittorrent", "cumulative": 1},
        ),
        (
            "peers_upload",
            "num",
            "",
            "number of peers by state wrt. uploading",
            [
                "peer.num_peers_up_disk",
                "peer.num_peers_up_interested",
                "peer.num_peers_up_unchoked_all",
                "peer.num_peers_up_unchoked_optimistic",
                "peer.num_peers_up_unchoked",
                "peer.num_peers_up_requests",
            ],
            {"category": "bittorrent"},
        ),
        (
            "peers_download",
            "num",
            "",
            "number of peers by state wrt. downloading",
            [
                "peer.num_peers_down_interested",
                "peer.num_peers_down_unchoked",
                "peer.num_peers_down_requests",
                "peer.num_peers_down_disk",
            ],
            {"category": "bittorrent"},
        ),
        (
            "peer_errors",
            "num",
            "",
            "number of peers by error that disconnected them",
            [
                "peer.disconnected_peers",
                "peer.eof_peers",
                "peer.connreset_peers",
                "peer.connrefused_peers",
                "peer.connaborted_peers",
                "peer.perm_peers",
                "peer.buffer_peers",
                "peer.unreachable_peers",
                "peer.broken_pipe_peers",
                "peer.addrinuse_peers",
                "peer.no_access_peers",
                "peer.invalid_arg_peers",
                "peer.aborted_peers",
            ],
            {"category": "network", "type": stacked, "cumulative": 1},
        ),
        (
            "peer_errors_incoming",
            "num",
            "",
            "number of peers by incoming or outgoing connection",
            ["peer.error_incoming_peers", "peer.error_outgoing_peers"],
            {"category": "network", "cumulative": 1},
        ),
        (
            "peer_errors_transport",
            "num",
            "",
            "number of peers by transport protocol",
            ["peer.error_tcp_peers", "peer.error_utp_peers"],
            {"category": "network", "cumulative": 1},
        ),
        (
            "peer_errors_encryption",
            "num",
            "",
            "number of peers by encryption level",
            [
                "peer.error_encrypted_peers",
                "peer.error_rc4_peers",
            ],
            {"category": "network", "cumulative": 1},
        ),
        (
            "incoming requests",
            "num",
            "",
            "incoming 16kiB block requests",
            ["ses.num_incoming_request"],
            {"category": "bittorrent", "cumulative": 1},
        ),
        (
            "waste",
            "downloaded bytes",
            "B",
            "proportion of all downloaded bytes that were wasted",
            [
                "net.recv_failed_bytes",
                "net.recv_redundant_bytes",
                "net.recv_ip_overhead_bytes",
            ],
            {"category": "bittorrent", "type": stacked, "cumulative": 1},
        ),
        (
            "waste by source",
            "num wasted bytes",
            "B",
            "what is causing the waste",
            [
                "ses.waste_piece_timed_out",
                "ses.waste_piece_cancelled",
                "ses.waste_piece_unknown",
                "ses.waste_piece_seed",
                "ses.waste_piece_end_game",
                "ses.waste_piece_closing",
            ],
            {"category": "bittorrent", "type": stacked, "cumulative": 1},
        ),
        (
            "disk_time",
            "% of total disk job time",
            "%",
            "proportion of time spent by the disk thread",
            ["disk.disk_read_time", "disk.disk_write_time", "disk.disk_hash_time"],
            {
                "category": "disk",
                "type": stacked,
                "cumulative": 1,
                "normalize": "disk.disk_job_time",
                "scale": 100,
            },
        ),
        (
            "disk_queue",
            "blocks (16kiB)",
            "",
            "disk store-buffer size",
            [
                "disk.num_write_jobs",
                "disk.num_read_jobs",
                "disk.num_jobs",
                "disk.queued_disk_jobs",
                "disk.blocked_disk_jobs",
            ],
            {"category": "disk"},
        ),
        (
            "disk fences",
            "num",
            "",
            "number of jobs currently blocked by a fence job",
            ["disk.blocked_disk_jobs"],
            {"category": "disk"},
        ),
        (
            "fence jobs",
            "num",
            "",
            "active fence jobs per type",
            [
                "disk.num_fenced_move_storage",
                "disk.num_fenced_release_files",
                "disk.num_fenced_delete_files",
                "disk.num_fenced_check_fastresume",
                "disk.num_fenced_save_resume_data",
                "disk.num_fenced_rename_file",
                "disk.num_fenced_stop_torrent",
                "disk.num_fenced_file_priority",
                "disk.num_fenced_clear_piece",
            ],
            {"category": "disk", "type": stacked},
        ),
        (
            "disk threads",
            "num",
            "",
            "number of disk threads currently writing",
            ["disk.num_writing_threads", "disk.num_running_threads"],
            {"category": "disk"},
        ),
        (
            "connection_type",
            "num",
            "",
            "peers by transport protocol",
            [
                "peer.num_tcp_peers",
                "peer.num_socks5_peers",
                "peer.num_http_proxy_peers",
                "peer.num_utp_peers",
                "peer.num_i2p_peers",
                "peer.num_ssl_peers",
                "peer.num_ssl_socks5_peers",
                "peer.num_ssl_http_proxy_peers",
                "peer.num_ssl_utp_peers",
            ],
            {"category": "network"},
        ),
        # (
        #     "uTP delay",
        #     "buffering delay",
        #     "s",
        #     "network delays measured by uTP",
        #     [
        #         "uTP peak send delay",
        #         "uTP peak recv delay",
        #         "uTP avg send delay",
        #         "uTP avg recv delay",
        #     ],
        # ),
        # (
        #     "uTP send delay histogram",
        #     "buffering delay",
        #     "s",
        #     "send delays measured by uTP",
        #     ["uTP avg send delay"],
        #     {"type": histogram, "binwidth": 0.05, "numbins": 100},
        # ),
        # (
        #     "uTP recv delay histogram",
        #     "buffering delay",
        #     "s",
        #     "receive delays measured by uTP",
        #     ["uTP avg recv delay"],
        #     {"type": histogram, "binwidth": 0.05, "numbins": 100},
        # ),
        (
            "uTP stats",
            "num",
            "",
            "number of uTP events",
            [
                "utp.utp_packet_loss",
                "utp.utp_timeout",
                "utp.utp_packets_in",
                "utp.utp_packets_out",
                "utp.utp_fast_retransmit",
                "utp.utp_packet_resend",
                "utp.utp_samples_above_target",
                "utp.utp_samples_below_target",
                "utp.utp_payload_pkts_in",
                "utp.utp_payload_pkts_out",
                "utp.utp_invalid_pkts_in",
                "utp.utp_redundant_pkts_in",
            ],
            {"category": "network", "type": stacked, "cumulative": 1},
        ),
        (
            "boost.asio messages",
            "num events",
            "",
            "number of messages posted",
            [
                "net.on_read_counter",
                "net.on_write_counter",
                "net.on_tick_counter",
                "net.on_lsd_counter",
                "net.on_lsd_peer_counter",
                "net.on_udp_counter",
                "net.on_accept_counter",
                "net.on_disk_counter",
            ],
            {"category": "network", "type": stacked, "cumulative": 1},
        ),
        (
            "send_buffer_sizes",
            "num",
            "",
            "",
            [
                "sock_bufs.socket_send_size3",
                "sock_bufs.socket_send_size4",
                "sock_bufs.socket_send_size5",
                "sock_bufs.socket_send_size6",
                "sock_bufs.socket_send_size7",
                "sock_bufs.socket_send_size8",
                "sock_bufs.socket_send_size9",
                "sock_bufs.socket_send_size10",
                "sock_bufs.socket_send_size11",
                "sock_bufs.socket_send_size12",
                "sock_bufs.socket_send_size13",
                "sock_bufs.socket_send_size14",
                "sock_bufs.socket_send_size15",
                "sock_bufs.socket_send_size16",
                "sock_bufs.socket_send_size17",
                "sock_bufs.socket_send_size18",
                "sock_bufs.socket_send_size19",
                "sock_bufs.socket_send_size20",
            ],
            {"category": "network", "type": stacked, "gradient": 18, "cumulative": 1},
        ),
        (
            "recv_buffer_sizes",
            "num",
            "",
            "",
            [
                "sock_bufs.socket_recv_size3",
                "sock_bufs.socket_recv_size4",
                "sock_bufs.socket_recv_size5",
                "sock_bufs.socket_recv_size6",
                "sock_bufs.socket_recv_size7",
                "sock_bufs.socket_recv_size8",
                "sock_bufs.socket_recv_size9",
                "sock_bufs.socket_recv_size10",
                "sock_bufs.socket_recv_size11",
                "sock_bufs.socket_recv_size12",
                "sock_bufs.socket_recv_size13",
                "sock_bufs.socket_recv_size14",
                "sock_bufs.socket_recv_size15",
                "sock_bufs.socket_recv_size16",
                "sock_bufs.socket_recv_size17",
                "sock_bufs.socket_recv_size18",
                "sock_bufs.socket_recv_size19",
                "sock_bufs.socket_recv_size20",
            ],
            {"category": "network", "type": stacked, "gradient": 18, "cumulative": 1},
        ),
        (
            "request latency",
            "latency",
            "s",
            "latency from receiving requests to sending response",
            ["disk.request_latency"],
            {"category": "disk", "scale": 1e-6},
        ),
        (
            "disk_read_latency",
            "num",
            "",
            "histogram of disk read-job latency. cumulative counts; the slope of each band is that"
            " bucket's rate over time",
            [
                "disk.disk_read_latency1",
                "disk.disk_read_latency2",
                "disk.disk_read_latency3",
                "disk.disk_read_latency4",
                "disk.disk_read_latency5",
                "disk.disk_read_latency6",
                "disk.disk_read_latency7",
                "disk.disk_read_latency8",
                "disk.disk_read_latency9",
                "disk.disk_read_latency10",
                "disk.disk_read_latency11",
                "disk.disk_read_latency12",
                "disk.disk_read_latency13",
                "disk.disk_read_latency14",
                "disk.disk_read_latency15",
                "disk.disk_read_latency16",
                "disk.disk_read_latency17",
                "disk.disk_read_latency18",
                "disk.disk_read_latency19",
                "disk.disk_read_latency20",
            ],
            {"category": "disk", "type": stacked, "gradient": 20, "cumulative": 1},
        ),
        (
            "incoming messages",
            "num",
            "",
            "number of received bittorrent messages, by type",
            [
                "ses.num_incoming_choke",
                "ses.num_incoming_unchoke",
                "ses.num_incoming_interested",
                "ses.num_incoming_not_interested",
                "ses.num_incoming_have",
                "ses.num_incoming_bitfield",
                "ses.num_incoming_request",
                "ses.num_incoming_piece",
                "ses.num_incoming_cancel",
                "ses.num_incoming_dht_port",
                "ses.num_incoming_suggest",
                "ses.num_incoming_have_all",
                "ses.num_incoming_have_none",
                "ses.num_incoming_reject",
                "ses.num_incoming_allowed_fast",
                "ses.num_incoming_ext_handshake",
                "ses.num_incoming_pex",
                "ses.num_incoming_metadata",
                "ses.num_incoming_extended",
            ],
            {"category": "bittorrent", "type": stacked, "cumulative": 1},
        ),
        (
            "outgoing messages",
            "num",
            "",
            "number of sent bittorrent messages, by type",
            [
                "ses.num_outgoing_choke",
                "ses.num_outgoing_unchoke",
                "ses.num_outgoing_interested",
                "ses.num_outgoing_not_interested",
                "ses.num_outgoing_have",
                "ses.num_outgoing_bitfield",
                "ses.num_outgoing_request",
                "ses.num_outgoing_piece",
                "ses.num_outgoing_cancel",
                "ses.num_outgoing_dht_port",
                "ses.num_outgoing_suggest",
                "ses.num_outgoing_have_all",
                "ses.num_outgoing_have_none",
                "ses.num_outgoing_reject",
                "ses.num_outgoing_allowed_fast",
                "ses.num_outgoing_ext_handshake",
                "ses.num_outgoing_pex",
                "ses.num_outgoing_metadata",
                "ses.num_outgoing_extended",
            ],
            {"category": "bittorrent", "type": stacked, "cumulative": 1},
        ),
        (
            "request in balance",
            "num",
            "",
            "request and piece message balance",
            [
                "ses.num_incoming_request",
                "ses.num_outgoing_piece",
                "ses.num_outgoing_reject",
            ],
            {"category": "bittorrent", "type": diff},
        ),
        (
            "request out balance",
            "num",
            "",
            "request and piece message balance",
            [
                "ses.num_outgoing_request",
                "ses.num_incoming_piece",
                "ses.num_incoming_reject",
            ],
            {"category": "bittorrent", "type": diff},
        ),
        (
            "piece_picker_invocations",
            "invocations of piece picker",
            "",
            "",
            [
                "picker.reject_piece_picks",
                "picker.unchoke_piece_picks",
                "picker.incoming_redundant_piece_picks",
                "picker.incoming_piece_picks",
                "picker.end_game_piece_picks",
                "picker.snubbed_piece_picks",
                "picker.interesting_piece_picks",
                "picker.hash_fail_piece_picks",
            ],
            {"category": "bittorrent", "type": stacked, "cumulative": 1},
        ),
        (
            "piece_picker_loops",
            "loops through piece picker",
            "",
            "",
            [
                "picker.piece_picker_partial_loops",
                "picker.piece_picker_suggest_loops",
                "picker.piece_picker_sequential_loops",
                "picker.piece_picker_reverse_rare_loops",
                "picker.piece_picker_rare_loops",
                "picker.piece_picker_rand_start_loops",
                "picker.piece_picker_rand_loops",
                "picker.piece_picker_busy_loops",
            ],
            {"category": "bittorrent", "type": stacked, "cumulative": 1},
        ),
        (
            "async_accept",
            "number of outstanding accept calls",
            "",
            "",
            ["ses.num_outstanding_accept"],
            {"category": "network"},
        ),
        (
            "queued_trackers",
            "number of queued tracker announces",
            "",
            "",
            ["tracker.num_queued_tracker_announces"],
            {"category": "bittorrent"},
        ),
        (
            "file_pool_size",
            "file pool sze",
            "",
            "",
            ["disk.file_pool_size"],
            {"category": "disk"},
        ),
        (
            "file_pool_misses",
            "file pool cache misses",
            "",
            "",
            ["disk.file_pool_misses", "disk.file_pool_thread_stall", "disk.file_pool_race"],
            {"category": "disk", "cumulative": 1},
        ),
        # (
        #     "picker_full_partials_distribution",
        #     "full pieces",
        #     "",
        #     "",
        #     ["num full partial pieces"],
        #     {"type": histogram, "binwidth": 5, "numbins": 120},
        # ),
        # (
        #     "picker_partials_distribution",
        #     "partial pieces",
        #     "",
        #     "",
        #     ["num downloading partial pieces"],
        #     {"type": histogram, "binwidth": 5, "numbins": 120},
        # ),
        (
            "disk_cache",
            "blocks",
            "",
            "",
            [ "disk.cached_blocks", "disk.disk_blocks_in_use", "disk.num_unhashed" ],
            {"category": "disk"},
        ),
        (
            "disk_io",
            "disk operations",
            "",
            "",
            [ "disk.num_write_ops", "disk.num_read_back", "disk.num_blocks_hashed"],
            {"category": "disk", "cumulative": 1},
        ),
        (
            "blocks per read op",
            "blocks per read call",
            "",
            "average number of blocks read per read operation",
            ["disk.num_blocks_read"],
            {"category": "disk", "cumulative": 1, "normalize": "disk.num_read_ops"},
        ),
        (
            "blocks per write op",
            "blocks per write call",
            "",
            "average number of blocks written per read operation",
            ["disk.num_blocks_written"],
            {"category": "disk", "cumulative": 1, "normalize": "disk.num_write_ops"},
        ),
        (
            "read ahead hit rate",
            "num",
            "",
            "forward block requests that landed inside a still-fresh "
            "read-ahead window (hit) versus ones that didn't (miss); the "
            "hit share of the stack is the predictor's accuracy",
            ["disk.num_read_ahead_hits", "disk.num_read_ahead_misses"],
            {"category": "disk", "type": stacked, "cumulative": 1},
        ),
        (
            "read ahead waste",
            "% of read-ahead blocks wasted",
            "%",
            "of the blocks speculatively issued for read-ahead, the "
            "fraction never subsequently requested before being evicted",
            ["disk.num_read_ahead_wasted_blocks"],
            {
                "category": "disk",
                "cumulative": 1,
                "normalize": "disk.num_read_ahead_issued_blocks",
                "scale": 100,
            },
        ),
    ]

    print("generating graphs")

    for g in range(num_generations):
        tasks = []
        print(f"[{' ' * len(reports)}] {g:04}\r[", end="", flush=True)
        for rep in reports:
            try:
                options = rep[5]
            except Exception:
                options = {}
            if "type" not in options:
                options["type"] = line_graph

            tasks.append(
                (
                    output_dir,
                    keys,
                    rep[0],
                    rep[1],
                    rep[4],
                    rep[2],
                    rep[3],
                    g,
                    output_dir / f"counters.{g:04}.dat",
                    options,
                )
            )

        pool.starmap(gen_report, tasks)
        print()

    pool.close()
    pool.join()

    print("\ngenerating html")
    gen_html(reports, num_generations, output_dir)


if __name__ == "__main__":
    p = ArgumentParser()
    p.add_argument(
        "input",
        type=Path,
        help="libtorrent log file to parse. It must include the session_stats log entries",
    )
    p.add_argument(
        "--threads", default=os.cpu_count() or 1, type=int, help="The number of processes to render graphs in"
    )
    p.add_argument(
        "--output-dir",
        default="./session_stats_report",
        type=Path,
        help="The directory to save the output files in",
    )

    args = p.parse_args()
    main(args.input, args.threads, args.output_dir)
