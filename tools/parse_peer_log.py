#!/usr/bin/env python

import glob
import os
import sys

# usage: parse_peer_log <path-to-libtorrent-peer-logs>

log_files = []

for p in glob.iglob(os.path.join(sys.argv[1], '*.log')):
	name = os.path.split(p)[1]
	if name == 'main_session.log': continue
	print name
	f = open(p, 'r')
	out_file = p + '.dat'
	log_files.append(out_file)
	out = open(out_file, 'w+')

	uploaded_blocks = 0;
	downloaded_blocks = 0;

	for l in f:
		t = l.split(': ')[0].split('.')[0]
		log_line = False
		if ' ==> PIECE' in l:
			uploaded_blocks+= 1
			log_line = True

		if ' <== PIECE' in l:
			downloaded_blocks+= 1
			log_line = True

		if log_line:
			print >>out, '%s\t%d\t%d' % (t, uploaded_blocks, downloaded_blocks)

	out.close()
	f.close()

out = open('peers.gnuplot', 'wb')
print >>out, "set term png size 1200,700"
print >>out, 'set xrange [0:*]'
print >>out, 'set xlabel "time"'
print >>out, 'set ylabel "blocks"'
print >>out, 'set key box'
print >>out, 'set xdata time'
print >>out, 'set timefmt "%H:%M:%S"'
print >>out, 'set title "uploaded blocks"'
print >>out, 'set output "peers_upload.png"'
print >>out, 'plot',
first = True
for n in log_files:
	if not first:
		print >>out, ',',
	first = False
	print >>out, ' "%s" using 1:2 title "%s" with steps' % (n, os.path.split(n)[1].split('.log')[0]),
print >>out, ''

print >>out, 'set title "downloaded blocks"'
print >>out, 'set output "peers_download.png"'
print >>out, 'plot',
first = True
for n in log_files:
	if not first:
		print >>out, ',',
	first = False
	print >>out, ' "%s" using 1:3 title "%s" with steps' % (n, os.path.split(n)[1].split('.log')[0]),
print >>out, ''
out.close()

os.system('gnuplot peers.gnuplot');

