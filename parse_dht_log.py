import sys
import os

up_time_quanta = 60

f = open(sys.argv[1])

announce_histogram = {}
node_uptime_histogram = {}

for i in xrange(0, 50): announce_histogram[i] = 0
for i in xrange(0, 5000, up_time_quanta): node_uptime_histogram[i] = 0

counter = 0;

for line in f:
	counter += 1
	if counter % 1000 == 0:
		print '\r%d' % counter,
	try:
		if 'distance:' in line:
			l = line.split(' ')

			d = int(l[4])
			announce_histogram[d] += 1
		if 'NODE FAILED' in line:
			l = line.split(' ')
			if int(l[9]) != 1: continue;
			d = int(l[11])
			node_uptime_histogram[d - (d % up_time_quanta)] += 1
	except:
		print line.split(' ')

out = open('dht_announce_distribution.dat', 'w+')
for k,v in announce_histogram.items():
	print >>out, '%d %d' % (k, v)
out.close()

out = open('dht_node_uptime_distribution.dat', 'w+')
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

