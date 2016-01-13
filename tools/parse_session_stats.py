#!/usr/bin/env python

# Copyright (c) 2016, Arvid Norberg
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the distribution.
#     * Neither the name of the author nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# this script can parse and generate reports from the alert log from a
# libtorrent session

import os, sys, time, os, math
from multiprocessing.pool import ThreadPool

thread_pool = ThreadPool(8)

output_dir = 'session_stats_report'

stat = open(sys.argv[1])
line = stat.readline()
while not 'session stats header:' in line:
	line = stat.readline()

keys = line.split('session stats header:')[1].strip().split(', ')

try: os.mkdir(output_dir)
except: pass
data_out = open(os.path.join(output_dir, 'counters.dat'), 'w+')

idx = 0
for l in stat:
	if not 'session stats (' in l: continue
	data_out.write(("%d\t" % idx) + l.split(' values): ')[1].strip().replace(', ', '\t') + '\n')
	idx += 1

data_out.close()

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

def to_title(key):
	return key.replace('_', ' ').replace('.', ' - ')

def gen_report(name, unit, lines, short_unit, generation, log_file, options):
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
			plot_expression = ' "%s" using 1:(%s) title "%s" axes %s with filledcurves x1 lc rgb "%s"' % (log_file, graph, to_title(k), axis, colors[color % len(colors)]) + plot_expression
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
			title += to_title(k)
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
			print >>out, ' "%s" using 1:%d title "%s" axes %s with steps lc rgb "%s"' % (log_file, column, to_title(k), axis, colors[color % len(colors)]),
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

	('torrents', 'num', '', 'number of torrents in different torrent states', [ \
		'ses.num_downloading_torrents', \
		'ses.num_seeding_torrents', \
		'ses.num_checking_torrents', \
		'ses.num_stopped_torrents', \
		'ses.num_upload_only_torrents', \
		'ses.num_error_torrents', \
		'ses.num_queued_seeding_torrents', \
		'ses.num_queued_download_torrents' \
	], {'type':stacked}),

	('peers', 'num', '', 'num connected peers', ['peer.num_peers_connected', 'peer.num_peers_half_open'], {'type':stacked}),
	('peers_max', 'num', '', 'num connected peers', ['peer.num_peers_connected', 'peer.num_peers_half_open']),
	('peer_churn', 'num', '', 'connecting and disconnecting peers', ['peer.num_peers_half_open', 'peer.connection_attempts']),
	('new_peers', 'num', '', '', ['peer.incoming_connections', 'peer.connection_attempts']),
	('connection_attempts', 'num', '', '', ['peer.connection_attempt_loops', 'peer.connection_attempts']),
	('pieces', 'num', '', 'number completed pieces', ['ses.num_total_pieces_added', 'ses.num_piece_passed', 'ses.num_piece_failed']),
	('disk_write_queue', 'Bytes', 'B', 'bytes queued up by peers, to be written to disk', ['disk.queued_write_bytes']),

	('peers_requests', 'num', '', 'incoming piece request rate', [ \
		'peer.piece_requests', \
		'peer.max_piece_requests', \
		'peer.invalid_piece_requests', \
		'peer.choked_piece_requests', \
		'peer.cancelled_piece_requests' \
	]),

	('peers_upload', 'num', '', 'number of peers by state wrt. uploading', [ \
		'peer.num_peers_up_disk', \
		'peer.num_peers_up_interested', \
		'peer.num_peers_up_unchoked_all', \
		'peer.num_peers_up_unchoked_optimistic', \
		'peer.num_peers_up_unchoked', \
		'peer.num_peers_up_requests' \
	]),

	('peers_download', 'num', '', 'number of peers by state wrt. downloading', [ \
		'peer.num_peers_down_interested', \
		'peer.num_peers_down_unchoked', \
		'peer.num_peers_down_requests', \
		'peer.num_peers_down_disk' \
	]),

	('peer_errors', 'num', '', 'number of peers by error that disconnected them', [ \
		'peer.disconnected_peers', \
		'peer.eof_peers', \
		'peer.connreset_peers', \
		'peer.connrefused_peers', \
		'peer.connaborted_peers', \
		'peer.perm_peers', \
		'peer.buffer_peers', \
		'peer.unreachable_peers', \
		'peer.broken_pipe_peers', \
		'peer.addrinuse_peers', \
		'peer.no_access_peers', \
		'peer.invalid_arg_peers', \
		'peer.aborted_peers' \
	], {'type':stacked}),

	('peer_errors_incoming', 'num', '', 'number of peers by incoming or outgoing connection', [ \
		'peer.error_incoming_peers', \
		'peer.error_outgoing_peers' \
	]),

	('peer_errors_transport', 'num', '', 'number of peers by transport protocol', [ \
		'peer.error_tcp_peers', \
		'peer.error_utp_peers'
	]),

	('peer_errors_encryption', 'num', '', 'number of peers by encryption level', [ \
		'peer.error_encrypted_peers', \
		'peer.error_rc4_peers', \
	]),

	('incoming requests', 'num', '', 'incoming 16kiB block requests', ['ses.num_incoming_request']),

	('waste', 'downloaded bytes', 'B', 'proportion of all downloaded bytes that were wasted', [ \
		'net.recv_failed_bytes', \
		'net.recv_redundant_bytes', \
		'net.recv_ip_overhead_bytes' \
	], {'type':stacked}),

	('waste by source', 'num wasted bytes', 'B', 'what is causing the waste', [ \
		'ses.waste_piece_timed_out', \
		'ses.waste_piece_cancelled', \
		'ses.waste_piece_unknown', \
		'ses.waste_piece_seed', \
		'ses.waste_piece_end_game', \
		'ses.waste_piece_closing' \
	], {'type':stacked}),

	('disk_time', '% of total disk job time', '%%', 'proportion of time spent by the disk thread', ['disk.disk_read_time', 'disk.disk_write_time', 'disk.disk_hash_time'], {'type': stacked}),
	('disk_cache_hits', 'blocks (16kiB)', '', '', ['disk.num_blocks_read', 'disk.num_blocks_cache_hits'], {'type':stacked}),
	('disk_cache', 'blocks (16kiB)', '', 'disk cache size and usage', ['disk.disk_blocks_in_use', 'disk.read_cache_blocks', 'disk.write_cache_blocks', 'disk.pinned_blocks']),
	('disk_readback', '% of written blocks', '%%', 'portion of written blocks that had to be read back for hash verification', ['disk.num_read_back']),
	('disk_queue', 'number of queued disk jobs', '', 'num disk jobs', ['disk.num_write_jobs', 'disk.num_read_jobs', 'disk.num_jobs', 'disk.queued_disk_jobs', 'disk.blocked_disk_jobs']),
	('disk fences', 'num', '', 'number of jobs currently blocked by a fence job', ['disk.blocked_disk_jobs']),
#	('fence jobs', 'num', '', 'active fence jobs per type', ['move_storage', 'release_files', 'delete_files', 'check_fastresume', 'save_resume_data', 'rename_file', 'stop_torrent', 'file_priority', 'clear_piece'], {'type':stacked}),
	('disk threads', 'num', '', 'number of disk threads currently writing', ['disk.num_writing_threads', 'disk.num_running_threads']),
#	('mixed mode', 'rate', 'B/s', 'rates by transport protocol', ['TCP up rate','TCP down rate','uTP up rate','uTP down rate','TCP up limit','TCP down limit']),

	('connection_type', 'num', '', 'peers by transport protocol', [ \
		'peer.num_tcp_peers', \
		'peer.num_socks5_peers', \
		'peer.num_http_proxy_peers', \
		'peer.num_utp_peers', \
		'peer.num_i2p_peers', \
		'peer.num_ssl_peers', \
		'peer.num_ssl_socks5_peers', \
		'peer.num_ssl_http_proxy_peers', \
		'peer.num_ssl_utp_peers' \
	]),

#	('uTP delay', 'buffering delay', 's', 'network delays measured by uTP', ['uTP peak send delay','uTP peak recv delay', 'uTP avg send delay', 'uTP avg recv delay']),
#	('uTP send delay histogram', 'buffering delay', 's', 'send delays measured by uTP', ['uTP avg send delay'], {'type': histogram, 'binwidth': 0.05, 'numbins': 100}),
#	('uTP recv delay histogram', 'buffering delay', 's', 'receive delays measured by uTP', ['uTP avg recv delay'], {'type': histogram, 'binwidth': 0.05, 'numbins': 100}),

	('uTP stats', 'num', '', 'number of uTP events', [ \
		'utp.utp_packet_loss', \
		'utp.utp_timeout', \
		'utp.utp_packets_in', \
		'utp.utp_packets_out', \
		'utp.utp_fast_retransmit', \
		'utp.utp_packet_resend', \
		'utp.utp_samples_above_target', \
		'utp.utp_samples_below_target', \
		'utp.utp_payload_pkts_in', \
		'utp.utp_payload_pkts_out', \
		'utp.utp_invalid_pkts_in', \
		'utp.utp_redundant_pkts_in' \
	], {'type': stacked}),

	('boost.asio messages', 'num events', '', 'number of messages posted', [ \
		'net.on_read_counter', \
		'net.on_write_counter', \
		'net.on_tick_counter', \
		'net.on_lsd_counter', \
		'net.on_lsd_peer_counter', \
		'net.on_udp_counter', \
		'net.on_accept_counter', \
		'net.on_disk_counter' \
		], {'type': stacked}),

	('send_buffer_sizes', 'num', '', '', [ \
		'sock_bufs.socket_send_size3', \
		'sock_bufs.socket_send_size4', \
		'sock_bufs.socket_send_size5', \
		'sock_bufs.socket_send_size6', \
		'sock_bufs.socket_send_size7', \
		'sock_bufs.socket_send_size8', \
		'sock_bufs.socket_send_size9', \
		'sock_bufs.socket_send_size10', \
		'sock_bufs.socket_send_size11', \
		'sock_bufs.socket_send_size12', \
		'sock_bufs.socket_send_size13', \
		'sock_bufs.socket_send_size14', \
		'sock_bufs.socket_send_size15', \
		'sock_bufs.socket_send_size16', \
		'sock_bufs.socket_send_size17', \
		'sock_bufs.socket_send_size18', \
		'sock_bufs.socket_send_size19', \
		'sock_bufs.socket_send_size20' \
	], {'type': stacked, 'colors':'gradient18'}),

	('recv_buffer_sizes', 'num', '', '', [ \
		'sock_bufs.socket_recv_size3', \
		'sock_bufs.socket_recv_size4', \
		'sock_bufs.socket_recv_size5', \
		'sock_bufs.socket_recv_size6', \
		'sock_bufs.socket_recv_size7', \
		'sock_bufs.socket_recv_size8', \
		'sock_bufs.socket_recv_size9', \
		'sock_bufs.socket_recv_size10', \
		'sock_bufs.socket_recv_size11', \
		'sock_bufs.socket_recv_size12', \
		'sock_bufs.socket_recv_size13', \
		'sock_bufs.socket_recv_size14', \
		'sock_bufs.socket_recv_size15', \
		'sock_bufs.socket_recv_size16', \
		'sock_bufs.socket_recv_size17', \
		'sock_bufs.socket_recv_size18', \
		'sock_bufs.socket_recv_size19', \
		'sock_bufs.socket_recv_size20' \
		], {'type': stacked, 'colors':'gradient18'}),

	('ARC', 'num pieces', '', '', [ \
		'disk.arc_mru_ghost_size', \
		'disk.arc_mru_size', \
		'disk.arc_volatile_size', \
		'disk.arc_mfu_size', \
		'disk.arc_mfu_ghost_size' \
	], {'allow-negative': True}),

	('torrent churn', 'num torrents', '', '', ['ses.num_loaded_torrents', 'ses.num_pinned_torrents', 'ses.torrent_evicted_counter']),
	('pinned torrents', 'num torrents', '', '', ['ses.num_pinned_torrents']),
	('loaded torrents', 'num torrents', '', '', ['ses.num_loaded_torrents', 'ses.num_pinned_torrents']),
	('request latency', 'us', '', 'latency from receiving requests to sending response', ['disk.request_latency']),
	('incoming messages', 'num', '', 'number of received bittorrent messages, by type', [ \
		'ses.num_incoming_choke', \
		'ses.num_incoming_unchoke', \
		'ses.num_incoming_interested', \
		'ses.num_incoming_not_interested', \
		'ses.num_incoming_have', \
		'ses.num_incoming_bitfield', \
		'ses.num_incoming_request', \
		'ses.num_incoming_piece', \
		'ses.num_incoming_cancel', \
		'ses.num_incoming_dht_port', \
		'ses.num_incoming_suggest', \
		'ses.num_incoming_have_all', \
		'ses.num_incoming_have_none', \
		'ses.num_incoming_reject', \
		'ses.num_incoming_allowed_fast', \
		'ses.num_incoming_ext_handshake', \
		'ses.num_incoming_pex', \
		'ses.num_incoming_metadata', \
		'ses.num_incoming_extended' \
	], {'type': stacked}),
	('outgoing messages', 'num', '', 'number of sent bittorrent messages, by type', [ \
		'ses.num_outgoing_choke', \
		'ses.num_outgoing_unchoke', \
		'ses.num_outgoing_interested', \
		'ses.num_outgoing_not_interested', \
		'ses.num_outgoing_have', \
		'ses.num_outgoing_bitfield', \
		'ses.num_outgoing_request', \
		'ses.num_outgoing_piece', \
		'ses.num_outgoing_cancel', \
		'ses.num_outgoing_dht_port', \
		'ses.num_outgoing_suggest', \
		'ses.num_outgoing_have_all', \
		'ses.num_outgoing_have_none', \
		'ses.num_outgoing_reject', \
		'ses.num_outgoing_allowed_fast', \
		'ses.num_outgoing_ext_handshake', \
		'ses.num_outgoing_pex', \
		'ses.num_outgoing_metadata', \
		'ses.num_outgoing_extended' \
	], {'type': stacked}),
	('request in balance', 'num', '', 'request and piece message balance', [ \
		'ses.num_incoming_request', \
		'ses.num_outgoing_piece', \
		'ses.num_outgoing_reject', \
	 ], {'type': diff}),
	('request out balance', 'num', '', 'request and piece message balance', [ \
		'ses.num_outgoing_request', \
		'ses.num_incoming_piece', \
		'ses.num_incoming_reject', \
	 ], {'type': diff}),

#somewhat uninteresting stats
#	('peer_dl_rates', 'num', '', 'peers split into download rate buckets', ['peers down 0', 'peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-'], {'type':stacked, 'colors':'gradient6'}),
#	('peer_dl_rates2', 'num', '', 'peers split into download rate buckets (only downloading peers)', ['peers down 0-2', 'peers down 2-5', 'peers down 5-10', 'peers down 50-100', 'peers down 100-'], {'type':stacked, 'colors':'gradient6'}),
#	('peer_ul_rates', 'num', '', 'peers split into upload rate buckets', ['peers up 0', 'peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-'], {'type':stacked, 'colors':'gradient6'}),
#	('peer_ul_rates2', 'num', '', 'peers split into upload rate buckets (only uploading peers)', ['peers up 0-2', 'peers up 2-5', 'peers up 5-10', 'peers up 50-100', 'peers up 100-'], {'type':stacked, 'colors':'gradient6'}),

	('piece_picker_invocations', 'invocations of piece picker', '', '', [ \
		'picker.reject_piece_picks', \
		'picker.unchoke_piece_picks', \
		'picker.incoming_redundant_piece_picks', \
		'picker.incoming_piece_picks', \
		'picker.end_game_piece_picks', \
		'picker.snubbed_piece_picks', \
		'picker.interesting_piece_picks', \
		'picker.hash_fail_piece_picks' \
	], {'type':stacked}),
	('piece_picker_loops', 'loops through piece picker', '', '', [ \
		'picker.piece_picker_partial_loops', \
		'picker.piece_picker_suggest_loops', \
		'picker.piece_picker_sequential_loops', \
		'picker.piece_picker_reverse_rare_loops', \
		'picker.piece_picker_rare_loops', \
		'picker.piece_picker_rand_start_loops', \
		'picker.piece_picker_rand_loops', \
		'picker.piece_picker_busy_loops' \
	], {'type': stacked}),

#	('picker_full_partials_distribution', 'full pieces', '', '', ['num full partial pieces'], {'type': histogram, 'binwidth': 5, 'numbins': 120}),
#	('picker_partials_distribution', 'partial pieces', '', '', ['num downloading partial pieces'], {'type': histogram, 'binwidth': 5, 'numbins': 120})
]

print 'generating graphs'
g = 0
generations = []
scripts = []

print '[%s] %04d\r[' % (' ' * len(reports), g),
for i in reports:
	try: options = i[5]
	except: options = {}
	if not 'type' in options:
		options['type'] = line_graph

	script = gen_report(i[0], i[1], i[4], i[2], g, os.path.join(output_dir, 'counters.dat'), options)
	if script != None: scripts.append(script)

generations.append(g)
g += 1

# run gnuplot on all scripts, in parallel
thread_pool.map(plot_fun, scripts)
scripts = []

print '\ngenerating html'
gen_html(reports, generations)

