#!/usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os, sys, time, os, math
from multiprocessing.pool import ThreadPool

thread_pool = ThreadPool(8)

stat = open(sys.argv[1])
line = stat.readline()
while not 'second:' in line:
	line = stat.readline()

keys = line.strip().split(':')[1:]

output_dir = 'session_stats_report'

line_graph = 0
histogram = 1
stacked = 2
diff = 3

graph_colors = []

pattern = [[0,0,1], [0,1,0], [1,0,0], [1,0,1], [0,1,1], [1,1,0]]

def process_color(c, op):
	for i in range(3):
		if op == 0:
			c[i] = min(255, c[i] + 0xb0)
		if op == 2:
			c[i] = max(0, c[i] - 0x50)
	return c

for i in range(0,len(pattern) * 3):

	op = i / len(pattern)

	c = list(pattern[i % len(pattern)])
	for i in range(3): c[i] *= 0xff
	c = process_color(c, op)

	c = '#%02x%02x%02x' % (c[0], c[1], c[2])
	graph_colors.append(c)

line_colors = list(graph_colors)
line_colors.reverse()

gradient16_colors = []
for i in range(0, 16):
	f = i / 16.
	pi = 3.1415927
	r = max(int(255 * (math.sin(f*pi)+0.2)), 0)
	g = max(int(255 * (math.sin((f-0.5)*pi)+0.2)), 0)
	b = max(int(255 * (math.sin((f+0.5)*pi)+0.2)), 0)
	c = '#%02x%02x%02x' % (min(r, 255), min(g, 255), min(b, 255))
	gradient16_colors.append(c)

gradient18_colors = []
for i in range(0, 18):
	f = i / 18.
	pi = 3.1415927
	r = max(int(255 * (math.sin(f*pi)+0.2)), 0)
	g = max(int(255 * (math.sin((f-0.5)*pi)+0.2)), 0)
	b = max(int(255 * (math.sin((f+0.5)*pi)+0.2)), 0)
	c = '#%02x%02x%02x' % (min(r, 255), min(g, 255), min(b, 255))
	gradient18_colors.append(c)

gradient6_colors = []
for i in range(0, 6):
	f = i / 6.
	c = '#%02x%02x%02x' % (min(int(255 * (-2 * f + 2)), 255), min(int(255 * (2 * f)), 255), 100)
	gradient6_colors.append(c)

def plot_fun(script):
	ret = os.system('gnuplot "%s" 2>/dev/null' % script)
	if ret != 0 and ret != 256:
		print 'system: %d\n' % ret
		raise Exception("abort")

	sys.stdout.write('.')
	sys.stdout.flush()

def gen_report(name, unit, lines, short_unit, generation, log_file, options):
	try:
		os.mkdir(output_dir)
	except: pass

	filename = os.path.join(output_dir, '%s_%04d.png' % (name, generation))
	thumb = os.path.join(output_dir, '%s_%04d_thumb.png' % (name, generation))

	# don't re-render a graph unless the logfile has changed
	try:
		dst1 = os.stat(filename)
		dst2 = os.stat(thumb)
		src = os.stat(log_file)

		if dst1.st_mtime > src.st_mtime and dst2.st_mtime > src.st_mtime:
			sys.stdout.write('.')
			return None

	except: pass
		
	script = os.path.join(output_dir, '%s_%04d.gnuplot' % (name, generation))
	out = open(script, 'wb')
	print >>out, "set term png size 1200,700"
	print >>out, 'set output "%s"' % filename
	if not 'allow-negative' in options:
		print >>out, 'set yrange [0:*]'
	print >>out, "set tics nomirror"
	print >>out, "set key box"
	print >>out, "set key left top"

	colors = graph_colors
	if options['type'] == line_graph:
		colors = line_colors

	try:
		if options['colors'] == 'gradient16':
			colors = gradient16_colors
		elif options['colors'] == 'gradient6':
			colors = gradient6_colors
		if options['colors'] == 'gradient18':
			colors = gradient18_colors
	except: pass

	if options['type'] == histogram:
		binwidth = options['binwidth']
		numbins = int(options['numbins'])

		print >>out, 'binwidth=%f' % binwidth
		print >>out, 'set boxwidth binwidth'
		print >>out, 'bin(x,width)=width*floor(x/width) + binwidth/2'
		print >>out, 'set xrange [0:%f]' % (binwidth * numbins)
		print >>out, 'set xlabel "%s"' % unit
		print >>out, 'set ylabel "number"'

		k = lines[0]
		try:
			column = keys.index(k) + 2
		except:
			print '"%s" not found' % k
			return
		print >>out, 'plot "%s" using (bin($%d,binwidth)):(1.0) smooth freq with boxes' % (log_file, column)
		print >>out, ''
		print >>out, ''
		print >>out, ''

	elif options['type'] == stacked:
		print >>out, 'set xrange [0:*]'
		print >>out, 'set ylabel "%s"' % unit
		print >>out, 'set xlabel "time (s)"'
		print >>out, 'set format y "%%.1s%%c%s";' % short_unit
		print >>out, 'set style fill solid 1.0 noborder' 
		print >>out, 'plot',
		column = 2
		first = True
		graph = ''
		plot_expression = ''
		color = 0
		for k in lines:
			try:
				column = keys.index(k) + 2
			except:
				print '"%s" not found' % k
				continue;
			if not first:
				plot_expression = ', ' + plot_expression
				graph += '+'
			axis = 'x1y1'
			graph += '$%d' % column
			plot_expression = ' "%s" using 1:(%s) title "%s" axes %s with filledcurves y1=0 lc rgb "%s"' % (log_file, graph, k, axis, colors[color % len(colors)]) + plot_expression
			first = False
			color += 1
		print >>out, plot_expression
	elif options['type'] == diff:
		print >>out, 'set xrange [0:*]'
		print >>out, 'set ylabel "%s"' % unit
		print >>out, 'set xlabel "time (s)"'
		print >>out, 'set format y "%%.1s%%c%s";' % short_unit
		column = 2
		first = True
		graph = ''
		title = ''
		for k in lines:
			try:
				column = keys.index(k) + 2
			except:
				print '"%s" not found' % k
				continue;
			if not first:
				graph += '-'
				title += ' - '
			graph += '$%d' % column
			title += k
			first = False
		print >>out, 'plot "%s" using 1:(%s) title "%s" with step' % (log_file, graph, title)
	else:
		print >>out, 'set xrange [0:*]'
		print >>out, 'set ylabel "%s"' % unit
		print >>out, 'set xlabel "time (s)"'
		print >>out, 'set format y "%%.1s%%c%s";' % short_unit
		print >>out, 'plot',
		column = 2
		first = True
		color = 0
		for k in lines:
			try:
				column = keys.index(k) + 2
			except:
				print '"%s" not found' % k
				continue;
			if not first: print >>out, ', ',
			axis = 'x1y1'
			print >>out, ' "%s" using 1:%d title "%s" axes %s with steps lc rgb "%s"' % (log_file, column, k, axis, colors[color % len(colors)]),
			first = False
			color += 1
		print >>out, ''

	print >>out, 'set term png size 150,100'
	print >>out, 'set output "%s"' % thumb
	print >>out, 'set key off'
	print >>out, 'unset tics'
	print >>out, 'set format x ""'
	print >>out, 'set format y ""'
	print >>out, 'set xlabel ""'
	print >>out, 'set ylabel ""'
	print >>out, 'set y2label ""'
	print >>out, 'set rmargin 0'
	print >>out, 'set lmargin 0'
	print >>out, 'set tmargin 0'
	print >>out, 'set bmargin 0'
	print >>out, "replot"
	out.close()
	return script

def gen_html(reports, generations):
	file = open(os.path.join(output_dir, 'index.html'), 'w+')

	css = '''img { margin: 0}
		#head { display: block }
		#graphs { white-space:nowrap; }
		h1 { line-height: 1; display: inline }
		h2 { line-height: 1; display: inline; font-size: 1em; font-weight: normal};'''

	print >>file, '<html><head><style type="text/css">%s</style></head><body>' % css

	for i in reports:
		print >>file, '<div id="head"><h1>%s </h1><h2>%s</h2><div><div id="graphs">' % (i[0], i[3])
		for g in generations:
			print >>file, '<a href="%s_%04d.png"><img src="%s_%04d_thumb.png"></a>' % (i[0], g, i[0], g)
		print >>file, '</div>'

	print >>file, '</body></html>'
	file.close()

reports = [
	('torrents', 'num', '', 'number of torrents in different torrent states', ['downloading torrents', 'seeding torrents', \
		'checking torrents', 'stopped torrents', 'upload-only torrents', 'error torrents', 'queued seed torrents', \
		'queued download torrents'], {'type':stacked}),
	('torrents_want_peers', 'num', '', 'number of torrents that want more peers', ['torrents want more peers']),
	('peers', 'num', '', 'num connected peers', ['peers', 'connecting peers'], {'type':stacked}),
	('peers_max', 'num', '', 'num connected peers', ['peers', 'connecting peers', 'max connections', 'total peers']),
	('peer_churn', 'num', '', 'connecting and disconnecting peers', ['connecting peers', 'connection attempts']),
	('new_peers', 'num', '', '', ['incoming connections', 'connection attempts']),
	('connection_attempts', 'num', '', '', ['connection attempt loops', 'connection attempts']),
	('peer_limits', 'num', '', 'number of connections per limit', ['average peers per limit']),
	('pieces', 'num', '', 'number completed pieces', ['total pieces', 'pieces flushed', 'pieces passed', 'pieces failed']),
	('connect_candidates', 'num', '', 'number of peers we know of that we can connect to', ['connect candidates']),
	('peers_list_size', 'num', '', 'number of known peers (not necessarily connected)', ['num list peers']),
	('overall_rates', 'rate', 'B/s', 'download and upload rates', ['uploaded bytes', 'downloaded bytes', 'upload rate', 'download rate', 'smooth upload rate', 'smooth download rate']),
	('disk_write_queue', 'Bytes', 'B', 'bytes queued up by peers, to be written to disk', ['disk write queued bytes']),
	('peers_requests', 'num', '', 'incoming piece request rate', ['piece requests', 'piece rejects', 'max piece requests', 'invalid piece requests', 'choked piece requests', 'cancelled piece requests']),
	('peers_upload_max', 'num', '', 'number of peers by state wrt. uploading', ['peers up interested', 'peers up unchoked', 'peers up requests', 'peers disk-up', 'peers up send buffer', 'peers bw-up', 'max unchoked']),
	('peers_upload', 'num', '', 'number of peers by state wrt. uploading', ['peers up interested', 'peers up unchoked', 'peers up requests', 'peers disk-up', 'peers up send buffer', 'peers bw-up']),
	('peers_download', 'num', '', 'number of peers by state wrt. downloading', ['peers down interesting', 'peers down unchoked', 'peers down requests', 'peers disk-down', 'peers bw-down','num end-game peers']),
	('peer_errors', 'num', '', 'number of peers by error that disconnected them', ['error peers', 'peer disconnects', 'peers eof', 'peers connection reset', 'connect timeouts', 'uninteresting peers disconnect', 'banned for hash failure', 'no memory peer errors', 'too many peers', 'transport timeout peers', 'connection refused peers', 'connection aborted peers', 'permission denied peers', 'no buffer peers', 'host unreachable peers', 'broken pipe peers', 'address in use peers', 'access denied peers', 'invalid argument peers', 'operation aborted peers']),
	('peer_errors_incoming', 'num', '', 'number of peers by incoming or outgoing connection', ['error incoming peers', 'error outgoing peers']),
	('peer_errors_transport', 'num', '', 'number of peers by transport protocol', ['error tcp peers', 'error utp peers']),
	('peer_errors_encryption', 'num', '', 'number of peers by encryption level', ['error encrypted peers', 'error rc4 peers', 'peer disconnects']),
	('incoming requests', 'num', '', 'incoming 16kiB block requests', ['pending incoming block requests', 'average pending incoming block requests']),
	('disk_write_time', 'write time', 's', 'distribution of write jobs timing', ['disk write time'], {'type': histogram, 'binwidth': 0.1, 'numbins': 400}),
	('disk_read_time', 'read time', 's', 'distribution of read jobs timing', ['disk read time'], {'type': histogram, 'binwidth': 0.1, 'numbins': 400}),
	('waste', '% of all downloaded bytes', '%%', 'proportion of all downloaded bytes that were wasted', ['% failed payload bytes', '% wasted payload bytes', '% protocol bytes'], {'type':stacked}),
	('waste by source', 'num wasted bytes', 'B', 'what is causing the waste', [ 'redundant timed-out', 'redundant cancelled', 'redundant unknown', 'redundant seed', 'redundant end-game', 'redundant closing'], {'type':stacked}),
	('average_disk_time_absolute', 'job time', 's', 'running averages of timings of disk operations', ['disk read time', 'disk write time', 'disk hash time']),
	('disk_time', '% of total disk job time', '%%', 'proportion of time spent by the disk thread', ['% read time', '% write time', '% hash time'], {'type': stacked}),
	('disk_cache_hits', 'blocks (16kiB)', '', '', ['disk block read', 'read cache hits'], {'type':stacked}),
	('disk_cache', 'blocks (16kiB)', '', 'disk cache size and usage', ['disk buffer allocations', 'read disk cache size', 'disk cache size', 'cache size', 'pinned blocks', 'cache trim low watermark']),
	('disk_readback', '% of written blocks', '%%', 'portion of written blocks that had to be read back for hash verification', ['% read back']),
	('disk_queue', 'number of queued disk jobs', '', 'queued disk jobs', ['disk queue size', 'disk read queue size', 'allocated jobs', 'allocated read jobs', 'allocated write jobs']),
	('disk_iops', 'operations/s', '', 'number of disk operations per second', ['read ops/s', 'write ops/s', 'smooth read ops/s', 'smooth write ops/s']),
	('disk pending reads', 'Bytes', '', 'number of bytes peers are waiting for to be read from the disk', ['pending reading bytes']),
	('disk fences', 'num', '', 'number of jobs currently blocked by a fence job', ['blocked jobs']),
	('fence jobs', 'num', '', 'active fence jobs per type', ['move_storage', 'release_files', 'delete_files', 'check_fastresume', 'save_resume_data', 'rename_file', 'stop_torrent', 'file_priority', 'clear_piece'], {'type':stacked}),
	('disk threads', 'num', '', 'number of disk threads currently writing', ['num writing threads', 'num running threads']),
	('mixed mode', 'rate', 'B/s', 'rates by transport protocol', ['TCP up rate','TCP down rate','uTP up rate','uTP down rate','TCP up limit','TCP down limit']),
	('connection_type', 'num', '', 'peers by transport protocol', ['utp peers','tcp peers']),
	('uTP delay', 'buffering delay', 's', 'network delays measured by uTP', ['uTP peak send delay','uTP peak recv delay', 'uTP avg send delay', 'uTP avg recv delay']),
	('uTP send delay histogram', 'buffering delay', 's', 'send delays measured by uTP', ['uTP avg send delay'], {'type': histogram, 'binwidth': 0.05, 'numbins': 100}),
	('uTP recv delay histogram', 'buffering delay', 's', 'receive delays measured by uTP', ['uTP avg recv delay'], {'type': histogram, 'binwidth': 0.05, 'numbins': 100}),
	('uTP stats', 'num', '', 'number of uTP sockets by state', ['uTP idle', 'uTP syn-sent', 'uTP connected', 'uTP fin-sent', 'uTP close-wait'], {'type': stacked}),
	('system memory', '', '', 'virtual memory page count', ['active resident pages', 'inactive resident pages', 'pinned resident pages', 'free pages'], {'type': stacked}),
	('memory paging', '', '', 'vm disk activity', ['pageins', 'pageouts']),
	('page faults', '', '', '', ['page faults']),
	('CPU usage', '%', '', '', ['network thread system time', 'network thread user+system time']),
	('boost.asio messages', 'events/s', '', 'number of messages posted per second', [ \
		'read_counter', 'write_counter', 'tick_counter', 'lsd_counter', \
		'lsd_peer_counter', 'udp_counter', 'accept_counter', 'disk_queue_counter', \
		'disk_counter'], {'type': stacked}),
	('send_buffer_sizes', 'num', '', '', ['up 8', 'up 16', 'up 32', 'up 64', 'up 128', 'up 256', \
		'up 512', 'up 1024', 'up 2048', 'up 4096', 'up 8192', 'up 16384', 'up 32768', 'up 65536', \
		'up 131072', 'up 262144', 'up 524288', 'up 1048576'], {'type': stacked, 'colors':'gradient18'}),
	('recv_buffer_sizes', 'num', '', '', ['down 8', 'down 16', 'down 32', 'down 64', 'down 128', \
		'down 256', 'down 512', 'down 1024', 'down 2048', 'down 4096', 'down 8192', 'down 16384', \
		'down 32768', 'down 65536', 'down 131072', 'down 262144', 'down 524288', 'down 1048576'], {'type': stacked, 'colors':'gradient18'}),
	('ARC', 'num pieces', '', '', ['arc LRU ghost pieces', 'arc LRU pieces', 'arc LRU volatile pieces', 'arc LFU pieces', 'arc LFU ghost pieces'], {'allow-negative': True}),
	('torrent churn', 'num torrents', '', '', ['loaded torrents', 'pinned torrents', 'loaded torrent churn']),
	('pinned torrents', 'num torrents', '', '', ['pinned torrents']),
	('loaded torrents', 'num torrents', '', '', ['loaded torrents', 'pinned torrents']),
	('requests', '', '', '', ['outstanding requests']),
	('request latency', 'us', '', 'latency from receiving requests to sending response', ['request latency']),
	('incoming messages', 'num', '', 'number of received bittorrent messages, by type', [ \
		'num_incoming_choke', 'num_incoming_unchoke', 'num_incoming_interested', \
		'num_incoming_not_interested', 'num_incoming_have', 'num_incoming_bitfield', \
		'num_incoming_request', 'num_incoming_piece', 'num_incoming_cancel', \
		'num_incoming_dht_port', 'num_incoming_suggest', 'num_incoming_have_all', \
		'num_incoming_have_none', 'num_incoming_reject', 'num_incoming_allowed_fast', \
		'num_incoming_ext_handshake', 'num_incoming_pex', 'num_incoming_metadata', 'num_incoming_extended' \
	 ], {'type': stacked}),
	('outgoing messages', 'num', '', 'number of sent bittorrent messages, by type', [ \
		'num_outgoing_choke', 'num_outgoing_unchoke', 'num_outgoing_interested', \
		'num_outgoing_not_interested', 'num_outgoing_have', 'num_outgoing_bitfield', \
		'num_outgoing_request', 'num_outgoing_piece', 'num_outgoing_cancel', \
		'num_outgoing_dht_port', 'num_outgoing_suggest', 'num_outgoing_have_all', \
		'num_outgoing_have_none', 'num_outgoing_reject', 'num_outgoing_allowed_fast', \
		'num_outgoing_ext_handshake', 'num_outgoing_pex', 'num_outgoing_metadata', 'num_outgoing_extended' \
	 ], {'type': stacked}),
	('request in balance', 'num', '', 'request and piece message balance', [ \
		'num_incoming_request', 'num_outgoing_piece', \
		'num_outgoing_reject'  \
	 ], {'type': diff}),
	('request out balance', 'num', '', 'request and piece message balance', [ \
		'num_outgoing_request', 'num_incoming_piece', \
		'num_incoming_reject'  \
	 ], {'type': diff}),
#	('absolute_waste', 'num', '', ['failed bytes', 'redundant bytes', 'download rate']),

#somewhat uninteresting stats
	('tick_rate', 'time between ticks', 's', '', ['tick interval', 'tick residual']),
	('peer_dl_rates', 'num', '', 'peers split into download rate buckets', ['peers down 0', 'peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-'], {'type':stacked, 'colors':'gradient6'}),
	('peer_dl_rates2', 'num', '', 'peers split into download rate buckets (only downloading peers)', ['peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-'], {'type':stacked, 'colors':'gradient6'}),
	('peer_ul_rates', 'num', '', 'peers split into upload rate buckets', ['peers up 0', 'peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-'], {'type':stacked, 'colors':'gradient6'}),
	('peer_ul_rates2', 'num', '', 'peers split into upload rate buckets (only uploading peers)', ['peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-'], {'type':stacked, 'colors':'gradient6'}),
	('piece_picker_invocations', 'invocations of piece picker', '', '', ['reject piece picks', \
		'unchoke piece picks', 'incoming redundant piece picks', \
		'incoming piece picks', 'end game piece picks', 'snubbed piece picks', 'interesting piece picks', 'hash fail piece picks'], \
		{'type':stacked}),
	('piece_picker_loops', 'loops through piece picker', '', '', [ \
		'piece_picker_partial_loops', 'piece_picker_suggest_loops', 'piece_picker_sequential_loops', 'piece_picker_reverse_rare_loops',
		'piece_picker_rare_loops', 'piece_picker_rand_start_loops', 'piece_picker_rand_loops', 'piece_picker_busy_loops'], {'type': stacked}),
	('picker_partials', 'pieces', '', '', ['num downloading partial pieces', 'num full partial pieces', 'num finished partial pieces', \
		'num 0-priority partial pieces'], {'type':stacked}),
	('picker_full_partials_distribution', 'full pieces', '', '', ['num full partial pieces'], {'type': histogram, 'binwidth': 5, 'numbins': 120}),
	('picker_partials_distribution', 'partial pieces', '', '', ['num downloading partial pieces'], {'type': histogram, 'binwidth': 5, 'numbins': 120})
]

print 'generating graphs'
log_file_path, log_file = os.path.split(sys.argv[1])
# count the number of log files (generations)
log_file_list = log_file.split('.')
g = int(log_file_list[1])
generations = []
scripts = []
while os.path.exists(os.path.join(log_file_path, log_file)):
	print '[%s] %04d\r[' % (' ' * len(reports), g),
	for i in reports:
		try: options = i[5]
		except: options = {}
		if not 'type' in options:
			options['type'] = line_graph

		script = gen_report(i[0], i[1], i[4], i[2], g, os.path.join(log_file_path, log_file), options)
		if script != None: scripts.append(script)
	generations.append(g)
	g += 1
	log_file_list[1] = '%04d' % g
	log_file = '.'.join(log_file_list)

	# run gnuplot on all scripts, in parallel
	thread_pool.map(plot_fun, scripts)
	print '' # newline
	scripts = []

print '\ngenerating html'
gen_html(reports, generations)

