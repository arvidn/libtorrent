import os, sys, time

keys = ['upload rate', 'download rate', 'downloading torrents', \
	'seeding torrents', 'peers', 'connecting peers', 'disk block buffers']

axes = ['x1y2', 'x1y2', 'x1y1', 'x1y1', 'x1y1', 'x1y1', 'x1y1']

out = open('session_stats.gnuplot', 'wb')
print >>out, "set term png size 1200,700"
print >>out, 'set output "session_stats.png"'
print >>out, 'set xrange [0:*]'
print >>out, 'set xlabel "time (s)"'
print >>out, 'set ylabel "number"'
print >>out, 'set y2label "Rate (B/s)"'
print >>out, 'set y2range [0:*]'
print >>out, 'set y2tics 20000'
print >>out, "set style data lines"
print >>out, "set key box"
print >>out, 'plot',
column = 2
for k in keys:
   print >>out, ' "%s" using 1:%d title "%s" axes %s with lines,' % (sys.argv[1], column, k, axes[column-2]),
   column = column + 1
print >>out, 'x=0'
out.close()

os.system('gnuplot session_stats.gnuplot');

