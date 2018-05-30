#!/usr/bin/env python
import os, sys, time

keys = [['upload rate', 'x1y1', 6], ['history entries', 'x1y2', 10], ['queue', 'x1y2', 4]]

out = open('bandwidth.gnuplot', 'wb')
print("set term png size 1200,700", file=out)
print('set output "bandwidth_manager.png"', file=out)
print('set xrange [0:*]', file=out)
print('set xlabel "time (ms)"', file=out)
print('set ylabel "Rate (B/s)"', file=out)
print('set ytics 10000', file=out)
print('set y2label "number"', file=out)
print('set y2range [0:*]', file=out)
#print >>out, "set style data lines"
print("set key box", file=out)
print('plot', end=' ', file=out)
for k, a, c in keys:
   print(' "%s" using 1:%d title "%s" axes %s with steps,' % (sys.argv[1], c, k, a), end=' ', file=out)
print('x=0', file=out)
out.close()

os.system('gnuplot bandwidth.gnuplot');

