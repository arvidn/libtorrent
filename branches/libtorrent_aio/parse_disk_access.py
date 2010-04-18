#! /usr/bin/env python

import os, sys, time

lines = open(sys.argv[1], 'rb').readlines()

# logfile format:
# <time(us)> <key>: <value>
# example:
# 16434 read cache: 17

keys = ['read', 'write', 'head movement', 'seek per read byte', 'seek per written byte',
	'read operations per second', 'write operations per second']
colors = ['305030', '503030', '3030f0', '10a010', 'a01010', 'd0d040', 'd040d0']
style = ['dots', 'points', 'lines', 'lines', 'lines', 'lines', 'lines']
axis = ['x1y1', 'x1y1', 'x1y2', 'x1y2', 'x1y2', 'x1y2', 'x1y2']
plot = [True, False, False, False, False, True, False]

out = open('disk_access_log.dat', 'w+')

time = 1000000

last_pos = 0
last_t = 0
cur_movement = 0
cur_read = 0
cur_write = 0
cur_read_ops = 0
cur_write_ops = 0

for l in lines:
	try:
		# strip newline
		l = l[0:-1].split(' ')
		t = int(l[0])
		k = l[1]
		n = int(l[2])
	except:
		print l
		continue

	read = '-'
	write = '-'
	movement = '-'
	amount_read = '-'
	amount_write = '-'
	read_ops = '-'
	write_ops = '-'
	if k == 'read':
		read = '%d' % n
		cur_read_ops += 1
	if k == 'write':
		write = '%d' % n
		cur_write_ops += 1
	if k == 'read_end': cur_read += n - last_pos
	if k == 'write_end': cur_write += n - last_pos

	cur_movement += abs(last_pos - n)
	last_pos = n

	if last_t + time <= t:
		movement = '%d' % cur_movement
		if cur_read > 0:
			amount_read = '%d' % (cur_movement / cur_read)
		if cur_write > 0:
			amount_write = '%d' % (cur_movement / cur_write)
		read_ops = '%d' % cur_read_ops
		write_ops = '%d' % cur_write_ops
		cur_movement = 0
		cur_read = 0
		cur_write = 0
		last_t = t
		cur_read_ops = 0
		cur_write_ops = 0

	print >>out, '%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s' % (t, read, write, movement, amount_read, amount_write, read_ops, write_ops)

out.close()

out = open('disk_access.gnuplot', 'wb')
print >>out, "set term png size 1200,700"
print >>out, 'set output "disk_access.png"'
print >>out, 'set xrange [*:*]'
#print >>out, 'set y2range [0:*]'
print >>out, 'set xlabel "time (us)"'
print >>out, 'set ylabel "drive offset"'
#print >>out, 'set y2label "bytes / %d second(s)"' % (time / 1000)
print >>out, "set key box"
print >>out, "set tics nomirror"
print >>out, "set y2tics auto"
print >>out, 'plot',
count = 1
for k in keys:
	count += 1
	if not plot[count-2]: continue
	print >>out, ' "disk_access_log.dat" using 1:%d title "%s" with %s lt rgb "#%s" axis %s,' \
		% (count, k, style[count-2], colors[count-2], axis[count-2]),
print >>out, 'x=0'
out.close()

os.system('gnuplot disk_access.gnuplot')

