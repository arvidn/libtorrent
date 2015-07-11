#!/usr/bin/env python
import os, sys, time

keys = [['upload rate', 'x1y1', 6], ['history entries', 'x1y2', 10], ['queue', 'x1y2', 4]]

out = open('bandwidth.gnuplot', 'wb')
print >>out, "set term png size 1200,700"
print >>out, 'set output "bandwidth_manager.png"'
print >>out, 'set xrange [0:*]'
print >>out, 'set xlabel "time (ms)"'
print >>out, 'set ylabel "Rate (B/s)"'
print >>out, 'set ytics 10000'
print >>out, 'set y2label "number"'
print >>out, 'set y2range [0:*]'
#print >>out, "set style data lines"
print >>out, "set key box"
print >>out, 'plot',
for k, a, c in keys:
   print >>out, ' "%s" using 1:%d title "%s" axes %s with steps,' % (sys.argv[1], c, k, a),
print >>out, 'x=0'
out.close()

os.system('gnuplot bandwidth.gnuplot');

