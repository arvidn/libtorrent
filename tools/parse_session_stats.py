#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

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
import sys
import math
from multiprocessing.pool import ThreadPool
from pathlib import Path
from argparse import ArgumentParser
from typing import Optional


line_graph = 0
histogram = 1
stacked = 2
diff = 3


def graph_colors() -> list[str]:
    colors: list[str] = []

    pattern = [[0, 0, 1], [0, 1, 0], [1, 0, 0], [1, 0, 1], [0, 1, 1], [1, 1, 0]]
    brightness = [0xD8, 0xBB, 0x60]

    for op in range(3):
        for c in pattern:
            c = [v * brightness[op] for v in c]
            colors.append("#%02x%02x%02x" % (c[0], c[1], c[2]))
    return colors


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


def plot_fun(script: Path) -> None:
    try:
        ret = os.system('gnuplot "%s" 2>/dev/null' % script)
    except Exception as e:
        print("please install gnuplot: sudo apt install gnuplot")
        raise e
    if ret != 0 and ret != 256:
        print("gnuplot failed: %d\n" % ret)
        raise Exception("abort")

    sys.stdout.write(".")
    sys.stdout.flush()


def to_title(key: str) -> str:
    return key.replace("_", " ").replace(".", " - ")


def gen_report(
    output_dir: Path,
    keys: list[str],
    name: str,
    unit: str,
    lines: list[str],
    short_unit: str,
    generation: int,
    log_file: Path,
    options: dict[str, int],
) -> Optional[Path]:
    filename = output_dir / f"{name}_{generation:04d}.png"
    thumb = output_dir / f"{name}_{generation:04d}_thumb.png"

    # don't re-render a graph unless the logfile has changed
    try:
        dst1 = filename.stat()
        dst2 = thumb.stat()
        src = log_file.stat()

        if dst1.st_mtime > src.st_mtime and dst2.st_mtime > src.st_mtime:
            sys.stdout.write(".")
            return None

    except Exception:
        pass

    script = output_dir / f"{name}_{generation:04d}.gnuplot"
    with open(script, "w") as out:
        print("set term png size 1200,700", file=out)
        print('set output "%s"' % filename, file=out)
        if "allow-negative" not in options:
            print("set yrange [0:*]", file=out)
        print("set tics nomirror", file=out)
        print("set key box", file=out)
        print("set key left top", file=out)

        colors = graph_colors()

        try:
            if "gradient" in options:
                colors = gradient_colors(options["gradient"])
        except Exception:
            pass

        first = True
        color = 0
        if options["type"] == histogram:
            binwidth = options["binwidth"]
            numbins = int(options["numbins"])

            print("binwidth=%f" % binwidth, file=out)
            print("set boxwidth binwidth", file=out)
            print("bin(x,width)=width*floor(x/width) + binwidth/2", file=out)
            print("set xrange [0:%f]" % (binwidth * numbins), file=out)
            print('set xlabel "%s"' % unit, file=out)
            print('set ylabel "number"', file=out)

            k = lines[0]
            try:
                column = keys.index(k) + 2
            except Exception:
                print('"%s" not found' % k)
                return None
            print(
                'plot "%s" using (bin($%d,binwidth)):(1.0) smooth freq with boxes'
                % (log_file, column),
                file=out,
            )
            print("", file=out)
            print("", file=out)
            print("", file=out)

        elif options["type"] == stacked:
            print("set xrange [0:*]", file=out)
            print('set ylabel "%s"' % unit, file=out)
            print('set xlabel "time (s)"', file=out)
            print('set format y "%%.1s%%c%s";' % short_unit, file=out)
            print("set style fill solid 1.0 noborder", file=out)
            print("plot", end=" ", file=out)
            graph = ""
            plot_expression = ""
            for k in lines:
                try:
                    column = keys.index(k) + 2
                except Exception:
                    print('"%s" not found' % k)
                    continue
                if not first:
                    plot_expression = ", " + plot_expression
                    graph += "+"
                axis = "x1y1"
                graph += "$%d" % column
                plot_expression = (
                    ' "%s" using 1:(%s) title "%s" axes %s with filledcurves x1 lc rgb "%s"'
                    % (log_file, graph, to_title(k), axis, colors[color % len(colors)])
                    + plot_expression
                )
                first = False
                color += 1
            print(plot_expression, file=out)
        elif options["type"] == diff:
            print("set xrange [0:*]", file=out)
            print('set ylabel "%s"' % unit, file=out)
            print('set xlabel "time (s)"', file=out)
            print('set format y "%%.1s%%c%s";' % short_unit, file=out)
            graph = ""
            title = ""
            for k in lines:
                try:
                    column = keys.index(k) + 2
                except Exception:
                    print('"%s" not found' % k)
                    continue
                if not first:
                    graph += "-"
                    title += " - "
                graph += "$%d" % column
                title += to_title(k)
                first = False
            print(
                'plot "%s" using 1:(%s) title "%s" with step'
                % (log_file, graph, title),
                file=out,
            )
        else:
            print("set xrange [0:*]", file=out)
            print('set ylabel "%s"' % unit, file=out)
            print('set xlabel "time (s)"', file=out)
            print('set format y "%%.1s%%c%s";' % short_unit, file=out)
            print("plot", end=" ", file=out)
            for k in lines:
                try:
                    column = keys.index(k) + 2
                except Exception:
                    print('"%s" not found' % k)
                    continue
                if not first:
                    print(", ", end=" ", file=out)
                axis = "x1y1"
                print(
                    ' "%s" using 1:%d title "%s" axes %s with steps lc rgb "%s"'
                    % (
                        log_file,
                        column,
                        to_title(k),
                        axis,
                        colors[color % len(colors)],
                    ),
                    end=" ",
                    file=out,
                )
                first = False
                color += 1
            print("", file=out)

        print("set term png size 150,100", file=out)
        print('set output "%s"' % thumb, file=out)
        print("set key off", file=out)
        print("unset tics", file=out)
        print('set format x ""', file=out)
        print('set format y ""', file=out)
        print('set xlabel ""', file=out)
        print('set ylabel ""', file=out)
        print('set y2label ""', file=out)
        print("set rmargin 0", file=out)
        print("set lmargin 0", file=out)
        print("set tmargin 0", file=out)
        print("set bmargin 0", file=out)
        print("replot", file=out)
    return script


def gen_html(
    reports: list[tuple[str, str, str, str, list[str], dict[str, int]]],
    generations: list[int],
    output_dir: Path,
) -> None:
    with open(output_dir / "index.html", "w+") as file:

        css = """img { margin: 0}
#head { display: block }
#graphs { white-space:nowrap; }
h1 { line-height: 1; display: inline }
h2 { line-height: 1; display: inline; font-size: 1em; font-weight: normal};"""

        print(
            '<html><head><style type="text/css">%s</style></head><body>' % css,
            file=file,
        )

        for i in reports:
            print(
                '<div id="head"><h1>%s </h1><h2>%s</h2><div><div id="graphs">'
                % (i[0], i[3]),
                file=file,
            )
            for g in generations:
                print(
                    '<a href="%s_%04d.png"><img src="%s_%04d_thumb.png"></a>'
                    % (i[0], g, i[0], g),
                    file=file,
                )
            print("</div>", file=file)

        print("</body></html>", file=file)


def main(input_file: Path, num_threads: int, output_dir: Path) -> None:
    thread_pool = ThreadPool(num_threads)
    output_dir.mkdir(parents=True, exist_ok=True)

    with open(input_file) as stat, open(output_dir / "counters.dat", "w+") as data_out:
        line = stat.readline()
        print("looking for stats header")
        while "session stats header:" not in line:
            line = stat.readline()

        print("found")
        keys = line.split("session stats header:")[1].strip().split(", ")

        idx = 0
        for line in stat:
            if "session stats (" not in line:
                continue
            data_out.write(
                ("%d\t" % idx)
                + line.split(" values): ")[1].strip().replace(", ", "\t")
                + "\n"
            )
            idx += 1

        data_out.close()

        reports: list[tuple[str, str, str, str, list[str], dict[str, int]]] = [
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
                {"type": stacked},
            ),
            (
                "peers",
                "num",
                "",
                "num connected peers",
                ["peer.num_peers_connected", "peer.num_peers_half_open"],
                {"type": stacked},
            ),
            (
                "peers_max",
                "num",
                "",
                "num connected peers",
                ["peer.num_peers_connected", "peer.num_peers_half_open"],
                {},
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
                {},
            ),
            (
                "new_peers",
                "num",
                "",
                "",
                ["peer.incoming_connections", "peer.connection_attempts"],
                {},
            ),
            (
                "connection_attempts",
                "num",
                "",
                "",
                ["peer.connection_attempt_loops", "peer.connection_attempts"],
                {},
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
                {},
            ),
            (
                "disk_write_queue",
                "Bytes",
                "B",
                "bytes queued up by peers, to be written to disk",
                ["disk.queued_write_bytes"],
                {},
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
                {},
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
                {},
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
                {},
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
                {"type": stacked},
            ),
            (
                "peer_errors_incoming",
                "num",
                "",
                "number of peers by incoming or outgoing connection",
                ["peer.error_incoming_peers", "peer.error_outgoing_peers"],
                {},
            ),
            (
                "peer_errors_transport",
                "num",
                "",
                "number of peers by transport protocol",
                ["peer.error_tcp_peers", "peer.error_utp_peers"],
                {},
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
                {},
            ),
            (
                "incoming requests",
                "num",
                "",
                "incoming 16kiB block requests",
                ["ses.num_incoming_request"],
                {},
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
                {"type": stacked},
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
                {"type": stacked},
            ),
            (
                "disk_time",
                "% of total disk job time",
                "%%",
                "proportion of time spent by the disk thread",
                ["disk.disk_read_time", "disk.disk_write_time", "disk.disk_hash_time"],
                {"type": stacked},
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
                {},
            ),
            (
                "disk fences",
                "num",
                "",
                "number of jobs currently blocked by a fence job",
                ["disk.blocked_disk_jobs"],
                {},
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
                {"type": stacked},
            ),
            (
                "disk threads",
                "num",
                "",
                "number of disk threads currently writing",
                ["disk.num_writing_threads", "disk.num_running_threads"],
                {},
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
                {},
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
                {"type": stacked},
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
                {"type": stacked},
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
                {"type": stacked, "gradient": 18},
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
                {"type": stacked, "gradient": 18},
            ),
            (
                "request latency",
                "us",
                "",
                "latency from receiving requests to sending response",
                ["disk.request_latency"],
                {},
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
                {"type": stacked},
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
                {"type": stacked},
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
                {"type": diff},
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
                {"type": diff},
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
                {"type": stacked},
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
                {"type": stacked},
            ),
            (
                "async_accept",
                "number of outstanding accept calls",
                "",
                "",
                ["ses.num_outstanding_accept"],
                {},
            ),
            (
                "queued_trackers",
                "number of queued tracker announces",
                "",
                "",
                ["tracker.num_queued_tracker_announces"],
                {},
            ),
            (
                "file_pool_size",
                "file pool sze",
                "",
                "",
                ["disk.file_pool_size"],
                {},
            ),
            (
                "file_pool_misses",
                "file pool cache misses",
                "",
                "",
                ["disk.file_pool_misses", "disk.file_pool_thread_stall", "disk.file_pool_race"],
                {},
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
        ]

        print("generating graphs")
        g = 0
        generations = []
        scripts: list[Path] = []

        print("[%s] %04d\r[" % (" " * len(reports), g), end="")
        for rep in reports:
            try:
                options = rep[5]
            except Exception:
                options = {}
            if "type" not in options:
                options["type"] = line_graph

            script = gen_report(
                output_dir,
                keys,
                rep[0],
                rep[1],
                rep[4],
                rep[2],
                g,
                output_dir / "counters.dat",
                options,
            )
            if script is not None:
                scripts.append(script)

        generations.append(g)
        g += 1

        # run gnuplot on all scripts, in parallel
        thread_pool.map(plot_fun, scripts)
        scripts = []

        print("\ngenerating html")
        gen_html(reports, generations, output_dir)


if __name__ == "__main__":
    p = ArgumentParser()
    p.add_argument(
        "input",
        type=Path,
        help="libtorrent log file to parse. It must include the session_stats log entries",
    )
    p.add_argument(
        "--threads", default=8, type=int, help="The number of threads to run gnuplot in"
    )
    p.add_argument(
        "--output-dir",
        default="./session_stats_report",
        type=Path,
        help="The directory to save the output files in",
    )

    args = p.parse_args()
    main(args.input, args.threads, args.output_dir)
