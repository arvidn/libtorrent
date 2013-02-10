#! /usr/bin/env python
import sys
import os

up_time_quanta = 2000

f = open(sys.argv[1])

announce_histogram = {}
node_uptime_histogram = {}

counter = 0;

for line in f:
	counter += 1
#	if counter % 1000 == 0:
#		print '\r%d' % counter,
	try:
		if 'distance:' in line:
			l = line.split(' ')
			idx = l.index('distance:')

			d = int(l[idx+1].strip())
			if not d in announce_histogram: announce_histogram[d] = 0
			announce_histogram[d] += 1
		if 'NODE FAILED' in line:
			l = line.split(' ')
			idx = l.index('fails:')
			if int(l[idx+1].strip()) != 1: continue;
			idx = l.index('up-time:')
			d = int(l[idx+1].strip())
			# quantize
			d = d - (d % up_time_quanta)
			if not d in node_uptime_histogram: node_uptime_histogram[d] = 0
			node_uptime_histogram[d] += 1
	except Exception, e:
		print line.split(' ')

out = open('dht_announce_distribution.dat', 'w+')
print 'announce distribution items: %d' % len(announce_histogram)
for k,v in announce_histogram.items():
	print >>out, '%d %d' % (k, v)
	print '%d %d' % (k, v)
out.close()

out = open('dht_node_uptime_distribution.dat', 'w+')
print 'node uptimes: %d' % len(node_uptime_histogram)
for k,v in node_uptime_histogram.items():
	print >>out, '%d %d' % (k + up_time_quanta/2, v)
out.close()

out = open('dht.gnuplot', 'w+')
out.write('''
set term png size 1200,700 small
set output "dht_announce_distribution.png"
set title "bucket # announces are made against relative to target node-id"
set ylabel "# of announces"
set style fill solid border -1 pattern 2
plot  "dht_announce_distribution.dat" using 1:2 title "announces" with boxes

set terminal postscript
set output "dht_announce_distribution.ps"
replot

set term png size 1200,700 small
set output "dht_node_uptime_distribution.png"
set title "node up time"
set ylabel "# of nodes"
set xlabel "uptime (seconds)"
set boxwidth %f
set style fill solid border -1 pattern 2
plot  "dht_node_uptime_distribution.dat" using 1:2 title "nodes" with boxes
''' % up_time_quanta)
out.close()

os.system('gnuplot dht.gnuplot');


