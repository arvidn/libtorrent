#!/usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os
import sys
import time

lines = open(sys.argv[1], 'rb').readlines()

#keys = ['send_buffer_utilization']
keys = ['send_buffer_size', 'used_send_buffer', 'protocol_buffer']
#keys = ['send_buffer_alloc', 'send_buffer', 'allocate_buffer_alloc', 'allocate_buffer', 'protocol_buffer']
#keys = ['send_buffer_alloc', 'send_buffer', 'allocate_buffer_alloc', 'allocate_buffer', 'protocol_buffer', 'append_send_buffer']

average = ['send_buffer_utilization', 'send_buffer_size', 'used_send_buffer']
average_interval = 120000
render = 'lines'

time_limit = -1
if len(sys.argv) > 2:
    time_limit = int(sys.argv[2])


# logfile format:
# <time(ms)> <key> <value>
# example:
# 16434 allocate_buffer: 17
for k in keys:

    last_sample = 0
    average_accumulator = 0
    average_samples = 0
    peak = 0

    out = open(k + '.dat', 'wb')
    eval_average = False
    if k in average:
        eval_average = True
        peak_out = open(k + '_peak.dat', 'wb')

    for l in lines:
        l = l.split(' ')
        if len(l) != 3:
            print(l)
            continue
        try:
            if l[1] == k + ':':
                if time_limit != -1 and int(l[0]) > time_limit:
                    break
                time = l[0]
                value = l[2]
                if eval_average:
                    while int(time) > last_sample + average_interval:
                        last_sample = last_sample + average_interval
                        if average_samples < 1:
                            average_samples = 1
                        print('%d %f' % (last_sample, average_accumulator / average_samples), file=out)
                        print('%d %f' % (last_sample, peak), file=peak_out)
                        average_accumulator = 0
                        average_samples = 0
                        peak = 0
                    average_accumulator = average_accumulator + float(value)
                    average_samples = average_samples + 1
                    if float(value) > peak:
                        peak = float(value)
                else:
                    print(time + ' ' + value, end=' ', file=out)
        except BaseException:
            print(l)

    out.close()
    peak_out.close()

out = open('send_buffer.gnuplot', 'wb')
print("set term png size 1200,700", file=out)
print('set output "send_buffer.png"', file=out)
print('set xrange [0:*]', file=out)
print('set xlabel "time (ms)"', file=out)
print('set ylabel "bytes (B)"', file=out)
print("set style data lines", file=out)
print("set key box", file=out)
print('plot', end=' ', file=out)
for k in keys:
    if k in average:
        print(' "%s.dat" using 1:2 title "%s %d seconds average" with %s,' %
              (k, k, average_interval / 1000., render), end=' ', file=out)
        print(' "%s_peak.dat" using 1:2 title "%s %d seconds peak" with %s,' %
              (k, k, average_interval / 1000., render), end=' ', file=out)
    else:
        print(' "%s.dat" using 1:2 title "%s" with %s,' % (k, k, render), end=' ', file=out)
print('x=0', file=out)
out.close()

os.system('gnuplot send_buffer.gnuplot')
