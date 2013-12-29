
import os, sys, time

# logfile format:
# <time(ms)> <torrent> <peer> <piece> <offset> <size> <upload-rate>

f = open(sys.argv[1], 'r')

out = open('requests.dat', 'w+')

peers = {}

num_columns = 1

try:
	for l in f:
		dat = l.split('\t')

		peer = dat[2]
		time = dat[0]
		tor = dat[1]
		piece = dat[3]
		offset = dat[4]
		if tor != sys.argv[2]: continue

		if peer not in peers:
			num_columns += 1
			peers[peer] = num_columns

		print >>out, '%s %s %s' % (dat[0], ' -' * (peers[peer] - 2), float(piece) + float(offset) / (128.0 * 16.0 * 1024.0))
except:
	pass

out.close()

out = open('peer_requests.gnuplot', 'wb')
print >>out, "set term png size 12000,7000"
print >>out, 'set output "peer_requests.png"'
print >>out, 'set xrange [0:*]'
print >>out, 'set xlabel "time (ms)"'
print >>out, 'set ylabel "bytes (B)"'
print >>out, "set style data lines"
print >>out, "set key box"
print >>out, 'plot',

for p in peers:
	print >>out, ' "requests.dat" using 1:%d title "%s" with points,' % (peers[p], p),

print >>out, 'x=0'

os.system('gnuplot peer_requests.gnuplot')
