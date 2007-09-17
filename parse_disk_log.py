import os, sys, time

lines = open(sys.argv[1], 'rb').readlines()

time_limit = -1
if len(sys.argv) > 2:
	time_limit = long(sys.argv[2])

keys = ['write', 'read', 'hash', 'move', 'release', 'idle']

# logfile format:
# <time(ms)> <state>
# example:
# 34523 idle
# 34722 write

quantization = 5000

out = open('disk_io.dat', 'wb')
state = 'idle'
time = 0
i = 0
state_timer = {}
for k in keys: state_timer[k] = 0
for l in lines:
	l = l[:-1].split(' ')
	if len(l) < 2:
		print l
		continue
	try:
		new_time = long(l[0])
		while new_time > i + quantization:
			i += quantization
			state_timer[state] += i - time
			time = i
			for k in keys: print >>out, state_timer[k],
			print >>out
			for k in keys: state_timer[k] = 0
		state_timer[state] += new_time - time
		time = new_time
		state = l[1]
	except:
		print l
out.close()


out = open('disk_io.gnuplot', 'wb')
print >>out, "set term png size 1200,700"
print >>out, 'set output "disk_io.png"'
print >>out, 'set xrange [0:*]'
print >>out, 'set ylabel "time (ms)"'
print >>out, "set style data lines"
print >>out, 'set title "disk io utilization per %s second(s)"' % (quantization / 1000)
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

