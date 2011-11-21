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
gen_report('send_buffer_sizes', ['up 8', 'up 16', 'up 32', 'up 64', 'up 128', 'up 256', 'up 512', 'up 1024', 'up 2048', 'up 4096', 'up 8192', 'up 16384', 'up 32768', 'up 65536', 'up 131072', 'up 262144', 'up 524288', 'up 1048576'])
gen_report('recv_buffer_sizes', ['down 8', 'down 16', 'down 32', 'down 64', 'down 128', 'down 256', 'down 512', 'down 1024', 'down 2048', 'down 4096', 'down 8192', 'down 16384', 'down 32768', 'down 65536', 'down 131072', 'down 262144', 'down 524288', 'down 1048576'])

