#! /usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os, sys, time

ignore = ['download rate', 'disk block buffers']

stat = open(sys.argv[1])
line = stat.readline()
while not 'second:' in line:
	line = stat.readline()

keys = line.strip().split(':')[1:]

def gen_report(name, lines):
	out = open('session_stats_%s.gnuplot' % name, 'wb')
	print >>out, "set term png size 1200,700"
	print >>out, 'set output "session_stats_%s.png"' % name
	print >>out, 'set xrange [0:*]'
	print >>out, 'set xlabel "time (s)"'
	print >>out, 'set ylabel "number"'
	print >>out, 'set y2label "Rate (B/s)"'
	print >>out, 'set y2range [0:*]'
	print >>out, 'set y2tics 10000'
	print >>out, "set style data lines"
	print >>out, "set key box"
	print >>out, 'plot',
	column = 2
	index = 0
	for k in keys:
		if k not in lines:
			column = column + 1
			continue
		if index > 0: print >>out, ', ',
		print >>out, ' "%s" using 1:%d title "%s" axes x1y2 with steps' % (sys.argv[1], column, k),
		index += 1
		column += 1
	out.close()
	os.system('gnuplot session_stats_%s.gnuplot' % name);

gen_report('rates', ['upload rate', 'download rate', 'downloading torrents', 'seeding torrents', 'peers', 'unchoked peers'])
gen_report('peers', ['peers', 'connecting peers', 'unchoked peers', 'num list peers'])
gen_report('buffers', ['upload rate', 'download rate', 'disk block buffers'])
gen_report('boost_asio_messages', [ \
	'read_counter', 'write_counter', 'tick_counter', 'lsd_counter', \
	'lsd_peer_counter', 'udp_counter', 'accept_counter', 'disk_queue_counter', \
	'disk_read_counter', 'disk_write_counter'])

