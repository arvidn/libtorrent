#!/usr/bin/env python

import os, sys, time

lines = open(sys.argv[1], 'rb').readlines()

# logfile format:
# <time(ms)> <key>: <value>
# example:
# 16434 read cache: 17

key_order = ['receive buffer', 'send buffer', 'released send buffer', 'posted send buffer',
	'received send buffer', 'dispatched send buffer', 'queued send buffer',
	'write cache', 'read cache', 'hash temp']
colors = ['30f030', '001070', '101080', '2040a0',
	'4070d0', '80a0f0', 'f03030',
	'80f080', 'f08080', '4040ff']

keys = []
fields = {}
maximum = {}
out = open('disk_buffer_log.dat', 'w+')

field_sum = {}
field_num_samples = {}
field_timestamp = {}

for c in key_order:
	keys.append(c)
	fields[c] = 0
	maximum[c] = 0
	field_sum[c] = 0
	field_num_samples[c] = 0
	field_timestamp[c] = 0

last_t = 0
for l in lines:
	try:
		t = int(l[0:l.find(' ')])
		c = l[l.find(' ')+1:l.find(':')]
		n = int(l[l.find(':')+1:-1])
	except:
		print(l)
		continue

	if last_t != t:
		print('%d\t' % last_t, end=' ', file=out)
		for i in keys:
			print('%d\t' % maximum[i], end=' ', file=out)
		print('\n', end=' ', file=out)

	if not c in keys: continue

	field_sum[c] += fields[c] * float(t - field_timestamp[c])
	field_timestamp[c] = t

	fields[c] = n

	if n > maximum[c]: maximum[c] = n

	if last_t != t:
		last_t = t
		maximum = fields

for i in keys:
	print('%s: avg: %f' % (i, field_sum[i] / last_t))
print()

out.close()

out = open('disk_buffer.gnuplot', 'wb')
print("set term png size 1200,700", file=out)
print('set output "disk_buffer.png"', file=out)
print('set xrange [0:*]', file=out)
print('set xlabel "time (ms)"', file=out)
print('set ylabel "buffers"', file=out)
print("set style data lines", file=out)
print("set key box", file=out)
print('plot', end=' ', file=out)
count = 1 + len(keys)
keys.reverse()
comma = ''
for k in keys:
	expr = "$%d" % count
	for i in range(2, count): expr += "+$%d" % i
	count -= 1
	print(' %s"disk_buffer_log.dat" using 1:(%s) title "%s" with filledcurves x1 lt rgb "#%s"' % (comma, expr, k, colors[count-1]), end=' ', file=out)
	comma = ','
out.close()

os.system('gnuplot disk_buffer.gnuplot')

