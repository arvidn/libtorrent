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
		axis = 'x1y1'
		if column-2 < len(axes): axis = axes[column-2]
		print >>out, ' "%s" using 1:%d title "%s" axes %s with steps' % (sys.argv[1], column, k, axis),
		first = False
		column = column + 1
	print >>out, ''
	out.close()
	os.system('gnuplot session_stats_%s.gnuplot' % name);

gen_report('torrents', ['downloading torrents', 'seeding torrents', 'checking torrents', 'stopped torrents', 'upload-only torrents', 'error torrents'])
gen_report('peers', ['peers', 'connecting peers', 'unchoked peers', 'peers disk-up', 'peers disk-down', 'peers bw-up', 'peers bw-down'])
gen_report('peers_list', ['num list peers', 'peer storage bytes'])
gen_report('overall_rates', ['upload rate', 'download rate', 'smooth upload rate', 'smooth download rate'])
gen_report('peer_dl_rates', ['peers down 0', 'peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-'])
gen_report('peer_dl_rates2', ['peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-'])
gen_report('peer_ul_rates', ['peers up 0', 'peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-'])
gen_report('peer_ul_rates2', ['peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-'])
gen_report('disk', ['disk write queued bytes', 'disk block buffers'])
gen_report('peers_upload', ['peers up interested', 'peers up unchoked', 'peers up requests', 'peers disk-up', 'peers bw-up'])
gen_report('peers_download', ['peers down interesting', 'peers down unchoked', 'peers down requests', 'peers disk-down', 'peers bw-down'])
gen_report('peer_errors', ['error peers', 'peer disconnects', 'peers eof', 'peers connection reset', 'connect timeouts', 'uninteresting peers disconnect'])
gen_report('piece_picker_end_game', ['end game piece picker blocks', 'strict end game piece picker blocks', 'piece picker blocks', 'piece picks', 'reject piece picks', 'unchoked piece picks', 'incoming redundant piece picks', 'incoming piece picks', 'end game piece picks', 'snubbed piece picks'])
gen_report('piece_picker', ['piece picks', 'reject piece picks', 'unchoked piece picks', 'incoming redundant piece picks', 'incoming piece picks', 'end game piece picks', 'snubbed piece picks'])
gen_report('bandwidth', ['% failed payload bytes', '% wasted payload bytes', '% protocol bytes'])
gen_report('disk_time', ['disk read time', 'disk write time', 'disk queue time', 'disk hash time'])
gen_report('disk_cache_hits', ['disk block read', 'read cache hits', 'disk block written'])
gen_report('disk_cache', ['read disk cache size', 'disk cache size', 'disk buffer allocations'])
gen_report('disk_queue', ['disk queue size', 'disk queued bytes'])
gen_report('waste', ['failed bytes', 'redundant bytes', 'download rate'])

