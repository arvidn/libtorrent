#! /usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os, sys, time

stat = open(sys.argv[1])
line = stat.readline()
while not 'second:' in line:
	line = stat.readline()

keys = line.strip().split(':')[1:]

axes = ['x1y2', 'x1y2', 'x1y1', 'x1y1', 'x1y1', 'x1y1', 'x1y1', 'x1y1', 'x1y2']

def gen_report(name, lines):
	out = open('session_stats_%s.gnuplot' % name, 'wb')
	print >>out, "set term png size 1200,700"
	print >>out, 'set output "session_stats_%s.png"' % name
	print >>out, 'set xrange [0:*]'
	print >>out, 'set xlabel "time (s)"'
	print >>out, 'set ylabel "number"'
	print >>out, 'set y2label "Rate (B/s)"'
	print >>out, 'set y2range [0:*]'
	print >>out, 'set y2tics auto'
	print >>out, "set tics nomirror"
	print >>out, "set style data lines"
	print >>out, "set key box"
	print >>out, 'plot',
	column = 2
	first = True
	for k in keys:
		if k not in lines:
			column = column + 1
			continue
		if not first: print >>out, ', ',
		print >>out, ' "%s" using 1:%d title "%s" axes %s with steps' % (sys.argv[1], column, k, axes[column-2]),
		first = False
		column = column + 1
	print >>out, ''
	out.close()
	os.system('gnuplot session_stats_%s.gnuplot' % name);

gen_report('torrents', ['downloading torrents', 'seeding torrents', 'checking torrents', 'stopped torrents'])
gen_report('peers', ['peers', 'connecting peers', 'unchoked peers', 'peers disk-up', 'peers disk-down', 'peers bw-up', 'peers bw-down')
gen_report('peers_list', ['num list peers')
gen_report('rates', ['upload rate', 'download rate', 'disk block buffers'])

