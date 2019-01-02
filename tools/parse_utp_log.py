#!/usr/bin/env python
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

from __future__ import print_function

import os
import sys
from functools import reduce

# usage: parse_log.py log-file [socket-index to focus on]


socket_filter = None
if len(sys.argv) >= 3:
    socket_filter = sys.argv[2].strip()

if socket_filter is None:
    print("scanning for socket with the most packets")
    file = open(sys.argv[1], 'rb')

    sockets = {}

    for l in file:
        if 'our_delay' not in l:
            continue

        try:
            a = l.strip().split(" ")
            socket_index = a[1][:-1]
        except Exception:
            continue

        # msvc's runtime library doesn't prefix pointers
        # with '0x'
        # if socket_index[:2] != '0x':
        #     continue

        if socket_index in sockets:
            sockets[socket_index] += 1
        else:
            sockets[socket_index] = 1

    items = list(sockets.items())
    items.sort(lambda x, y: y[1] - x[1])

    count = 0
    for i in items:
        print('%s: %d' % (i[0], i[1]))
        count += 1
        if count > 5:
            break

    file.close()
    socket_filter = items[0][0]
    print('\nfocusing on socket %s' % socket_filter)

file = open(sys.argv[1], 'rb')
out_file = 'utp.out%s' % socket_filter
out = open(out_file, 'wb')

delay_samples = 'points lc rgb "blue"'
delay_base = 'steps lw 2 lc rgb "purple"'
target_delay = 'steps lw 2 lc rgb "red"'
off_target = 'dots lc rgb "blue"'
cwnd = 'steps lc rgb "green" lw 2'
window_size = 'steps lc rgb "sea-green"'
rtt = 'lines lc rgb "light-blue"'
send_buffer = 'lines lc rgb "light-red"'

metrics = {
    'our_delay': ['our delay (ms)', 'x1y2', delay_samples],
    'upload_rate': ['send rate (B/s)', 'x1y1', 'lines'],
    'max_window': ['cwnd (B)', 'x1y1', cwnd],
    'target_delay': ['target delay (ms)', 'x1y2', target_delay],
    'cur_window': ['bytes in-flight (B)', 'x1y1', window_size],
    'cur_window_packets': ['number of packets in-flight', 'x1y2', 'steps'],
    'packet_size': ['current packet size (B)', 'x1y2', 'steps'],
    'rtt': ['rtt (ms)', 'x1y2', rtt],
    'off_target': ['off-target (ms)', 'x1y2', off_target],
    'delay_sum': ['delay sum (ms)', 'x1y2', 'steps'],
    'their_delay': ['their delay (ms)', 'x1y2', delay_samples],
    'get_microseconds': ['clock (us)', 'x1y1', 'steps'],
    'wnduser': ['advertised window size (B)', 'x1y1', 'steps'],
    'ssthres': ['slow-start threshold (B)', 'x1y1', 'steps'],
    'timeout': ['until next timeout (ms)', 'x1y2', 'steps'],
    'rto': ['current timeout (ms)', 'x1y2', 'steps'],

    'delay_base': ['delay base (us)', 'x1y1', delay_base],
    'their_delay_base': ['their delay base (us)', 'x1y1', delay_base],
    'their_actual_delay': ['their actual delay (us)', 'x1y1', delay_samples],
    'actual_delay': ['actual_delay (us)', 'x1y1', delay_samples],
    'send_buffer': ['send buffer size (B)', 'x1y1', send_buffer],
    'recv_buffer': ['receive buffer size (B)', 'x1y1', 'lines'],
    'packet_loss': ['packet lost', 'x1y2', 'steps'],
    'packet_timeout': ['packet timed out', 'x1y2', 'steps'],
    'acked_bytes': ['Bytes ACKed by packet', 'x1y2', 'steps'],
    'bytes_sent': ['cumulative bytes sent', 'x1y2', 'steps'],
    'bytes_resent': ['cumulative bytes resent', 'x1y2', 'steps'],
    'written': ['reported written bytes', 'x1y2', 'steps'],
    'ack_nr': ['acked sequence number', 'x1y2', 'steps'],
    'seq_nr': ['sent sequence number', 'x1y2', 'steps'],
}

histogram_quantization = 1.0
socket_index = None

columns = []

begin = None

title = "-"
packet_loss = 0
packet_timeout = 0
num_acked = 0

delay_histogram = {}
packet_size_histogram = {}
window_size = {'0': 0, '1': 0}
bytes_sent = 0
bytes_resent = 0
written = 0
ack_nr = 0
seq_nr = 0

# [35301484] 0x00ec1190: actual_delay:1021583 our_delay:102 their_delay:-1021345 off_target:297 max_window:2687
# upload_rate:18942 delay_base:1021481154 delay_sum:-1021242 target_delay:400 acked_bytes:1441 cur_window:2882
# scaled_gain:2.432

counter = 0

print("reading log file")

for line in file:
    if "UTP_Connect" in line:
        title = line[:-2]
        if socket_filter is not None:
            title += ' socket: %s' % socket_filter
        else:
            title += ' sum of all sockets'
        continue

    try:
        a = line.strip().split(" ")
        t = a[0][1:-1]
        socket_index = a[1][:-1]
    except Exception:
        continue
#    if socket_index[:2] != '0x':
#        continue

    if socket_filter is not None and socket_index != socket_filter:
        continue

    counter += 1
    if (counter % 300 == 0):
        print("\r%d  " % counter, end=' ')

    if "lost." in line:
        packet_loss += 1
        continue

    if "lost (timeout)" in line:
        packet_timeout += 1
        continue

    if "acked packet " in line:
        num_acked += 1

    if "sending packet" in line:
        v = line.split('size:')[1].split(' ')[0]
        packet_size_histogram[v] = 1 + packet_size_histogram.get(v, 0)
        bytes_sent += int(v)

    if "re-sending packet" in line:
        v = line.split('size:')[1].split(' ')[0]
        bytes_resent += int(v)

    if 'calling write handler' in line:
        v = line.split('written:')[1].split(' ')[0]
        written += int(v)

    if "incoming packet" in line \
            and "ERROR" not in line \
            and "seq_nr:" in line \
            and "type:ST_SYN" not in line:
        if "ack_nr:" not in line:
            print(line)
        ack_nr = int(line.split('ack_nr:')[1].split(' ')[0])
        seq_nr = int(line.split('seq_nr:')[1].split(' ')[0])

    if "our_delay:" not in line:
        continue

# used for Logf timestamps
#    t, m = t.split(".")
#    t = time.strptime(t, "%H:%M:%S")
#    t = list(t)
#    t[0] += 107
#    t = tuple(t)
#    m = float(m)
#    m /= 1000.0
#    t = time.mktime(t) + m

# used for tick count timestamps
    t = int(t)

    if begin is None:
        begin = t
    t = t - begin
    # print time. Convert from milliseconds to seconds
    print('%f\t' % (float(t) / 1000.), end=' ', file=out)

    # if t > 200000:
    #    break

    fill_columns = not columns
    for i in a[2:]:
        try:
            n, v = i.split(':')
        except Exception:
            continue
        v = float(v)
        if n == "our_delay":
            bucket = int(v / histogram_quantization)
            delay_histogram[bucket] = 1 + delay_histogram.get(bucket, 0)
        if n not in metrics:
            continue
        if fill_columns:
            columns.append(n)
        if n == "max_window":
            window_size[socket_index] = v
            print('%f\t' % int(reduce(lambda a, b: a + b, list(window_size.values()))), end=' ', file=out)
        else:
            print('%f\t' % v, end=' ', file=out)

    if fill_columns:
        columns += ['packet_loss', 'packet_timeout', 'bytes_sent', 'ack_nr',
                    'seq_nr', 'bytes_resent', 'written']
    print(float(packet_loss), float(packet_timeout), float(bytes_sent),
          ack_nr, seq_nr, float(bytes_resent), written, file=out)
    packet_loss = 0
    packet_timeout = 0
    num_acked = 0
    written = 0

out.close()

out = open('%s.histogram' % out_file, 'wb')
for d, f in delay_histogram.items():
    print(float(d * histogram_quantization) + histogram_quantization / 2.0, f, file=out)
out.close()

out = open('%s_packet_size.histogram' % out_file, 'wb')
for d, f in packet_size_histogram.items():
    print(d, f, file=out)
out.close()

plot = [
    {
        'data': ['max_window', 'send_buffer', 'cur_window', 'rtt'],
        'title': 'send-packet-size',
        'y1': 'Bytes',
        'y2': 'Time (ms)'
    },
    {
        'data': ['max_window', 'send_buffer', 'cur_window', 'written'],
        'title': 'bytes-written',
        'y1': 'Bytes',
        'y2': 'Time (ms)'
    },
    {
        'data': ['upload_rate', 'max_window', 'cur_window', 'wnduser', 'cur_window_packets', 'packet_size', 'rtt'],
        'title': 'slow-start',
        'y1': 'Bytes',
        'y2': 'Time (ms)'
    },
    {
        'data': ['max_window', 'cur_window', 'our_delay', 'target_delay', 'ssthres'],
        'title': 'cwnd',
        'y1': 'Bytes',
        'y2': 'Time (ms)'
    },
    {
        'data': ['max_window', 'cur_window', 'packet_loss'],
        'title': 'packet-loss',
        'y1': 'Bytes',
        'y2': 'count'
    },
    {
        'data': ['max_window', 'cur_window', 'packet_timeout'],
        'title': 'packet-timeout',
        'y1': 'Bytes',
        'y2': 'count'
    },
    {
        'data': ['max_window', 'cur_window', 'bytes_sent', 'bytes_resent'],
        'title': 'cumulative-bytes-sent',
        'y1': 'Bytes',
        'y2': 'Cumulative Bytes'
    },
    {
        'data': ['max_window', 'cur_window', 'rto', 'timeout'],
        'title': 'connection-timeout',
        'y1': 'Bytes',
        'y2': 'Time (ms)'
    },
    {
        'data': ['our_delay', 'max_window', 'target_delay', 'cur_window', 'wnduser', 'cur_window_packets'],
        'title': 'uploading',
        'y1': 'Bytes',
        'y2': 'Time (ms)'
    },
    {
        'data': ['our_delay', 'max_window', 'target_delay', 'cur_window', 'send_buffer'],
        'title': 'uploading-packets',
        'y1': 'Bytes',
        'y2': 'Time (ms)'
    },
    {
        'data': ['their_delay', 'target_delay', 'rtt'],
        'title': 'their-delay',
        'y1': '',
        'y2': 'Time (ms)'
    },
    {
        'data': ['their_actual_delay', 'their_delay_base'],
        'title': 'their-delay-base',
        'y1': 'Time (us)',
        'y2': ''
    },
    {
        'data': ['our_delay', 'target_delay', 'rtt'],
        'title': 'our-delay',
        'y1': '',
        'y2': 'Time (ms)'
    },
    {
        'data': ['actual_delay', 'delay_base'],
        'title': 'our-delay-base',
        'y1': 'Time (us)',
        'y2': ''
    },
    {
        'data': ['ack_nr', 'seq_nr'],
        'title': 'sequence-numbers',
        'y1': 'Bytes',
        'y2': 'seqnr'
    },
    {
        'data': ['max_window', 'cur_window', 'acked_bytes'],
        'title': 'ack-rate',
        'y1': 'Bytes',
        'y2': 'Bytes'
    }
]

out = open('utp.gnuplot', 'w+')

files = ''

# print('set xtics 0, 20', file=out)
print("set term png size 1280,800", file=out)
print('set output "%s.delays.png"' % out_file, file=out)
print('set xrange [0:200]', file=out)
print('set xlabel "delay (ms)"', file=out)
print('set boxwidth 1', file=out)
print('set ylabel "number of packets"', file=out)
print('plot "%s.histogram" using 1:2 with boxes fs solid 0.3' % out_file, file=out)
files += out_file + '.delays.png '

print('set output "%s.packet_sizes.png"' % out_file, file=out)
print('set xrange [0:*]', file=out)
print('set xlabel "packet size (B)"', file=out)
print('set boxwidth 1', file=out)
print('set ylabel "number of packets sent"', file=out)
print('set logscale y', file=out)
print('plot "%s_packet_size.histogram" using 1:2 with boxes fs solid 0.3' % out_file, file=out)
print('set nologscale y', file=out)
files += out_file + '.packet_sizes.png '

print("set style data steps", file=out)
# print("set yrange [0:*]", file=out)
print("set y2range [*:*]", file=out)
# set hidden3d
# set title "Peer bandwidth distribution"
# set xlabel "Ratio"

for p in plot:
    print('set title "%s %s"' % (p['title'], title), file=out)
    print('set xlabel "time (s)"', file=out)
    print('set ylabel "%s"' % p['y1'], file=out)
    print("set tics nomirror", file=out)
    print('set y2tics', file=out)
    print('set y2label "%s"' % p['y2'], file=out)
    print('set xrange [0:*]', file=out)
    print("set key box", file=out)
    print("set term png size 1280,800", file=out)
    print('set output "%s-%s.png"' % (out_file, p['title']), file=out)
    files += '%s-%s.png ' % (out_file, p['title'])

    comma = ''
    print("plot", end=' ', file=out)

    for c in p['data']:
        if c not in metrics:
            continue
        i = columns.index(c)
        print('%s"%s" using ($1/1000):%d title "%s-%s" axes %s with %s' %
              (comma, out_file, i + 2, metrics[c][0], metrics[c][1], metrics[c][1], metrics[c][2]), end=' ', file=out)
        comma = ', '
    print('', file=out)

out.close()

os.system("gnuplot utp.gnuplot")

os.system("open %s" % files)
