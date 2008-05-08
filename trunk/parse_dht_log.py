import sys
import os

f = open(sys.argv[1])

histogram = {}

for i in xrange(0, 50): histogram[i] = 0

for line in f.readlines():
	try:
		if not 'distance:' in line: continue;
		l = line.split(' ')

		d = int(l[4])
		histogram[d] += 1
	except:
		print line.split(' ')

out = open('dht_announce_distribution.dat', 'w+')
for k,v in histogram.items():
	print >>out, '%d %d' % (k, v)
out.close()

out = open('dht_announce_distribution.gnuplot', 'w+')
out.write('''
set term png size 1200,700
set output "dht_announce_distribution.png"
set title "bucket # announces are made against relative to target node-id"
set ylabel "# of announces"
set style fill solid border -1 pattern 2
plot  "dht_announce_distribution.dat" using 1:2 title "announces" with boxes
''')
out.close()

os.system('gnuplot dht_announce_distribution.gnuplot');

