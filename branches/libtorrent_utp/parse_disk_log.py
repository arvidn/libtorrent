#! /usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os, sys, time

lines = open(sys.argv[1], 'rb').readlines()

if len(sys.argv) < 2:
	print "usage: parse_disk_log.py logfile [seconds]"
	sys.exit(1)

keys = ['write', 'read', 'read-cache-hit', 'hash', 'move', 'release', 'idle', \
	'delete', 'check_fastresume', 'check_files', 'clear-cache', \
	'abort_thread', 'abort_torrent', 'save_resume_data', 'rename_file', \
	'flushing', 'update_settings', 'finalize_file', 'sorting_job', \
	'check_cache_hit']
throughput_keys = ['write', 'read', 'read-cache-hit']

# logfile format:
# <time(ms)> <state>
# example:
# 34523 idle
# 34722 write

if len(sys.argv) > 2:
	quantization = long(sys.argv[2]) * 1000000
else:
	quantization = 1000000

out = open('disk_io.dat', 'wb')
out2 = open('disk_throughput.dat', 'wb')
state = 'idle'
time = -1
start_time = -1
i = 0
state_timer = {}
throughput = {}
for k in keys: state_timer[k] = 0
for k in throughput_keys: throughput[k] = 0

for l in lines:
	l = l.strip().split()
	if len(l) < 2:
		print l
		continue
#	try:
	new_time = long(l[0])
	if time == -1:
		time = new_time
		i = new_time
		start_time = new_time
	while new_time > i + quantization:
		i += quantization
		state_timer[state] += i - time
		time = i
		for k in keys: print >>out, (state_timer[k] / float(quantization) * 100.),
		print >>out
		print >>out2, time - start_time,
		for k in throughput_keys:
			print >>out2, throughput[k] * 1000 / float(quantization),
			print '-- %s %d' % (k, throughput[k])
		print >>out2
		for k in keys: state_timer[k] = 0
		for k in throughput_keys: throughput[k] = 0
	state_timer[state] += new_time - time
	time = new_time
	state = l[1]
	if state in throughput_keys:
		throughput[state] += long(l[2])
#	except:
#		print l

i += quantization
state_timer[state] += i - time
time = i
for k in keys: print >>out, (state_timer[k] / float(quantization) * 100.),
print >>out
print >>out2, time - start_time,
for k in throughput_keys:
	print >>out2, throughput[k] * 1000 / float(quantization),
	print '-- %s %d' % (k, throughput[k])
print >>out2
for k in keys: state_timer[k] = 0
for k in throughput_keys: throughput[k] = 0
out.close()
out2.close()

out = open('disk_io.gnuplot', 'wb')
print >>out, "set term png size 1200,700"

print >>out, 'set output "disk_throughput.png"'
print >>out, 'set title "disk throughput per %f second(s)"' % (quantization / float(1000000))
print >>out, 'set ylabel "throughput (kB/s)"'
print >>out, 'plot',
i = 0
for k in throughput_keys:
	print >>out, ' "disk_throughput.dat" using 1:%d title "%s" with lines,' % (i + 2, throughput_keys[i]),
	i = i + 1
print >>out, 'x=0'

print >>out, 'set output "disk_io.png"'
print >>out, 'set ylabel "utilization (%)"'
print >>out, 'set xrange [0:*]'
print >>out, 'set title "disk io utilization per %f second(s)"' % (quantization / float(1000000))
print >>out, "set key box"
print >>out, "set style data histogram"
print >>out, "set style histogram rowstacked"
print >>out, "set style fill solid"
print >>out, 'plot',
i = 0
for k in keys:
	if k != 'idle':
		print >>out, ' "disk_io.dat" using %d title "%s",' % (i + 1, keys[i]),
	i = i + 1
print >>out, 'x=0'
out.close()

os.system('gnuplot disk_io.gnuplot');

