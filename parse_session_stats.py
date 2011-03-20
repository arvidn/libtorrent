#! /usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os, sys, time, os

stat = open(sys.argv[1])
line = stat.readline()
while not 'second:' in line:
	line = stat.readline()

keys = line.strip().split(':')[1:]

axes = ['x1y2', 'x1y2', 'x1y1', 'x1y1', 'x1y1', 'x1y1', 'x1y1', 'x1y1', 'x1y2']

output_dir = 'session_stats_report'

def gen_report(name, unit, lines, generation, log_file):
	try:
		os.mkdir(output_dir)
	except: pass

	out = open('session_stats_%s.gnuplot' % name, 'wb')
	print >>out, "set term png size 1200,700"
	print >>out, 'set output "%s"' % (os.path.join(output_dir, 'session_stats_%s_%04d.png' % (name, generation)))
	print >>out, 'set xrange [0:*]'
	print >>out, 'set xlabel "time (s)"'
	print >>out, 'set ylabel "%s"' % unit
	print >>out, 'set yrange [0:*]'
	print >>out, "set tics nomirror"
	print >>out, "set style data lines"
	print >>out, "set key box"
	print >>out, 'plot',
	column = 2
	first = True
	for k in lines:
		try:
			column = keys.index(k) + 2
		except:
			print '"%s" not found' % k
			continue;
		if not first: print >>out, ', ',
		axis = 'x1y1'
		if column-2 < len(axes): axis = axes[column-2]
		print >>out, ' "%s" using 1:%d title "%s" axes %s with steps' % (log_file, column, k, axis),
		first = False
		column = column + 1
	print >>out, ''

	print >>out, "set term png size 300,150"
	print >>out, 'set output "%s"' % (os.path.join(output_dir, 'session_stats_%s_%04d_thumb.png' % (name, generation)))
	print >>out, 'set key off'
	print >>out, 'unset tics'
	print >>out, 'set format x ""'
	print >>out, 'set format y ""'
	print >>out, 'set xlabel ""'
	print >>out, 'set ylabel ""'
	print >>out, 'set y2label ""'
	print >>out, "replot"
	out.close()
	os.system('gnuplot session_stats_%s.gnuplot 2>/dev/null' % name);
	sys.stdout.write('.')
	sys.stdout.flush()

def gen_html(reports, generations):
	file = open(os.path.join(output_dir, 'index.html'), 'w+')

	css = '''img { margin: 0}
		#head { display: block }
		h1 { line-height: 1; display: inline }
		h2 { line-height: 1; display: inline; font-size: 1em; font-weight: normal};'''

	print >>file, '<html><head><style type="text/css">%s</style></head><body>' % css

	for i in reports:
		print >>file, '<div id="head"><h1>%s </h1><h2>%s</h2><div>' % (i[0], i[2])
		for g in generations:
			print >>file, '<a href="session_stats_%s_%04d.png"><img src="session_stats_%s_%04d_thumb.png"></a>' % (i[0], g, i[0], g)

	print >>file, '</body></html>'
	file.close()

reports = [
	('torrents', 'num', 'number of torrents in different torrent states', ['downloading torrents', 'seeding torrents', 'checking torrents', 'stopped torrents', 'upload-only torrents', 'error torrents']),
	('peers', 'num', 'num connected peers', ['peers', 'connecting peers', 'connection attempts', 'banned peers', 'max connections']),
	('peers_list_size', 'num', 'number of known peers (not necessarily connected)', ['num list peers']),
	('overall_rates', 'Bytes / second', 'download and upload rates', ['upload rate', 'download rate', 'smooth upload rate', 'smooth download rate']),
	('disk_write_queue', 'Bytes', 'bytes queued up by peers, to be written to disk', ['disk write queued bytes', 'disk queue limit', 'disk queue low watermark']),
	('peers_upload', 'num', 'number of peers by state wrt. uploading', ['peers up interested', 'peers up unchoked', 'peers up requests', 'peers disk-up', 'peers bw-up']),
	('peers_download', 'num', 'number of peers by state wrt. downloading', ['peers down interesting', 'peers down unchoked', 'peers down requests', 'peers disk-down', 'peers bw-down']),
	('peer_errors', 'num', 'number of peers by error that disconnected them', ['error peers', 'peer disconnects', 'peers eof', 'peers connection reset', 'connect timeouts', 'uninteresting peers disconnect', 'banned for hash failure']),
	('waste', '% of all downloaded bytes', 'proportion of all downloaded bytes that were wasted', ['% failed payload bytes', '% wasted payload bytes', '% protocol bytes']),
	('average_disk_time_absolute', 'microseconds', 'running averages of timings of disk operations', ['disk read time', 'disk write time', 'disk queue time', 'disk hash time', 'disk job time', 'disk sort time']),
	('disk_time', '% of total disk job time', 'proportion of time spent by the disk thread', ['% read time', '% write time', '% hash time', '% sort time']),
	('disk_cache_hits', 'blocks (16kiB)', '', ['disk block read', 'read cache hits', 'disk block written', 'disk read back']),
	('disk_cache', 'blocks (16kiB)', 'disk cache size and usage', ['read disk cache size', 'disk cache size', 'disk buffer allocations', 'cache size']),
	('disk_readback', '% of written blocks', 'portion of written blocks that had to be read back for hash verification', ['% read back']),
	('disk_queue', 'number of queued disk jobs', 'queued disk jobs', ['disk queue size', 'disk read queue size']),
#	('absolute_waste', 'num', '', ['failed bytes', 'redundant bytes', 'download rate']),
	('connect_candidates', 'num', 'number of peers we know of that we can connect to', ['connect candidates']),

#somewhat uninteresting stats
	('peer_dl_rates', 'num', 'peers split into download rate buckets', ['peers down 0', 'peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-']),
	('peer_dl_rates2', 'num', 'peers split into download rate buckets (only downloading peers)', ['peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-']),
	('peer_ul_rates', 'num', 'peers split into upload rate buckets', ['peers up 0', 'peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-']),
	('peer_ul_rates2', 'num', 'peers split into upload rate buckets (only uploading peers)', ['peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-']),
	('piece_picker_end_game', '', 'blocks', ['end game piece picker blocks', 'piece picker blocks', 'piece picks', 'reject piece picks', 'unchoke piece picks', 'incoming redundant piece picks', 'incoming piece picks', 'end game piece picks', 'snubbed piece picks']),
	('piece_picker', 'blocks', '', ['piece picks', 'reject piece picks', 'unchoke piece picks', 'incoming redundant piece picks', 'incoming piece picks', 'end game piece picks', 'snubbed piece picks']),
]

print 'generating graphs'
log_file_path, log_file = os.path.split(sys.argv[1])
# count the number of log files (generations)
log_file_list = log_file.split('.')
g = int(log_file_list[1])
generations = []
while os.path.exists(log_file):
	print '[%s] %04d\r[' % (' ' * len(reports), g),
	for i in reports: gen_report(i[0], i[1], i[3], g, os.path.join(log_file_path, log_file))
	print ''
	generations.append(g)
	g += 1
	log_file_list[1] = '%04d' % g
	log_file = '.'.join(log_file_list)
print 'generating html'
gen_html(reports, generations)
