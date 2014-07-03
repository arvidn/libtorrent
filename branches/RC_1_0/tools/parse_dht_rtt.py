#! /usr/bin/env python

import sys
import os

quantize = 100
max_rtt = 5000

f = open(sys.argv[1])
distribution = {}
num_messages = 0

for i in range(0, max_rtt, quantize):
	distribution[i] = 0

for line in f:
	time = int(line.split('\t')[1])
	if (time < 0 or time > max_rtt - quantize): continue
	num_messages += 1
	time /= quantize
	time *= quantize
	distribution[time] += 1

f = open('round_trip_distribution.log', 'w+')

for k, v in distribution.items():
	print >>f, '%f %d' % ((k + (quantize / 2)) / 1000.0, v)
f.close();

f = open('round_trip_distribution.gnuplot', 'w+')

f.write('''
set term png size 1200,700
set title "Message round trip times"
set terminal postscript
set ylabel "# of requests"
set xlabel "Round trip time (seconds)"
set xrange [0:*]
set grid
set style fill solid border -1 pattern 2
set output "round_trip_distribution.ps"
set boxwidth %f
plot "round_trip_distribution.log" using 1:2 title "requests" with boxes

set terminal png small
set output "round_trip_distribution.png"
replot
''' % (float(quantize) / 1000.0))
f.close()

os.system('gnuplot round_trip_distribution.gnuplot');

